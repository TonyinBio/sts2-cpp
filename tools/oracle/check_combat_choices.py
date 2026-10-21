#!/usr/bin/env python3
"""Live full-state checks for suspended combat choices."""
import argparse
from pathlib import Path
import shutil
import record


def check(game_dir, validator, logs):
    logs.mkdir(parents=True, exist_ok=True)
    original = record.inject

    def inject(source):
        original(source)
        shutil.copy2(Path(__file__).parent / 'adapters/CombatChoiceAudit.cs', source)
        program = source / 'Program.cs'
        program.write_text(program.read_text().replace('case "start_run":',
            'case "audit_choice_setup": return sim.AuditChoiceSetup(cmd);\ncase "start_run":', 1))

    record.inject = inject
    try:
        with record.reference_build(game_dir, logs) as (command, env, _):
            for card in ('Survivor', 'Discovery'):
                host = record.JsonProcess(command, env, logs / card, 30)
                try:
                    host.receive()
                    host.send(dict(cmd='oracle_run_start', request_id='init',
                                   setup=dict(character='Ironclad', ascension=0, seed='CHOICE')))
                    host.send(dict(cmd='enter_room', type='combat', encounter='SHRINKER_BEETLE_WEAK'))
                    response = host.send(dict(cmd='audit_choice_setup', request_id='setup', card=card))
                    record.validate(validator, 'snapshot', response['state'])
                    request = record.action_request({'setup': {'kind': 'seeded_run'}},
                        dict(kind='play_card', card_instance=response['card_instance']), response['state'], 'play')
                    state = host.send(request)['state']
                    record.validate(validator, 'snapshot', state)
                    record.require(state['pending_choice'] is not None, 'Missing suspended choice')
                    repeated = host.send(dict(cmd='oracle_run_state', request_id='repeat'))['state']
                    record.require(repeated == state, 'Repeated capture changed full state')
                    # Discovery allows cancellation; Survivor requires a discard.
                    action = 'skip_select' if card == 'Discovery' else 'select_cards'
                    args = {} if action == 'skip_select' else dict(indices=[0])
                    state = host.send(dict(cmd='oracle_run_action', request_id='answer', action=action, args=args))['state']
                    record.validate(validator, 'snapshot', state)
                    record.require(state['pending_choice'] is None, 'Choice did not resume')
                    print(f'{card}: full-state capture and choice resumption validated')
                finally:
                    host.close()
    finally:
        record.inject = original


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    args = parser.parse_args()
    check(args.game_dir, args.validator.resolve(), args.logs.resolve())
