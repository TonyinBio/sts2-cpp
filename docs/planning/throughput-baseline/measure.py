"""Diagnostic fresh-process victory timing; deliberately not a warm speedup test."""
import argparse
import gzip
import hashlib
import json
import platform
from pathlib import Path
import statistics
import subprocess
import sys
import time

repo = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(repo / 'tools/oracle'))
from reference_runtime import JsonProcess, action_request, reference_build, require, start_request, strict_json
from play import PlaySession

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--game-dir', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--repeat', type=int, default=3)
args = parser.parse_args()
require(args.repeat > 0, 'Repeat must be positive')
output = args.output_dir.resolve()
output.mkdir(parents=True, exist_ok=False)
fixture = repo / 'tests/parity/fixtures/reference/run-victory-1234/dense'
raw = gzip.decompress((fixture / 'actions.json.gz').read_bytes())
actions = strict_json(raw)
expected = strict_json(gzip.decompress((fixture / 'states.json.gz').read_bytes()))
require(hashlib.sha256(raw).hexdigest() == expected['actions_sha256'], 'Digest mismatch')
report = dict(scope='fresh process, setup through victory, privileged JSON per boundary; NOT warm throughput',
              python=sys.version, platform=platform.platform(),
              cpu=subprocess.check_output(['lscpu'], text=True),
              git_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
              actions_sha256=hashlib.sha256(raw).hexdigest(), runs=[])
started = time.perf_counter()
with reference_build(args.game_dir, output) as (command, env, provenance):
    report['build_seconds'] = time.perf_counter() - started
    report['provenance'] = provenance
    require(all(expected['reference'][k] == v for k, v in provenance.items()), 'Provenance mismatch')
    for index in range(args.repeat):
        launch = time.perf_counter()
        host = JsonProcess(command, env, output / f'run-{index}', 60, log_protocol=False)
        try:
            require(host.receive().get('type') == 'ready', 'Missing ready')
            ready = time.perf_counter()
            session = PlaySession(host, actions, provenance, None, None, 1)
            responses = [host.send(start_request(actions))]
            state, session.identity = session.response_state(responses[-1], 'initial', validate_state=False)
            require(dict(provenance, **session.identity) == expected['reference'], 'Runtime mismatch')
            for entry in actions['actions']:
                response = host.send(action_request(actions, entry['action'], state, entry['id']))
                state, _ = session.response_state(response, entry['id'], validate_state=False)
                responses.append(response)
            finished = time.perf_counter()
            require(state['boundary_kind'] == 'run_complete' and state['run_ending_reason'] == 'victory', 'Not victory')
            # Compare after timing. Both sides were decoded with the repository strict JSON
            # parser. Retaining responses is diagnostic overhead included in timing.
            wanted = [expected['initial'], *[x['state'] for x in expected['transitions']]]
            require(len(responses) == len(wanted), 'Boundary count mismatch')
            require(all(x['state'] == y for x, y in zip(responses, wanted)), 'State mismatch')
            row = dict(launch_to_ready_seconds=ready-launch, setup_to_victory_seconds=finished-ready,
                       boundaries=len(responses), actions=len(actions['actions']),
                       state_comparison='all dense boundaries equal as parsed Python JSON values')
            report['runs'].append(row)
            print(json.dumps(row), flush=True)
        finally:
            host.close()
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
report['median_seconds'] = statistics.median(x['setup_to_victory_seconds'] for x in report['runs'])
report['script_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
(output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
