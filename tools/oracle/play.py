#!/usr/bin/env python3
"""Play through sts2-cli's interface while recording validated actions and full states."""
import argparse
import contextlib
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid

from reference_runtime import (FEATURES, REPO, JsonProcess, atomic_write, check_scope,
                    reference_build, require, validate, wire_json, strict_json, start_request, action_request)

def positive_int(text):
    try:
        value = int(text)
    except (ValueError, TypeError):
        raise argparse.ArgumentTypeError('Expected a positive integer') from None
    if value <= 0:
        raise argparse.ArgumentTypeError('Expected a positive integer')
    return value


DEFAULT_CLOCK = {'kind': 'action_clock_v1', 'epoch_seconds': 1700000000, 'seconds_per_action': 1}


def new_actions(seed, recording_id, run=True, character="Ironclad", ascension=0):
    if run:
        return {'schema_version': 1, 'profile': 'actions-v1', 'state_profile': 'run-parity-v1',
                'recording_id': recording_id, 'required_features': ['run_flow_v1'],
                'setup': {'kind': 'seeded_run', 'character': character, 'ascension': ascension, 'seed': seed, 'clock': dict(DEFAULT_CLOCK)},
                'actions': []}
    return {'schema_version': 1, 'profile': 'actions-v1', 'state_profile': 'run-parity-v1',
            'recording_id': recording_id, 'required_features': list(FEATURES),
            'setup': {'kind': 'seeded_combat', 'character': 'Ironclad', 'ascension': 0,
                      'seed': seed, 'clock': dict(DEFAULT_CLOCK), 'encounter': 'SHRINKER_BEETLE_WEAK'}, 'actions': []}


def resolve(state, ref):
    value = state
    for token in ref[1:].split('/'):
        key = token.replace('~1', '/').replace('~0', '~')
        value = value[int(key)] if isinstance(value, list) else value[key]
    return value


def combat_finished(state):
    return (state['boundary_kind'] == 'run_complete' or state['combat'] is None or
            not state['combat']['turn_control']['is_in_progress'])


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
            # Independently obtained backend identity is also available to the
            # simulator runner without reading it from expected checkpoints.
            atomic_write(temporary / 'reference.json', reference)
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


def main():
    from upstream_play import RecordingTransport, load_interface
    parser = argparse.ArgumentParser(description='Play sts2-cli with validated action and state recording.')
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True, help='New directory for this recording')
    parser.add_argument('--run', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--character', choices=['Ironclad', 'Silent', 'Defect', 'Regent', 'Necrobinder'], default='Ironclad')
    parser.add_argument('--ascension', type=int, choices=range(11), default=0)
    parser.add_argument('--lang', choices=['en', 'zh', 'both'], default='en')
    parser.add_argument('--auto', action='store_true', help='Use sts2-cli autoplay')
    parser.add_argument('--load', type=Path, help='Replay a sts2-cli action-log save, then continue recording')
    parser.add_argument('--seed', default='ORACLECHECK01')
    parser.add_argument('--checkpoint-every', type=positive_int, default=1, metavar='N')
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--protocol-log', action='store_true')
    args = parser.parse_args()
    require(args.timeout > 0, 'Timeout must be positive')
    require(bool(args.seed), 'Seed must not be empty')
    validator = args.validator.resolve()
    require(validator.is_file(), 'Build parity_validate first, or pass --validator')
    interface = load_interface()
    interface.LANG = args.lang
    if args.load:
        args.character, args.seed, _ = interface._load_game(str(args.load.resolve()))
        saved = strict_json(args.load.read_bytes())
        args.ascension = saved.get('ascension', args.ascension)
    base = args.output_dir.resolve()
    directory = base
    run_number = 1
    restart = True
    while restart:
        directory.mkdir(parents=True, exist_ok=False)
        logs = directory / 'logs'
        logs.mkdir()
        interface.SAVE_DIR = str(directory / 'saves')
        actions = new_actions(args.seed, directory.name, run=True,
                              character=args.character, ascension=args.ascension)
        with reference_build(args.game_dir, logs) as (command, env, provenance):
            host = JsonProcess(command, env, logs / 'reference', args.timeout, log_protocol=args.protocol_log)
            session = PlaySession(host, actions, provenance, validator,
                                  RecordingWriter(directory, validator), args.checkpoint_every)
            transport = RecordingTransport(session)
            try:
                restart = interface.play(character=args.character, seed=args.seed,
                    ascension=args.ascension, auto=args.auto, log=False,
                    load_path=str(args.load.resolve()) if args.load else None,
                    command_sender=transport, checkpoint=lambda: session.publish(final=True))
                if not transport.closed:
                    transport({'cmd': 'quit'})
                host.process.wait(timeout=args.timeout)
                require(host.process.returncode == 0, 'Reference exited unsuccessfully')
            finally:
                host.close()
        print(f'Action file: {directory / "current/actions.json"}')
        print(f'State file:  {directory / "current/states.json"}')
        if restart:
            import secrets
            args.seed, args.load, args.auto = secrets.token_hex(8), None, False
            run_number += 1
            directory = base.with_name(f'{base.name}-{run_number:03d}')


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
