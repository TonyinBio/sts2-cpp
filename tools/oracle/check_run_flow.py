#!/usr/bin/env python3
"""Audit run backend boundaries against the pinned installed game (no interactive UI)."""
import argparse
from pathlib import Path
from record import JsonProcess, reference_build, require, validate


def check(game_dir, validator, logs):
    logs.mkdir(parents=True, exist_ok=True)
    with reference_build(game_dir, logs) as (command, env, _):
        host = JsonProcess(command, env, logs / 'run-flow', 30)
        count = 0
        def send(command):
            nonlocal count
            response = host.send(command)
            if response.get('type') == 'oracle_state':
                validate(validator, 'snapshot', response['state'])
                count += 1
            return response
        def action(kind, **args):
            return send(dict(cmd='oracle_run_action', request_id=kind, action=kind, args=args))['state']
        def snapshot(label):
            return send(dict(cmd='oracle_run_state', request_id=label))['state']
        try:
            require(host.receive()['type'] == 'ready', 'Missing ready response')
            state = send(dict(cmd='oracle_run_start', request_id='initial',
                setup=dict(character='Ironclad', ascension=0, seed='PARITY')))['state']
            require(state['run_flow']['room']['id'] == 'EVENT.NEOW', 'Seeded run must start at Neow')
            state = action('choose_option', option_index=0)
            require(state['run_flow']['pending_choice']['kind'] == 'card_select', 'Neow must pause for its choice')
            state = action('select_cards', indices=[0])
            require(state['run_flow']['room']['finished'], 'Neow did not finish')
            require(action('proceed')['run_flow']['room']['kind'] == 'map', 'Missing map transition')
            # Existing fixture commands place native rooms without implementing a
            # combat campaign. All subsequent choices use the production backend.
            send(dict(cmd='enter_room', type='shop'))
            state = snapshot('shop')
            require(state['run_flow']['room']['cards'], 'Missing generated shop inventory')
            before = state['players'][0]['gold']
            state = action('buy_card', card_index=0)
            require(state['players'][0]['gold'] < before, 'Purchase did not spend gold')
            action('proceed')
            send(dict(cmd='enter_room', type='rest'))
            snapshot('rest')
            state = action('choose_option', option_index=1)
            require(state['run_flow']['pending_choice']['require_manual_confirmation'], 'Lost Smith preferences')
            state = action('select_cards', indices=[0])
            require(state['players'][0]['deck'][0]['current_upgrade_level'] == 1, 'Smith did not upgrade selected card')
            action('proceed')
            send(dict(cmd='enter_room', type='treasure'))
            state = snapshot('treasure')
            require(state['run_flow']['room']['relics'], 'Missing treasure offers')
            state = action('pick_treasure', index=0)
            require(state['run_flow']['room']['picked'], 'Treasure choice did not finish')
            action('proceed')
            state = action('abandon')
            require(state['boundary_kind'] == 'run_complete' and state['run_ending_reason'] == 'abandoned', 'Incorrect ending')
            require(state['run_flow'] is None, 'Terminal boundary retains pending flow')
        finally:
            host.close()
        for ending in ('death', 'victory'):
            host = JsonProcess(command, env, logs / ending, 30)
            try:
                host.receive()
                send(dict(cmd='oracle_run_start', request_id='initial',
                    setup=dict(character='Ironclad', ascension=0, seed='PARITY')))
                if ending == 'death':
                    send(dict(cmd='enter_room', type='combat', encounter='SHRINKER_BEETLE_WEAK'))
                    send(dict(cmd='set_player', hp=1))
                else:
                    send(dict(cmd='enter_room', type='event', event='THE_ARCHITECT'))
                state = snapshot(ending)
                for _ in range(5):
                    if state['boundary_kind'] == 'run_complete':
                        break
                    if ending == 'death':
                        state = action('end_turn')
                    elif state['run_flow']['room']['finished']:
                        state = action('proceed')
                    else:
                        state = action('choose_option', option_index=0)
                require(state['run_ending_reason'] == ending, f'Incorrect native {ending} ending')
            finally:
                host.close()
        print(f'Validated {count} live run backend snapshots')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    args = parser.parse_args()
    check(args.game_dir, args.validator.resolve(), args.logs.resolve())
