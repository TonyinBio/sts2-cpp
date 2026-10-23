"""Profiler accounting and real-play entry/exit behavior without a game installation."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/oracle'))
import profile_play
import reference_runtime


class SpeedProfileTests(unittest.TestCase):
    def test_nested_costs_are_exclusive_and_exceptions_do_not_poison_accounting(self):
        now = [0.0]
        profiler = profile_play.SpeedProfile(clock=lambda: now[0])
        profiler.change_phase('replay')
        with profiler.measure('outer'):
            now[0] = 2.0
            with self.assertRaises(ValueError), profiler.measure('inner'):
                now[0] = 5.0
                raise ValueError('simulated failure')
            now[0] = 7.0
        with profiler.measure('next'):
            now[0] = 8.0
        report = profiler.report()
        costs = {row['category']: row for row in report['costs']}
        self.assertEqual(costs['outer']['inclusive_seconds'], 7.0)
        self.assertEqual(costs['outer']['exclusive_seconds'], 4.0)
        self.assertEqual(costs['inner']['exclusive_seconds'], 3.0)
        self.assertEqual(costs['next']['exclusive_seconds'], 1.0)
        self.assertEqual(report['phase_seconds']['replay'], 8.0)

    def test_stops_before_input_measures_cleanup_and_restores_patches_between_runs(self):
        events = []
        original_input = __builtins__['input'] if isinstance(__builtins__, dict) else __builtins__.input
        original_wire = reference_runtime.wire_json
        def simulated_play():
            try:
                reference_runtime.wire_json({'setup': True})
                input('First prompt')
                events.append('unexpected input')
            finally:
                reference_runtime.wire_json({'export': True})
                events.append('cleanup')
        with tempfile.TemporaryDirectory() as temporary:
            for index in range(2):
                directory = Path(temporary) / str(index)
                with patch('profile_play.play.main', simulated_play):
                    report = profile_play.profile_run([], directory, capture_cprofile=True)
                self.assertEqual(report['status'], 'ready')
                self.assertIsNotNone(report['ready_seconds'])
                self.assertIn('shutdown', report['phase_seconds'])
                self.assertTrue((directory / 'python.prof').is_file())
                self.assertEqual(json.loads((directory / 'profile.json').read_text())['status'], 'ready')
                self.assertIn('First prompt', (directory / 'console.log').read_text())
                self.assertIs(reference_runtime.wire_json, original_wire)
                self.assertIs(__import__('builtins').input, original_input)
                costs = {(row['phase'], row['category']): row for row in report['costs']}
                self.assertEqual(costs[('startup', 'json_encode')]['calls'], 1)
                self.assertEqual(costs[('shutdown', 'json_encode')]['calls'], 1)
        self.assertEqual(events, ['cleanup', 'cleanup'])

    def test_failed_load_retains_report_and_never_claims_readiness(self):
        def simulated_play():
            raise RuntimeError('backend rejected saved action')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'failed'
            with patch('profile_play.play.main', simulated_play):
                report = profile_play.profile_run([], directory)
            self.assertEqual(report['status'], 'failed')
            self.assertIsNone(report['ready_seconds'])
            self.assertIn('backend rejected', report['error'])
            self.assertTrue((directory / 'profile.json').is_file())

    def test_unexpected_prompt_before_replay_is_a_failed_measurement(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch('profile_play.play.main', lambda: input('Unexpected prompt')):
                report = profile_play.profile_run([], Path(temporary) / 'failed', expected_commands=2)
            self.assertEqual(report['status'], 'failed')
            self.assertIn('before the saved commands completed', report['error'])

    def test_cli_repeats_preserve_source_and_forward_options_to_separate_recordings(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'game save.json'
            original = b'{"character":"Ironclad","seed":"TEST","actions":[]}'
            source.write_bytes(original)
            validator = root / 'validator binary'
            validator.write_bytes(b'test validator')
            destination = root / 'profile output'
            received = []
            def simulated_play():
                received.append(sys.argv[1:])
                input('Ready')
            arguments = ['profile_play.py', '--output-dir', str(destination), '--repeat', '2',
                         '--max-commands', '1', '--load', str(source), '--validator', str(validator),
                         '--checkpoint-every', '7', '--lang', 'en']
            with patch.object(sys, 'argv', arguments), patch('profile_play.play.main', simulated_play), \
                    contextlib.redirect_stdout(io.StringIO()):
                result = profile_play.main()
            self.assertEqual(result, 0)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(len(received), 2)
            for index, forwarded in enumerate(received, 1):
                self.assertEqual(forwarded[forwarded.index('--validator') + 1], str(validator))
                self.assertEqual(forwarded[forwarded.index('--checkpoint-every') + 1], '7')
                self.assertEqual(forwarded[forwarded.index('--output-dir') + 1],
                                 str(destination / f'run-{index:03d}' / 'recording'))
                effective = Path(forwarded[forwarded.index('--load') + 1])
                self.assertEqual(json.loads(effective.read_text())['actions'], [])
            summary = json.loads((destination / 'summary.json').read_text())
            self.assertEqual(summary['runs'], ['ready', 'ready'])
            self.assertEqual(len(summary['ready_seconds']), 2)


if __name__ == '__main__':
    unittest.main()
