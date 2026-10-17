#!/usr/bin/env python3
"""Offline recorder failure tests; no game or .NET required."""
import json
import hashlib
import os
from pathlib import Path
import sys
import tempfile
import unittest

from record import JsonProcess, atomic_write, record_session, strict_json, validate

VALIDATOR = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None

class RecorderTests(unittest.TestCase):
    def test_rejects_duplicate_keys_and_non_json_numbers(self):
        for text in ('{"a":1,"a":2}', '{"a":NaN}', '{"a":Infinity}'):
            with self.subTest(text=text), self.assertRaises((ValueError, RuntimeError)):
                strict_json(text)

    def test_captured_fixture_validates_and_matches_action_digest(self):
        if VALIDATOR is None:
            self.skipTest('Pass parity_validate executable')
        repo = Path(__file__).resolve().parents[2]
        raw = (repo / 'tests/parity/examples/starter.actions.json').read_bytes()
        actions = strict_json(raw)
        states = strict_json((repo / 'tests/parity/fixtures/starter.states.json').read_bytes())
        self.assertEqual(states['actions_sha256'], hashlib.sha256(raw).hexdigest())
        validate(VALIDATOR, 'states', states, actions)

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

    def test_response_identity_mismatch_fails_before_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            code = 'import json,sys; print(json.dumps({"type":"ready"}),flush=True); sys.stdin.readline(); print(json.dumps({"type":"oracle_state","request_id":"wrong"}),flush=True)'
            with self.assertRaisesRegex(RuntimeError, 'Mismatched response'):
                record_session([sys.executable, '-c', code], os.environ,
                               {'setup': {}, 'required_features': [], 'actions': []},
                               VALIDATOR, Path(directory) / 'fake', 2)

if __name__ == '__main__':
    unittest.main()
