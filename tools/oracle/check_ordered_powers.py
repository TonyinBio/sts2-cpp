#!/usr/bin/env python3
"""Capture independently authored power scenarios; self-replay and compare exactly.

Custom deck / PowerCmd controls are diagnostic setup, never player actions.
No expected checkpoint is supplied to either gameplay implementation.
"""
import argparse
import hashlib
import tempfile
from pathlib import Path
from check_consecutive_combats import compare, settle
from play import new_actions, resolve
from reference_runtime import JsonProcess, reference_build, require, strict_json, wire_json

CASES = {
    'ashes-joss-ending': ([('TREMBLE', False)], [('play', 'TREMBLE')]),
    'ashes-blocked': ([('TREMBLE', False)], [('play', 'TREMBLE')]),
    'joss-threshold': ([('PURITY', True)] * 8, [('play', 'PURITY'), ('select', [3, 1, 0, 2])]),
    'ashes-purity-ending': ([('PURITY', True)] * 8, [('play', 'PURITY'), ('select', [2, 0])]),
    'ashes-purity-ending-late': ([('PURITY', True)] * 8, [('play', 'PURITY'), ('select', [0, 2])]),
    'ashes-purity-ordered': ([('PURITY', True)] * 8, [('play', 'PURITY'), ('select', [2, 0])]),
    'inflame-stack': ([('INFLAME', False), ('INFLAME', True), ('STRIKE_IRONCLAD', False)],
                       [('play', 'INFLAME'), ('play', 'INFLAME'), ('play', 'STRIKE_IRONCLAD')]),
    'uppercut-order': ([('UPPERCUT', False), ('STRIKE_IRONCLAD', False)],
                       [('play', 'UPPERCUT'), ('play', 'STRIKE_IRONCLAD'), ('end', None)]),
    'shockwave-duration': ([('SHOCKWAVE', True), ('STRIKE_IRONCLAD', False)],
                       [('play', 'SHOCKWAVE'), ('play', 'STRIKE_IRONCLAD'), ('end', None), ('end', None)]),
    'tremble-duration': ([('TREMBLE', True), ('STRIKE_IRONCLAD', False)],
                       [('play', 'TREMBLE'), ('play', 'STRIKE_IRONCLAD'), ('end', None)]),

    'artifact-prevention': ([('SHOCKWAVE', False)], [('power', ('ARTIFACT_POWER', 1, True)), ('play', 'SHOCKWAVE')]),
    'player-duration': ([('DEFEND_IRONCLAD', False)], [('power', ('WEAK_POWER', 2)), ('power', ('VULNERABLE_POWER', 2)), ('end', None), ('end', None)]),
    'applier-retention': ([('STRIKE_IRONCLAD', False)], [('power', ('STRENGTH_POWER', 2)), ('power', ('STRENGTH_POWER', 1, False, True)), ('power', ('STRENGTH_POWER', 1, False, False, False)), ('play', 'STRIKE_IRONCLAD')]),
    'signed-modifiers': ([('STRIKE_IRONCLAD', False), ('DEFEND_IRONCLAD', False)], [('power', ('STRENGTH_POWER', -2)), ('power', ('DEXTERITY_POWER', -3)), ('play', 'STRIKE_IRONCLAD'), ('play', 'DEFEND_IRONCLAD'), ('power', ('STRENGTH_POWER', 0)), ('power', ('STRENGTH_POWER', 2)), ('power', ('DEXTERITY_POWER', 5))]),
}

def native_start(host, setup, deck, extra=None):
    require(host.receive().get('type') == 'ready', 'Missing handshake')
    reply = host.send({'cmd': 'oracle_run_start', 'request_id': 'initial',
                       'setup': setup, 'scenario_deck': deck, **(extra or {})})
    require('state' in reply, f'Native start: {reply}')
    return reply


def native_step(host, operation, traces=None):
    if operation['cmd'] == 'scenario_card_played_probe':
        request = dict(operation, cmd='oracle_card_played_probe', request_id='consumer-probe')
    elif operation['cmd'] == 'scenario_duration':
        request = dict(operation, cmd='oracle_duration_scenario', request_id='probe')
    elif operation['cmd'] == 'scenario_power':
        request = dict(operation, cmd='oracle_power_scenario', request_id='scenario')
    else:
        action = operation['action']
        request = {'cmd': 'oracle_run_action', 'request_id': 'action', 'action': action['kind'],
                   'args': {k: v for k, v in action.items() if k != 'kind'}}
    reply = host.send(request)
    require('state' in reply, f'Native operation: {reply}')
    if traces is not None: traces.append(reply.get('dispatch_trace'))
    return settle(host, reply['state'])


