#!/usr/bin/env python3
"""Check full structured public output before permitting warm throughput samples.

No throughput samples are accepted if any matched public boundary differs.
The worker transports are benchmark adapters, not a public JSON product API.
"""
import argparse
import hashlib
import platform
from pathlib import Path
from reference_runtime import JsonProcess, reference_build, require, wire_json, strict_json


def first_difference(expected, actual, path=''):
    if isinstance(expected, dict) and isinstance(actual, dict):
        if expected.keys() != actual.keys():
            return {'path': path, 'expected_keys': sorted(expected), 'actual_keys': sorted(actual)}
        for key in expected:
            difference = first_difference(expected[key], actual[key], path + '/' + key)
            if difference: return difference
    elif isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            return {'path': path, 'expected_length': len(expected), 'actual_length': len(actual)}
        for index, (left, right) in enumerate(zip(expected, actual)):
            difference = first_difference(left, right, path + '/' + str(index))
            if difference: return difference
    elif expected != actual:
        return {'path': path, 'expected': expected, 'actual': actual}
    return None


def verify(game_dir, recording, worker, output, validator):
    output.mkdir(parents=True, exist_ok=False)
    scenario = strict_json((recording / 'scenario.json').read_bytes())
    report = {'profile': 'warm-public-structured-v1', 'status': 'running',
        'reference_configuration': 'Release', 'workers_per_engine': 1,
        'platform': platform.platform(), 'processor': platform.processor(),
        'worker_sha256': hashlib.sha256(worker.read_bytes()).hexdigest(),
        'scenario_sha256': hashlib.sha256((recording / 'scenario.json').read_bytes()).hexdigest(),
        'public_boundary_comparisons': 0, 'throughput_samples': [],
        'timing_status': 'not_started_public_equivalence_required'}
    def save(): (output / 'verification.json').write_text(wire_json(report) + '\n')
    save()
    try:
        with reference_build(game_dir, output, extra_sources=(Path(__file__),), configuration='Release') as (command, env, provenance):
            report['reference'] = provenance
            native = JsonProcess(command, env, output / 'native', 60, False)
            cpp = JsonProcess([str(worker)], None, output / 'cpp', 60, False)
            comparator = JsonProcess([str(validator)], None, output / 'comparator', 60, False)
            try:
                require(native.receive().get('type') == 'ready', 'Missing native handshake')
                for index, operation in enumerate([{'cmd': 'start', 'setup': scenario['setup']}] + scenario['operations']):
                    expected = native.send({'cmd': 'benchmark_operation', 'operation': operation})
                    actual = cpp.send(operation)
                    (output / 'native-last.json').write_text(wire_json(expected) + '\n')
                    (output / 'cpp-last.json').write_text(wire_json(actual) + '\n')
                    require(expected.get('ok') and actual.get('ok'), f'Worker operation failed: {expected}, {actual}')
                    native_caps, cpp_caps = expected['capabilities'], actual['capabilities']
                    require(len(native_caps) == len(cpp_caps), 'Capability diagnostic cardinality differs')
                    differences = [{'action_index': i, 'native': n, 'cpp': c} for i, (n, c) in enumerate(zip(native_caps, cpp_caps)) if n != c]
                    require(all(d['native'] == 0 and d['cpp'] == 9 for d in differences), 'Unexpected capability diagnostic difference')
                    report.setdefault('capability_diagnostics', []).append(dict(boundary=index, native=native_caps, cpp=cpp_caps))
                    if index == 0 and differences:
                        rejection = cpp.send({'cmd': 'capability_rejection', 'option_index': differences[0]['action_index']})
                        require(rejection.get('unsupported') and rejection.get('restriction') == 9 and rejection.get('checkpoint_unchanged'), 'Unsupported Neow option mutated complete state')
                        require(cpp.send({'cmd': 'query'}) == actual, 'Unsupported Neow option changed public output')
                        report['unsupported_neow_rejection'] = rejection
                    difference = first_difference(expected['observation'], actual['observation'])
                    if difference:
                        report.update(status='blocked_public_output_mismatch', boundary=index, operation=operation,
                                      difference=difference)
                        return report
                    audit = native.send({'cmd': 'benchmark_query_audit'})
                    comparison = comparator.send({'kind': 'compare', 'expected': wire_json(audit['before']), 'actual': wire_json(audit['after'])})
                    require(comparison.get('equal'), f'Native public query mutated complete state: {comparison}')
                    require(audit['observation'] == expected['observation'], 'Native repeat query changed public output')
                    cpp_audit = cpp.send({'cmd': 'query_audit'})
                    require(cpp_audit['checkpoint_unchanged'] and cpp_audit['observation'] == actual['observation'], 'C++ public query mutated complete state')
                    report.setdefault('observations', []).append(actual['observation'])
                    report['public_boundary_comparisons'] += 1
                report['status'] = 'public_output_passed'
                report['timing_status'] = 'not_measured_by_correctness_gate'
            finally: native.close(); cpp.close(); comparator.close()
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally: save()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--recording-dir', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.game_dir, args.recording_dir, args.worker.resolve(), args.output_dir, args.validator.resolve())
    print(wire_json(report))
    if report['status'] != 'public_output_passed': raise SystemExit(1)


if __name__ == '__main__': main()
