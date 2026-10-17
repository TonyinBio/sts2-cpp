#!/usr/bin/env python3
"""Build a disposable sts2-cli copy and probe installed DLL state access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[2]
DLLS = (
    'sts2.dll', 'SmartFormat.dll', 'SmartFormat.ZString.dll', 'Sentry.dll',
    'Sentry.Godot.dll', 'Steamworks.NET.dll', 'MonoMod.Backports.dll',
    'MonoMod.ILHelpers.dll', '0Harmony.dll', 'System.IO.Hashing.dll',
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True,
                        help='Directory containing sts2.dll (or its parent)')
    parser.add_argument('--output', type=Path, default=REPO / 'build/oracle-check/report.json')
    args = parser.parse_args()
    game = args.game_dir.resolve()
    if not (game / 'sts2.dll').is_file():
        matches = list(game.glob('data_*/sts2.dll'))
        require(len(matches) == 1, 'Pass the directory containing sts2.dll')
        game = matches[0].parent
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT='1',
               DOTNET_CLI_HOME=str(output.parent / 'dotnet-home'))
    # No changes to the submodule or installed game, including localization.
    with tempfile.TemporaryDirectory(prefix='sts2-state-probe-') as temp:
        root = Path(temp)
        shutil.copytree(REPO / 'sts2-cli', root, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('.git', 'bin', 'obj', 'lib',
                                                     'logs', 'saves', '__pycache__'))
        lib = root / 'lib'
        lib.mkdir()
        env['STS2_LIB'] = str(lib)
        env['STS2_GAME_DIR'] = str(game)
        for name in DLLS:
            shutil.copy2(game / name, lib / name)
        source = root / 'src/Sts2Headless'
        shutil.copy2(Path(__file__).with_name('StateAccessProbe.cs'), source)
        sim = source / 'RunSimulator.cs'
        text = sim.read_text()
        require(text.count('public class RunSimulator') == 1, 'Unexpected RunSimulator declaration')
        sim.write_text(text.replace('public class RunSimulator', 'public partial class RunSimulator', 1))
        program = source / 'Program.cs'
        text = program.read_text()
        require(text.count('case "start_run":') == 1, 'Unexpected command dispatcher')
        program.write_text(text.replace('case "start_run":',
            'case "probe_state":\n                return sim.ProbeState();\n            case "start_run":', 1))
        build = subprocess.run(['dotnet', 'build', str(source / 'Sts2Headless.csproj'), '--nologo'],
                               env=env, capture_output=True, text=True, timeout=120)
        output.with_suffix('.build.log').write_text(build.stdout + build.stderr)
        require(build.returncode == 0, f'Build failed; see {output.with_suffix(".build.log")}')
        commands = [
            {'cmd': 'start_run', 'character': 'Ironclad', 'seed': 'ORACLECHECK01'},
            {'cmd': 'enter_room', 'type': 'combat', 'encounter': 'SHRINKER_BEETLE_WEAK'},
            {'cmd': 'probe_state'}, {'cmd': 'probe_state'},
            {'cmd': 'action', 'action': 'end_turn'}, {'cmd': 'probe_state'},
            {'cmd': 'action', 'action': 'end_turn'}, {'cmd': 'probe_state'},
            {'cmd': 'quit'},
        ]
        def run(index):
            process = subprocess.run(['dotnet', str(source / 'bin/Debug/net9.0/Sts2Headless.dll')],
                input=''.join(json.dumps(c) + '\n' for c in commands),
                text=True, capture_output=True, env=env, timeout=60)
            output.with_suffix(f'.run{index}.stdout.log').write_text(process.stdout)
            output.with_suffix(f'.run{index}.stderr.log').write_text(process.stderr)
            require(process.returncode == 0, 'Reference process failed')
            responses = []
            for line in process.stdout.splitlines():
                try:
                    value = json.loads(line)
                except ValueError:
                    continue  # The engine also writes non-protocol diagnostics.
                require(value.get('type') != 'error', f'Reference error: {value}')
                responses.append(value)
            snapshots = [v for v in responses if v.get('type') == 'probe_state']
            require(len(snapshots) == 4, 'Missing combat snapshots')
            return snapshots
        snapshots = run(1)
        require(snapshots == run(2), 'Same-seed process replays diverged')
        require(snapshots[0] == snapshots[1], 'Repeated export changed state')
        for snapshot in snapshots:
            require(snapshot['phase'] == 'Play', 'Snapshot not at player play phase')
            for prefix in ('run', 'player'):
                rng = snapshot[f'{prefix}_rng']['rngs']
                require(set(rng) == set(snapshot[f'{prefix}_rng_types']), 'Missing RNG streams')
                for state in rng.values():
                    require(set(state) == {'counter', 's0', 's1', 's2', 's3'},
                            'Unexpected serialized RNG fields')
            require(snapshot['rng_round_trips'] and all(snapshot['rng_round_trips'].values()),
                    'RNG restoration check failed')
            ids = [card['instance_id'] for pile in snapshot['piles'].values() for card in pile]
            require(len(ids) == 10 and len(set(ids)) == 10, 'Starter card identities not conserved')
        first, _, second, third = snapshots
        require(first['round'] == 1 and second['round'] == 2 and third['round'] == 3,
                'Turns did not advance')
        # The first end-turn draws the untouched remaining five cards in backing-list order.
        require(first['piles']['draw'] == [dict(c, index=i) for i, c in
                enumerate(second['piles']['hand'])], 'Draw order does not match subsequent hand')
        require(second['run_rng']['rngs']['Shuffle'] != third['run_rng']['rngs']['Shuffle'],
                'Reshuffle did not advance Shuffle RNG')
        release_path = game.parent / 'release_info.json'
        release_info = json.loads(release_path.read_text()) if release_path.is_file() else None
        report = {
            'status': 'passed',
            'release_info': release_info,
            'reference_revision': subprocess.check_output(
                ['git', '-C', str(REPO / 'sts2-cli'), 'rev-parse', 'HEAD'], text=True).strip(),
            'original_dll_sha256': hashlib.sha256((game / 'sts2.dll').read_bytes()).hexdigest(),
            'commands': commands, 'snapshots': snapshots,
            'scope': 'State access probe only; not complete combat serialization or game parity proof',
        }
        output.write_text(json.dumps(report, indent=2) + '\n')
        print(f'PASS: ordered piles, all serialized run/player RNG streams, restoration, '
              f'non-mutating exports, and two identical process replays. Report: {output}')


if __name__ == '__main__':
    main()
