#!/usr/bin/env python3
"""Measure only after fresh/warm exact state and full public output gates pass."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import random
import statistics
import time
from native_run import exact_dump
from reference_runtime import JsonProcess, reference_build, require, strict_json, wire_json
from check_warm_public import first_difference, verify


def resolve_public(previous, operation):
    """Resolve recorded intent against actual current public output, per worker."""
    if operation['cmd'] != 'action': return
    require(previous is not None, 'Action before actual public observation')
    action = operation['action']; kind = action['kind']
    def matches(choice):
        if choice['kind'] != kind: return False
        if kind == 'select_map_node': return choice['col'] == action['col'] and choice['row'] == action['row']
        field = {'choose_option': 'option_index', 'play_card': 'card_index', 'take_reward': 'index', 'discard_potion': 'potion_index'}.get(kind)
        return field is None or choice['index'] == action[field]
    offered = [(index, choice) for index, choice in enumerate(previous['observation']['choices']) if matches(choice)]
    require(len(offered) == 1 and offered[0][1]['enabled'], 'Requested intent is not currently public-legal')
    index, choice = offered[0]
    require(previous['capabilities'][index] == 0, 'Submitted unsupported operation must stop')
    if kind == 'play_card':
        hand = previous['observation']['combat']['hand']
        require(action['card_index'] < len(hand), 'Actual hand has no requested card')
        require(choice['target_enemy'] == ('target_index' in action), 'Actual public target requirement differs')
        if 'target_index' in action: require(action['target_index'] == 0, 'Unsupported public target')


def confidence(samples):
    rng = random.Random(50)
    ratios = []
    for _ in range(10000):
        block = [samples[rng.randrange(len(samples))] for _ in samples]
        ratios.append(statistics.median(x['native']['seconds'] for x in block) /
                      statistics.median(x['cpp']['seconds'] for x in block))
    ratios.sort()
    return {'method': 'paired block percentile bootstrap of ratio of medians; seed50;10000 resamples',
            'confidence': 0.95, 'lower': ratios[249], 'upper': ratios[9749]}


def measure(args):
    lifecycle = strict_json((args.lifecycle / 'verification.json').read_bytes())
    require(lifecycle['status'].startswith('passed_') and lifecycle.get('completed_cycles') == 7,
            'Native lifecycle isolation gate has not passed')
    require(lifecycle.get('reset_listeners') is not None, 'Lifecycle lacks singleton listener audit')
    require(args.samples >= 15, 'At least15 paired measured samples required')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    gate = verify(args.game_dir, args.recording_dir, args.worker.resolve(), args.output_dir / 'public-gate', args.validator.resolve())
    require(gate['status'] == 'public_output_passed',
            f'Public equivalence gate failed: {args.output_dir / "public-gate/verification.json"}')
    scenario = strict_json((args.recording_dir / 'scenario.json').read_bytes())
    operations = [{'cmd': 'start', 'setup': scenario['setup']}] + scenario['operations']
    report = {'profile': 'warm-public-structured-v1', 'status': 'running', 'workers_per_engine': 1,
        'hardware': {'platform': platform.platform(), 'cpu_affinity': sorted(os.sched_getaffinity(0)),
                     'cpuinfo': Path('/proc/cpuinfo').read_text(), 'loadavg': os.getloadavg()},
        'environment': {k: v for k, v in os.environ.items() if k.startswith(('DOTNET_', 'COMPlus_'))},
        'lifecycle_gate_sha256': hashlib.sha256((args.lifecycle / 'verification.json').read_bytes()).hexdigest(),
        'public_gate': gate, 'cpp_build_cache': {line.split('=', 1)[0]: line.split('=', 1)[1] for line in (args.worker.resolve().parent / 'CMakeCache.txt').read_text().splitlines() if line.startswith(('CMAKE_CXX_COMPILER:', 'CMAKE_CXX_FLAGS_RELEASE:', 'CMAKE_BUILD_TYPE:'))}, 'warmups': [], 'samples': [], 'notes': [
            'One worker per engine, same inherited hardware affinity; alternating paired order.',
            'Includes fresh run setup, previous state cleanup/reset, gameplay, validation, projection, JSON serialization, pipes, parsing, digest and count consumption.',
            'Excludes process startup/build/JIT initialization before warmups; privileged state comparison and checkpoints run in separate gates.',
            'No English/on-demand descriptions, victory, natural second encounter, or2x certification claim.',
            'No startup/kernel/snapshot/checkpoint cost is subtracted; these profiles are not separately measured here.']}
    def save(): (args.output_dir / 'samples.json').write_text(wire_json(report) + '\n')
    logs = args.output_dir / 'logs'; logs.mkdir()
    save()
    try:
        with reference_build(args.game_dir, logs, extra_sources=(Path(__file__),), configuration='Release') as (command, env, provenance):
            report['reference'] = provenance
            workers = {'native': JsonProcess(command, env, logs / 'native', 60, False),
                       'cpp': JsonProcess([str(args.worker.resolve())], None, logs / 'cpp', 60, False)}
            require(workers['native'].receive().get('type') == 'ready', 'Missing native handshake')
            report['native_runtime'] = workers['native'].send({'cmd': 'benchmark_runtime_info'})
            def run(engine, index, phase):
                host = workers[engine]; digest = hashlib.sha256(); byte_count = 0; records = 0
                observations = []; diagnostics = []; previous = None
                started = time.perf_counter_ns()
                for operation in operations:
                    resolve_public(previous, operation)
                    request = dict(cmd='benchmark_operation', operation=operation) if engine == 'native' else operation
                    response = host.send(request)
                    require(response.get('ok'), f'{engine} operation failed: {response}')
                    byte_count += len(host.last_response_line.encode('utf-8'))
                    # Consume every returned field, including separate implementation capability diagnostics.
                    digest.update(exact_dump(response, sort_keys=True).encode('utf-8')); records += 1
                    observations.append(response['observation']); diagnostics.append(response['capabilities']); previous = response
                reset = host.send({'cmd': 'oracle_reset', 'audit': False} if engine == 'native' else {'cmd': 'reset'})
                require(reset.get('reset'), 'Reset failed')
                byte_count += len(host.last_response_line.encode('utf-8'))
                digest.update(exact_dump(reset, sort_keys=True).encode('utf-8')); records += 1
                elapsed = (time.perf_counter_ns() - started) / 1e9
                sample = dict(seconds=elapsed, records=records, response_bytes=byte_count, consumed_sha256=digest.hexdigest())
                # Artifact writing is outside the measured region; consumption above is inside.
                (args.output_dir / f'{phase}-{index:03}-{engine}.json').write_text(wire_json(dict(observations=observations, capabilities=diagnostics)) + '\n')
                return sample, observations, diagnostics
            def paired(index, phase):
                pair = {}; observed = {}; capabilities = {}
                for engine in (['native', 'cpp'] if index % 2 == 0 else ['cpp', 'native']):
                    pair[engine], observed[engine], capabilities[engine] = run(engine, index, phase)
                require(first_difference(observed['native'], observed['cpp']) is None, 'Timed public output diverged')
                require(observed['native'] == gate['observations'], 'Warm public output drifted from fresh execution')
                for engine in ('native', 'cpp'):
                    require(capabilities[engine] == [entry[engine] for entry in gate['capability_diagnostics']], 'Capability diagnostics changed from fresh execution')
                return pair
            try:
                stabilized = False
                for index in range(50):
                    report['warmups'].append(paired(index, 'warmup')); save()
                    if (index + 1) >= 15 and (index + 1) % 5 == 0:
                        blocks = report['warmups'][-15:]
                        stabilized = all(max(medians := [statistics.median(x[engine]['seconds'] for x in blocks[i:i+5]) for i in (0, 5, 10)]) / min(medians) <= 1.05 for engine in ('native', 'cpp'))
                        if stabilized: break
                require(stabilized, 'Warmup failed three five-run block medians within5% by50 runs; inconclusive')
                for index in range(args.samples): report['samples'].append(paired(index, 'sample')); save()
                report['uncertainty'] = confidence(report['samples'])
                report['median_seconds'] = {engine: statistics.median(x[engine]['seconds'] for x in report['samples']) for engine in ('native', 'cpp')}
                report['status'] = 'scoped_measurement_complete'
            finally:
                for worker in workers.values(): worker.close()
    except Exception as error:
        report.update(status='inconclusive', error=str(error)); raise
    finally: save()
    print(wire_json({k: report[k] for k in ('status', 'median_seconds', 'uncertainty')}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('game-dir', 'recording-dir', 'worker', 'validator', 'lifecycle', 'output-dir'): parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--samples', type=int, default=15)
    measure(parser.parse_args())


if __name__ == '__main__': main()
