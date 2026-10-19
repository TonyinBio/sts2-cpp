#!/usr/bin/env python3
"""Play the audited sts2-cli starter combat while recording schema-valid actions and state checkpoints."""
import argparse
import contextlib
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import uuid

from record import (FEATURES, REPO, JsonProcess, atomic_write, check_scope,
                    reference_build, require, validate, wire_json, strict_json, start_request, action_request)

HELP = """Commands (positions start at 1):
  play CARD [ENEMY]  Play a card from the displayed hand; attacks default to enemy 1.
  end                End your turn.
  act 'JSON'         Submit a schema-valid gameplay action (use single quotes around JSON).
  state/map/deck/inventory  Inspect the last captured state without recording an action.
  save               Save an extra state checkpoint now.
  every N            Change the state checkpoint interval (measured from action 1).
  quit               Save a final checkpoint and exit. EOF/Ctrl+C also save the last validated state.
  help               Show these commands.
"""


def positive_int(text):
    try:
        value = int(text)
    except (ValueError, TypeError):
        raise argparse.ArgumentTypeError('Expected a positive integer') from None
    if value <= 0:
        raise argparse.ArgumentTypeError('Expected a positive integer')
    return value


def new_actions(seed, recording_id, run=False):
    if run:
        return {'schema_version': 1, 'profile': 'actions-v1', 'state_profile': 'run-parity-v1',
                'recording_id': recording_id, 'required_features': ['run_flow_v1'],
                'setup': {'kind': 'seeded_run', 'character': 'Ironclad', 'ascension': 0, 'seed': seed},
                'actions': []}
    return {'schema_version': 1, 'profile': 'actions-v1', 'state_profile': 'run-parity-v1',
            'recording_id': recording_id, 'required_features': list(FEATURES),
            'setup': {'kind': 'seeded_combat', 'character': 'Ironclad', 'ascension': 0,
                      'seed': seed, 'encounter': 'SHRINKER_BEETLE_WEAK'}, 'actions': []}


def resolve(state, ref):
    value = state
    for token in ref[1:].split('/'):
        key = token.replace('~1', '/').replace('~0', '~')
        value = value[int(key)] if isinstance(value, list) else value[key]
    return value


def combat_finished(state):
    return (state['boundary_kind'] == 'run_complete' or state['combat'] is None or
            not state['combat']['turn_control']['is_in_progress'])


def show_state(state, write=print):
    player = state['players'][0]
    combat = state['combat']
    if combat is None:
        if state.get('run_flow'):
            write(wire_json(state['run_flow']))
        write(f"HP {player['current_hp']}/{player['max_hp']} | {state['boundary_kind']}")
        return
    pc = combat['players'][0]
    creature = resolve(state, pc['creature'])
    write(f"Round {combat['round_number']} | HP {player['current_hp']}/{player['max_hp']} | "
          f"Block {creature['block']} | Energy {pc['energy']}")
    for index, ref in enumerate(combat['enemies'], 1):
        enemy = resolve(state, ref)
        monster = enemy['monster']
        write(f"  Enemy {index}: {monster['id']} | HP {monster['current_hp']}/{monster['max_hp']} | "
              f"Block {enemy['block']} | Move {monster['ai']['next_move']}")
    write('Hand:')
    for index, ref in enumerate(pc['piles']['Hand'], 1):
        card = resolve(state, ref)
        write(f"  {index}. {card['saved']['id']} | Cost {card['energy_cost']['base']} | "
              f"Values {wire_json(card['dynamic_vars'])}")
    if combat_finished(state):
        write(f"Combat finished. Run boundary: {state['boundary_kind']}"
              + (f" ({state['run_ending_reason']})" if state['run_ending_reason'] else ''))


def show_view(tokens, state, write):
    player = state['players'][0]
    if tokens == ['deck']:
        for index, card in enumerate(player['deck'], 1):
            write(f"{index}. {card['id']} +{card['current_upgrade_level']}")
    elif tokens == ['inventory']:
        write(f"Gold: {player['gold']}")
        write('Relics: ' + wire_json(player['relics']))
        write('Potions: ' + wire_json(player['potions']))
    elif tokens != ['state']:
        flow = state.get('run_flow')
        if flow and flow['room']['kind'] == 'map':
            write('Reachable map nodes: ' + wire_json(flow['room']['choices']))
        else:
            write('Map choices are shown at a captured map boundary.')
    else:
        show_state(state, write)


