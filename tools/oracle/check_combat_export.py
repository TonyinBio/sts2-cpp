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
            'case "audit_removed_monster": return sim.AuditRemovedMonster(cmd);\n'
            'case "audit_kill_hopper": return sim.AuditKillHopper(cmd);\n'
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
            host = reference.JsonProcess(command, env, logs / 'removed-monster', 30)
            try:
                host.receive()
                host.send(dict(cmd='oracle_run_start', request_id='initial', setup=dict(
                    character='Ironclad', ascension=0, seed='GENERIC')))
                host.send({'cmd': 'audit_freeze_clock'})
                host.send(dict(cmd='enter_room', type='combat', encounter='SLIMES_WEAK'))
                state = host.send(dict(cmd='audit_removed_monster', request_id='kill'))['state']
                reference.validate(validator, 'snapshot', state)
                dead = [('/combat/creatures/' + key, creature['monster'])
                        for key, creature in state['combat']['creatures'].items()
                        if creature['monster'] and creature['monster']['id'] == 'MONSTER.TWIG_SLIME_S']
                reference.require(len(dead) == 1 and dead[0][1]['current_hp'] == 0 and
                                  dead[0][1]['ai'] is None, 'Lost removed monster or exported fabricated AI')
                reference.require(dead[0][0] not in state['combat']['enemies'], 'Dead monster remains active')
                reference.require(any(entry.get('kind') == 'DamageReceivedEntry' and
                                      entry['result']['receiver'] == dead[0][0] for entry in state['combat']['history']),
                                  'Lost historical reference to removed monster')
                reference.require(state == host.send(dict(cmd='oracle_run_state', request_id='repeat'))['state'],
                                  'Export mutated removed monster state')
                response = host.send(dict(cmd='oracle_run_action', request_id='end', action='end_turn', args={}))
                reference.validate(validator, 'snapshot', response['state'])
                total += 2
                print('Removed Twig Slime: retained history, null AI, and continued combat validated')
            finally:
                host.close()
            host = reference.JsonProcess(command, env, logs / 'hopper-theft', 30)
            try:
                host.receive()
                host.send(dict(cmd='oracle_run_start', request_id='initial', setup=dict(
                    character='Ironclad', ascension=0, seed='GENERIC')))
                host.send({'cmd': 'audit_freeze_clock'})
                host.send(dict(cmd='enter_room', type='combat', encounter='THIEVING_HOPPER_WEAK'))
                before = host.send(dict(cmd='oracle_run_state', request_id='before'))['state']
                deck_size = len(before['players'][0]['deck'])
                state = host.send(dict(cmd='oracle_run_action', request_id='steal', action='end_turn', args={}))['state']
                reference.validate(validator, 'snapshot', state)
                reference.require(len(state['players'][0]['deck']) == deck_size - 1, 'Hopper did not steal a deck card')
                swipe = next(p for p in state['combat']['powers'].values() if p['id'] == 'POWER.SWIPE_POWER')
                stolen = resolve(state, swipe['model_fields']['SwipePower._stolenCard'])
                original_ref = stolen['deck_version']
                original_card = resolve(state, original_ref)
                reference.require(original_ref.startswith('/combat/cards/') and original_card['deck_version'] is None,
                                  'Removed original deck card was not retained separately')
                reference.require(original_card['has_been_removed_from_state'] and
                                  original_card['owner'] == stolen['owner'], 'Removed original lost its runtime state')
                reference.require(original_ref not in [ref for player in state['combat']['players']
                    for pile in player['piles'].values() for ref in pile], 'Removed deck original is in a combat pile')
                host.send({'cmd': 'oracle_display'})
                reference.require(state == host.send(dict(cmd='oracle_run_state', request_id='repeat'))['state'],
                                  'Display/export mutated the stolen card graph')
                response = host.send(dict(cmd='audit_kill_hopper', request_id='kill'))
                returned = response['returned_card']
                state = response['state']
                reference.validate(validator, 'snapshot', state)
                state = host.send(dict(cmd='oracle_run_action', request_id='rewards', action='proceed', args={}))['state']
                for index in range(30):
                    reference.validate(validator, 'snapshot', state)
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
                else:
                    raise RuntimeError('Hopper rewards did not settle')
                reference.require(returned in state['players'][0]['deck'], 'Returned original card is missing from the permanent deck')
                reference.require(len(state['players'][0]['deck']) == deck_size + 1,
                                  'Expected original deck, returned stolen card, and one normal card reward')
                total += 2
                print('Hopper: theft, retained deck original, display purity, and return reward validated')
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
