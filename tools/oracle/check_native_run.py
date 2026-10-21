#!/usr/bin/env python3
"""Live native SaveManager round-trip regression tests; requires the pinned game DLL."""
import argparse
import gzip
import base64
import hashlib
import json
from pathlib import Path
import tempfile

from native_run import assert_unchanged, differences, exact_dump, exact_json, reference_build
from native_run_fixtures import rich_save
from reference_runtime import GAME_SHA256, REFERENCE_REVISION, JsonProcess, REPO, require


def wire_state(text):
    value = exact_json(text)
    if value.get('map_drawings') is not None:
        # Compare the complete uncompressed packet, not compressor implementation details.
        value['map_drawings'] = gzip.decompress(base64.b64decode(value['map_drawings'], validate=True))
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=REPO / 'build/native-run-check/report.json')
    args = parser.parse_args()
    try:
        from jsonschema import Draft202012Validator, FormatChecker
    except ImportError as error:
        raise RuntimeError('Install tools/oracle/requirements-test.txt to validate native schemas') from error
    schema = exact_json((REPO / 'docs/specs/schemas/serializable-run-v20.schema.json').read_text())
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema, format_checker=FormatChecker())
    output = args.output.resolve()
    logs = output.parent / (output.name + '.logs')
    cases = []
    with reference_build(args.game_dir, logs) as (command, env):
        host = JsonProcess(command, env, logs / 'reference', 30)
        try:
            require(host.receive().get('type') == 'ready', 'Reference did not start')

            def roundtrip(name, source, **options):
                response = host.send({'cmd': 'native_run_roundtrip', 'json': source, **options})
                require(response.get('type') == 'native_run_roundtrip', 'Unexpected round-trip response')
                for key in ('serialized', 'reserialized'):
                    validator.validate(exact_json(response[key]))
                return response

            def unchanged(name, source, **options):
                response = roundtrip(name, source, **options)
                assert_unchanged(exact_json(response['before']), exact_json(response['after']))
                assert_unchanged(wire_state(response['serialized']), wire_state(response['reserialized']))
                cases.append({'name': name, 'status': 'unchanged'})
                return response

            rich = unchanged('all typed properties, nested history, precise decimals and RNG limits', rich_save())
            canonical = rich['serialized']
            golden = REPO / 'tests/native/fixtures/serializable-run-v20.json'
            assert_unchanged(wire_state(golden.read_text()), wire_state(canonical))
            unchanged('canonical native JSON, including all metadata', canonical)

            drawings = {'drawings': [{'playerId': 2**64 - 1, 'lines': [
                # Native packets quantize X to 13 bits over [-3, 3] and Y to 16
                # over [-2, 2]. These coordinates are exactly on those grids.
                {'isEraser': False, 'mapPoints': [{'X': 0, 'Y': 0.25}, {'X': 1.5, 'Y': -0.5}]},
                {'isEraser': True, 'mapPoints': [{'X': -1.5, 'Y': 1.25}]},
            ]}]}
            drawn = unchanged('nonempty native drawing packet and coordinates', canonical,
                              drawings_json=exact_dump(drawings))
            require(exact_json(drawn['before'])['map_drawings']['drawings'],
                    'Drawing test did not populate native data')

            for counter in (-2147483648, 2147483647):
                limits = exact_json(canonical)
                for rng in (limits['rng'], limits['players'][0]['rng']):
                    for stream in rng['rngs'].values():
                        stream['counter'] = counter
                unchanged(f'all 15 RNG counters at {counter}', exact_dump(limits))

            # Only conditional/default fields are omitted; none is excluded from comparison.
            sparse = exact_json(canonical)
            for field in ('events_seen', 'visited_map_coords', 'map_point_history', 'dailyTime'):
                sparse.pop(field, None)
            sparse['game_mode'] = 'standard'
            for player in sparse['players']:
                for field in ('deck', 'relics', 'potions', 'discovered_cards', 'discovered_enemies',
                              'discovered_epochs', 'discovered_potions', 'discovered_relics'):
                    player.pop(field, None)
            unchanged('native defaults and omitted collections', exact_dump(sparse))

            reordered = exact_dump(exact_json(canonical), sort_keys=True)
            unchanged('object key order is immaterial', reordered)

            # Exercise a real generated checkpoint, not just synthetic serialization types.
            with tempfile.TemporaryDirectory(prefix='sts2-native-save-') as directory:
                save_path = Path(directory) / 'current_run.save'
                host.send({'cmd': 'start_run', 'character': 'Ironclad', 'seed': 'ROUNDTRIP01', 'lang': 'en'})
                saved = host.send({'cmd': 'write_continue_save', 'path': str(save_path)})
                require(saved.get('success') is True, 'Failed to generate native checkpoint')
                unchanged('engine-generated native checkpoint', save_path.read_text())

            flipped = roundtrip('one-bit RNG corruption', canonical, flip_rng_bit=True)
            changes = differences(exact_json(flipped['before']), exact_json(flipped['after']))
            require(len(changes) == 1 and changes[0]['path'].endswith('/Shuffle/s2'),
                    f'One-bit corruption was not isolated: {changes}')
            require(changes[0]['expected'] == 2**64 - 1, 'Corruption test must exceed float precision')
            require(changes[0]['expected'] ^ changes[0]['actual'] == 1, 'Wrong corruption detected')
            cases.append({'name': 'one-bit RNG corruption', 'status': 'detected', 'path': changes[0]['path']})

            # The native writer elides false, but the constructor initializes this field to true.
            # Keep this as an explicit loss-detection case, never a blanket comparison exception.
            lossy = exact_json(canonical)
            lossy['acts'][0]['saved_map']['points'][0]['can_modify'] = False
            result = roundtrip('native can_modify default mismatch', exact_dump(lossy))
            changes = differences(exact_json(result['before']), exact_json(result['after']))
            expected_path = '/acts/0/saved_map/points/0/can_modify'
            require(changes == [{'path': expected_path, 'expected': False, 'actual': True}],
                    f'Native default mismatch behavior changed: {changes}')
            cases.append({'name': 'native can_modify default mismatch', 'status': 'known_native_loss',
                          'path': expected_path, 'before': False, 'after': True})

            lossy = exact_json(canonical)
            lossy['acts'][0]['rooms']['elite_encounter_ids'] = []
            result = roundtrip('native empty encounter list', exact_dump(lossy))
            changes = differences(exact_json(result['before']), exact_json(result['after']))
            expected_path = '/acts/0/rooms/elite_encounter_ids'
            require(changes == [{'path': expected_path, 'expected': [], 'actual': None}],
                    f'Native empty-list behavior changed: {changes}')
            cases.append({'name': 'native empty encounter list', 'status': 'known_native_loss',
                          'path': expected_path, 'before': [], 'after': None})
            require(host.send({'cmd': 'quit'}).get('type') == 'quit_result', 'Reference shutdown failed')
            host.process.wait(timeout=30)
            require(host.process.returncode == 0, 'Reference exited unsuccessfully')
        finally:
            host.close()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({'status': 'passed', 'game_dll_sha256': GAME_SHA256,
        'reference_revision': REFERENCE_REVISION,
        'schema_sha256': hashlib.sha256((REPO / 'docs/specs/schemas/serializable-run-v20.schema.json')
                                       .read_bytes()).hexdigest(),
        'cases': cases}, indent=2) + '\n')
    print(f'PASS: {sum(c["status"] == "unchanged" for c in cases)} unchanged round trips; '
          f'one-bit RNG corruption detected; two native loss cases explicitly detected. Report: {output}')


if __name__ == '__main__':
    main()
