"""Synthetic native serialization data, not a fixture for launching a playable run."""
import copy
import json


def rich_save():
    word = {'counter': 17, 's0': 0, 's1': 2**63, 's2': 2**64 - 1, 's3': 1}
    card = {'id': 'CARD.BASH', 'current_upgrade_level': 1, 'floor_added_to_deck': 2}
    props = {
        'ints': [{'name': 'Counter', 'value': -2147483648},
                 {'name': 'OtherCounter', 'value': 2147483647}],
        'bools': [{'name': 'Triggered', 'value': True}],
        'strings': [{'name': 'Label', 'value': 'preserve ~ / Unicode: é'}],
        'int_arrays': [{'name': 'History', 'value': [0, 1, -2]}],
        'model_ids': [{'name': 'Model', 'value': 'CARD.BASH'}],
        'cards': [{'name': 'SavedCard', 'value': copy.deepcopy(card)}],
        'card_arrays': [{'name': 'SavedCards', 'value': [copy.deepcopy(card), copy.deepcopy(card)]}],
    }
    enhanced = dict(card, props=props, enchantment={
        'id': 'ENCHANTMENT.TEST', 'amount': 3, 'props': copy.deepcopy(props)})
    loc = {'table': 'events', 'key': 'TEST.title'}
    stats = {
        'player_id': 2**64 - 1,
        'ancient_choice': [{'title': loc, 'was_chosen': True}],
        'cards_gained': [card], 'cards_removed': [card],
        'card_choices': [{'card': card, 'was_picked': False}],
        'relic_choices': [{'choice': 'RELIC.BURNING_BLOOD', 'was_picked': True}],
        'potion_choices': [{'choice': 'POTION.TEST', 'was_picked': False}],
        'cards_enchanted': [{'card': card, 'enchantment': 'ENCHANTMENT.TEST'}],
        'cards_transformed': [{'original_card': card, 'final_card': card}],
        'event_choices': [{'title': loc, 'variables': {
            'Precise': {'type': 'Decimal', 'decimal_value': '__precise_decimal__',
                        'bool_value': False, 'string_value': None},
            'Largest': {'type': 'Decimal', 'decimal_value': 79228162514264337593543950335,
                        'bool_value': False, 'string_value': None},
            'Label': {'type': 'String', 'decimal_value': 0,
                      'bool_value': False, 'string_value': 'example'},
            'Flag': {'type': 'Bool', 'decimal_value': 0,
                     'bool_value': True, 'string_value': None},
        }}],
    }
    point = {'coord': {'col': 0, 'row': 1}, 'type': 'monster',
             'can_modify': True, 'children': [{'col': 0, 'row': 2}]}
    start = {'coord': {'col': 0, 'row': 0}, 'type': 'ancient',
             'can_modify': True, 'children': [{'col': 0, 'row': 1}]}
    boss = {'coord': {'col': 0, 'row': 2}, 'type': 'boss',
            'can_modify': True, 'children': []}
    run_names = ('up_front', 'shuffle', 'unknown_map_point', 'combat_card_generation',
                 'combat_potion_generation', 'combat_card_selection', 'combat_energy_costs',
                 'combat_targets', 'monster_ai', 'niche', 'combat_orbs', 'treasure_room_relics')
    save = {
        'schema_version': 20,
        'acts': [{'id': 'ACT.OVERGROWTH', 'rooms': {
            'event_ids': ['EVENT.NEOW'], 'events_visited': 1,
            'normal_encounter_ids': ['ENCOUNTER.SHRINKER_BEETLE_WEAK'],
            'normal_encounters_visited': 0, 'elite_encounter_ids': ['ENCOUNTER.TEST_ELITE'],
            'elite_encounters_visited': 0, 'boss_encounters_visited': 0,
            'boss_id': None, 'second_boss_id': None, 'ancient_id': None,
        }, 'saved_map': {'points': [point], 'boss': boss, 'start': start,
                         'start_coords': [{'col': 0, 'row': 0}], 'width': 1, 'height': 3}}],
        'ascension': 0, 'current_act_index': 0, 'game_mode': 'daily',
        'dailyTime': '2026-10-17T00:00:00+00:00',
        'modifiers': [{'id': 'MODIFIER.TEST', 'props': copy.deepcopy(props)}],
        'events_seen': ['EVENT.NEOW'], 'extra_fields': {'started_with_neow': True},
        'visited_map_coords': [{'col': 0, 'row': 0}],
        'map_point_history': [[{'map_point_type': 'ancient', 'player_stats': [stats],
                               'rooms': [{'room_type': 'event', 'model_id': 'EVENT.NEOW',
                                          'monster_ids': [], 'turns_taken': 0}]}]],
        'rng': {'seed': 'ROUNDTRIP01', 'rngs': {name: dict(word) for name in run_names}},
        'odds': {'unknown_map_point_monster_odds_value': 0.1,
                 'unknown_map_point_elite_odds_value': -1,
                 'unknown_map_point_treasure_odds_value': 0.02,
                 'unknown_map_point_shop_odds_value': 0.03},
        'shared_relic_grab_bag': {'relic_id_lists': {'common': ['RELIC.BURNING_BLOOD']}},
        'players': [{
            'character_id': 'CHARACTER.IRONCLAD', 'current_hp': 67, 'max_hp': 80,
            'max_energy': 3, 'max_potion_slot_count': 3, 'base_orb_slot_count': 0,
            'gold': 42, 'net_id': 2**64 - 1,
            'deck': [{'id': 'CARD.STRIKE_IRONCLAD'}, enhanced, copy.deepcopy(card)],
            'relics': [{'id': 'RELIC.BURNING_BLOOD', 'floor_added_to_deck': 1,
                        'props': copy.deepcopy(props)}],
            'potions': [{'id': 'POTION.TEST', 'slot_index': 1}],
            'rng': {'seed': 2**64 - 1, 'rngs': {
                name: dict(word) for name in ('rewards', 'shops', 'transformations')}},
            'odds': {'card_rarity_odds_value': -0.05, 'potion_reward_odds_value': 0.4},
            'relic_grab_bag': {'relic_id_lists': {'common': ['RELIC.BURNING_BLOOD']}},
            'extra_fields': {}, 'unlock_state': {
                'unlocked_epochs': ['NEOW'], 'encounters_seen': [], 'number_of_runs': 3},
            'discovered_cards': [], 'discovered_enemies': [], 'discovered_epochs': ['NEOW'],
            'discovered_potions': [], 'discovered_relics': [],
        }],
        'pre_finished_room': {
            'room_type': 'monster', 'encounter_id': 'ENCOUNTER.SHRINKER_BEETLE_WEAK',
            'event_id': None, 'is_pre_finished': True, 'reward_proportion': 1,
            'parent_event_id': None, 'should_resume_parent_event': True,
            'extra_rewards': {str(2**64 - 1): [
                {'reward_type': 'special_card', 'special_card': copy.deepcopy(card)}]},
            'encounter_state': {'phase': 'example'},
        },
        # Empty native drawing packet, emitted by the inspected game serializer.
        'map_drawings': 'H4sIAAAAAAAEA2NgYGAAABzfRCEEAAAA',
        'platform_type': 'none', 'save_time': 123, 'start_time': 100,
        'run_time': 23, 'win_time': 0, 'num_reloads': 4,
    }
    # Emit a decimal token without first rounding through a Python float.
    return json.dumps(save, ensure_ascii=False).replace(
        '"__precise_decimal__"', '0.1234567890123456789012345678')
