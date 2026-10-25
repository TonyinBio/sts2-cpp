#!/usr/bin/env python3
"""Capture independently authored power scenarios; self-replay and compare exactly.

Custom deck / PowerCmd controls are diagnostic setup, never player actions.
No expected checkpoint is supplied to either gameplay implementation.
"""
import argparse
import hashlib
from pathlib import Path
from check_consecutive_combats import compare
from play import new_actions, resolve
from reference_runtime import JsonProcess, reference_build, require, wire_json

CASES = {
    'inflame-stack': ([('INFLAME', False), ('INFLAME', True), ('STRIKE_IRONCLAD', False)],
                       [('play', 'INFLAME'), ('play', 'INFLAME'), ('play', 'STRIKE_IRONCLAD')]),
    'uppercut-order': ([('UPPERCUT', False), ('STRIKE_IRONCLAD', False)],
                       [('play', 'UPPERCUT'), ('play', 'STRIKE_IRONCLAD'), ('end', None)]),
    'shockwave-duration': ([('SHOCKWAVE', True), ('STRIKE_IRONCLAD', False)],
                       [('play', 'SHOCKWAVE'), ('play', 'STRIKE_IRONCLAD'), ('end', None), ('end', None)]),
    'tremble-duration': ([('TREMBLE', True), ('STRIKE_IRONCLAD', False)],
                       [('play', 'TREMBLE'), ('play', 'STRIKE_IRONCLAD'), ('end', None)]),
    'signed-modifiers': ([('STRIKE_IRONCLAD', False), ('DEFEND_IRONCLAD', False)],
                       [('power', ('STRENGTH_POWER', -2)), ('power', ('DEXTERITY_POWER', -3)),
                        ('play', 'STRIKE_IRONCLAD'), ('play', 'DEFEND_IRONCLAD'),
                        ('power', ('STRENGTH_POWER', 2)), ('power', ('DEXTERITY_POWER', 5))]),
}

def native_start(host, setup, deck):
    require(host.receive().get('type') == 'ready', 'Missing handshake')
    reply = host.send({'cmd': 'oracle_run_start', 'request_id': 'initial',
                       'setup': setup, 'scenario_deck': deck})
    require('state' in reply, f'Native start: {reply}')
    return reply


def native_step(host, operation):
    if operation['cmd'] == 'scenario_power':
        request = dict(operation, cmd='oracle_power_scenario', request_id='scenario')
    else:
        action = operation['action']
        request = {'cmd': 'oracle_run_action', 'request_id': 'action', 'action': action['kind'],
                   'args': {k: v for k, v in action.items() if k != 'kind'}}
    reply = host.send(request)
    require('state' in reply, f'Native operation: {reply}')
    return reply['state']


def check(game_dir, output, simulator, validator):
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'; logs.mkdir()
    setup = new_actions('ORACLECHECK01', 'ordered-powers', run=False)['setup']
    with reference_build(game_dir, logs) as (command, env, provenance):
        comparator = JsonProcess([str(validator)], None, logs / 'comparator', 30, log_protocol=False)
        try:
            for name, (cards, script) in CASES.items():
                destination = output / name; destination.mkdir()
                deck = [{'id': 'CARD.' + card, 'upgraded': upgraded} for card, upgraded in cards]
                native = JsonProcess(command, env, logs / (name + '-capture'), 30, log_protocol=False)
                operations = []; states = []
                try:
                    initial = native_start(native, setup, deck); states.append(initial['state'])
                    for kind, payload in script:
                        if kind == 'power':
                            operation = {'cmd': 'scenario_power', 'id': 'POWER.' + payload[0], 'amount': payload[1]}
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
                        states.append(native_step(native, operation)); operations.append(operation)
                finally:
                    native.close()
                scenario = {'profile': 'ordered-powers-scenario-v1', 'setup': setup, 'scenario_deck': deck, 'operations': operations}
                identity = {k: initial[k] for k in ('assembly_mvid', 'runtime_version')}
                for filename, value in (('scenario.json', scenario), ('states.json', states), ('reference.json', dict(provenance, **identity))):
                    (destination / filename).write_text(wire_json(value) + '\n')
                replay = JsonProcess(command, env, logs / (name + '-self-replay'), 30, log_protocol=False)
                try:
                    compare(comparator, states[0], native_start(replay, setup, deck)['state'], name + ' native initial')
                    for index, operation in enumerate(operations):
                        compare(comparator, states[index + 1], native_step(replay, operation), f'{name} native {index + 1}')
                finally:
                    replay.close()
                cpp = JsonProcess([str(simulator)], None, logs / (name + '-cpp'), 30, log_protocol=False)
                try:
                    for index, operation in enumerate([{'cmd': 'start', 'setup': setup, 'scenario_deck': deck}] + operations):
                        reply = cpp.send(operation)
                        require(reply.get('ok'), f'{name} C++ {index}: {reply}')
                        (destination / 'cpp-last.json').write_text(wire_json(reply['state']) + '\n')
                        compare(comparator, states[index], reply['state'], f'{name} C++ {index}')
                finally:
                    cpp.close()
                hashes = {f: hashlib.sha256((destination / f).read_bytes()).hexdigest() for f in ('scenario.json', 'states.json', 'reference.json')}
                (destination / 'verification.json').write_text(wire_json({'native_self_replay': 'passed', 'cpp_exact_comparison': 'passed', 'boundaries': len(states), 'sha256': hashes}) + '\n')
                print(f'{name}: {len(states)} independently replayed native / C++ exact boundaries', flush=True)
        finally:
            comparator.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--simulator', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    args = parser.parse_args()
    check(args.game_dir, args.output_dir.resolve(), args.simulator.resolve(), args.validator.resolve())
