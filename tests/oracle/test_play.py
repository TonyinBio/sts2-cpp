#!/usr/bin/env python3
"""Interactive recorder and matched-file publication checks; no game or .NET required."""
import argparse
import copy
import contextlib
import io
import hashlib
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/oracle'))
from play import PlaySession, RecordingWriter, new_actions, positive_int
from upstream_play import RecordingTransport, load_interface
from reference_runtime import JsonProcess, ValidationSession, strict_json, validate

VALIDATOR = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None
REPO = Path(__file__).resolve().parents[2]
FIXTURE = strict_json((REPO / 'tests/parity/fixtures/starter.states.json').read_bytes())


class FakeHost:
    def __init__(self, stop_after=None):
        self.requests = []
        self.count = 0
        self.stop_after = stop_after
        self.bad_response = None

    def receive(self):
        return {'type': 'ready'}

    def send(self, request):
        self.requests.append(request)
        if request['cmd'] == 'quit':
            return {'type': 'quit_result'}
        if request['cmd'] == 'get_map':
            return {'type': 'unavailable'}
        if request['cmd'] == 'oracle_display':
            return {'type': 'decision', 'decision': 'combat_play', 'round': 1,
                    'energy': 3, 'max_energy': 3, 'hand': [], 'enemies': [],
                    'player': {'name': 'Ironclad', 'hp': 80, 'max_hp': 80}}
        if request['cmd'] == 'oracle_run_action':
            self.count += 1
        state = copy.deepcopy(FIXTURE['initial'])
        state['run_time'] = self.count
        if self.stop_after == self.count:
            state['combat']['turn_control']['is_in_progress'] = False
            state['boundary_kind'] = 'run_complete'
            state['run_ending_reason'] = 'death'
        response = {'type': 'oracle_state', 'request_id': request['request_id'],
                    'assembly_mvid': 'fake', 'runtime_version': 'fake', 'state': state}
        if self.bad_response == 'identity':
            response['assembly_mvid'] = 'other'
        if self.bad_response == 'request':
            response['request_id'] = 'other'
        if self.bad_response == 'state':
            del state['native_model_state']
        if self.bad_response == 'interrupt':
            raise KeyboardInterrupt()
        if self.bad_response == 'missing_identity':
            del response['assembly_mvid']
        if self.bad_response == 'missing_state':
            del response['state']
        return response


