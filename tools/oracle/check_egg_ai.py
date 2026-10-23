#!/usr/bin/env python3
"""Live regression for omitting obsolete Tough Egg AI aliases with Pact's End."""
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
            'case "audit_prepare_egg_kill": return sim.AuditPrepareEggKill(cmd);\ncase "start_run":', 1))

    reference.inject = inject
    try:
        with reference.reference_build(game_dir, logs) as (command, env, _):
            host = reference.JsonProcess(command, env, logs / 'egg-ai', 30)
            try:
                host.receive()
                host.send(dict(cmd='oracle_run_start', request_id='start', setup=dict(
                    character='Ironclad', ascension=0, seed='EGG_AI', clock=DEFAULT_CLOCK)))
                host.send(dict(cmd='enter_room', type='combat', encounter='OVICOPTER_NORMAL'))
                host.send(dict(cmd='oracle_run_action', request_id='spawn-eggs', action='end_turn', args={}))
                prepared = host.send(dict(cmd='audit_prepare_egg_kill', request_id='prepare'))
                reference.validate(validator, 'snapshot', prepared['state'])
                live_eggs = [c['monster'] for c in prepared['state']['combat']['creatures'].values()
                             if c['monster'] and c['monster']['id'] == 'MONSTER.TOUGH_EGG']
                reference.require(live_eggs and all(m['model_fields']['ToughEgg._afterHatchedState'].endswith(
                    '/monster/ai/move_states/NIBBLE_MOVE') for m in live_eggs), 'Live egg lost its AI alias')
                state = host.send(dict(cmd='oracle_run_action', request_id='pacts-end',
                    action='play_card', args=dict(card_index=prepared['card_index'])))['state']
                reference.validate(validator, 'snapshot', state)
                reference.require(state['combat'] is not None, 'Ovicopter should survive')
                dead = [c['monster'] for c in state['combat']['creatures'].values()
                        if c['monster'] and c['monster']['id'] == 'MONSTER.TOUGH_EGG' and c['monster']['current_hp'] == 0]
                reference.require(dead and all(m['ai'] is None for m in dead), 'Missing removed Tough Egg history')
                for monster in dead:
                    reference.require('ToughEgg._afterHatchedState' not in monster['model_fields'],
                                      'Removed egg retained an obsolete AI alias')
                    reference.require('retained_move_states' not in monster,
                                      'Removed egg added retained moves to the schema')
                host.send(dict(cmd='oracle_display'))
                repeated = host.send(dict(cmd='oracle_run_state', request_id='repeat'))['state']
                reference.require(repeated == state, 'Export/display mutated retained moves')
                state = host.send(dict(cmd='oracle_run_action', request_id='continue', action='end_turn', args={}))['state']
                reference.validate(validator, 'snapshot', state)
                reference.require(state['combat'] is not None, 'Combat did not continue')
                print("Pact's End: live AI aliases preserved, obsolete aliases omitted, next turn validated")
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
