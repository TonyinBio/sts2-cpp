#!/usr/bin/env python3
"""Offline contract checks for the single evolving combat parity representation."""
import copy
import unittest
from urllib.parse import urljoin
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/oracle'))
from native_run import assert_unchanged, exact_json
from record import REPO

try:
    from jsonschema import Draft202012Validator
    from referencing import Registry, Resource
except ImportError:
    Draft202012Validator = None

EXTENSIONS = {'boundary_kind', 'run_ending_reason', 'current_room',
              'native_model_state', 'combat', 'pending_choice', 'run_flow'}


def fixture(native):
    value = copy.deepcopy(native)
    value.update(boundary_kind='awaiting_input', run_ending_reason=None,
                 current_room=None, native_model_state={}, combat=None, pending_choice=None, run_flow=None)
    return value


def survivor_fixture(native):
    """Synthetic post-block, pre-discard Survivor boundary, matching the probed shape."""
    value = fixture(native)
    value['current_room'] = {
        'encounter_id': 'ENCOUNTER.SHRINKER_BEETLE_WEAK', 'event_id': None,
        'is_pre_finished': False, 'parent_event_id': None,
        'reward_proportion': 1, 'room_type': 'monster', 'should_resume_parent_event': False}
    player = '/players/0'
    creature = '/combat/creatures/player'
    card = '/combat/cards/survivor'
    candidate = '/combat/cards/discard_candidate'
    play = '/combat/card_plays/play0'
    saved = copy.deepcopy(native['players'][0]['deck'][0])
    def combat_card(saved_card, deck_version):
        return dict(saved=saved_card, owner=player, deck_version=deck_version, clone_of=None,
                    is_dupe=False, base_replay_count=0, local_keywords=[],
                    exhaust_on_next_play=False, has_single_turn_retain=False,
                    has_single_turn_sly=False, has_been_removed_from_state=False,
                    dynamic_vars={}, energy_cost=dict(base=1, captured_x_value=0,
                    local_modifiers=[]), base_star_cost=0, temporary_star_costs=[],
                    last_stars_spent=0, affliction=None, enchantment_runtime=None, model_fields={})
    survivor = {'id': 'CARD.SURVIVOR', 'current_upgrade_level': 0,
                'floor_added_to_deck': 0}
    combat = dict(
        round_number=1, current_side='Player',
        turn_control=dict(is_in_progress=True, is_starting=False,
                          is_enemy_turn_started=False, ending_player_turn_phase_one=False,
                          ending_player_turn_phase_two=False, has_pending_loss=False,
                          player_actions_disabled=False, card_or_potion_effect_depth=1,
                          players_taking_extra_turn=[], players_ready_to_end_turn=[],
                          players_ready_to_begin_enemy_turn=[]),
        allies=[creature], enemies=[], escaped_creatures=[], spawned_enemy_ids=[],
        encounter_fields={}, players=[dict(player=player, creature=creature, turn_number=1,
            phase='Play', energy=2, stars=0, can_use_or_remove_potions=True,
            is_active_for_hooks=True, piles=dict(Hand=[candidate], Draw=[], Discard=[],
            Exhaust=[], Play=[card]), pets=[], orb_capacity=0, orbs=[])],
        creatures={'player':dict(player=player, monster=None, side='Player', block=8,
            hp_is_infinite=False, slot_name=None, pet_owner=None, powers=[])},
        cards={'survivor':combat_card(survivor, None),
               'discard_candidate':combat_card(saved, '/players/0/deck/0')},
        powers={}, orbs={}, afflictions={}, attacks={}, retained_inventory_models={},
        card_plays={'play0':dict(card=card, player=player, target=None, result_pile='Discard',
            resources=dict(energy_spent=1, energy_value=1, stars_spent=0, star_value=0),
            is_auto_play=False, play_index=0, play_count=1)},
        active_card_plays=[dict(card_play=play, result_location=dict(player=player,
            pile_type='Discard', position='Bottom'))],
        history=[dict(kind='CardPlayStartedEntry', actor=creature, round_number=1,
            current_side='Player', player_turn_numbers=[dict(player=player, turn_number=1)],
            card_play=play)])
    value['combat'] = combat
    value['pending_choice'] = dict(player=player, sources=[card], candidates=[candidate],
        min_select=1, max_select=1, require_manual_confirmation=False, cancelable=False)
    return value


