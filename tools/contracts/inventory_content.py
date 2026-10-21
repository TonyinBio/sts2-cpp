#!/usr/bin/env python3
"""Inventory pinned decompiled model declarations; never claims reachability/parity."""
import argparse
import csv
import io
from pathlib import Path
import re

CATEGORIES = ('Acts', 'Characters', 'Cards', 'CardPools', 'Monsters', 'Encounters',
              'Powers', 'Relics', 'RelicPools', 'Potions', 'PotionPools', 'Events',
              'Orbs', 'Enchantments', 'Afflictions', 'Modifiers')


def inventory(root):
    rows = []
    for category in CATEGORIES:
        directory = root / ('MegaCrit.Sts2.Core.Models.' + category)
        if not directory.is_dir():
            raise ValueError(f'Missing source directory: {directory}')
        for path in sorted(directory.glob('*.cs')):
            source = path.read_text()
            if re.search(r'^public abstract class ', source, re.M):
                continue
            match = re.search(r'^public (?:sealed )?class (\w+)\s*:', source, re.M)
            if not match:
                raise ValueError(f'Unrecognized concrete model declaration: {path}')
            name = match[1]
            multiplayer = bool(re.search(
                r'override CardMultiplayerConstraint MultiplayerConstraint\s*=>\s*'
                r'CardMultiplayerConstraint.MultiplayerOnly\s*;', source))
            scope = ('excluded_multiplayer' if multiplayer else
                     'excluded_deprecated' if name.startswith('Deprecated') else
                     'candidate_single_player')
            rows.append((category, name, scope, 'not_implemented', 'not_verified',
                         'not_verified', path.relative_to(root).as_posix()))
    buffer = io.StringIO(newline='')
    writer = csv.writer(buffer, delimiter='\t', lineterminator='\n')
    writer.writerow(('category', 'source_class', 'scope', 'implementation',
                     'reference_verification', 'interaction_verification', 'source'))
    writer.writerows(rows)
    return buffer.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path(
        'docs/specs/engine/content-inventory-v0.111.0.tsv'))
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    result = inventory(args.source)
    if args.check:
        if args.output.read_text() != result:
            parser.exit(1, 'Content inventory differs from source declarations\n')
        print('Content inventory matches source declarations')
    else:
        args.output.write_text(result)
        print(f'Wrote {len(result.splitlines()) - 1} model declarations')


if __name__ == '__main__':
    main()
