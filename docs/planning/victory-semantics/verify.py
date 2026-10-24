"""Check root coverage and local source identities; never certifies game semantics."""
import argparse
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
GAME_SHA256 = '0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=Path, default=REPO / 'build/combat-state-audit/decompiled')
parser.add_argument('--game-dll', type=Path)
args = parser.parse_args()
source_root = args.source_root.resolve()
roots_path = HERE.parent / 'victory-mechanics/scope-roots.json'
roots = json.loads(roots_path.read_text())
wanted = {x for values in roots.values() for x in values}
seen = set()
sources = {}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def source(path):
    relative = str(path).split('decompiled/', 1)[-1]
    actual = source_root / relative
    if not actual.is_file():
        raise RuntimeError(f'Missing source: {relative}')
    sources[relative] = {'sha256': digest(actual), 'lines': len(actual.read_text().splitlines())}
    return sources[relative]

def visit(value):
    if isinstance(value, dict):
        identity = value.get('id', value.get('model_id'))
        if identity in wanted:
            if identity in seen:
                raise RuntimeError(f'Duplicate root: {identity}')
            seen.add(identity)
        p = value.get('source')
        if isinstance(p, str) and p.endswith('.cs'):
            metadata = source(p)
            expected_hash = value.get('sha256', value.get('source_sha256'))
            if expected_hash and metadata['sha256'] != expected_hash:
                raise RuntimeError(f'Changed source: {p}')
            # Citation index lines are checked against the owning source. This
            # checks index integrity only, not whether a summary interprets it.
            def check_lines(item):
                if isinstance(item, dict):
                    for key, child in item.items():
                        if key == 'line' and isinstance(child, int):
                            if not 1 <= child <= metadata['lines']:
                                raise RuntimeError(f'Invalid citation: {p}:{child}')
                        check_lines(child)
                elif isinstance(item, list):
                    for child in item:
                        check_lines(child)
            check_lines(value)
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)

artifacts = ['cards.json', 'inventory.json', 'encounters.json']
for name in artifacts:
    visit(json.loads((HERE / name).read_text()))
if seen != wanted:
    raise RuntimeError(f'Root mismatch: missing={sorted(wanted-seen)}')

# Preserve every explicitly cited shared source as well as per-model sources.
for markdown in HERE.glob('*.md'):
    for path in re.findall(r'MegaCrit\.Sts2\.Core\.[A-Za-z0-9_.]+/[A-Za-z0-9_]+\.cs', markdown.read_text()):
        source(path)
for name in ['CardFactory', 'PotionFactory', 'CardPoolModel', 'CardSelectorPrefs',
             'MerchantPotionEntry', 'MerchantEntry', 'RelicCmd', 'CardPileCmd',
             'PowerCmd', 'CreatureCmd', 'CardSelectCmd', 'PotionModel', 'CardModel',
             'MonsterModel', 'AttackCommand', 'Hook', 'CombatState', 'CombatManager',
             'RunState', 'RandomBranchState', 'CardEnergyCost', 'OrbCmd']:
    for path in source_root.glob(f'*/{name}.cs'):
        source(path.relative_to(source_root))

if args.game_dll and digest(args.game_dll) != GAME_SHA256:
    raise RuntimeError('Unsupported game assembly')
report = {
    'evidence_kind': 'source identity and inventory integrity, not behavior certification',
    'game_dll_sha256': GAME_SHA256,
    'game_dll_checked_this_run': args.game_dll is not None,
    'decompiler': 'ILSpy 9.1.0.7988',
    'root_file_sha256': digest(roots_path),
    'root_counts': {key: len(value) for key, value in roots.items()},
    'roots_checked': len(seen),
    'artifact_sha256': {name: digest(HERE / name) for name in artifacts},
    'sources': dict(sorted(sources.items())),
}
(HERE / 'source-index.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'Checked {len(seen)} roots and {len(sources)} source identities')
