"""Recapture this victory route densely, compare sparse source states, and self-replay."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys

repo = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(repo / 'tools/oracle'))
from play import PlaySession, RecordingWriter
from recording_journal import export_recording
from reference_runtime import JsonProcess, ValidationSession, reference_build, require, wire_json
from replay import compare_checkpoint, load_pair, replay

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--game-dir', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--source-dir', type=Path, default=Path(__file__).resolve().parent / 'original')
parser.add_argument('--validator', type=Path, default=repo / 'build/release/parity_validate')
args = parser.parse_args()
source = args.source_dir.resolve()
output = args.output_dir.resolve()
output.mkdir(parents=True, exist_ok=False)
logs = output / 'logs'
logs.mkdir()
validator = args.validator.resolve()
actions, states = load_pair(source / 'actions.json', source / 'states.json', validator)
expected = {x['action_id']: x['state'] for x in states['transitions']}
new_actions = copy.deepcopy(actions)
new_actions['actions'] = []
with reference_build(args.game_dir, logs) as (command, env, provenance):
    (output / 'build-provenance.json').write_text(wire_json(provenance) + '\n')
    print('Current build provenance: ' + wire_json(provenance), flush=True)
    host = JsonProcess(command, env, logs / 'capture', 30, log_protocol=False)
    session = PlaySession(host, new_actions, provenance, validator, RecordingWriter(output, validator), 1)
    try:
        session.start()
        compare_checkpoint(validator, states['initial'], session.state, 'original-initial')
        for index, entry in enumerate(actions['actions'], 1):
            session.step(entry['action'])
            require(session.actions['actions'][-1]['id'] == entry['id'], 'Action identity changed')
            if entry['id'] in expected:
                compare_checkpoint(validator, expected[entry['id']], session.state, 'original-' + entry['id'])
                print(f'Original checkpoint matches at {index}', flush=True)
        require(session.state['boundary_kind'] == 'run_complete' and session.state['run_ending_reason'] == 'victory', 'Not a victory')
        session.checkpoint()
        with ValidationSession(validator, logs / 'export-validation', timeout=600) as validation:
            export_recording(output, validation)
        reference = dict(provenance, **session.identity)
        (output / 'reference.json').write_text(wire_json(reference) + '\n')
    finally:
        host.close()
    with ValidationSession(validator, logs / 'load-validation', timeout=600) as validation:
        dense_actions, dense_states = load_pair(output / 'current/actions.json', output / 'current/states.json', validation)
    require(dense_actions == actions, 'Recapture changed action document')
    host = JsonProcess(command, env, logs / 'independent-replay', 30, log_protocol=False)
    try:
        counts = replay(host, dense_actions, dense_states, provenance, validator)
    finally:
        host.close()
    result = {'actions': counts[0], 'checkpoints': counts[1], 'original_checkpoints_compared': len(expected) + 1,
              'original_provenance': states['reference'], 'recapture_provenance': reference,
              'original_actions_sha256': hashlib.sha256((source / 'actions.json').read_bytes()).hexdigest(),
              'dense_actions_sha256': hashlib.sha256((output / 'current/actions.json').read_bytes()).hexdigest(),
              'original_state_comparison': 'all saved checkpoints equal', 'independent_dense_self_replay': 'passed'}
    (output / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