class CommandTests(unittest.TestCase):
    def test_backend_errors_keep_stack_trace_in_stderr_log(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'error'
            code = 'import json; print(json.dumps({"type":"error","message":"event failed","stack_trace":"event stack trace"}))'
            host = JsonProcess([sys.executable, '-c', code], os.environ, prefix, 5,
                               log_protocol=False)
            try:
                with self.assertRaisesRegex(RuntimeError, 'event failed'):
                    host.receive()
            finally:
                host.close()
            self.assertIn('event stack trace', Path(str(prefix) + '.stderr.log').read_text())

    def test_positive_interval(self):
        self.assertEqual(positive_int('5'), 5)
        for value in ('0', '-1', '1.5', 'five'):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                positive_int(value)

    def test_transport_resolves_hand_positions_to_stable_ids(self):
        from types import SimpleNamespace
        from play import resolve
        state = FIXTURE['initial']
        actions = []
        bridge = RecordingTransport(SimpleNamespace(state=state, step=actions.append))
        bridge._action('play_card', {'card_index': 0, 'target_index': 0})
        self.assertEqual(actions[0]['card_instance'],
                         state['combat']['players'][0]['piles']['Hand'][0].rsplit('/', 1)[1])
        self.assertEqual(actions[0]['target'], 'enemy_0')
        self.assertNotIn('card_index', actions[0])
        self.assertNotIn('target_index', actions[0])

    def test_all_characters_and_ascensions_reach_recording_setup(self):
        for character in ('Ironclad', 'Silent', 'Defect', 'Regent', 'Necrobinder'):
            actions = new_actions('SEED', 'test', run=True, character=character, ascension=7)
            self.assertEqual(actions['setup']['character'], character)
            self.assertEqual(actions['setup']['ascension'], 7)

    def test_transport_preserves_combat_selection_identity_and_noncombat_indices(self):
        from types import SimpleNamespace
        state = copy.deepcopy(FIXTURE['initial'])
        actions = []
        bridge = RecordingTransport(SimpleNamespace(state=state, step=actions.append))
        refs = state['combat']['players'][0]['piles']['Hand']
        state['pending_choice'] = {'candidates': refs}
        bridge._action('select_cards', {'indices': '0,1'})
        self.assertEqual(actions[-1], {'kind': 'select_cards', 'card_instances':
                                      [ref.rsplit('/', 1)[1] for ref in refs[:2]]})
        state['pending_choice'] = None
        bridge._action('select_cards', {'indices': '0,1'})
        self.assertEqual(actions[-1], {'kind': 'select_cards', 'indices': [0, 1]})

    def test_implicit_transitions_and_rewards_are_recorded(self):
        from types import SimpleNamespace
        states = [
            {'room': {'kind': 'event', 'finished': True}, 'pending_choice': None, 'reward_sets': []},
            {'room': {'kind': 'treasure', 'picked': False}, 'pending_choice': None, 'reward_sets': []},
            {'room': {'kind': 'treasure', 'picked': True}, 'pending_choice': None, 'reward_sets': []},
            {'room': {'kind': 'combat_end'}, 'pending_choice': None,
             'reward_sets': [{'rewards': [{'kind': 'CardReward', 'taken': False}]}]},
        ]
        actions = []
        session = SimpleNamespace(state={'run_flow': states[0]}, finished=lambda: False)
        def step(action):
            actions.append(action)
            session.state = {'run_flow': states[len(actions)]} if len(actions) < len(states) else {
                'pending_choice': {'kind': 'card_reward'}}
        session.step = step
        RecordingTransport(session)._settle_interface()
        self.assertEqual(actions, [{'kind': 'proceed'}, {'kind': 'pick_treasure', 'index': 0},
                                  {'kind': 'proceed'}, {'kind': 'take_reward', 'index': 0}])

    def test_renderers_remain_the_upstream_functions(self):
        import types
        source = (REPO / 'sts2-cli/python/play.py').read_text()
        module = load_interface()
        original = compile(source, str(REPO / 'sts2-cli/python/play.py'), 'exec')
        for code in original.co_consts:
            if isinstance(code, types.CodeType) and code.co_name.startswith('show_'):
                self.assertEqual(getattr(module, code.co_name).__code__.co_code,
                                 code.co_code, code.co_name)

    def test_skipped_rewards_are_offered_once_and_other_rewards_remain_available(self):
        from types import SimpleNamespace
        for kinds in (['CardReward', 'CardReward'], ['PotionReward', 'CardReward']):
            with self.subTest(kinds=kinds):
                actions = []
                flow = {'room': {'kind': 'combat_end'}, 'pending_choice': None,
                        'reward_sets': [{'rewards': [{'kind': kind, 'taken': False} for kind in kinds]}]}
                session = SimpleNamespace(state={'run_flow': flow}, finished=lambda: False)
                def step(action):
                    actions.append(action)
                    if action['kind'] == 'take_reward':
                        if flow['reward_sets'][-1]['rewards'][action['index']]['kind'] == 'CardReward':
                            flow['pending_choice'] = {'kind': 'card_reward'}
                    elif action['kind'] == 'skip_card_reward':
                        flow['pending_choice'] = None
                    elif action['kind'] == 'skip_rewards':
                        flow['reward_sets'] = []
                        flow['room'] = {'kind': 'map'}
                session.step = step
                session.host = SimpleNamespace(send=lambda _: {'decision':
                    'card_reward' if flow['pending_choice'] else 'map_select'})
                bridge = RecordingTransport(session)
                bridge._settle_interface()
                if kinds[0] == 'CardReward':
                    self.assertEqual(bridge({'cmd': 'action', 'action': 'skip_card_reward'})['decision'],
                                     'card_reward')
                result = bridge({'cmd': 'action', 'action': 'skip_card_reward'})
                self.assertEqual(result['decision'], 'map_select')
                self.assertEqual([action['index'] for action in actions if action['kind'] == 'take_reward'],
                                 [0, 1])
                self.assertEqual(actions[-1], {'kind': 'skip_rewards'})
                # A later reward set must get fresh offers even at the same depth.
                flow['reward_sets'] = [{'rewards': [{'kind': 'CardReward', 'taken': False}]}]
                flow['room'] = {'kind': 'combat_end'}
                bridge._settle_interface()
                self.assertEqual(actions[-1], {'kind': 'take_reward', 'index': 0})
                self.assertIsNotNone(flow['pending_choice'])

    def test_card_reward_accepts_skip_when_display_allows_it(self):
        interface = load_interface()
        interface.LANG = 'en'
        commands = []
        def send(command):
            commands.append(command)
            if command['cmd'] == 'start_run':
                return {'type': 'decision', 'decision': 'card_reward', 'can_skip': True,
                        'cards': [{'index': 0, 'name': 'Strike'}]}
            if command['cmd'] == 'quit':
                return {'type': 'quit_result'}
            return None
        output = io.StringIO()
        with patch('builtins.input', return_value='s') as read, contextlib.redirect_stdout(output):
            interface.play(seed='REWARD', log=False, command_sender=send)
        self.assertEqual(read.call_count, 1)
        self.assertIn({'cmd': 'action', 'action': 'skip_card_reward'}, commands)
        self.assertNotIn('Invalid. Options:', output.getvalue())


@unittest.skipUnless(VALIDATOR, 'Pass parity_validate executable')
class InteractiveRecorderTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.host = FakeHost()
        self.validation = ValidationSession(VALIDATOR, self.directory / 'validator')
        self.validation.__enter__()
        self.addCleanup(self.validation.__exit__, None, None, None)
        self.writer = RecordingWriter(self.directory, self.validation)
        provenance = {key: FIXTURE['reference'][key]
                      for key in ('revision', 'game_dll_sha256', 'adapter_sha256')}
        self.session = PlaySession(self.host, new_actions('ORACLECHECK01', 'interactive-test', run=True),
                                   provenance, self.validation, self.writer, 5)

    def saved(self):
        directory = (self.directory / 'current').resolve()
        raw = (directory / 'actions.json').read_bytes()
        actions = strict_json(raw)
        states = strict_json((directory / 'states.json').read_bytes())
        self.assertEqual(states['actions_sha256'], hashlib.sha256(raw).hexdigest())
        self.assertEqual(strict_json((directory / 'reference.json').read_bytes()), states['reference'])
        validate(VALIDATOR, 'states', states, actions)
        return actions, states

    def run_commands(self, commands):
        lines = iter(commands)
        def read(_):
            try:
                value = next(lines)
            except StopIteration:
                raise EOFError from None
            if isinstance(value, BaseException):
                raise value
            return value
        output = io.StringIO()
        interface = load_interface()
        interface.LANG = 'en'
        interface.SAVE_DIR = str(self.directory / 'saves')
        transport = RecordingTransport(self.session)
        with patch('builtins.input', side_effect=read), contextlib.redirect_stdout(output):
            interface.play(seed='ORACLECHECK01', log=False, command_sender=transport,
                           checkpoint=lambda: self.session.publish(final=True))
        return output.getvalue().splitlines()

    def test_every_action_autosaves_but_only_periodic_states_accumulate(self):
        self.session.start()
        for _ in range(6):
            self.session.step({'kind': 'end_turn'})
        actions, states = self.saved()
        self.assertEqual(len(actions['actions']), 6)
        self.assertEqual([step['action_id'] for step in states['transitions']], ['action-000005'])
        self.assertEqual(states['transitions'][0]['state']['run_time'], 5)
        self.session.publish(final=True)
        actions, states = self.saved()
        self.assertEqual([step['action_id'] for step in states['transitions']],
                         ['action-000005', 'action-000006'])
        self.assertEqual(states['transitions'][-1]['state']['run_time'], 6)
        self.assertEqual(len(list(self.directory.glob('.pending-*'))), 1)

    def test_manual_save_and_quit_do_not_become_game_actions(self):
        self.run_commands(['e', 'save', 'save', 'e', 'quit', 'n'])
        actions, states = self.saved()
        self.assertEqual(len(actions['actions']), 2)
        self.assertEqual([s['action_id'] for s in states['transitions']],
                         ['action-000001', 'action-000002'])
        self.assertEqual(len([r for r in self.host.requests if r['cmd'] == 'oracle_run_action']), 2)

    def test_inspection_commands_do_not_record_gameplay(self):
        self.run_commands(['map', 'deck', 'potions', 'relics', 'help', 'quit', 'n'])
        actions, states = self.saved()
        self.assertEqual(actions['actions'], [])
        self.assertEqual(states['transitions'], [])
        self.assertFalse(any(r['cmd'] == 'oracle_run_action' for r in self.host.requests))

    def test_abandon_is_recorded_but_quit_is_not(self):
        self.run_commands(['abandon', 'y'])
        actions, _ = self.saved()
        self.assertEqual([entry['action'] for entry in actions['actions']], [{'kind': 'abandon'}])

    def test_bad_input_does_not_dispatch_actions(self):
        messages = self.run_commands(['999', 'invalid', 'quit', 'n'])
        self.assertEqual(self.saved()[0]['actions'], [])
        self.assertTrue(any('Invalid. Options:' in message for message in messages))
        self.assertFalse(any(r['cmd'] == 'oracle_run_action' for r in self.host.requests))

    def test_eof_and_ctrl_c_save_final_partial_interval(self):
        for ending in (EOFError(), KeyboardInterrupt()):
            with self.subTest(ending=ending):
                # Fresh writer/session for each interruption path.
                self.setUp()
                self.run_commands(['e', ending])
                actions, states = self.saved()
                self.assertEqual(len(actions['actions']), 1)
                self.assertEqual(states['transitions'][0]['action_id'], 'action-000001')

    def test_combat_end_forces_final_checkpoint_and_stops_prompting(self):
        self.host.stop_after = 2
        self.run_commands(['e', 'e'])
        actions, states = self.saved()
        self.assertEqual(len(actions['actions']), 2)
        self.assertEqual(states['transitions'][0]['action_id'], 'action-000002')
        self.assertEqual(states['transitions'][0]['state']['boundary_kind'], 'run_complete')

    def test_invalid_responses_leave_previous_pair_and_action_prefix_intact(self):
        self.session.start()
        for bad in ('identity', 'request', 'state', 'missing_identity', 'missing_state'):
            self.host.bad_response = bad
            with self.subTest(bad=bad), self.assertRaises(RuntimeError):
                self.session.step({'kind': 'end_turn'})
            self.assertEqual(self.saved()[0]['actions'], [])
            self.assertEqual(self.session.actions['actions'], [])

    def test_interrupt_during_action_saves_only_validated_prefix(self):
        self.session.start()
        self.host.bad_response = 'interrupt'
        transport = RecordingTransport(self.session)
        with self.assertRaises(KeyboardInterrupt):
            transport({'cmd': 'action', 'action': 'end_turn'})
        self.host.bad_response = None
        transport({'cmd': 'quit'})
        actions, states = self.saved()
        self.assertEqual(self.host.count, 1)
        self.assertEqual(actions['actions'], [])
        self.assertEqual(states['transitions'], [])
        self.assertEqual(states['initial']['run_time'], 0)

    def test_interrupt_immediately_after_publish_cannot_break_current(self):
        self.session.start()
        replace = os.replace
        def interrupt_after_link(source, target):
            result = replace(source, target)
            if Path(target).name == 'current':
                raise KeyboardInterrupt()
            return result
        with patch('play.os.replace', side_effect=interrupt_after_link), self.assertRaises(KeyboardInterrupt):
            self.session.step({'kind': 'end_turn'})
        actions, states = self.saved()
        self.assertEqual(len(actions['actions']), 1)
        self.assertEqual(self.writer.generation, (self.directory / 'current').resolve())
        self.assertEqual(len(list(self.directory.glob('.pending-*'))), 1)

    def test_publication_failure_preserves_previous_matched_pair(self):
        self.session.start()
        before = (self.directory / 'current').resolve()
        replace = os.replace
        def fail_link(source, target):
            if Path(target).name == 'current':
                raise OSError('simulated publication failure')
            return replace(source, target)
        with patch('play.os.replace', side_effect=fail_link), self.assertRaisesRegex(OSError, 'publication failure'):
            self.session.step({'kind': 'end_turn'})
        self.assertEqual((self.directory / 'current').resolve(), before)
        self.assertEqual(self.saved()[0]['actions'], [])
        self.assertEqual(len(list(self.directory.glob('.pending-*'))), 1)

    def test_actions_share_one_validator_process_with_publication(self):
        pid = self.validation.host.process.pid
        with patch('reference_runtime.subprocess.run', side_effect=AssertionError('Unexpected validator subprocess')):
            self.session.start()
            self.session.step({'kind': 'end_turn'})
            self.session.publish(final=True)
        self.assertEqual(self.validation.host.process.pid, pid)
        self.assertIsNone(self.validation.host.process.poll())
        self.saved()  # Independently validate the published pair in a fresh process.

    def test_validator_exit_keeps_the_previous_pair_and_action_prefix(self):
        self.session.start()
        previous = (self.directory / 'current').resolve()
        self.validation.host.close()
        with self.assertRaises(RuntimeError):
            self.session.step({'kind': 'end_turn'})
        self.assertEqual((self.directory / 'current').resolve(), previous)
        self.assertEqual(self.session.actions['actions'], [])
        self.assertEqual(self.host.count, 0)
        self.saved()

    def test_cached_snapshots_do_not_hide_mutations_or_invalid_metadata(self):
        self.session.start()
        for _ in range(2):
            self.session.step({'kind': 'end_turn'})
            self.session.publish(final=True)
        previous = (self.directory / 'current').resolve()
        good = copy.deepcopy(self.session.sequence)
        for mutate in (
                lambda sequence: sequence['initial'].pop('native_model_state'),
                lambda sequence: sequence['transitions'][-1]['state'].pop('native_model_state'),
                lambda sequence: sequence['transitions'].reverse(),
                lambda sequence: sequence['transitions'][0].update(action_id='unknown')):
            sequence = copy.deepcopy(good)
            mutate(sequence)
            with self.assertRaises(RuntimeError):
                self.writer.publish(self.session.actions, sequence,
                                    dict(self.session.provenance, **self.session.identity))
            self.assertEqual((self.directory / 'current').resolve(), previous)
        bad_reference = dict(self.session.provenance, **self.session.identity, adapter_sha256='invalid')
        with self.assertRaises(RuntimeError):
            self.writer.publish(self.session.actions, good, bad_reference)
        bad_actions = copy.deepcopy(self.session.actions)
        bad_actions['actions'][-1]['action']['kind'] = 'unknown'
        with self.assertRaises(RuntimeError):
            self.writer.publish(bad_actions, good, dict(self.session.provenance, **self.session.identity))
        self.assertEqual((self.directory / 'current').resolve(), previous)
        # Failed requests neither poison the cache nor kill the validator.
        self.session.publish(final=True)
        self.saved()


@unittest.skipUnless(VALIDATOR, 'Pass parity_validate executable')
class CanonicalReplayTests(unittest.TestCase):
    setUp = InteractiveRecorderTests.setUp

    def pair(self):
        self.session.start()
        for _ in range(7):
            self.session.step({'kind': 'end_turn'})
        self.session.publish(final=True)
        from replay import load_pair
        current = self.directory / 'current'
        return load_pair(current / 'actions.json', current / 'states.json', VALIDATOR)

    def test_retained_corpus_digest_schema_and_logical_clocks(self):
        from replay import load_pair
        for action_path in sorted((REPO / 'tests/parity/fixtures/reference').glob('*/actions.json')):
            root = action_path.parent
            scenario = root.name
            actions, states = load_pair(root / 'actions.json', root / 'states.json', VALIDATOR)
            clock = actions['setup']['clock']
            positions = {entry['id']: index for index, entry in enumerate(actions['actions'], 1)}
            checkpoints = [('initial', states['initial'])] + [
                (entry['action_id'], entry['state']) for entry in states['transitions']]
            for boundary, state in checkpoints:
                elapsed = 0 if boundary == 'initial' else positions[boundary] * clock['seconds_per_action']
                with self.subTest(scenario=scenario, boundary=boundary):
                    self.assertEqual(state['start_time'], clock['epoch_seconds'])
                    self.assertEqual(state['save_time'], clock['epoch_seconds'] + elapsed)
                    self.assertEqual(state['run_time'], elapsed)

    def test_sparse_replay_executes_every_action_once(self):
        from replay import replay
        actions, states = self.pair()
        host = FakeHost()
        self.assertEqual(replay(host, actions, states, self.session.provenance, VALIDATOR), (7, 3))
        self.assertEqual(host.count, 7)
        self.assertEqual([r['request_id'] for r in host.requests],
                         ['initial'] + [a['id'] for a in actions['actions']])

    def test_clock_divergence_stops_before_any_action(self):
        from replay import replay, ReplayMismatch
        actions, states = self.pair()
        states['initial']['start_time'] += 1
        host = FakeHost()
        with self.assertRaisesRegex(ReplayMismatch, 'initial.*start_time'):
            replay(host, actions, states, self.session.provenance, VALIDATOR)
        self.assertEqual(host.count, 0)

    def test_checkpoint_divergence_reports_action(self):
        from replay import replay, ReplayMismatch
        actions, states = self.pair()
        states['transitions'][0]['state']['run_time'] += 1
        host = FakeHost()
        with self.assertRaisesRegex(ReplayMismatch, 'action-000005.*run_time'):
            replay(host, actions, states, self.session.provenance, VALIDATOR)
        self.assertEqual(host.count, 5)

    def test_provenance_rejected_before_execution(self):
        from replay import replay
        actions, states = self.pair()
        host = FakeHost()
        with self.assertRaisesRegex(RuntimeError, 'provenance'):
            replay(host, actions, states, dict(self.session.provenance, adapter_sha256='0' * 64), VALIDATOR)
        self.assertEqual(host.requests, [])

    def test_digest_checks_exact_file_bytes(self):
        from replay import load_pair
        self.pair()
        current = self.directory / 'current'
        with (current / 'actions.json').open('a') as stream:
            stream.write(' ')
        with self.assertRaisesRegex(RuntimeError, 'digest'):
            load_pair(current / 'actions.json', current / 'states.json', VALIDATOR)

    def test_legacy_clock_is_explicitly_unreplayable(self):
        from replay import load_pair
        actions = new_actions('ORACLECHECK01', 'legacy', run=True)
        del actions['setup']['clock']
        self.session.actions = actions
        self.session.start()
        current = self.directory / 'current'
        with self.assertRaisesRegex(RuntimeError, 'no reproducible clock'):
            load_pair(current / 'actions.json', current / 'states.json', VALIDATOR)


if __name__ == '__main__':
    unittest.main()
