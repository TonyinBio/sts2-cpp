#!/usr/bin/env python3
"""Live regressions for headless event choices and their recorded gameplay effects."""
import argparse
from pathlib import Path
import shutil

import reference_runtime as reference
from reference_runtime import JsonProcess, reference_build, require, validate
from play import DEFAULT_CLOCK


def check(game_dir, validator, logs):
    logs.mkdir(parents=True, exist_ok=True)
    with reference_build(game_dir, logs) as (command, env, _):
        for option in (1, 0):
            host = JsonProcess(command, env, logs / f'jungle-maze-{option}', 30)
            try:
                host.receive()
                host.send(dict(cmd='oracle_run_start', request_id='initial',
                               setup=dict(character='Ironclad', ascension=1, seed='123')))
                host.send(dict(cmd='enter_room', type='event', event='JUNGLE_MAZE_ADVENTURE'))
                before = host.send(dict(cmd='oracle_run_state', request_id='before'))['state']
                validate(validator, 'snapshot', before)
                variables = before['run_flow']['room']['dynamic_vars']
                after = host.send(dict(cmd='oracle_run_action', request_id='choice',
                                       action='choose_option', args=dict(option_index=option)))['state']
                validate(validator, 'snapshot', after)
                require(after['run_flow']['room']['finished'], 'Event did not finish')
                reward = variables['JoinForcesGold' if option == 1 else 'SoloGold']
                # Native GainGold truncates the awarded decimal amount to integer gold.
                require(after['players'][0]['gold'] == before['players'][0]['gold'] + int(reward),
                        'Event granted incorrect gold')
                loss = 0 if option == 1 else variables['SoloHp']
                require(after['players'][0]['current_hp'] == before['players'][0]['current_hp'] - loss,
                        'Event applied incorrect damage')
                state = host.send(dict(cmd='oracle_run_action', request_id='proceed',
                                       action='proceed', args={}))['state']
                validate(validator, 'snapshot', state)
                require(state['run_flow']['room']['kind'] == 'map', 'Event did not return to map')
                require(host.send(dict(cmd='oracle_display'))['decision'] == 'map_select', 'Missing map display')
                print(f'Jungle Maze option {option}: gold, HP, completion and map transition validated')
            finally:
                host.close()
    _check_lantern_key(game_dir, validator, logs / 'lantern-key')


def _check_lantern_key(game_dir, validator, logs):
    logs.mkdir(parents=True, exist_ok=True)
    original = reference.inject
    def inject(source):
        original(source)
        shutil.copy2(Path(__file__).parent / 'adapters/CombatExportAudit.cs', source)
        program = source / 'Program.cs'
        program.write_text(program.read_text().replace('case "start_run":',
            'case "audit_event_combat": return sim.AuditEventCombat(cmd);\ncase "start_run":', 1))
    reference.inject = inject
    try:
        with reference_build(game_dir, logs) as (command, env, _):
            host = JsonProcess(command, env, logs / 'lantern', 30)
            try:
                host.receive()
                host.send(dict(cmd='oracle_run_start', request_id='initial', setup=dict(
                    character='Ironclad', ascension=0, seed='1234', clock=DEFAULT_CLOCK)))
                host.send(dict(cmd='enter_room', type='event', event='THE_LANTERN_KEY'))
                state = host.send(dict(cmd='oracle_run_action', request_id='keep',
                                       action='choose_option', args=dict(option_index=1)))['state']
                validate(validator, 'snapshot', state)
                require(not state['run_flow']['room']['finished'], 'Keeping the key prematurely finished the event')
                state = host.send(dict(cmd='oracle_run_action', request_id='fight',
                                       action='choose_option', args=dict(option_index=0)))['state']
                validate(validator, 'snapshot', state)
                room = state['current_room']
                require(state['combat'] is not None and room['parent_event_id'] == 'EVENT.THE_LANTERN_KEY',
                        'Fight lost the parent event or combat state')
                require(not room['is_pre_finished'] and not room['should_resume_parent_event'],
                        'Active event combat flags were changed for serialization')
                require(room['encounter_id'] == 'ENCOUNTER.MYSTERIOUS_KNIGHT_EVENT_ENCOUNTER', 'Wrong event encounter')
                rewards = [reward for values in room['extra_rewards'].values() for reward in values]
                require(any((reward.get('special_card') or {}).get('id') == 'CARD.LANTERN_KEY' for reward in rewards),
                        'Event combat lost the Lantern Key reward')
                require(host.send(dict(cmd='oracle_display'))['decision'] == 'combat_play', 'Missing event combat display')
                repeated = host.send(dict(cmd='audit_event_combat', request_id='guard'))['state']
                require(repeated == state, 'Display/export mutated live event combat state')
                state = host.send(dict(cmd='audit_event_combat', request_id='win', win=True))['state']
                validate(validator, 'snapshot', state)
                require(state['combat'] is None, 'Event combat did not complete')
                for index in range(30):
                    flow = state['run_flow']
                    if flow['pending_choice']:
                        action, args = 'select_card_reward', {'card_index': 0}
                    elif flow['reward_sets']:
                        remaining = [i for i, reward in enumerate(flow['reward_sets'][-1]['rewards']) if not reward['taken']]
                        action, args = ('take_reward', {'index': remaining[0]}) if remaining else ('skip_rewards', {})
                    elif flow['room']['kind'] == 'map':
                        break
                    else:
                        action, args = 'proceed', {}
                    state = host.send(dict(cmd='oracle_run_action', request_id=f'collect-{index}', action=action, args=args))['state']
                    validate(validator, 'snapshot', state)
                else:
                    raise RuntimeError('Lantern Key combat rewards did not reach the map')
                require(any(card['id'] == 'CARD.LANTERN_KEY' for card in state['players'][0]['deck']),
                        'Lantern Key reward was not added to the deck')
                print('Lantern Key: active event combat, native save guard, display purity, reward and map return validated')
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
