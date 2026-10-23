#!/usr/bin/env python3
"""Live regression for mandatory Ancient rewards at the start of acts 2 and 3."""
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
        shutil.copy2(Path(__file__).parent / 'adapters/AncientRewardsAudit.cs', source)
        program = source / 'Program.cs'
        program.write_text(program.read_text().replace('case "start_run":',
            'case "audit_next_act": return sim.AuditNextAct(cmd);\ncase "start_run":', 1))

    reference.inject = inject
    try:
        with reference.reference_build(game_dir, logs) as (command, env, _):
            host = reference.JsonProcess(command, env, logs / 'ancients', 30)
            try:
                host.receive()
                host.send(dict(cmd='oracle_run_start', request_id='start', setup=dict(
                    character='Ironclad', ascension=0, seed='ORACLECHECK01',
                    clock=dict(DEFAULT_CLOCK, seconds_per_action=0))))

                def action(kind, **args):
                    state = host.send(dict(cmd='oracle_run_action', request_id=kind,
                                           action=kind, args=args))['state']
                    reference.validate(validator, 'snapshot', state)
                    return state

                action('choose_option', option_index=0)
                action('proceed')
                for act in (2, 3):
                    prepared = host.send(dict(cmd='audit_next_act', request_id=f'act-{act}'))
                    state = prepared['state']
                    reference.validate(validator, 'snapshot', state)
                    start = prepared['starting_node']
                    expected = [dict(**start, point_type='Ancient')]
                    reference.require(state['run_flow']['room']['choices'] == expected,
                                      f'Act {act}: snapshot permits bypassing the Ancient')
                    display = host.send(dict(cmd='oracle_display'))
                    reference.require(display['choices'] == [dict(**start, type='Ancient')],
                                      f'Act {act}: interface permits bypassing the Ancient')
                    try:
                        action('select_map_node', **prepared['first_fight'])
                    except RuntimeError as error:
                        reference.require('Map node is not reachable' in str(error), str(error))
                    else:
                        raise RuntimeError(f'Act {act}: accepted bypassing the Ancient')
                    after = host.send(dict(cmd='oracle_run_state', request_id='unchanged'))['state']
                    reference.require(after == state, 'Rejected bypass or display mutated state')
                    state = action('select_map_node', **start)
                    room = state['run_flow']['room']
                    reference.require(room['kind'] == 'event' and not room['finished'],
                                      f'Act {act}: Ancient event did not open')
                    relic = room['options'][0]['relic']['id']
                    state = action('choose_option', option_index=0)
                    reference.require(any(r['id'] == relic for r in state['players'][0]['relics']),
                                      f'Act {act}: Ancient reward was not obtained')
                    state = action('proceed')
                    reference.require(state['run_flow']['room']['choices'] and all(
                        c['row'] == 1 for c in state['run_flow']['room']['choices']),
                        f'Act {act}: first fights unavailable after Ancient reward')
                    print(f'Act {act}: Ancient required, {relic} obtained, first fights unlocked')
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
