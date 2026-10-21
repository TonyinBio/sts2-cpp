#!/usr/bin/env python3
"""Live checks for generic combat export against the pinned installed game."""
import argparse
from decimal import Decimal
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
            'case "audit_freeze_clock": return sim.AuditFreezeClock();\n'
            'case "audit_generic_contract": return sim.AuditGenericContract();\n'
            'case "audit_generic_graph": return sim.AuditGenericGraph(cmd);\ncase "start_run":', 1))
    reference.inject = inject
    try:
        with reference.reference_build(game_dir, logs) as (command, env, _):
            total = 0
            def resolve(state, ref):
                value = state
                for key in ref[1:].split('/'):
                    key = key.replace('~1', '/').replace('~0', '~')
                    value = value[int(key)] if isinstance(value, list) else value[key]
                return value
            for character in ('Ironclad', 'Silent', 'Defect', 'Regent', 'Necrobinder'):
                host = reference.JsonProcess(command, env, logs / character, 30)
                try:
                    host.receive()
                    host.send({'cmd': 'oracle_run_start', 'request_id': 'initial', 'setup':
                               dict(character=character, ascension=0, seed='GENERIC')})
                    host.send({'cmd': 'audit_freeze_clock'})
                    if character == 'Ironclad':
                        # The deliberately unclassified test model is outside the DLL catalog.
                        contract = host.send({'cmd': 'audit_generic_contract'})
                        reference.require(contract['members'] >= 293 and contract['unknown_field_rejected'], 'Incomplete field audit')
                    if character == 'Ironclad':
                        host.send(dict(cmd='set_player', potions=['WEAK_POTION']))
                    encounters = {'Silent': 'BOWLBUGS_WEAK', 'Defect': 'CUBEX_CONSTRUCT_NORMAL', 'Regent': 'CEREMONIAL_BEAST_BOSS'}
                    host.send(dict(cmd='enter_room', type='combat', encounter=encounters.get(character, 'SHRINKER_BEETLE_WEAK')))
                    def capture(cmd='oracle_run_state'):
                        nonlocal total
                        response = host.send(dict(cmd=cmd, request_id='capture'))
                        state = response['state']
                        reference.validate(validator, 'snapshot', state)
                        total += 1
                        return state
                    state = capture()
                    display = host.send({'cmd': 'oracle_display'})
                    reference.require(display['decision'] == 'combat_play', 'Missing native combat display')
                    reference.require(display['hand'] and display['hand'][0]['description'], 'Missing readable cards')
                    reference.require(state == capture(), f'{character}: display/capture mutated full state')
                    if character == 'Ironclad':
                        graph = capture('audit_generic_graph')
                        cards = graph['combat']['cards']
                        claw_ref = next('/combat/cards/' + key for key, card in cards.items()
                                        if card['saved']['id'] == 'CARD.CLAW' and card['clone_of'] is None)
                        claw = resolve(graph, claw_ref)
                        reference.require(claw['model_fields']['Claw._extraDamageFromClawPlays'] ==
                            Decimal('0.1234567890123456789012345678'), 'Decimal field lost precision')
                        reference.require(claw['affliction'] is not None and claw['enchantment_runtime'] is not None, 'Missing attachments')
                        reference.require(any(c['clone_of'] == claw_ref for c in cards.values()), 'Missing clone identity')
                        powers = list(graph['combat']['powers'].values())
                        afterimage = next(p for p in powers if p['id'] == 'POWER.AFTERIMAGE_POWER')
                        reference.require(afterimage['model_fields']['AfterimagePower.amountsForPlayedCards'] ==
                            [dict(key=claw_ref, value=2)], 'Lost dictionary card alias')
                        nightmare = next(p for p in powers if p['id'] == 'POWER.NIGHTMARE_POWER')
                        floating = nightmare['model_fields']['NightmarePower.selectedCard']
                        reference.require(resolve(graph, floating)['model_fields']['Rampage._extraDamageFromPlays'] == 7, 'Missing floating card')
                        reference.require(len(graph['combat']['orbs']) >= 2 and graph['combat']['attacks'], 'Missing orb/attack tables')
                        reference.require(graph == capture(), 'Repeated graph export mutated state')
                    else:
                        response = host.send(dict(cmd='oracle_run_action', request_id='end', action='end_turn', args={}))
                        reference.validate(validator, 'snapshot', response['state'])
                        total += 1
                        capture()
                    print(f'{character}: generic combat snapshots validated')
                finally:
                    host.close()
            print(f'Validated {total} combat snapshots and the declared-field contract')
    finally:
        reference.inject = original


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    args = parser.parse_args()
    check(args.game_dir, args.validator.resolve(), args.logs.resolve())
