"""Check visibility-audit source identity and citation bounds, not public parity."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
GAME_SHA256 = '0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dll', type=Path)
    args = parser.parse_args()
    sources = {}
    citations = 0
    for name in ['adapter', 'combat', 'prompts', 'invariants']:
        entries = json.loads((HERE / f'{name}-sources.json').read_text())
        for entry in entries:
            relative = entry['source']
            path = REPO / relative
            if sha(path) != entry['sha256']:
                raise RuntimeError(f'Changed source: {relative}')
            lines = len(path.read_text().splitlines())
            for start, end in entry['inspected_ranges']:
                if not 1 <= start <= end <= lines:
                    raise RuntimeError(f'Invalid citation: {relative}:{start}-{end}')
                citations += 1
            sources[relative] = {'sha256': entry['sha256'], 'lines': lines}
    if args.game_dll and sha(args.game_dll) != GAME_SHA256:
        raise RuntimeError('Unsupported game DLL')
    wrapper_revision = subprocess.check_output(
        ['git', '-C', str(REPO / 'sts2-cli'), 'rev-parse', 'HEAD'], text=True).strip()
    if wrapper_revision != '084d1aa3d8e118ca7ce8d8774ad16d6be9c92367':
        raise RuntimeError('Unsupported reference wrapper revision')
    artifacts = sorted(p for p in HERE.iterdir()
                       if p.suffix in ('.md', '.json', '.py')
                       and p.name != 'source-index.json')
    report = {
        'evidence_kind': 'Source integrity only; golden specifications are not runtime captures',
        'game_dll_sha256': GAME_SHA256,
        'game_dll_checked_this_run': args.game_dll is not None,
        'local_decompilation_tool': 'ILSpy 9.1.0.7988',
        'reference_wrapper_revision': wrapper_revision,
        'sources_checked': len(sources),
        'indexed_citations_checked': citations,
        'sources': dict(sorted(sources.items())),
        'artifact_sha256': {p.name: sha(p) for p in artifacts},
    }
    (HERE / 'source-index.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Checked {len(sources)} source identities and {citations} indexed citations')


if __name__ == '__main__':
    main()