@unittest.skipUnless(Draft202012Validator, 'Install tools/oracle/requirements-test.txt')
class RunParitySchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.native_schema = exact_json((REPO / 'docs/specs/schemas/serializable-run-v20.schema.json').read_text())
        cls.schema = exact_json((REPO / 'docs/specs/schemas/run-parity-v1.schema.json').read_text())
        cls.native = exact_json((REPO / 'tests/native/fixtures/serializable-run-v20.json').read_text())
        Draft202012Validator.check_schema(cls.schema)
        uri = urljoin(cls.schema['$id'], 'serializable-run-v20.schema.json')
        registry = Registry().with_resource(uri, Resource.from_contents(cls.native_schema))
        cls.validator = Draft202012Validator(cls.schema, registry=registry)
        cls.native_validator = Draft202012Validator(cls.native_schema)

    def test_native_field_projection_cannot_drift(self):
        root = self.native_schema['$defs']['SerializableRun']
        projection = self.schema['$defs']['NativeRunFields']
        self.assertEqual(set(projection['properties']), set(root['properties']))
        self.assertEqual(projection['required'], root['required'])
        for name, definition in projection['properties'].items():
            self.assertEqual(definition, {'$ref': 'serializable-run-v20.schema.json'
                '#/$defs/SerializableRun/properties/' + name})

    def test_native_payload_is_unchanged(self):
        for value in (fixture(self.native), survivor_fixture(self.native)):
            self.validator.validate(value)
            stripped = {k:v for k,v in value.items() if k not in EXTENSIONS}
            self.native_validator.validate(stripped)
            assert_unchanged(self.native, stripped)
            self.assertFalse(self.native_validator.is_valid(value))

    def test_survivor_selection_and_references(self):
        value = survivor_fixture(self.native)
        self.validator.validate(value)
        # Resolve every reference in this concrete example, including native deck aliases.
        def visit(node):
            if isinstance(node, dict):
                for val in node.values(): visit(val)
            elif isinstance(node, list):
                for val in node: visit(val)
            elif isinstance(node, str) and node.startswith('/'):
                target = value
                for token in node[1:].split('/'):
                    key = token.replace('~1', '/').replace('~0', '~')
                    target = target[int(key)] if isinstance(target, list) else target[key]
                self.assertIsNotNone(target)
        visit(value)
        self.assertNotEqual(value['combat']['cards']['discard_candidate']['deck_version'],
                            value['pending_choice']['candidates'][0])

    def test_ordinary_play_has_no_pending_choice(self):
        value = survivor_fixture(self.native)
        value['pending_choice'] = None
        self.validator.validate(value)

    def test_real_cost_modifier_shapes(self):
        value = survivor_fixture(self.native)
        card = value['combat']['cards']['survivor']
        for expiration in (0, 2, 4, 6):
            card['energy_cost']['local_modifiers'] = [dict(amount=-1, type='Relative',
                expiration=expiration, is_reduce_only=True)]
            card['temporary_star_costs'] = [dict(cost=0, clears_when_turn_ends=True,
                clears_when_card_is_played=True)]
            self.validator.validate(value)
        card['energy_cost']['local_modifiers'][0]['expiration'] = 1
        self.assertFalse(self.validator.is_valid(value))

    def test_concrete_model_fields(self):
        value = survivor_fixture(self.native)
        value['combat']['cards']['survivor']['saved']['id'] = 'CARD.CLAW'
        fields = value['combat']['cards']['survivor']['model_fields']
        fields['Claw._extraDamageFromClawPlays'] = 3
        value['native_model_state']['/players/0/relics/0'] = {
            'model_fields': {'Pocketwatch._cardsPlayedLastTurn': 2}}
        self.validator.validate(value)
        fields['Claw._extraDamageFromClawPlays'] = 'three'
        self.assertFalse(self.validator.is_valid(value))
        fields.clear()
        fields['unverified_counter'] = 3
        self.assertFalse(self.validator.is_valid(value))

    def test_reference_keyed_power_state_and_floating_card(self):
        value = survivor_fixture(self.native)
        combat = value['combat']
        combat['cards']['floating'] = copy.deepcopy(combat['cards']['survivor'])
        combat['cards']['floating']['clone_of'] = '/combat/cards/survivor'
        combat['powers']['nightmare'] = dict(id='POWER.NIGHTMARE_POWER', amount=3,
            amount_on_turn_start=0, skip_next_duration_tick=False, applier=None,
            target=None, dynamic_vars={},
            model_fields={'NightmarePower.selectedCard': '/combat/cards/floating'})
        combat['powers']['dampen'] = dict(id='POWER.DAMPEN_POWER', amount=1,
            amount_on_turn_start=0, skip_next_duration_tick=False, applier=None,
            target=None, dynamic_vars={}, model_fields={
                'DampenPower.casters': ['/combat/creatures/player'],
                'DampenPower.downgradedCardsToOldUpgradeLevels': [
                    dict(key='/combat/cards/floating', value=1)]})
        combat['creatures']['player']['powers'] = [
            '/combat/powers/nightmare', '/combat/powers/dampen']
        self.validator.validate(value)
        fields = combat['powers']['dampen']['model_fields']
        fields['DampenPower.downgradedCardsToOldUpgradeLevels'][0]['value'] = 'one'
        self.assertFalse(self.validator.is_valid(value))

    def test_all_parity_fixtures_use_run_schema(self):
        example = exact_json((REPO / 'tests/parity/examples/starter.json').read_text())
        self.validator.validate(example)
        recording = exact_json((REPO / 'tests/parity/fixtures/starter.states.json').read_text())
        for state in [recording['initial']] + [step['state'] for step in recording['transitions']]:
            self.validator.validate(state)
            self.native_validator.validate({k: v for k, v in state.items() if k not in EXTENSIONS})
        for path in (REPO / 'tests/parity').rglob('*.actions.json'):
            actions = exact_json(path.read_text())
            self.assertEqual(actions['state_profile'], 'run-parity-v1')

    def test_checked_in_example(self):
        path = REPO / 'docs/specs/schemas/examples/run-parity-survivor.json'
        value = exact_json(path.read_text())
        self.validator.validate(value)
        self.assertEqual(value, survivor_fixture(self.native))

    def test_gameplay_history_rejects_missing_captured_inputs(self):
        value = survivor_fixture(self.native)
        entry = value['combat']['history'][0]
        entry.update(kind='CardPlayFinishedEntry', was_ethereal=True)
        self.validator.validate(value)
        del entry['was_ethereal']
        self.assertFalse(self.validator.is_valid(value))
        entry['kind'] = 'MadeUpEntry'
        self.assertFalse(self.validator.is_valid(value))

    def test_terminal_outcome_and_residual_combat(self):
        for residual_combat in (False, True):
            value = survivor_fixture(self.native) if residual_combat else fixture(self.native)
            value.update(boundary_kind='run_complete', pending_choice=None)
            if residual_combat:
                value['combat']['turn_control']['is_in_progress'] = False
                value['combat']['active_card_plays'] = []
            for reason in ('victory', 'death', 'abandoned'):
                value['run_ending_reason'] = reason
                self.validator.validate(value)
            for reason in (None, 'defeat', ''):
                value['run_ending_reason'] = reason
                self.assertFalse(self.validator.is_valid(value))
            value['run_ending_reason'] = 'victory'
            value['pending_choice'] = survivor_fixture(self.native)['pending_choice']
            self.assertFalse(self.validator.is_valid(value))

    def test_invalid_native_data_and_speculative_fields_rejected(self):
        mutations = [lambda x:x.update(schema_version=21),
            lambda x:x['players'][0].update(unexpected=True),
            lambda x:x['rng']['rngs']['shuffle'].update(s0=2**64),
            lambda x:x.update(run_ending_reason='death'),
            lambda x:x.update(instances=[]), lambda x:x.update(parity={}),
            lambda x:x['combat'].update(execution={}),
            lambda x:x['combat']['cards']['survivor'].update(target_type='AnyEnemy'),
            lambda x:x['pending_choice'].update(allow_duplicates=False),
            lambda x:x['pending_choice'].update(prompt='Discard a card'),
            lambda x:x['combat']['creatures']['player'].update(is_alive=True),
            lambda x:x.update(current_room=None), lambda x:x.update(combat=None)]
        for index, mutate in enumerate(mutations):
            value = survivor_fixture(self.native)
            mutate(value)
            with self.subTest(index=index):
                self.assertFalse(self.validator.is_valid(value))

    def test_noncombat_rooms_and_pending_choices(self):
        rooms = [dict(kind='map', current_coord=None, choices=[]),
                 dict(kind='rest_site', options=[dict(id='SMITH', enabled=True)]),
                 dict(kind='shop', cards=[], relics=[], potions=[], removal=None),
                 dict(kind='treasure', relics=[], picked=False),
                 dict(kind='combat_end'), dict(kind='victory')]
        card = copy.deepcopy(self.native['players'][0]['deck'][0])
        choices = [None,
            dict(kind='card_select', cards=[card], deck_indices=[0], min_select=1,
                 max_select=1, require_manual_confirmation=True, cancelable=False),
            dict(kind='card_reward', cards=[card], alternatives=['SkipCardRewardAlternative'], can_skip=True),
            dict(kind='bundle_select', bundles=[[card]])]
        for room in rooms:
            for choice in choices:
                value = fixture(self.native)
                value['run_flow'] = dict(room=room, pending_choice=choice, reward_sets=[],
                    operations=[] if choice is None else [dict(kind='choose_option', index=0)],
                    combat_rewards_offered=False)
                with self.subTest(room=room['kind'], choice=choice):
                    self.validator.validate(value)
                    broken = copy.deepcopy(value)
                    broken['run_flow']['room']['unexpected'] = True
                    self.assertFalse(self.validator.is_valid(broken))
                    broken = copy.deepcopy(value)
                    broken.update(boundary_kind='run_complete', run_ending_reason='abandoned')
                    self.assertFalse(self.validator.is_valid(broken))


if __name__ == '__main__':
    unittest.main()
