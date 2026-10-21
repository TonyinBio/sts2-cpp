#!/usr/bin/env python3
"""Check that native UI display/inspection leaves a seeded gameplay trace unchanged."""
import argparse
from pathlib import Path
import shutil

import reference_runtime as reference


def check(game_dir, validator, logs):
    logs.mkdir(parents=True, exist_ok=True)
    original = reference.inject

    def inject(source):
        original(source)
        shutil.copy2(Path(__file__).parent / 'adapters/CombatExportAudit.cs', source)
        program = source / 'Program.cs'
        program.write_text(program.read_text().replace('case "start_run":',
            'case "audit_freeze_clock": return sim.AuditFreezeClock();\ncase "start_run":', 1))

    reference.inject = inject
    try:
        with reference.reference_build(game_dir, logs) as (command, env, _):
            traces = []
            for display_enabled in (False, True):
                host = reference.JsonProcess(command, env, logs / str(display_enabled), 30)
                trace = []
                try:
                    host.receive()
                    host.send({'cmd': 'audit_freeze_clock'})
                    state = host.send(dict(cmd='oracle_run_start', request_id='start', setup=dict(
                        character='Ironclad', ascension=0, seed='ORACLECHECK01')))['state']
                    commands = [('choose_option', dict(option_index=0)), ('proceed', {}),
                                ('select_map_node', dict(col=0, row=1)),
                                ('play_card', dict(card_index=0)), ('end_turn', {})]
                    for index in range(len(commands) + 1):
                        reference.validate(validator, 'snapshot', state)
                        trace.append(state)
                        room = (state.get('run_flow') or {}).get('room', {})
                        if display_enabled and not (room.get('kind') == 'event' and room.get('finished')):
                            first = host.send({'cmd': 'oracle_display'})
                            second = host.send({'cmd': 'oracle_display'})
                            reference.require(first == second, 'Repeated display response changed')
                            host.send({'cmd': 'get_map'})
                            after = host.send(dict(cmd='oracle_run_state', request_id='inspect'))['state']
                            reference.require(after == state, 'Display or map inspection mutated full state')
                        if index < len(commands):
                            kind, args = commands[index]
                            state = host.send(dict(cmd='oracle_run_action', request_id=str(index),
                                                   action=kind, args=args))['state']
                    traces.append(trace)
                finally:
                    host.close()
            reference.require(traces[0] == traces[1], 'Rendering changed seeded gameplay outcomes')
            print(f'{len(traces[0])} full checkpoints match with display enabled and disabled')
    finally:
        reference.inject = original


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    args = parser.parse_args()
    check(args.game_dir, args.validator.resolve(), args.logs.resolve())