def parse_action(tokens, state):
    """Map displayed positions to this audited exporter's stable action identities."""
    if tokens and tokens[0] == 'act':
        try:
            action = strict_json(' '.join(tokens[1:]))
        except (ValueError, TypeError) as error:
            raise ValueError(f'Invalid action JSON: {error}') from None
        if not isinstance(action, dict):
            raise ValueError('Action must be a JSON object')
        return action
    if tokens == ['end']:
        return {'kind': 'end_turn'}
    if not tokens or tokens[0] != 'play' or len(tokens) not in (2, 3):
        raise ValueError('Use play CARD [ENEMY], end, state, save, every N, or quit')
    if combat_finished(state):
        raise ValueError('Combat has finished')
    pc = state['combat']['players'][0]
    try:
        index = positive_int(tokens[1]) - 1
    except argparse.ArgumentTypeError as error:
        raise ValueError(str(error)) from None
    if index >= len(pc['piles']['Hand']):
        raise ValueError('Card position is outside the current hand')
    ref = pc['piles']['Hand'][index]
    card = resolve(state, ref)
    model = card['saved']['id']
    if model not in ('CARD.STRIKE_IRONCLAD', 'CARD.BASH', 'CARD.DEFEND_IRONCLAD'):
        raise ValueError(f'Unsupported card: {model}')
    if card['energy_cost']['base'] > pc['energy']:
        raise ValueError('Not enough energy for that card')
    action = {'kind': 'play_card', 'card_instance': ref.rsplit('/', 1)[1]}
    if model == 'CARD.DEFEND_IRONCLAD':
        if len(tokens) == 3:
            raise ValueError('Defend does not take an enemy target')
    else:
        try:
            target_index = positive_int(tokens[2]) - 1 if len(tokens) == 3 else 0
        except argparse.ArgumentTypeError as error:
            raise ValueError(str(error)) from None
        # The current driver registers exactly one Shrinker as enemy_0. Snapshot
        # creature labels are document-local and must not be used as action IDs.
        enemies = state['combat']['enemies']
        if target_index != 0 or len(enemies) != 1 or resolve(state, enemies[0])['monster']['current_hp'] <= 0:
            raise ValueError('Choose the living enemy shown as enemy 1')
        action['target'] = 'enemy_0'
    return action


class RecordingWriter:
    """Publish a matched action/state pair by atomically switching one directory link.

    Every successful action is autosaved; only sampled snapshots grow the sequence.
    A previous pair remains readable if writing or validation fails.
    """
    def __init__(self, directory, validator):
        self.directory = Path(directory)
        self.validator = validator
        self.generation = None

    def publish(self, actions, sequence, reference):
        raw = (wire_json(actions) + '\n').encode('utf-8')
        states = {'schema_version': 1, 'profile': 'states-v1',
                  'recording_id': actions['recording_id'],
                  'actions_sha256': hashlib.sha256(raw).hexdigest(),
                  'reference': reference, **sequence}
        validate(self.validator, 'actions', actions)
        validate(self.validator, 'states', states, actions)
        temporary = Path(tempfile.mkdtemp(prefix='.pending-', dir=self.directory))
        link = self.directory / ('.current-' + uuid.uuid4().hex)
        previous = self.generation
        try:
            with (temporary / 'actions.json').open('wb') as stream:
                stream.write(raw)
                stream.flush()
                os.fsync(stream.fileno())
            atomic_write(temporary / 'states.json', states)
            os.symlink(temporary.name, link, target_is_directory=True)
            os.replace(link, self.directory / 'current')
        finally:
            if link.is_symlink():
                link.unlink()
            # SIGINT can arrive immediately after os.replace. Detect publication
            # from the link itself so cleanup never removes its target.
            published = ((self.directory / 'current').is_symlink() and
                         os.readlink(self.directory / 'current') == temporary.name)
            if published:
                self.generation = temporary
                # Only retire directories created by this writer, after the new
                # pair is published. Unrelated files are never removed.
                if previous is not None:
                    with contextlib.suppress(OSError):
                        shutil.rmtree(previous)
            else:
                shutil.rmtree(temporary)
        return self.directory / 'current'


class PlaySession:
    def __init__(self, host, actions, provenance, validator, writer, checkpoint_every):
        require(isinstance(checkpoint_every, int) and not isinstance(checkpoint_every, bool) and checkpoint_every > 0,
                'Checkpoint interval must be a positive integer')
        self.host, self.actions, self.validator = host, actions, validator
        self.provenance, self.writer = provenance, writer
        self.checkpoint_every = checkpoint_every
        self.identity = None
        self.state = None
        self.sequence = None

    def response_state(self, response, request_id):
        require(response.get('type') == 'oracle_state' and response.get('request_id') == request_id,
                f'Mismatched response for {request_id}')
        require('state' in response, 'Reference response is missing state')
        identity = {key: response.get(key) for key in ('assembly_mvid', 'runtime_version')}
        require(all(isinstance(value, str) and value for value in identity.values()),
                'Reference response is missing runtime identity')
        require(self.identity is None or identity == self.identity, 'Reference identity changed')
        validate(self.validator, 'snapshot', response['state'])
        return response['state'], identity

    def start(self):
        validate(self.validator, 'actions', self.actions)
        check_scope(self.actions)
        require(self.host.receive().get('type') == 'ready', 'Missing reference ready message')
        response = self.host.send(start_request(self.actions))
        self.state, self.identity = self.response_state(response, 'initial')
        self.sequence = {'initial': self.state, 'transitions': []}
        self.publish()

    def checkpoint(self):
        if self.actions['actions']:
            action_id = self.actions['actions'][-1]['id']
            if not self.sequence['transitions'] or self.sequence['transitions'][-1]['action_id'] != action_id:
                self.sequence['transitions'].append({'action_id': action_id, 'state': self.state})

    def publish(self, final=False):
        if final:
            self.checkpoint()
        return self.writer.publish(self.actions, self.sequence, dict(self.provenance, **self.identity))

    def finished(self):
        return (self.state['boundary_kind'] == 'run_complete' if self.actions['setup']['kind'] == 'seeded_run'
                else combat_finished(self.state))

    def step(self, action):
        require(not self.finished(), 'Action after combat end')
        index = len(self.actions['actions']) + 1
        entry = {'id': f'action-{index:06d}', 'action': action}
        candidate = dict(self.actions, actions=[*self.actions['actions'], entry])
        validate(self.validator, 'actions', candidate)
        check_scope(candidate)
        response = self.host.send(action_request(self.actions, action, self.state, entry['id']))
        state, _ = self.response_state(response, entry['id'])
        self.actions, self.state = candidate, state
        if index % self.checkpoint_every == 0 or self.finished():
            self.checkpoint()
        self.publish()


