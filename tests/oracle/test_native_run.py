#!/usr/bin/env python3
"""Offline exact-comparison and native-schema regression tests; no game DLL needed."""
import copy
from decimal import Decimal
import unittest

from pathlib import Path
import sys

# Allow direct execution without installing the reference tools as a package.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/oracle'))

from native_run import assert_unchanged, differences, exact_dump, exact_json
from reference_runtime import REPO

try:
    from jsonschema import Draft202012Validator, FormatChecker
except ImportError:
    Draft202012Validator = None


class ExactComparisonTests(unittest.TestCase):
    def test_uint64_and_decimal_roundtrip_without_float_conversion(self):
        value = exact_json('{"word":18446744073709551615,"decimal":0.1234567890123456789012345678}')
        self.assertEqual(value['word'], 2**64 - 1)
        self.assertEqual(value['decimal'], Decimal('0.1234567890123456789012345678'))
        assert_unchanged(value, exact_json(exact_dump(value)))

    def test_rejects_invalid_numbers_and_duplicate_keys(self):
        for text in ('{"a":1,"a":2}', 'NaN', 'Infinity', '-Infinity'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                exact_json(text)
        with self.assertRaises(ValueError):
            exact_dump(Decimal('NaN'))

    def test_object_order_is_immaterial_but_array_order_is_not(self):
        assert_unchanged({'a': 1, 'b': 2}, {'b': 2, 'a': 1})
        self.assertEqual([c['path'] for c in differences(['Strike', 'Bash'], ['Bash', 'Strike'])],
                         ['/0', '/1'])

    def test_missing_null_empty_and_boolean_values_remain_distinct(self):
        for before, after in (({}, {'a': None}), ({'a': []}, {'a': None}),
                              ({'a': True}, {'a': 1}), ([1], [1, None])):
            with self.subTest(before=before, after=after), self.assertRaises(AssertionError):
                assert_unchanged(before, after)

    def test_one_bit_rng_changes_above_float_precision_are_detected(self):
        before = {'rng': {'shuffle': {'s0': 2**64 - 2}}}
        after = {'rng': {'shuffle': {'s0': 2**64 - 1}}}
        self.assertEqual(differences(before, after), [{
            'path': '/rng/shuffle/s0', 'expected': 2**64 - 2, 'actual': 2**64 - 1}])

    def test_decimal_changes_beyond_double_precision_are_detected(self):
        with self.assertRaises(AssertionError):
            assert_unchanged(exact_json('0.1234567890123456789012345678'),
                             exact_json('0.1234567890123456789012345679'))

    def test_metadata_is_not_ignored_and_pointer_tokens_are_escaped(self):
        self.assertEqual([c['path'] for c in differences(
            {'save_time': 1, 'a/b~c': 1}, {'save_time': 2, 'a/b~c': 2})],
            ['/a~1b~0c', '/save_time'])


@unittest.skipUnless(Draft202012Validator, 'Install tools/oracle/requirements-test.txt for schema checks')
class NativeSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema = exact_json((REPO / 'data/schemas/serializable-run-v20.schema.json').read_text())
        Draft202012Validator.check_schema(cls.schema)
        cls.validator = Draft202012Validator(cls.schema, format_checker=FormatChecker())
        cls.fixture = exact_json((REPO / 'tests/native/fixtures/serializable-run-v20.json').read_text())

    def test_game_serialized_fixture_validates_without_numeric_loss(self):
        self.validator.validate(self.fixture)
        self.assertEqual(self.fixture['players'][0]['net_id'], 2**64 - 1)
        self.assertEqual(self.fixture['rng']['rngs']['shuffle']['s2'], 2**64 - 1)
        variables = self.fixture['map_point_history'][0][0]['player_stats'][0]['event_choices'][0]['variables']
        self.assertEqual(variables['Precise']['decimal_value'], Decimal('0.1234567890123456789012345678'))
        self.assertEqual(variables['Largest']['decimal_value'], 79228162514264337593543950335)

    def test_native_optional_defaults_can_be_omitted(self):
        value = copy.deepcopy(self.fixture)
        for field in ('acts', 'events_seen', 'dailyTime', 'visited_map_coords', 'map_point_history'):
            value.pop(field, None)
        for field in ('deck', 'potions', 'relics'):
            value['players'][0].pop(field, None)
        self.validator.validate(value)

    def test_rng_words_must_be_numeric_uint64_and_all_words_present(self):
        for mutation in ('string', 'negative', 'overflow', 'missing'):
            value = copy.deepcopy(self.fixture)
            stream = value['rng']['rngs']['shuffle']
            if mutation == 'missing':
                stream.pop('s3')
            else:
                stream['s0'] = {'string': '1', 'negative': -1, 'overflow': 2**64}[mutation]
            with self.subTest(mutation=mutation):
                self.assertFalse(self.validator.is_valid(value))

    def test_native_signed_counter_bounds(self):
        for counter in (-2147483648, 2147483647):
            value = copy.deepcopy(self.fixture)
            value['rng']['rngs']['shuffle']['counter'] = counter
            self.validator.validate(value)
        value['rng']['rngs']['shuffle']['counter'] = 2147483648
        self.assertFalse(self.validator.is_valid(value))

    def test_native_properties_are_objects_not_compact_tuples(self):
        value = copy.deepcopy(self.fixture)
        value['players'][0]['deck'][1]['props']['ints'] = [['Counter', 3]]
        self.assertFalse(self.validator.is_valid(value))

    def test_wrong_version_unknown_fields_and_wrong_variable_tags_fail(self):
        for mutation in ('version', 'unknown', 'tag'):
            value = copy.deepcopy(self.fixture)
            if mutation == 'version':
                value['schema_version'] = 19
            elif mutation == 'unknown':
                value['envelope'] = {}
            else:
                variables = value['map_point_history'][0][0]['player_stats'][0]['event_choices'][0]['variables']
                variables['Precise']['type'] = 'decimal'
            with self.subTest(mutation=mutation):
                self.assertFalse(self.validator.is_valid(value))


if __name__ == '__main__':
    unittest.main()
