#!/usr/bin/env python3
"""Interactive recorder and matched-file publication checks; no game or .NET required."""
import argparse
import copy
import hashlib
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/oracle'))
from play import (PlaySession, RecordingWriter, new_actions, parse_action, play,
                  positive_int)
from reference_runtime import strict_json, validate

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
        if request['cmd'] == 'oracle_action':
            self.count += 1
        state = copy.deepcopy(FIXTURE['initial'])
        state['run_time'] = self.count
        if self.stop_after == self.count:
            state['combat']['turn_control']['is_in_progress'] = False
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
    def test_positive_interval(self):
        self.assertEqual(positive_int('5'), 5)
        for value in ('0', '-1', '1.5', 'five'):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                positive_int(value)

    def test_displayed_hand_positions_map_to_stable_action_ids(self):
        state = FIXTURE['initial']
        hand = state['combat']['players'][0]['piles']['Hand']
        for index, ref in enumerate(hand, 1):
            from play import resolve
            model = resolve(state, ref)['saved']['id']
            action = parse_action(['play', str(index)], state)
            self.assertEqual(action['card_instance'], ref.rsplit('/', 1)[1])
            if model == 'CARD.DEFEND_IRONCLAD':
                self.assertNotIn('target', action)
            else:
                self.assertEqual(action['target'], 'enemy_0')
        self.assertEqual(parse_action(['end'], state), {'kind': 'end_turn'})

    def test_bad_positions_targets_and_insufficient_energy_are_local_errors(self):
        for tokens in (['play', '0'], ['play', '999'], ['play', 'x'], ['play'], ['end', 'extra']):
            with self.subTest(tokens=tokens), self.assertRaises(ValueError):
                parse_action(tokens, FIXTURE['initial'])
        state = copy.deepcopy(FIXTURE['initial'])
        state['combat']['players'][0]['energy'] = 0
        with self.assertRaisesRegex(ValueError, 'energy'):
            parse_action(['play', '1'], state)
        from play import resolve
        for index, ref in enumerate(state['combat']['players'][0]['piles']['Hand'], 1):
            state['combat']['players'][0]['energy'] = 3
            model = resolve(state, ref)['saved']['id']
            with self.assertRaises(ValueError):
                parse_action(['play', str(index), '2'], state)


@unittest.skipUnless(VALIDATOR, 'Pass parity_validate executable')
class InteractiveRecorderTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.host = FakeHost()
        self.writer = RecordingWriter(self.directory, VALIDATOR)
        provenance = {key: FIXTURE['reference'][key]
                      for key in ('revision', 'game_dll_sha256', 'adapter_sha256')}
        self.session = PlaySession(self.host, new_actions('ORACLECHECK01', 'interactive-test'),
                                   provenance, VALIDATOR, self.writer, 5)

    def saved(self):
        directory = (self.directory / 'current').resolve()
        raw = (directory / 'actions.json').read_bytes()
        actions = strict_json(raw)
        states = strict_json((directory / 'states.json').read_bytes())
        self.assertEqual(states['actions_sha256'], hashlib.sha256(raw).hexdigest())
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
        messages = []
        play(self.session, read=read, write=messages.append)
        return messages

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

    def test_manual_save_interval_change_and_quit_do_not_become_game_actions(self):
        self.run_commands(['end', 'save', 'save', 'every 2', 'end', 'quit'])
        actions, states = self.saved()
        self.assertEqual(len(actions['actions']), 2)
        self.assertEqual([s['action_id'] for s in states['transitions']],
                         ['action-000001', 'action-000002'])
        self.assertEqual(len([r for r in self.host.requests if r['cmd'] == 'oracle_action']), 2)

    def test_inspection_commands_do_not_record_or_dispatch(self):
        self.run_commands(['map', 'open_map', 'open map', 'deck', 'inventory', 'state', 'help', 'quit'])
        actions, states = self.saved()
        self.assertEqual(actions['actions'], [])
        self.assertEqual(states['transitions'], [])
        self.assertEqual([r['cmd'] for r in self.host.requests], ['oracle_start'])

    def test_run_commands_use_run_backend_and_preserve_gameplay_only(self):
        self.session.actions = new_actions('PARITY', 'run-test', run=True)
        self.run_commands(['map', "act '{\"kind\":\"choose_option\",\"option_index\":0}'", 'deck', 'quit'])
        actions, states = self.saved()
        self.assertEqual([r['cmd'] for r in self.host.requests], ['oracle_run_start', 'oracle_run_action'])
        self.assertEqual(actions['actions'][0]['action'], {'kind': 'choose_option', 'option_index': 0})
        self.assertEqual(len(actions['actions']), 1)
        self.assertEqual(len(states['transitions']), 1)

    def test_bad_input_does_not_dispatch_actions(self):
        messages = self.run_commands(['play 999', 'every 0', 'every', 'end extra', '"', 'quit'])
        self.assertEqual(self.saved()[0]['actions'], [])
        self.assertTrue(any(message.startswith('Input:') for message in messages))
        self.assertFalse(any(r['cmd'] == 'oracle_action' for r in self.host.requests))

    def test_eof_and_ctrl_c_save_final_partial_interval(self):
        for ending in (EOFError(), KeyboardInterrupt()):
            with self.subTest(ending=ending):
                # Fresh writer/session for each interruption path.
                self.setUp()
                self.run_commands(['end', ending])
                actions, states = self.saved()
                self.assertEqual(len(actions['actions']), 1)
                self.assertEqual(states['transitions'][0]['action_id'], 'action-000001')

    def test_combat_end_forces_final_checkpoint_and_stops_prompting(self):
        self.host.stop_after = 2
        self.run_commands(['end', 'end'])
        actions, states = self.saved()
        self.assertEqual(len(actions['actions']), 2)
        self.assertEqual(states['transitions'][0]['action_id'], 'action-000002')
        self.assertEqual(states['transitions'][0]['state']['boundary_kind'], 'awaiting_input')

    def test_invalid_responses_leave_previous_pair_and_action_prefix_intact(self):
        self.session.start()
        for bad in ('identity', 'request', 'state', 'missing_identity', 'missing_state'):
            self.host.bad_response = bad
            with self.subTest(bad=bad), self.assertRaises(RuntimeError):
                self.session.step({'kind': 'end_turn'})
            self.assertEqual(self.saved()[0]['actions'], [])
            self.assertEqual(self.session.actions['actions'], [])

    def test_interrupt_during_action_saves_only_validated_prefix(self):
        def read(_):
            self.host.bad_response = 'interrupt'
            return 'end'
        play(self.session, read=read, write=lambda message: None)
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


if __name__ == '__main__':
    unittest.main()
