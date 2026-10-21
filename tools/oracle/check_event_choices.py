#!/usr/bin/env python3
"""Live regressions for headless event choices and their recorded gameplay effects."""
import argparse
from pathlib import Path

from reference_runtime import JsonProcess, reference_build, require, validate


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


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    args = parser.parse_args()
    check(args.game_dir, args.validator.resolve(), args.logs.resolve())
