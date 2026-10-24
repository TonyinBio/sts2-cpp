"""Verify lifetime-audit source hashes and citation bounds, not behavior."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
GAME_SHA256 = '0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--game-dll', type=Path)
args = parser.parse_args()
sources = {}
citations_checked = 0

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def visit(value):
    global citations_checked
    if isinstance(value, dict):
        path = value.get('source')
        if isinstance(path, str) and value.get('sha256'):
            actual = REPO / path
            if sha(actual) != value['sha256']:
                raise RuntimeError(f'Changed source: {path}')
            count = len(actual.read_text().splitlines())
            declared_count = value.get('source_lines', value.get('line_count'))
            if isinstance(declared_count, int) and count != declared_count:
                raise RuntimeError(f'Changed line count: {path}')
            for start, end in value.get('inspected_ranges', []):
                if not 1 <= start <= end <= count:
                    raise RuntimeError(f'Invalid citation: {path}:{start}-{end}')
                citations_checked += 1
            for item in value.get('citations', []):
                start = item.get('start', item.get('line'))
                end = item.get('end', item.get('end_line', start))
                if not isinstance(start, int) or not isinstance(end, int) or not 1 <= start <= end <= count:
                    raise RuntimeError(f'Invalid citation: {path}:{item}')
                citations_checked += 1
            for item in value.get('member_citations', []):
                if not 1 <= item['line'] <= count:
                    raise RuntimeError(f'Invalid member citation: {path}:{item}')
                citations_checked += 1
            sources[path] = {'sha256': value['sha256'], 'lines': count}
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)

for name in ['cards-inventory.json', 'entities.json', 'history-dispatch.json', 'rooms-continuations.json']:
    visit(json.loads((HERE / name).read_text()))
examples = json.loads((HERE / 'recording-examples.json').read_text())
if examples['boundaries_scanned'] != 697 or len(examples['retained_choice_inventory_sources']) != 3:
    raise RuntimeError('Unexpected recording example inventory')
if args.game_dll and sha(args.game_dll) != GAME_SHA256:
    raise RuntimeError('Unsupported game DLL')
artifacts = sorted(p for p in HERE.iterdir() if p.suffix in ('.md', '.json', '.py') and p.name != 'source-index.json')
report = {
    'evidence_kind': 'Artifact integrity only; not runtime parity, semantic completeness or GC proof',
    'game_dll_sha256': GAME_SHA256,
    'game_dll_checked_this_run': args.game_dll is not None,
    'local_decompilation_tool': 'ILSpy 9.1.0.7988',
    'git_revision_of_main_worktree': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
    'sources_checked': len(sources),
    'indexed_citations_checked': citations_checked,
    'sources': dict(sorted(sources.items())),
    'artifact_sha256': {p.name: sha(p) for p in artifacts},
}
(HERE / 'source-index.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'Checked {len(sources)} source identities and {citations_checked} indexed citations')