def public_step(cpp, operation):
    action = operation.get('action', {})
    if action.get('kind') != 'select_cards':
        return cpp.send(operation)
    for candidate in action['indices']:
        reply = cpp.send({'cmd': 'action', 'action': {'kind': 'select_card', 'candidate_index': candidate}})
        require(reply.get('ok'), f'Partial C++ selection: {reply}')
    return cpp.send({'cmd': 'action', 'action': {'kind': 'confirm_selection'}})


def check(game_dir, output, simulator, validator, selected=None):
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'; logs.mkdir()
    setup = new_actions('ORACLECHECK01', 'ordered-powers', run=False)['setup']
    clock_step = setup['clock']['seconds_per_action']
    with reference_build(game_dir, logs) as (command, env, provenance):
        comparator = JsonProcess([str(validator)], None, logs / 'comparator', 30, log_protocol=False)
        try:
            for name, (cards, script) in CASES.items():
                if selected and name != selected: continue
                setup['clock']['seconds_per_action'] = 0 if any(kind == 'select' for kind, _ in script) else clock_step
                destination = output / name; destination.mkdir()
                deck = [{'id': 'CARD.' + card, 'upgraded': upgraded} for card, upgraded in cards]
                native = JsonProcess(command, env, logs / (name + '-capture'), 30, log_protocol=False)
                extra = {}
                if name.startswith('ashes-'):
                    extra['scenario_relics'] = ['RELIC.CHARONS_ASHES', 'RELIC.JOSS_PAPER']
                    extra['scenario_enemy_hp'] = 3 if name in ('ashes-joss-ending', 'ashes-purity-ending') else 6 if name == 'ashes-purity-ending-late' else 40
                    if name.startswith('ashes-purity-'):
                        extra['scenario_joss_counter'] = 4 if name == 'ashes-purity-ending' else 3
                    if name == 'ashes-blocked':
                        extra['scenario_enemy_block'] = 3
                elif name == 'joss-threshold':
                    extra['scenario_relics'] = ['RELIC.JOSS_PAPER']
                operations = []; states = []; traces = [None]
                try:
                    initial = native_start(native, setup, deck, extra); states.append(initial['state'])
                    for kind, payload in script:
                        if kind == 'power':
                            operation = {'cmd': 'scenario_power', 'id': 'POWER.' + payload[0], 'amount': payload[1], 'enemy_owner': payload[2] if len(payload) > 2 else False, 'applier_enemy': payload[3] if len(payload) > 3 else False, 'has_applier': payload[4] if len(payload) > 4 else True}
                        elif kind == 'select':
                            operation = {'cmd': 'action', 'action': {'kind': 'select_cards', 'indices': payload}}
                        elif kind == 'cardtrace':
                            operation = {'cmd': 'scenario_card_played_probe', 'card_index': 0, 'power_index': 0}
                        elif kind == 'duration':
                            operation = {'cmd': 'scenario_duration', 'operation': payload[0]}
                            if payload[1] is not None: operation['index'] = payload[1]
                        else:
                            action = {'kind': 'end_turn'}
                            if kind == 'play':
                                state = states[-1]
                                hand = state['combat']['players'][0]['piles']['Hand']
                                index = next(i for i, ref in enumerate(hand) if resolve(state, ref)['saved']['id'] == 'CARD.' + payload)
                                action = {'kind': 'play_card', 'card_index': index}
                                if payload in ('UPPERCUT', 'STRIKE_IRONCLAD', 'TREMBLE'):
                                    action['target_index'] = 0
                            operation = {'cmd': 'action', 'action': action}
                        states.append(native_step(native, operation, traces)); operations.append(operation)
                finally:
                    native.close()
                scenario = {'profile': 'ordered-powers-scenario-v1', 'setup': setup, 'scenario_deck': deck, 'operations': operations, **extra}
                identity = {k: initial[k] for k in ('assembly_mvid', 'runtime_version')}
                for filename, value in (('scenario.json', scenario), ('states.json', states), ('reference.json', dict(provenance, **identity)), ('trace.json', traces)):
                    (destination / filename).write_text(wire_json(value) + '\n')
                replay = JsonProcess(command, env, logs / (name + '-self-replay'), 30, log_protocol=False)
                try:
                    compare(comparator, states[0], native_start(replay, setup, deck, extra)['state'], name + ' native initial')
                    for index, operation in enumerate(operations):
                        replay_trace = []
                        compare(comparator, states[index + 1], native_step(replay, operation, replay_trace), f'{name} native {index + 1}')
                        require(replay_trace[0] == traces[index + 1], f'{name}: native dispatch trace differs')
                finally:
                    replay.close()
                cpp = JsonProcess([str(simulator)], None, logs / (name + '-cpp'), 30, log_protocol=False)
                correspondence = []; previous_boundary = None
                try:
                    for index, operation in enumerate([{'cmd': 'start', 'setup': setup, 'scenario_deck': deck, **extra}] + operations):
                        reply = public_step(cpp, operation)
                        require(reply.get('ok'), f'{name} C++ {index}: {reply}')
                        (destination / 'cpp-last.json').write_text(wire_json(reply['state']) + '\n')
                        compare(comparator, states[index], reply['state'], f'{name} C++ {index}')
                        require(reply.get('dispatch_trace') == traces[index], f'{name}: C++ dispatch trace differs: {reply.get("dispatch_trace")} vs {traces[index]}')
                        if index:
                            action = operation.get('action', {})
                            correspondence.append({'native_operation': index - 1, 'cpp_boundary_begin': previous_boundary,
                                'cpp_boundary_end': reply['boundary'], 'cpp_input_count': 0 if operation['cmd'] != 'action' else len(action['indices']) + 1 if action.get('kind') == 'select_cards' else 1})
                        previous_boundary = reply['boundary']
                finally:
                    cpp.close()
                scenario['boundary_correspondence'] = correspondence
                (destination / 'scenario.json').write_text(wire_json(scenario) + '\n')
                hashes = {f: hashlib.sha256((destination / f).read_bytes()).hexdigest() for f in ('scenario.json', 'states.json', 'reference.json', 'trace.json')}
                (destination / 'verification.json').write_text(wire_json({'native_self_replay': 'passed', 'cpp_exact_comparison': 'passed', 'boundaries': len(states), 'sha256': hashes}) + '\n')
                print(f'{name}: {len(states)} independently replayed native / C++ exact boundaries', flush=True)
        finally:
            comparator.close()


