#!/usr/bin/env python3
"""Live regression for defeating Decimillipede segments with Pact's End."""
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
            'case "audit_prepare_reattach": return sim.AuditPrepareReattach(cmd);\ncase "start_run":', 1))

    reference.inject = inject
    try:
        with reference.reference_build(game_dir, logs) as (command, env, _):
            host = reference.JsonProcess(command, env, logs / 'reattach', 30)
            try:
                host.receive()
                host.send(dict(cmd='oracle_run_start', request_id='start', setup=dict(
                    character='Ironclad', ascension=0, seed='REATTACH', clock=DEFAULT_CLOCK)))
                host.send(dict(cmd='enter_room', type='combat', encounter='DECIMILLIPEDE_ELITE'))
                prepared = host.send(dict(cmd='audit_prepare_reattach', request_id='prepare'))
                reference.validate(validator, 'snapshot', prepared['state'])
                state = host.send(dict(cmd='oracle_run_action', request_id='pacts-end',
                    action='play_card', args=dict(card_index=prepared['card_index'])))['state']
                reference.validate(validator, 'snapshot', state)
                reference.require(state['combat'] is None, "Pact's End did not finish the Decimillipede fight")
                state = host.send(dict(cmd='oracle_run_action', request_id='rewards',
                    action='proceed', args={}))['state']
                reference.validate(validator, 'snapshot', state)
                reference.require(state['run_flow']['reward_sets'], 'Victory did not offer combat rewards')
                print("Pact's End: all Decimillipede segments defeated and rewards offered")
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
