#!/usr/bin/env python3
"""Probe native card identity and runtime storage in the pinned installed game DLL."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from record import DLLS, GAME_SHA256, REFERENCE_REVISION, REPO, JsonProcess, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=REPO / 'build/combat-state-audit/live-report.json')
    args = parser.parse_args()
    game = args.game_dir.resolve()
    if not (game / 'sts2.dll').is_file():
        candidates = list(game.glob('data_*/sts2.dll'))
        require(len(candidates) == 1, 'Pass the game directory or directory containing sts2.dll')
        game = candidates[0].parent
    require(hashlib.sha256((game / 'sts2.dll').read_bytes()).hexdigest() == GAME_SHA256,
            'Unsupported game DLL; this audit is pinned to v0.111.0')
    submodule = REPO / 'sts2-cli'
    require(subprocess.check_output(['git', '-C', str(submodule), 'rev-parse', 'HEAD'],
                                   text=True).strip() == REFERENCE_REVISION, 'Unsupported CLI revision')
    require(not subprocess.check_output(['git', '-C', str(submodule), 'status', '--porcelain'],
                                        text=True).strip(), 'Reference submodule has local changes')
    output = args.output.resolve()
    logs = output.parent / 'live'
    logs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sts2-field-audit-') as temporary:
        root = Path(temporary)
        shutil.copytree(submodule, root, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('.git', 'bin', 'obj', 'lib', 'logs',
                                                     'saves', '__pycache__'))
        lib = root / 'lib'
        lib.mkdir()
        for name in DLLS:
            shutil.copy2(game / name, lib / name)
        source = root / 'src/Sts2Headless'
        shutil.copy2(Path(__file__).parent / 'adapters' / 'CombatFieldAudit.cs', source)
        simulator = source / 'RunSimulator.cs'
        text = simulator.read_text()
        require(text.count('public class RunSimulator') == 1, 'Unexpected simulator declaration')
        simulator.write_text(text.replace('public class RunSimulator', 'public partial class RunSimulator', 1))
        program = source / 'Program.cs'
        text = program.read_text()
        require(text.count('case "start_run":') == 1, 'Unexpected dispatcher')
        program.write_text(text.replace('case "start_run":',
            'case "audit_combat_fields": return new Dictionary<string, object?> { '
            '["type"] = "combat_field_audit", ["data"] = sim.AuditCombatFields() };\n'
            '            case "audit_add_selection_card": return sim.AuditAddSelectionCard();\n'
            '            case "start_run":', 1))
        env = dict(os.environ, DOTNET_CLI_HOME=str(logs / 'dotnet-home'),
                   DOTNET_CLI_TELEMETRY_OPTOUT='1', STS2_LIB=str(lib), STS2_GAME_DIR=str(lib))
        build = subprocess.run(['dotnet', 'build', str(source / 'Sts2Headless.csproj'), '--nologo',
                                '--ignore-failed-sources'], env=env, capture_output=True,
                               text=True, timeout=120)
        (logs / 'build.log').write_text(build.stdout + build.stderr)
        require(build.returncode == 0, f'Build failed; see {logs / "build.log"}')
        host = JsonProcess(['dotnet', str(source / 'bin/Debug/net9.0/Sts2Headless.dll')],
                           env, logs / 'reference', 30)
        try:
            require(host.receive().get('type') == 'ready', 'Reference did not start')
            for command in (
                {'cmd': 'start_run', 'character': 'Ironclad', 'seed': 'FIELDAUDIT01', 'lang': 'en'},
                {'cmd': 'enter_room', 'type': 'combat', 'encounter': 'SHRINKER_BEETLE_WEAK'},
            ):
                response = host.send(command)
                require(response.get('type') != 'error', f'Reference error: {response}')

            def capture():
                response = host.send({'cmd': 'audit_combat_fields'})
                require(response.get('type') == 'combat_field_audit', f'Capture failed: {response}')
                return response['data']

            initial = capture()
            require(initial == capture(), 'Repeated capture changed exported state')
            response = host.send({'cmd': 'action', 'action': 'end_turn'})
            require(response.get('type') != 'error', f'End turn failed: {response}')
            later = capture()
            require(initial['phase'] == later['phase'] == 'Play', 'Not at settled player input')
            require(initial['round'] == 1 and later['round'] == 2, 'Did not advance a round')
            require(len(initial['cards']) == 10, 'Missing starter cards')
            require(all(card['has_distinct_deck_version'] for card in initial['cards']),
                    'Combat cards are not distinct copies linked to deck cards')
            require(sorted(card['deck_index'] for card in initial['cards']) == list(range(10)),
                    'Incomplete deck-copy mapping')
            require(initial['monsters'] and all(not monster['same_rng_as_run_monster_ai']
                                               for monster in initial['monsters']),
                    'Expected a distinct per-monster RNG object')
            cost = initial['cost_example']
            require(cost['before_cleanup']['modifiers'] and not cost['after_cleanup']['modifiers'],
                    'Isolated cost modifier did not expire')
            require(later['history_entry_types'], 'Combat history is empty')
            setup = host.send({'cmd': 'audit_add_selection_card'})
            require(setup.get('type') == 'audit_selection_setup', f'Selection setup failed: {setup}')
            response = host.send({'cmd': 'action', 'action': 'play_card',
                                  'args': {'card_index': setup['card_index']}})
            require(response.get('type') != 'error', f'Survivor play failed: {response}')
            selection = capture()
            require(selection == capture(), 'Repeated selection capture changed state')
            require(selection['pending_selection'] and selection['pending_cards'], 'No suspended card choice')
            require(selection['pending_min'] == selection['pending_max'] == 1, 'Wrong selection bounds')
            require(selection['energy'] == later['energy'] - 1 and selection['block'] == later['block'] + 8,
                    'Survivor did not pay cost and grant block before awaiting input')
            require(any(card['id'] == 'CARD.SURVIVOR' and card['pile'] == 'Play'
                        for card in selection['cards']), 'Suspended Survivor not in play pile')
            require(selection['started_plays'][-1]['card'] == 'CARD.SURVIVOR', 'Missing in-flight CardPlay')
            response = host.send({'cmd': 'action', 'action': 'select_cards', 'args': {'indices': '0'}})
            require(response.get('type') != 'error', f'Selection resolution failed: {response}')
            resolved = capture()
            require(not resolved['pending_selection'] and 'CardPlayFinishedEntry' in resolved['history_entry_types'],
                    'Card play did not finish after selection')
            output.write_text(json.dumps({
                'game_dll_sha256': GAME_SHA256, 'reference_revision': REFERENCE_REVISION,
                'scope': 'Bounded accessibility/identity probe; not full combat state or parity proof',
                'initial': initial, 'next_turn': later, 'selection': selection, 'resolved': resolved,
            }, indent=2) + '\n')
        finally:
            host.close()
    print(f'PASS: native deck/combat identity, separate monster RNG, cost expiry, history, '
          f'suspended Survivor selection and repeatable export. Report: {output}')


if __name__ == '__main__':
    main()
