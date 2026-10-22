#!/usr/bin/env python3
"""Live regression for one successful card removal per merchant visit."""
import argparse
from pathlib import Path

from play import DEFAULT_CLOCK
from reference_runtime import JsonProcess, reference_build, require, validate


def check(game_dir, validator, logs):
    logs.mkdir(parents=True, exist_ok=True)
    with reference_build(game_dir, logs) as (command, env, _):
        host = JsonProcess(command, env, logs / 'shop-removal', 30)
        try:
            host.receive()
            host.send(dict(cmd='oracle_run_start', request_id='initial', setup=dict(
                character='Ironclad', ascension=0, seed='1234', clock=DEFAULT_CLOCK)))
            host.send(dict(cmd='set_player', gold=1000))
            host.send(dict(cmd='enter_room', type='shop'))

            def state():
                value = host.send(dict(cmd='oracle_run_state', request_id='state'))['state']
                validate(validator, 'snapshot', value)
                return value

            def action(kind, **args):
                value = host.send(dict(cmd='oracle_run_action', request_id=kind,
                                       action=kind, args=args))['state']
                validate(validator, 'snapshot', value)
                return value

            before = state()
            cost = host.send(dict(cmd='oracle_display'))['card_removal_cost']
            require(cost is not None, 'New shop has no removal option')
            action('remove_card')
            cancelled = action('skip_select')
            require(not cancelled['run_flow']['room']['removal']['used'], 'Cancellation consumed removal')
            require(cancelled['players'] == before['players'], 'Cancellation charged gold or removed a card')
            require(host.send(dict(cmd='oracle_display'))['card_removal_cost'] == cost,
                    'Cancellation hid removal')
            action('remove_card')
            removed = action('select_cards', indices=[0])
            require(removed['run_flow']['room']['removal']['used'], 'Successful removal was not marked used')
            require(len(removed['players'][0]['deck']) == len(before['players'][0]['deck']) - 1,
                    'Removal did not remove exactly one card')
            require(removed['players'][0]['gold'] == before['players'][0]['gold'] - cost,
                    'Removal charged incorrect gold')
            require(host.send(dict(cmd='oracle_display'))['card_removal_cost'] is None,
                    'Purchased removal still appears in shop')
            try:
                action('remove_card')
            except RuntimeError as error:
                require('unavailable' in str(error), f'Unexpected rejection: {error}')
            else:
                raise RuntimeError('Shop accepted a second removal')
            rejected = state()
            require(rejected['players'] == removed['players'] and
                    rejected['run_flow'] == removed['run_flow'],
                    'Rejected second removal mutated the player or shop')
            action('leave_room')
            host.send(dict(cmd='enter_room', type='shop'))
            require(not state()['run_flow']['room']['removal']['used'], 'New shop inherited used removal')
            require(host.send(dict(cmd='oracle_display'))['card_removal_cost'] is not None,
                    'New shop has no removal option')
            print('Shop removal: cancellation, one purchase, hidden option, duplicate rejection and new visit validated')
        finally:
            host.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    args = parser.parse_args()
    check(args.game_dir, args.validator.resolve(), args.logs.resolve())
