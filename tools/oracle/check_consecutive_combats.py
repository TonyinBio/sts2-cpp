#!/usr/bin/env python3
"""Verify declared consecutive-room scenarios, separately from natural travel.

Scenario EnterRoom operations are diagnostic controls, never actions-v1 player
inputs. Native gameplay and cleanup use the existing recorder. Every published
logical boundary is compared with the existing exact native-state comparator.
"""
import argparse
import hashlib
import tempfile
from pathlib import Path

from native_run import exact_dump
from play import new_actions, resolve
from reference_runtime import (JsonProcess, reference_build, require, wire_json,
                               start_request, strict_json)


def settle(host, state):
    """Same forced-transition correspondence as the typed public run API."""
    for _ in range(4):
        flow = state.get('run_flow')
        if not flow or flow.get('pending_choice'):
            return state
        room = flow.get('room') or {}
        forced = (room.get('kind') == 'event' and room.get('finished')) or (
            room.get('kind') == 'combat_end' and not flow.get('reward_sets'))
        if not forced:
            return state
        state = host.send({'cmd': 'oracle_run_action', 'request_id': 'forced',
                           'action': 'proceed', 'args': {}})['state']
    raise RuntimeError('Forced transition did not settle')


def native_operation(host, operation):
    if operation['cmd'] == 'scenario_enter_combat':
        reply = host.send({'cmd': 'enter_room', 'type': 'combat',
                           'encounter': operation['encounter'].removeprefix('ENCOUNTER.')})
        require(reply.get('type') != 'error', 'Native scenario entry failed')
        return host.send({'cmd': 'oracle_run_state', 'request_id': 'scenario'})['state']
    action = operation['action']
    state = host.send({'cmd': 'oracle_run_action', 'request_id': 'public',
                       'action': action['kind'],
                       'args': {k: v for k, v in action.items() if k != 'kind'}})['state']
    return settle(host, state)


def compare(validator, expected, actual, label):
    result = validator.send({'kind': 'compare', 'expected': exact_dump(expected),
                             'actual': exact_dump(actual)})
    require(result.get('equal') is True, f'{label}: {result}')


