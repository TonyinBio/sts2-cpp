#!/usr/bin/env python3
"""Run actual initialized NPlayerHand callbacks in scene-capable Godot.

This includes the native Purity autoplay wrapper and command/effect continuation.
See the probe README for the precise seam and remaining #18 obligations.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
from xml.sax.saxutils import escape

from reference_runtime import GAME_SHA256, require
from reference_runtime import JsonProcess
from play import new_actions


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(command, env, cwd, log):
    with log.open('w') as stream:
        result = subprocess.run(command, env=env, cwd=cwd, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=120)
    require(result.returncode == 0, f'Command failed ({result.returncode}); see {log}')


def compare_public_policies(results, simulator, output):
    checked = []
    for native in results:
        if native['case_name'] not in ('purity-consumer', 'armaments-consumer'):
            continue
        source = 'CARD.PURITY' if native['case_name'] == 'purity-consumer' else 'CARD.ARMAMENTS'
        setup = new_actions('CHOICEPROBE', 'native-selection-policy', run=False)['setup']
        setup['clock']['seconds_per_action'] = 2
        deck = [{'id': source, 'upgraded': False}] + [{'id': 'CARD.DEFEND_IRONCLAD', 'upgraded': False}] * 4
        cpp = JsonProcess([str(simulator)], None, output / (native['case_name'] + '-cpp'), 60, log_protocol=False)
        steps = []
        try:
            reply = cpp.send({'cmd': 'start', 'setup': setup, 'scenario_deck': deck})
            require(reply.get('ok'), f'Public scenario initialization failed: {reply}')
            source_index = next(i for i, card in enumerate(reply['hand']) if card['model_id'] == source)
            initial_boundary = reply['boundary']
            initial_time = reply['state']['run_time']
            reply = cpp.send({'cmd': 'action', 'action': {'kind': 'play_card', 'card_index': source_index}})
            for index, observation in enumerate(native['observations']):
                if index:
                    label = observation['input']
                    prefix = 'deselect' if label.startswith('deselect') else 'choose'
                    action = {'kind': 'deselect_card' if prefix == 'deselect' else 'select_card',
                              'candidate_index': int(label[len(prefix):])}
                    reply = cpp.send({'cmd': 'action', 'action': action})
                require(reply.get('ok'), f'Public partial input failed: {reply}')
                projection = reply['selection']
                require(projection is not None and observation['pending'], 'Native/public pending choice differs')
                require(projection['selected_indices'] == observation['selected'], 'Native/public selected order differs')
                require(projection['can_confirm'] == observation['confirm'], 'Native/public confirm availability differs')
                public_candidates = [{'model_id': card['model_id'], 'upgraded': card['upgraded']} for card in projection['candidates']]
                require(public_candidates == native['candidates'], 'Native/public candidate model order differs')
                require(projection['minimum'] == native['minimum'] and projection['maximum'] == native['maximum']
                        and projection['automatic_at_maximum'] == native['automatic_submission'], 'Native/public selection restrictions differ')
                require(reply['boundary'] == initial_boundary + index + 1 and reply['state']['run_time'] == initial_time + 2 * (index + 1),
                        'Declared partial-input boundary/clock correspondence differs')
                steps.append({'native_callback': observation['input'], 'public_boundary': reply['boundary'],
                              'run_time': reply['state']['run_time'], 'selected': projection['selected_indices'], 'can_confirm': projection['can_confirm']})
            reply = cpp.send({'cmd': 'action', 'action': {'kind': 'confirm_selection'}})
            require(reply.get('ok') and reply['selection'] is None, 'Public confirmation did not resume effect')
            require(reply['boundary'] == initial_boundary + len(steps) + 1 and reply['state']['run_time'] == initial_time + 2 * (len(steps) + 1),
                    'Declared confirmation boundary/clock correspondence differs')
            checked.append({'case_name': native['case_name'], 'steps': steps, 'confirmation_boundary': reply['boundary'],
                            'confirmation_run_time': reply['state']['run_time'], 'seconds_per_public_input': 2,
                            'native_transport_clock': 'Not advanced by UI callbacks; atomic native transport correspondence is separately zero-step'})
        finally:
            cpp.close()
    return checked


def check(game_dir, godot, output, only=None, simulator=None):
    game_dir, godot, output = game_dir.resolve(), godot.resolve(), output.resolve()
    dlls = list(game_dir.glob('data_*/sts2.dll'))
    require(len(dlls) == 1, 'Pass the installed game directory containing its .pck')
    dll, pack = dlls[0], game_dir / 'SlayTheSpire2.pck'
    require(digest(dll) == GAME_SHA256, 'Unsupported game DLL')
    require(pack.is_file(), 'Missing installed game resource pack')
    require(godot.is_file(), 'Missing Godot executable')
    packages = godot.parent / 'GodotSharp/Tools/nupkgs'
    require(packages.is_dir(), 'Pass a Godot 4.5.1 Mono executable with bundled SDK packages')
    output.mkdir(parents=True, exist_ok=False)
    project = output / 'project'
    shutil.copytree(Path(__file__).with_name('native_choice_probe'), project)
    (project / 'Probe.csproj').write_text(
        '<Project Sdk="Godot.NET.Sdk/4.5.1"><PropertyGroup>'
        '<TargetFramework>net9.0</TargetFramework><Nullable>enable</Nullable>'
        '<EnableDynamicLoading>true</EnableDynamicLoading></PropertyGroup>'
        '<ItemGroup><Reference Include="sts2"><HintPath>' + escape(str(dll)) +
        '</HintPath><Private>false</Private></Reference></ItemGroup></Project>\n')
    (project / 'NuGet.Config').write_text(
        '<configuration><packageSources><clear/><add key="bundled" value="' +
        escape(str(packages), {'"': '&quot;'}) + '"/></packageSources></configuration>\n')
    env = dict(os.environ, DOTNET_CLI_HOME=str(output / 'dotnet-home'),
               NUGET_PACKAGES=str(output / 'packages'), DOTNET_CLI_TELEMETRY_OPTOUT='1',
               XDG_DATA_HOME=str(output / 'userdata'),
               STS2_CHOICE_GAME_DLL_DIR=str(dll.parent), STS2_CHOICE_GAME_PACK=str(pack))
    run(['dotnet', 'build', str(project / 'Probe.csproj')], env, project, output / 'build.log')
    results = []
    teardown = {}
    for case in ('ordered', 'zero', 'single', 'purity-consumer', 'armaments-consumer', 'headbutt-pile-command'):
        if only and case not in only:
            continue
        repeated = []
        for attempt in range(2):
            log = output / f'{case}-{attempt}.log'
            run([str(godot), '--headless', '--path', str(project)],
                dict(env, STS2_CHOICE_CASE=case), project, log)
            lines = log.read_text().splitlines()
            require('PROBE CLEANUP COMPLETE' in lines, f'Native cleanup did not complete; see {log}')
            cleanup = lines.index('PROBE CLEANUP COMPLETE')
            classified = []
            for index, line in enumerate(lines):
                require('PROBE ERROR' not in line, f'Native probe failed; see {log}')
                if line.startswith('ERROR:'):
                    shutdown_allocation = re.fullmatch(
                        r"ERROR: (?:\d+ RID allocations of type '[^']+' were leaked at exit\.|\d+ resources still in use at exit \(run with --verbose for details\)\.)", line)
                    require(index > cleanup and shutdown_allocation,
                            f'Native runtime error; see {log}')
                    classified.append(line)
            teardown[log.name] = classified
            records = [json.loads(line.removeprefix('CHOICE_RESULT ')) for line in lines
                       if line.startswith('CHOICE_RESULT ')]
            require(len(records) == 1, f'Missing unique successful callback result; see {log}')
            repeated.append(records[0])
        require(repeated[0] == repeated[1], f'Independent native callback repeat differs: {case}')
        results.append(repeated[0])
    require(results, 'No selected native callback cases')
    report = {'profile': 'native-selection-policies-v2', 'cases': results,
              'independent_native_repeat': 'passed',
              'game_dll_sha256': digest(dll), 'game_pack_sha256': digest(pack),
              'godot_sha256': digest(godot), 'runner_sha256': digest(Path(__file__)),
              'logs_sha256': {p.name: digest(p) for p in sorted(output.glob('*.log'))},
              'probe_sources_sha256': {p.name: digest(p) for p in sorted(project.iterdir()) if p.is_file()},
              'shutdown_allocation_diagnostics': teardown,
              'public_policy_comparison': compare_public_policies(results, simulator, output) if simulator else [],
              'read_purity_surface': ['run/player RNG save representations', 'player/enemy HP and Block', 'energy and round', 'ordered native combat pile IDs and upgrade levels'],
              'limits': ['Purity/Armaments consumers use native autoplay wrappers and BlockingPlayerChoiceContext; manual payment/queue is verified by the separate atomic model corpus',
                         'Pile case invokes native FromCombatPile and native Add(skipVisuals:true) using Headbutt preferences; full Headbutt model is verified by the separate atomic corpus',
                         'Scenario setup supplies native NGame services/scene containers; no full game startup or presentation certification',
                         'Public policy comparison covers prompt candidates, selection order/restrictions, confirmation and declared logical clocks; it is not whole-state or whole-screen projection certification',
                         'Native resource-cache allocation leaks at process teardown are retained and classified only after successful explicit scene cleanup']}
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Native initialized selection callbacks: {len(results)} cases, independent repeat passed; {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--only', action='append')
    parser.add_argument('--simulator', type=Path)
    args = parser.parse_args()
    check(args.game_dir, args.godot, args.output_dir, args.only, args.simulator.resolve() if args.simulator else None)
