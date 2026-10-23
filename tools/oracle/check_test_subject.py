#!/usr/bin/env python3
"""Live regression for Test Subject's revivals and completed empty combat rewards."""
import argparse
from pathlib import Path
import shutil

import reference_runtime as reference
from play import DEFAULT_CLOCK


def check(game_dir, validator, logs):
    logs.mkdir(parents=True, exist_ok=True)
    original = reference.inject

    def inject(source):
        original(source)
        shutil.copy2(Path(__file__).parent / 'adapters/CombatExportAudit.cs', source)
        program = source / 'Program.cs'
        program.write_text(program.read_text().replace('case "start_run":',
            'case "audit_knock_out_test_subject": return sim.AuditKnockOutTestSubject(cmd);\n'
            'case "audit_offer_empty_rewards": return sim.AuditOfferEmptyCombatRewards(cmd);\ncase "start_run":', 1))

    reference.inject = inject
    try:
        with reference.reference_build(game_dir, logs) as (command, env, _):
            host = reference.JsonProcess(command, env, logs / 'test-subject', 30)
            try:
                host.receive()
                host.send(dict(cmd='oracle_run_start', request_id='start', setup=dict(
                    character='Ironclad', ascension=0, seed='TEST_SUBJECT', clock=DEFAULT_CLOCK)))
                host.send(dict(cmd='enter_room', type='combat', encounter='TEST_SUBJECT_BOSS'))
                for phase, hp in ((1, 200), (2, 300)):
                    state = host.send(dict(cmd='audit_knock_out_test_subject',
                                           request_id=f'knockout-{phase}'))['state']
                    reference.validate(validator, 'snapshot', state)
                    display = host.send(dict(cmd='oracle_display'))
                    reference.require(display['decision'] == 'combat_play' and not display['enemies'],
                                      'Knocked-out Test Subject should leave a player turn with no targets')
                    reference.require(host.send(dict(cmd='oracle_run_state', request_id='inspect'))['state'] == state,
                                      'Displaying the empty-target turn mutated gameplay')
                    state = host.send(dict(cmd='oracle_run_action', request_id=f'revive-{phase}',
                                           action='end_turn', args={}))['state']
                    reference.validate(validator, 'snapshot', state)
                    monsters = [c['monster'] for c in state['combat']['creatures'].values()
                                if c['monster'] and c['monster']['id'] == 'MONSTER.TEST_SUBJECT']
                    reference.require(len(monsters) == 1 and monsters[0]['current_hp'] == hp,
                                      f'Test Subject did not revive into phase {phase + 1}')
                host.send(dict(cmd='oracle_run_action', request_id='third-phase-turn',
                               action='end_turn', args={}))
                state = host.send(dict(cmd='audit_knock_out_test_subject', request_id='final-kill'))['state']
                reference.require(state['combat'] is None, 'Test Subject did not stay defeated')
                state = host.send(dict(cmd='audit_offer_empty_rewards', request_id='empty-rewards'))['state']
                reference.validate(validator, 'snapshot', state)
                if state['run_flow']['reward_sets']:
                    # This is the same automatic skip that crashed after the final boss.
                    host.send(dict(cmd='oracle_run_action', request_id='skip-empty',
                                   action='skip_rewards', args={}))
                reference.require(not state['run_flow']['reward_sets'],
                                  'Native-completed empty rewards remained pending in the recorder')
                print('Test Subject: both revivals, third-phase turn, and empty combat rewards passed')
            finally:
                host.close()
    finally:
        reference.inject = original


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    args = parser.parse_args()
    check(args.game_dir, args.validator.resolve(), args.logs.resolve())