def check(game_dir, output, simulator, validator):
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'; logs.mkdir()
    setup = new_actions('KERNELRUN14', 'consecutive-scenarios')['setup']
    operations = []; states = []
    with reference_build(game_dir, logs) as (command, env, provenance):
        host = JsonProcess(command, env, logs / 'capture', 30, log_protocol=False)
        try:
            require(host.receive().get('type') == 'ready', 'Missing native handshake')
            initial = host.send(start_request(new_actions('KERNELRUN14', 'scenario-capture')))
            state = initial['state']; states.append(state)
            def step(operation):
                nonlocal state
                state = native_operation(host, operation)
                operations.append(operation); states.append(state)
            def action(kind, **args):
                step({'cmd': 'action', 'action': {'kind': kind, **args}})
            action('choose_option', option_index=0)
            node = state['run_flow']['room']['choices'][0]
            action('select_map_node', col=node['col'], row=node['row'])
            for combat_index in range(2):
                if combat_index:
                    step({'cmd': 'scenario_enter_combat', 'encounter': 'ENCOUNTER.SHRINKER_BEETLE_WEAK'})
                for _ in range(200):
                    if state['combat'] is None:
                        break
                    hand = state['combat']['players'][0]['piles']['Hand']
                    chosen = None
                    for i, ref in enumerate(hand):
                        card = resolve(state, ref)
                        if card['saved']['id'] in ('CARD.STRIKE_IRONCLAD', 'CARD.BASH') and card['energy_cost']['base'] <= state['combat']['players'][0]['energy']:
                            chosen = i; break
                    if chosen is None: action('end_turn')
                    else: action('play_card', card_index=chosen, target_index=0)
                require(state['run_ending_reason'] is None and state['run_flow']['reward_sets'], 'Combat did not reach rewards')
                # Preserve the potion inventory into the second encounter.
                if combat_index == 0:
                    action('take_reward', index=0)
                    action('take_reward', index=1)
                action('skip_rewards')
            identity = {k: initial[k] for k in ('assembly_mvid', 'runtime_version')}
        finally:
            host.close()
        (output / 'states.json').write_text(wire_json(states) + '\n')
        (output / 'scenario.json').write_text(wire_json({'profile': 'run-scenario-v1', 'setup': setup, 'operations': operations}) + '\n')
        (output / 'reference.json').write_text(wire_json(dict(provenance, **identity)) + '\n')
        comparator = JsonProcess([str(validator)], None, logs / 'comparator', 30, log_protocol=False)
        try:
            # A fresh independently initialized native process must reproduce all
            # state; expected snapshots are never sent to either game engine.
            replay = JsonProcess(command, env, logs / 'self-replay', 30, log_protocol=False)
            try:
                require(replay.receive().get('type') == 'ready', 'Missing replay handshake')
                actual = replay.send(start_request(new_actions('KERNELRUN14', 'scenario-replay')))['state']
                compare(comparator, states[0], actual, 'native initial')
                for i, operation in enumerate(operations):
                    compare(comparator, states[i + 1], native_operation(replay, operation), f'native boundary {i + 1}')
            finally:
                replay.close()
            cpp = JsonProcess([str(simulator)], None, logs / 'cpp', 30, log_protocol=False)
            try:
                result = cpp.send({'cmd': 'start', 'setup': setup})
                require(result.get('ok'), f'C++ initialization: {result}')
                compare(comparator, states[0], result['state'], 'C++ initial')
                for i, operation in enumerate(operations):
                    result = cpp.send(operation)
                    require(result.get('ok'), f'C++ boundary {i + 1}: {result}')
                    (output / 'cpp-last.json').write_text(wire_json(result['state']) + '\n')
                    compare(comparator, states[i + 1], result['state'], f'C++ boundary {i + 1}')
            finally:
                cpp.close()
        finally:
            comparator.close()
    scenario = {'profile': 'run-scenario-v1', 'setup': setup, 'operations': operations,
                'correspondence': 'Forced native proceeds are automatic public transitions; scenario entry consumes no action clock.',
                'scope': 'One naturally entered Shrinker followed by declared native EnterRoom; no natural second-encounter claim.'}
    for name, value in (('scenario.json', scenario), ('states.json', states),
                        ('reference.json', dict(provenance, **identity))):
        (output / name).write_text(wire_json(value) + '\n')
    hashes = {name: hashlib.sha256((output / name).read_bytes()).hexdigest()
              for name in ('scenario.json', 'states.json', 'reference.json')}
    (output / 'verification.json').write_text(wire_json({'boundaries': len(states), 'native_self_replay': 'passed',
        'cpp_exact_comparison': 'passed', 'checkpoint_clone_resume': 'every C++ boundary', 'sha256': hashes}) + '\n')
    print(f'{len(states)} boundaries: independent native self-replay and public C++ exact comparison passed')


def check_saved(recording, simulator, validator):
    scenario = strict_json((recording / 'scenario.json').read_text())
    states = strict_json((recording / 'states.json').read_text())
    verification = strict_json((recording / 'verification.json').read_text())
    require(scenario['profile'] == 'run-scenario-v1', 'Unsupported scenario profile')
    require(verification['native_self_replay'] == 'passed', 'Fixture lacks independent native self-replay')
    for name, digest in verification['sha256'].items():
        require(hashlib.sha256((recording / name).read_bytes()).hexdigest() == digest, f'Fixture hash changed: {name}')
    require(len(states) == len(scenario['operations']) + 1, 'Scenario boundary count differs')
    logs = Path(tempfile.mkdtemp(prefix='sts2-consecutive-replay-'))
    cpp = JsonProcess([str(simulator)], None, logs / 'cpp', 30, log_protocol=False)
    comparator = JsonProcess([str(validator)], None, logs / 'compare', 30, log_protocol=False)
    try:
        for i, operation in enumerate([{'cmd': 'start', 'setup': scenario['setup']}] + scenario['operations']):
            result = cpp.send(operation)
            require(result.get('ok'), f'C++ boundary {i}: {result}; logs: {logs}')
            compare(comparator, states[i], result['state'], f'C++ boundary {i}; logs: {logs}')
    finally:
        cpp.close(); comparator.close()
    print(f'{len(states)} exact scenario boundaries passed; logs: {logs}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--recording-dir', type=Path)
    parser.add_argument('--simulator', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    args = parser.parse_args()
    if args.recording_dir:
        check_saved(args.recording_dir, args.simulator.resolve(), args.validator.resolve())
    else:
        if not args.game_dir or not args.output_dir:
            parser.error('--game-dir and --output-dir are required for live capture')
        check(args.game_dir, args.output_dir, args.simulator.resolve(), args.validator.resolve())


if __name__ == '__main__':
    main()
