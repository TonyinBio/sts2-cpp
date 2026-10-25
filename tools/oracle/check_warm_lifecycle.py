#!/usr/bin/env python3
"""Gate native warm reuse against fresh exact consecutive-scenario boundaries.

No throughput is reported by this diagnostic: full exports are correctness work,
not public observation work. Historical fixture bytes are read without rewriting.
"""
import argparse
import hashlib
from pathlib import Path
from check_consecutive_combats import compare, native_operation
from play import resolve
from reference_runtime import JsonProcess, reference_build, require, start_request, strict_json, wire_json


def verify(game_dir, recording, validator, output):
    output.mkdir(parents=True, exist_ok=False)
    scenario = strict_json((recording / 'scenario.json').read_bytes())
    require(scenario['profile'] == 'run-scenario-v1', 'Unsupported lifecycle scenario')
    fixture_states = strict_json((recording / 'states.json').read_bytes())
    logs = output / 'logs'; logs.mkdir()
    workloads = {'A': dict(setup=scenario['setup'], operations=scenario['operations']),
                 'B': dict(setup=dict(scenario['setup'], seed='WARMLOOP50'), operations=None)}
    report = {'profile': 'native-warm-lifecycle-v1', 'status': 'running',
              'configuration': 'Release', 'sequence': list('AABABBA'),
              'fixture_sha256': {name: hashlib.sha256((recording / name).read_bytes()).hexdigest()
                                 for name in ('scenario.json', 'states.json', 'reference.json')},
              'limits': ['B uses declared scenario EnterRoom for both encounters; no natural travel claim',
                         'no public output throughput or victory claim',
                         'pending choices/rewards/operations reject reset'], 'comparisons': 0}
    def save(): (output / 'verification.json').write_text(wire_json(report) + '\n')
    save()
    try:
        with reference_build(game_dir, logs, extra_sources=(Path(__file__),), configuration='Release') as (command, env, provenance):
            report['reference'] = provenance
            report['workloads'] = workloads
            comparator = JsonProcess([str(validator)], None, logs / 'comparator', 60, False)
            def execute(host, workload, expected=None):
                actions = {'setup': workload['setup'], 'required_features': ['run_flow_v1']}
                states = [host.send(start_request(actions))['state']]
                if workload['operations'] is None:
                    workload['operations'] = []
                    def step(operation):
                        states.append(native_operation(host, operation))
                        workload['operations'].append(operation)
                    def action(kind, **args): step({'cmd': 'action', 'action': dict(kind=kind, **args)})
                    action('choose_option', option_index=0)
                    for encounter in range(2):
                        step({'cmd': 'scenario_enter_combat', 'encounter': 'ENCOUNTER.SHRINKER_BEETLE_WEAK'})
                        for _ in range(200):
                            state = states[-1]
                            if state['combat'] is None: break
                            player = state['combat']['players'][0]
                            selected = None
                            for index, ref in enumerate(player['piles']['Hand']):
                                card = resolve(state, ref)
                                if card['saved']['id'] in ('CARD.STRIKE_IRONCLAD', 'CARD.BASH') and card['energy_cost']['base'] <= player['energy']:
                                    selected = index; break
                            if selected is None: action('end_turn')
                            else: action('play_card', card_index=selected, target_index=0)
                        require(states[-1]['run_ending_reason'] is None and states[-1]['run_flow']['reward_sets'], 'Workload did not reach rewards')
                        action('skip_rewards')
                else:
                    for operation in workload['operations']:
                        states.append(native_operation(host, operation))
                if expected is not None:
                    require(len(states) == len(expected), 'Boundary count changed')
                    for index, (truth, actual) in enumerate(zip(expected, states)):
                        compare(comparator, truth, actual, f'lifecycle boundary {index}')
                        report['comparisons'] += 1
                return states
            try:
                fresh = {}
                for key, workload in workloads.items():
                    host = JsonProcess(command, env, logs / f'fresh-{key}', 60, False)
                    try:
                        require(host.receive().get('type') == 'ready', 'Missing fresh handshake')
                        fresh[key] = execute(host, workload, fixture_states if key == 'A' else None)
                        (output / f'fresh-{key}.json').write_text(wire_json(fresh[key]) + '\n')
                    finally: host.close()
                warm = JsonProcess(command, env, logs / 'warm', 60, False)
                try:
                    require(warm.receive().get('type') == 'ready', 'Missing warm handshake')
                    for index, key in enumerate(report['sequence']):
                        execute(warm, workloads[key], fresh[key])
                        reset = warm.send({'cmd': 'oracle_reset'})
                        require(reset.get('reset') is True, 'Native reset was not acknowledged')
                        if index == 0: report['reset_listeners'] = reset['singleton_listeners']
                        require(reset['singleton_listeners'] == report['reset_listeners'], 'Singleton listener count grew')
                        report['completed_cycles'] = index + 1
                        save()
                finally: warm.close()
            finally: comparator.close()
        report['status'] = 'passed_scoped_clock_interleaving'
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = str(error)
        raise
    finally: save()
    print(f"Lifecycle gate: {report['status']}; {report['comparisons']} exact boundaries")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--recording-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    verify(args.game_dir, args.recording_dir, args.validator.resolve(), args.output_dir)


if __name__ == '__main__': main()
