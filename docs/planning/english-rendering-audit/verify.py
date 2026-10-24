"""Check English-audit source/resource integrity, not exact rendered parity."""
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    sources = {}
    citations = 0
    for name in ['resources', 'adapter', 'formatting', 'prompts-feedback']:
        for entry in json.loads((HERE / f'{name}-sources.json').read_text()):
            relative = entry['source']
            path = REPO / relative
            if sha(path) != entry['sha256']:
                raise RuntimeError(f'Changed source: {relative}')
            count = len(path.read_text().splitlines())
            for start, end in entry['inspected_ranges']:
                if not 1 <= start <= end <= count:
                    raise RuntimeError(f'Invalid citation: {relative}:{start}-{end}')
                citations += 1
            sources[relative] = {'sha256': entry['sha256'], 'lines': count}
    resources = json.loads((HERE / 'resources.json').read_text())
    adapter = json.loads((HERE / 'adapter-resource-comparison.json').read_text())
    if len(resources['tables']) != 46 or resources['english_key_count'] != 7028:
        raise RuntimeError('Unexpected resource inventory')
    if len(resources['root_prefix_inventory']) != 224:
        raise RuntimeError('Unexpected root inventory')
    by_name = {Path(entry['resource']).name: entry for entry in resources['tables']}
    if len(adapter['tables']) != 46 or adapter['submodule_only'] or adapter['installed_only']:
        raise RuntimeError('Unexpected adapter table set')
    for entry in adapter['tables']:
        resource = by_name[entry['table']]
        paths = [REPO / 'sts2-cli/localization_eng' / entry['table'],
                 REPO / adapter['installed_extraction_root'] / entry['table']]
        if not entry['byte_equal'] or not resource['byte_identical']:
            raise RuntimeError('Resource comparison did not match')
        expected = resource['sha256']
        if any(sha(path) != expected for path in paths):
            raise RuntimeError(f'Changed resource: {entry["table"]}')
        if expected != entry['submodule_sha256'] or expected != entry['installed_sha256']:
            raise RuntimeError('Independent comparison manifests disagree')
        if entry['entries'] != resource['keys']:
            raise RuntimeError('Key counts disagree')
    revision = subprocess.check_output(
        ['git', '-C', str(REPO / 'sts2-cli'), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != resources['wrapper_revision'] or revision != adapter['submodule_revision']:
        raise RuntimeError('Reference wrapper revision changed')
    artifacts = sorted(p for p in HERE.iterdir()
                       if p.suffix in ('.md', '.json', '.py')
                       and p.name != 'source-index.json')
    report = {
        'evidence_kind': 'Source/resource integrity only; no native rendered goldens captured',
        'game_dll_sha256': resources['binaries_sha256']['sts2.dll'],
        'pck_sha256': resources['pck_sha256'],
        'reference_wrapper_revision': revision,
        'sources_checked': len(sources), 'indexed_citations_checked': citations,
        'english_tables_checked': 46, 'english_keys_in_tables': 7028,
        'sources': dict(sorted(sources.items())),
        'artifact_sha256': {p.name: sha(p) for p in artifacts},
    }
    (HERE / 'source-index.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Checked {len(sources)} source identities, {citations} citation ranges '
          'and 46 English table comparisons')


if __name__ == '__main__':
    main()
