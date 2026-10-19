#!/usr/bin/env python3
"""Offline recorder failure tests; no game or .NET required."""
import json
import hashlib
from decimal import Decimal
import os
from pathlib import Path
import sys
import tempfile
import unittest


# Allow direct execution without installing the reference tools as a package.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/oracle'))

from record import JsonProcess, atomic_write, record_session, strict_json, validate, start_request, action_request

VALIDATOR = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None

class RecorderTests(unittest.TestCase):
    def test_rejects_duplicate_keys_and_non_json_numbers(self):
        for text in ('{"a":1,"a":2}', '{"a":NaN}', '{"a":Infinity}'):
            with self.subTest(text=text), self.assertRaises((ValueError, RuntimeError)):
                strict_json(text)

    def test_run_parity_capture_validates_and_matches_action_digest(self):
        if VALIDATOR is None:
            self.skipTest('Pass parity_validate executable')
        repo = Path(__file__).resolve().parents[2]
        raw = (repo / 'tests/parity/examples/starter.actions.json').read_bytes()
        actions = strict_json(raw)
        states = strict_json((repo / 'tests/parity/fixtures/starter.states.json').read_bytes())
        self.assertEqual(states['actions_sha256'], hashlib.sha256(raw).hexdigest())
        validate(VALIDATOR, 'states', states, actions)

    def test_decimal_wire_and_publication_are_lossless(self):
        from record import wire_json
        value = strict_json('{"amount":0.1234567890123456789012345678}')
        self.assertEqual(value['amount'], Decimal('0.1234567890123456789012345678'))
        self.assertEqual(strict_json(wire_json(value)), value)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'exact.json'
            atomic_write(path, value)
            self.assertEqual(strict_json(path.read_text()), value)

    def test_process_crash_and_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            for code, expected in (("import sys; sys.exit(3)", 'exited'),
                                   ("import time; time.sleep(10)", 'timed out')):
                host = JsonProcess([sys.executable, '-c', code], os.environ,
                                   Path(directory) / 'fake', 0.2)
                try:
                    with self.assertRaisesRegex(RuntimeError, expected):
                        host.receive()
                finally:
                    host.close()

    def test_malformed_json_is_not_treated_as_a_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            host = JsonProcess([sys.executable, '-c', 'print("{broken}")'], os.environ,
                               Path(directory) / 'fake', 2)
            try:
                with self.assertRaisesRegex(RuntimeError, 'Malformed'):
                    host.receive()
            finally:
                host.close()

    def test_failed_atomic_publication_preserves_previous_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'states.json'
            path.write_text('previous results')
            with self.assertRaises(TypeError):
                atomic_write(path, {'invalid': object()})
            self.assertEqual(path.read_text(), 'previous results')
            self.assertEqual(list(Path(directory).glob('*.tmp')), [])
            atomic_write(path, {'valid': 1})
            self.assertEqual(json.loads(path.read_text()), {'valid': 1})

    def test_transcript_drains_stderr_and_preserves_diagnostics(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'fake'
            code = 'import sys,json; sys.stderr.write("x"*100000); print("engine diagnostic"); print(json.dumps({"type":"ready"}),flush=True)'
            host = JsonProcess([sys.executable, '-c', code], os.environ, prefix, 2)
            try:
                self.assertEqual(host.receive(), {'type': 'ready'})
                host.process.wait(timeout=2)
            finally:
                host.close()
            self.assertIn('engine diagnostic', Path(str(prefix) + '.stdout.log').read_text())
            self.assertEqual(len(Path(str(prefix) + '.stderr.log').read_text()), 100000)

    def test_uses_cpp_validation(self):
        if VALIDATOR is None:
            self.skipTest('Pass parity_validate executable')
        with self.assertRaisesRegex(RuntimeError, 'Invalid actions'):
            validate(VALIDATOR, 'actions', {'profile': 'wrong'})

    def fake_session(self, checkpoint_every=1, action_count=12, terminal_after=-1, invalid_after=-1):
        if VALIDATOR is None:
            self.skipTest('Pass parity_validate executable')
        repo = Path(__file__).resolve().parents[2]
        actions = strict_json((repo / 'tests/parity/examples/starter.actions.json').read_bytes())
        actions['actions'] = [{'id': f'action-{i}', 'action': {'kind': 'end_turn'}}
                              for i in range(1, action_count + 1)]
        # A protocol peer records every dispatched action and stamps the state with
        # its position, so the assertions verify execution as well as sampling.
        code = r"""
import json, sys
state = json.load(open(sys.argv[1]))['initial']
terminal_after, invalid_after = map(int, sys.argv[3:5])
count = 0
print(json.dumps({'type': 'ready'}), flush=True)
for line in sys.stdin:
    command = json.loads(line)
    if command['cmd'] == 'quit':
        print(json.dumps({'type': 'quit_result'}), flush=True)
        break
    if command['cmd'] == 'oracle_action':
        count += 1
        with open(sys.argv[2], 'a') as log:
            log.write(json.dumps(command['action']) + '\n')
    snapshot = json.loads(json.dumps(state))
    snapshot['run_time'] = count
    if count == terminal_after:
        snapshot.update(boundary_kind='run_complete', run_ending_reason='death', pending_choice=None)
        snapshot['combat']['turn_control']['is_in_progress'] = False
    if count == invalid_after:
        del snapshot['native_model_state']
    print(json.dumps({'type': 'oracle_state', 'request_id': command['request_id'],
                      'assembly_mvid': 'fake', 'runtime_version': 'fake', 'state': snapshot}), flush=True)
"""
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'actions.log'
            command = [sys.executable, '-c', code,
                       str(repo / 'tests/parity/fixtures/starter.states.json'), str(log),
                       str(terminal_after), str(invalid_after)]
            sequence, identity = record_session(command, os.environ, actions, VALIDATOR,
                                                Path(directory) / 'fake', 10, checkpoint_every)
            dispatched = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
            self.assertEqual(dispatched, [entry['action'] for entry in actions['actions']])
            return sequence

    def test_sparse_checkpoints_execute_every_action_and_include_final_partial_interval(self):
        value = self.fake_session(checkpoint_every=5)
        self.assertEqual([step['action_id'] for step in value['transitions']],
                         ['action-5', 'action-10', 'action-12'])
        self.assertEqual([step['state']['run_time'] for step in value['transitions']], [5, 10, 12])
        self.assertEqual(value['initial']['run_time'], 0)

    def test_default_records_every_action(self):
        self.assertEqual(len(self.fake_session(action_count=3)['transitions']), 3)

    def test_long_interval_still_records_final_action(self):
        value = self.fake_session(checkpoint_every=20, action_count=3)
        self.assertEqual([step['action_id'] for step in value['transitions']], ['action-3'])

    def test_zero_actions_records_only_initial_state(self):
        self.assertEqual(self.fake_session(checkpoint_every=5, action_count=0)['transitions'], [])

    def test_invalid_intermediate_state_is_rejected_even_when_not_saved(self):
        with self.assertRaisesRegex(RuntimeError, 'Invalid snapshot'):
            self.fake_session(checkpoint_every=5, invalid_after=2)

    def test_terminal_state_between_checkpoints_stops_following_actions(self):
        with self.assertRaisesRegex(RuntimeError, 'action after run completion'):
            self.fake_session(checkpoint_every=5, terminal_after=2)

    def test_final_terminal_state_is_retained_between_checkpoints(self):
        value = self.fake_session(checkpoint_every=5, action_count=2, terminal_after=2)
        self.assertEqual(value['transitions'][0]['state']['boundary_kind'], 'run_complete')

    def test_invalid_checkpoint_intervals_fail_before_launching_reference(self):
        for interval in (0, -1, 1.5, True):
            with self.subTest(interval=interval), self.assertRaisesRegex(RuntimeError, 'positive integer'):
                record_session([], {}, {}, None, Path('/unused'), 1, interval)

    def test_run_action_mapping_preserves_decisions(self):
        actions = {'setup': {'kind': 'seeded_run'}, 'required_features': ['run_flow_v1']}
        self.assertEqual(start_request(actions)['cmd'], 'oracle_run_start')
        state = {'combat': {'players': [{'piles': {'Hand': ['/combat/cards/card_9']}}],
                            'enemies': ['/combat/creatures/enemy_0'],
                            'creatures': {'enemy_0': {'monster': {'current_hp': 10}}}}}
        request = action_request(actions, {'kind': 'play_card', 'card_instance': 'card_9', 'target': 'enemy_0'}, state, 'a')
        self.assertEqual(request['args'], {'card_index': 0, 'target_index': 0})
        self.assertEqual(request['cmd'], 'oracle_run_action')
        for decision in ({'kind': 'select_cards', 'indices': [2, 0]},
                         {'kind': 'choose_option', 'option_index': 0}, {'kind': 'abandon'}):
            request = action_request(actions, decision, state, 'b')
            self.assertEqual(request['action'], decision['kind'])
            self.assertEqual(request['args'], {k:v for k,v in decision.items() if k != 'kind'})
        with self.assertRaisesRegex(RuntimeError, 'not in hand'):
            action_request(actions, {'kind': 'play_card', 'card_instance': 'card_1'}, state, 'c')

    def test_response_identity_mismatch_fails_before_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            code = 'import json,sys; print(json.dumps({"type":"ready"}),flush=True); sys.stdin.readline(); print(json.dumps({"type":"oracle_state","request_id":"wrong"}),flush=True)'
            with self.assertRaisesRegex(RuntimeError, 'Mismatched response'):
                record_session([sys.executable, '-c', code], os.environ,
                               {'setup': {'kind': 'seeded_combat'}, 'required_features': [], 'actions': []},
                               VALIDATOR, Path(directory) / 'fake', 2)

if __name__ == '__main__':
    unittest.main()