def play(session, read=input, write=print):
    session.start()
    write(HELP)
    show_state(session.state, write)
    try:
        while not session.finished():
            try:
                tokens = shlex.split(read('sts2> '))
            except (EOFError, KeyboardInterrupt):
                write('Saving the last validated state and exiting.')
                break
            except ValueError as error:
                write(f'Input: {error}')
                continue
            if not tokens:
                continue
            if tokens == ['quit']:
                break
            if tokens == ['help']:
                write(HELP)
                continue
            if tokens in (['state'], ['map'], ['open_map'], ['open', 'map'], ['deck'], ['inventory']):
                show_view(tokens, session.state, write)
                continue
            if tokens == ['save']:
                session.publish(final=True)
                write('Checkpoint saved.')
                continue
            if tokens[0] == 'every':
                try:
                    if len(tokens) != 2:
                        raise ValueError('Use every N with a positive integer')
                    session.checkpoint_every = positive_int(tokens[1])
                except (ValueError, argparse.ArgumentTypeError) as error:
                    write(f'Input: {error}')
                    continue
                write(f'Saving states every {session.checkpoint_every} actions, plus manual/final checkpoints.')
                continue
            try:
                action = parse_action(tokens, session.state)
            except ValueError as error:
                write(f'Input: {error}')
                continue
            session.step(action)
            show_state(session.state, write)
        session.publish(final=True)
    except KeyboardInterrupt:
        # A command may have been sent but not acknowledged. Save only the prefix
        # with validated responses; never invent the result of that command.
        session.publish(final=True)
        write('Interrupted; saved only the validated action prefix.')
    write(f"Saved {len(session.actions['actions'])} actions and "
          f"{len(session.sequence['transitions']) + 1} state checkpoints.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True, help='New directory for this session')
    parser.add_argument('--run', action='store_true', help='Record a seeded run through the run backend')
    parser.add_argument('--seed', default='ORACLECHECK01')
    parser.add_argument('--checkpoint-every', type=positive_int, default=1, metavar='N')
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--protocol-log', action='store_true', help='Also log every full protocol response')
    args = parser.parse_args()
    require(args.timeout > 0, 'Timeout must be positive')
    require(bool(args.seed), 'Seed must not be empty')
    validator = args.validator.resolve()
    require(validator.is_file(), 'Build parity_validate first, or pass --validator')
    directory = args.output_dir.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    logs = directory / 'logs'
    logs.mkdir()
    actions = new_actions(args.seed, directory.name, run=args.run)
    if args.run:
        print('Starting a seeded Ironclad run; combat exporter coverage remains limited.')
    else:
        print('Starting Ironclad A0 / SHRINKER_BEETLE_WEAK (audited combat scope).')
        print('Map, reward, shop, event, and other character recordings are not supported by this exporter.')
    with reference_build(args.game_dir, logs, extra_sources=(Path(__file__),)) as (command, env, provenance):
        host = JsonProcess(command, env, logs / 'reference', args.timeout, log_protocol=args.protocol_log)
        try:
            writer = RecordingWriter(directory, validator)
            session = PlaySession(host, actions, provenance, validator, writer, args.checkpoint_every)
            play(session)
            require(host.send({'cmd': 'quit'}).get('type') == 'quit_result', 'Reference shutdown failed')
            host.process.wait(timeout=args.timeout)
            require(host.process.returncode == 0, 'Reference exited unsuccessfully')
        finally:
            host.close()
    print(f'Action file: {directory / "current/actions.json"}')
    print(f'State file:  {directory / "current/states.json"}')


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('Interactive recording interrupted during startup or shutdown.', file=sys.stderr)
        sys.exit(130)
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
        print(f'Interactive recording stopped: {error}', file=sys.stderr)
        print('Any previously published current/ pair remains available.', file=sys.stderr)
        sys.exit(1)
