"""Extract local localization evidence and record hashes without publishing tables."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
GAME_SHA = '0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9'
PCK_SHA = 'c60f672ee7804e6aefa1e19a582fa1c80b126a7b0eef4d084d3abf110df2eab7'
WRAPPER = '084d1aa3d8e118ca7ce8d8774ad16d6be9c92367'


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--extracted', type=Path,
                        default=REPO / 'build/english-source-audit/extracted')
    args = parser.parse_args()
    game = args.game_dir
    dll = game / 'data_sts2_windows_x86_64/sts2.dll'
    pck = game / 'SlayTheSpire2.pck'
    if sha(dll) != GAME_SHA or sha(pck) != PCK_SHA:
        raise RuntimeError('Installation differs from this audited DLL/PCK pair')
    revision = subprocess.check_output(
        ['git', '-C', str(REPO / 'sts2-cli'), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != WRAPPER:
        raise RuntimeError('Reference wrapper revision changed')
    release_path = game / 'release_info.json'
    release = json.loads(release_path.read_text())
    if release['version'] != 'v0.111.0' or release['commit'] != '41cef1ea':
        raise RuntimeError('Unexpected installation release information')
    extractor_path = REPO / 'sts2-cli/scripts/extract_localization.py'
    spec = importlib.util.spec_from_file_location('pinned_extractor', extractor_path)
    extractor = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(extractor)
    extracted_count = extractor.extract(pck, args.extracted)
    native = args.extracted / 'localization_eng'
    wrapper = REPO / 'sts2-cli/localization_eng'
    native_names = sorted(p.name for p in native.glob('*.json'))
    wrapper_names = sorted(p.name for p in wrapper.glob('*.json'))
    if native_names != wrapper_names:
        raise RuntimeError('Native and wrapper table filename sets differ')
    tables = []
    data_by_table = {}
    for name in native_names:
        data = json.loads((native / name).read_text())
        if not isinstance(data, dict) or not all(
                isinstance(k, str) and isinstance(v, str) for k, v in data.items()):
            raise RuntimeError(f'Unexpected localization shape: {name}')
        data_by_table[name] = data
        native_sha, wrapper_sha = sha(native / name), sha(wrapper / name)
        tables.append({
            'resource': f'res://localization/eng/{name}',
            'bytes': (native / name).stat().st_size,
            'keys': len(data), 'sha256': native_sha,
            'wrapper_sha256': wrapper_sha,
            'byte_identical': native_sha == wrapper_sha,
        })
    roots_path = REPO / 'docs/planning/victory-mechanics/scope-roots.json'
    roots = json.loads(roots_path.read_text())
    root_inventory = []
    for group, models in roots.items():
        for model in models:
            prefix = model.split('.', 1)[1] + '.'
            matches = {name: sorted(k for k in data if k.startswith(prefix))
                       for name, data in data_by_table.items()}
            matches = {name: keys for name, keys in matches.items() if keys}
            root_inventory.append({
                'group': group, 'model': model,
                'matching_tables': {name: len(keys) for name, keys in matches.items()},
                'key_names_sha256': hashlib.sha256(
                    json.dumps(matches, sort_keys=True).encode()).hexdigest(),
            })
    binaries = {}
    for name in ['sts2.dll', 'SmartFormat.dll', 'SmartFormat.ZString.dll', 'sts2.deps.json']:
        binaries[name] = sha(dll.parent / name)
    with pck.open('rb') as stream:
        header = struct.unpack('<6I', stream.read(24))
    report = {
        'evidence_kind': 'Installed resource bytes; not rendered output certification',
        'association': 'Co-installed release metadata, DLL and PCK recorded as an explicit pair; not a vendor signature or proof that the DLL alone identifies content',
        'release_info': release, 'release_info_sha256': sha(release_path),
        'binaries_sha256': binaries, 'pck_sha256': PCK_SHA,
        'pck_header': dict(zip(['magic', 'format', 'engine_major', 'engine_minor',
                                'engine_patch', 'flags'], header)),
        'extractor_source': 'sts2-cli/scripts/extract_localization.py',
        'extractor_sha256': sha(extractor_path), 'wrapper_revision': revision,
        'extracted_eng_and_zhs_table_count': extracted_count,
        'english_table_count': len(tables),
        'english_key_count': sum(item['keys'] for item in tables),
        'all_wrapper_english_tables_byte_identical': all(item['byte_identical'] for item in tables),
        'tables': tables,
        'root_inventory_scope': 'Prefix lookup only for the 224 audited roots; not semantic closure, inherited/fixed dependency coverage or exact rendering validation',
        'roots_source_sha256': sha(roots_path), 'root_prefix_inventory': root_inventory,
    }
    (HERE / 'resources.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"Extracted {extracted_count} tables; {len(tables)} English tables / "
          f"{report['english_key_count']} keys; wrapper byte match: "
          f"{report['all_wrapper_english_tables_byte_identical']}")


if __name__ == '__main__':
    main()