def check_saved(recording, simulator, validator, selected=None):
    """Replay source-controlled evidence without the local game installation."""
    logs = Path(tempfile.mkdtemp(prefix='sts2-ordered-powers-replay-'))
    comparator = JsonProcess([str(validator)], None, logs / 'compare', 30, log_protocol=False)
    count = 0
    try:
        for name in CASES:
            if selected and name != selected:
                continue
            directory = recording / name
            scenario = strict_json((directory / 'scenario.json').read_text())
            states = strict_json((directory / 'states.json').read_text())
            verification = strict_json((directory / 'verification.json').read_text())
            traces = strict_json((directory / 'trace.json').read_text())
            require(scenario['profile'] == 'ordered-powers-scenario-v1', 'Unsupported profile')
            require(verification['native_self_replay'] == 'passed', 'Missing native self-replay')
            for filename, digest in verification['sha256'].items():
                require(hashlib.sha256((directory / filename).read_bytes()).hexdigest() == digest,
                        f'{name}: evidence hash changed: {filename}')
            require(len(states) == len(scenario['operations']) + 1, 'Boundary count differs')
            start = {'cmd': 'start', **{key: value for key, value in scenario.items()
                     if key == 'setup' or key.startswith('scenario_')}}
            cpp = JsonProcess([str(simulator)], None, logs / name, 30, log_protocol=False)
            try:
                for index, operation in enumerate([start] + scenario['operations']):
                    reply = public_step(cpp, operation)
                    require(reply.get('ok'), f'{name} boundary {index}: {reply}; logs: {logs}')
                    compare(comparator, states[index], reply['state'], f'{name} boundary {index}')
                    require(reply.get('dispatch_trace') == traces[index], f'{name}: dispatch trace differs')
                    if index and 'boundary_correspondence' in scenario:
                        require(reply['boundary'] == scenario['boundary_correspondence'][index - 1]['cpp_boundary_end'], f'{name}: public boundary correspondence differs')
                    count += 1
            finally:
                cpp.close()
    finally:
        comparator.close()
    print(f'{count} exact power scenario boundaries passed; logs: {logs}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path)
    parser.add_argument('--recording-dir', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--simulator', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--case', choices=tuple(CASES))
    args = parser.parse_args()
    if args.recording_dir:
        check_saved(args.recording_dir.resolve(), args.simulator.resolve(), args.validator.resolve(), args.case)
    else:
        if not args.game_dir or not args.output_dir:
            parser.error('--game-dir and --output-dir are required to capture')
        check(args.game_dir, args.output_dir.resolve(), args.simulator.resolve(), args.validator.resolve(), args.case)
