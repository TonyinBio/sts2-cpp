#!/usr/bin/env python3
"""Repeatable wall-time profiling of the real play.py load-to-first-input path."""
import argparse
import collections
import contextlib
import contextvars
import cProfile
import functools
import hashlib
import json
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path
from unittest.mock import patch

import play
import recording_journal as journal
import reference_runtime as runtime
import upstream_play


class ProfileComplete(BaseException):
    """Stop before user input, while allowing the real shutdown/export to run."""


class SpeedProfile:
    def __init__(self, expected_commands=0, clock=time.perf_counter):
        self.clock = clock
        self.started = self.phase_started = clock()
        self.phase = 'startup'
        self.ready_seconds = None
        self.expected_commands = expected_commands
        self.phases = collections.defaultdict(float)
        self.costs = collections.defaultdict(lambda: {'calls': 0, 'inclusive_seconds': 0.0,
                                                     'exclusive_seconds': 0.0})
        self.commands = []
        self.session = None
        self.frames = contextvars.ContextVar('profile_frames', default=())

    def change_phase(self, phase):
        now = self.clock()
        self.phases[self.phase] += now - self.phase_started
        self.phase_started, self.phase = now, phase

    @contextlib.contextmanager
    def measure(self, category):
        frame = [self.phase, category, 0.0]
        parents = self.frames.get()
        token = self.frames.set((*parents, frame))
        start = self.clock()
        try:
            yield
        finally:
            elapsed = self.clock() - start
            self.frames.reset(token)
            if parents:
                parents[-1][2] += elapsed
            cost = self.costs[(frame[0], category)]
            cost['calls'] += 1
            cost['inclusive_seconds'] += elapsed
            cost['exclusive_seconds'] += max(0.0, elapsed - frame[2])

    def wrap(self, function, category):
        @functools.wraps(function)
        def timed(*args, **kwargs):
            name = category(*args, **kwargs) if callable(category) else category
            with self.measure(name):
                return function(*args, **kwargs)
        return timed

    def input_ready(self, prompt=''):
        runtime.require(len(self.commands) == self.expected_commands,
                        'Input requested before the saved commands completed')
        self.ready_seconds = self.clock() - self.started
        self.change_phase('shutdown')
        print(prompt, '[profiler stopped before interactive input]')
        raise ProfileComplete()

    def report(self):
        self.change_phase('finished')
        costs = [dict(phase=phase, category=category, **values)
                 for (phase, category), values in self.costs.items()]
        costs.sort(key=lambda row: row['exclusive_seconds'], reverse=True)
        unattributed = {phase: max(0.0, seconds - sum(row['exclusive_seconds'] for row in costs
                                                    if row['phase'] == phase))
                        for phase, seconds in self.phases.items()}
        return {'ready_seconds': self.ready_seconds, 'phase_seconds': dict(self.phases),
                'unattributed_seconds': unattributed,
                'costs': costs, 'commands': self.commands,
                'recorded_actions': len(self.session.actions['actions']) if self.session else 0}

    @contextlib.contextmanager
    def instrument(self):
        """Patch only for this run. Nested exclusive times never count children twice."""
        with contextlib.ExitStack() as patches:
            def timed(owner, name, category):
                patches.enter_context(patch.object(owner, name, self.wrap(getattr(owner, name), category)))

            for owner in (runtime, journal):
                timed(owner, 'wire_json', 'json_encode')
            for owner in (runtime, journal, play):
                timed(owner, 'strict_json', 'json_decode')
                timed(owner, 'validate', lambda validator, kind, *args, **kwargs: 'validate.' + kind)
            timed(journal.os, 'fsync', 'fsync')
            timed(journal.os, 'replace', 'atomic_replace')
            timed(journal, 'sync_directory', 'sync_directory')
            timed(journal, 'atomic_write', 'atomic_file_write')
            timed(journal.RecordingWriter, '_commit', 'journal_commit')
            timed(journal._PairWriter, 'publish', 'json_export')
            timed(runtime.subprocess, 'run', lambda command, *a, **k:
                  'dotnet_build' if isinstance(command, list) and command[:2] == ['dotnet', 'build']
                  else 'subprocess_run')

            original_init = runtime.JsonProcess.__init__
            def process_init(process, command, *args, **kwargs):
                prefix = args[1] if len(args) > 1 else kwargs['log_prefix']
                process.profile_role = 'validator' if Path(prefix).name == 'validator' else 'backend'
                with self.measure(process.profile_role + '_process_start'):
                    return original_init(process, command, *args, **kwargs)
            patches.enter_context(patch.object(runtime.JsonProcess, '__init__', process_init))
            original_send = runtime.JsonProcess.send
            def send(process, command):
                previous = getattr(process, 'profile_request', 'ready')
                process.profile_request = command.get('kind', command.get('cmd', 'unknown'))
                category = 'validator_send' if process.profile_role == 'validator' else 'backend.' + process.profile_request
                try:
                    with self.measure(category):
                        return original_send(process, command)
                finally:
                    process.profile_request = previous
            patches.enter_context(patch.object(runtime.JsonProcess, 'send', send))
            timed(runtime.JsonProcess, 'receive', lambda process:
                  process.profile_role + '_wait.' + getattr(process, 'profile_request', 'ready'))
            timed(runtime.JsonProcess, 'close', 'process_cleanup')

            original_build = play.reference_build
            @contextlib.contextmanager
            def reference_build(*args, **kwargs):
                stack = contextlib.ExitStack()
                try:
                    with self.measure('reference_build'):
                        result = stack.enter_context(original_build(*args, **kwargs))
                    yield result
                finally:
                    with self.measure('reference_build_cleanup'):
                        stack.close()
            patches.enter_context(patch.object(play, 'reference_build', reference_build))

            original_start = play.PlaySession.start
            def start_session(session):
                self.session = session
                with self.measure('session_start'):
                    return original_start(session)
            patches.enter_context(patch.object(play.PlaySession, 'start', start_session))
            timed(play.PlaySession, 'step', 'record_action')

            original_call = upstream_play.RecordingTransport.__call__
            def command(transport, value):
                name = value['cmd']
                if name == 'quit':
                    self.change_phase('shutdown')
                if name != 'action':
                    return original_call(transport, value)
                if self.phase != 'replay':
                    self.change_phase('replay')
                start = self.clock()
                before = len(transport.session.actions['actions'])
                completed = False
                try:
                    result = original_call(transport, value)
                    completed = True
                    return result
                finally:
                    self.commands.append({'index': len(self.commands) + 1,
                                          'action': value.get('action'),
                                          'completed': completed,
                                          'recorded_actions': len(transport.session.actions['actions']) - before,
                                          'seconds': self.clock() - start})
            patches.enter_context(patch.object(upstream_play.RecordingTransport, '__call__', command))
            patches.enter_context(patch('builtins.input', self.input_ready))
            yield


def profile_run(arguments, directory, expected_commands=0, capture_cprofile=False):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    profiler = SpeedProfile(expected_commands)
    detailed = cProfile.Profile() if capture_cprofile else None
    status, error = 'failed', None
    with (directory / 'console.log').open('w') as console:
        try:
            with contextlib.redirect_stdout(console), contextlib.redirect_stderr(console), \
                    profiler.instrument(), patch.object(sys, 'argv', ['play.py', *arguments]):
                if detailed:
                    detailed.enable()
                try:
                    play.main()
                except ProfileComplete:
                    status = 'ready'
                else:
                    error = 'play.py returned before requesting interactive input'
        except (Exception, SystemExit, KeyboardInterrupt) as failure:
            error = f'{type(failure).__name__}: {failure}'
        finally:
            if detailed:
                detailed.disable()
            report = profiler.report()
            if detailed:
                detailed.dump_stats(str(directory / 'python.prof'))
    reference = directory / 'recording/current/reference.json'
    report['reference'] = json.loads(reference.read_text()) if reference.exists() else None
    report.update(schema_version=1, status=status, error=error, play_arguments=arguments,
                  cprofile=capture_cprofile)
    (directory / 'profile.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def print_report(report, directory):
    print(f"{directory.name}: {report['status']}")
    for phase, seconds in report['phase_seconds'].items():
        print(f'  {phase:12s} {seconds:9.3f} s')
    if report['ready_seconds'] is not None:
        print(f"  ready total  {report['ready_seconds']:9.3f} s")
    print(f"  saved commands: {len(report['commands'])}; recorded actions: {report['recorded_actions']}")
    print('  replay costs (exclusive wall time):')
    replay_seconds = report['phase_seconds'].get('replay', 0.0)
    for cost in (row for row in report['costs'] if row['phase'] == 'replay'):
        share = cost['exclusive_seconds'] / replay_seconds * 100 if replay_seconds else 0
        print(f"    {cost['category']:25s} {cost['exclusive_seconds']:8.3f} s  {share:5.1f}%  {cost['calls']:5d} calls")
    print(f"    other/uninstrumented      {report['unattributed_seconds'].get('replay', 0.0):8.3f} s")
    if report['error']:
        print('  error:', report['error'])
    print('  report:', directory / 'profile.json', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False,
        epilog='Other options are forwarded to play.py. Each run stops before its first input prompt.')
    parser.add_argument('--output-dir', type=Path, required=True, help='New directory for retained profiles and recordings')
    parser.add_argument('--repeat', type=play.positive_int, default=1)
    parser.add_argument('--max-commands', type=play.positive_int, help='Profile only this prefix of the action-log save')
    parser.add_argument('--cprofile', action='store_true', help='Also save Python function stats; adds measurement overhead')
    args, forwarded = parser.parse_known_args()
    if forwarded[:1] == ['--']:
        forwarded = forwarded[1:]
    # Parse supported load options without duplicating play.py's other defaults.
    load_parser = argparse.ArgumentParser(add_help=False)
    load_parser.add_argument('--load', type=Path)
    load_parser.add_argument('--auto', action='store_true')
    load_parser.add_argument('--export')
    load_parser.add_argument('--validator', type=Path, default=runtime.REPO / 'build/debug/parity_validate')
    load_args, _ = load_parser.parse_known_args(forwarded)
    runtime.require(not load_args.auto and load_args.export is None, '--auto and --export are not supported by this load profiler')
    runtime.require(not args.max_commands or load_args.load, '--max-commands requires --load')
    runtime.require(load_args.validator.is_file(), 'Build parity_validate first, or pass --validator')
    source = load_args.load.read_bytes() if load_args.load else None
    saved = runtime.strict_json(source) if source else None
    runtime.require(saved is None or isinstance(saved, dict) and isinstance(saved.get('actions'), list),
                    '--load must be an action-log JSON save')
    expected = len(saved['actions']) if saved else 0
    root = args.output_dir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    if args.max_commands:
        saved['actions'] = saved['actions'][:args.max_commands]
        expected = len(saved['actions'])
        limited = root / 'load-prefix.json'
        limited.write_text(runtime.wire_json(saved) + '\n')
        forwarded = ['--load', str(limited), *load_parser.parse_known_args(forwarded)[1]]
        # Keep the explicitly selected validator when reconstructing arguments.
        forwarded += ['--validator', str(load_args.validator.resolve())]
    revision = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=runtime.REPO,
                              capture_output=True, text=True)
    code_hashes = {name: hashlib.sha256((Path(__file__).parent / name).read_bytes()).hexdigest()
                   for name in ('profile_play.py', 'play.py', 'reference_runtime.py',
                                'recording_journal.py', 'upstream_play.py', 'native_run.py')}
    validator_hash = hashlib.sha256(load_args.validator.read_bytes()).hexdigest()
    reports = []
    for index in range(1, args.repeat + 1):
        directory = root / f'run-{index:03d}'
        arguments = [*forwarded, '--output-dir', str(directory / 'recording')]
        report = profile_run(arguments, directory, expected, args.cprofile)
        reports.append(report)
        print_report(report, directory)
        if report['status'] != 'ready':
            break
    ready = [report['ready_seconds'] for report in reports if report['status'] == 'ready']
    summary = {'schema_version': 1, 'python': sys.version, 'platform': platform.platform(),
               'git_revision': revision.stdout.strip() if revision.returncode == 0 else None,
               'source_hashes': code_hashes,
               'source_path': str(load_args.load.resolve()) if load_args.load else None,
               'source_sha256': hashlib.sha256(source).hexdigest() if source is not None else None,
               'saved_commands': expected, 'validator_path': str(load_args.validator.resolve()),
               'validator_sha256': validator_hash, 'requested_repeats': args.repeat,
               'max_commands': args.max_commands,
               'cprofile': args.cprofile, 'runs': [report['status'] for report in reports],
               'ready_seconds': ready, 'median_ready_seconds': statistics.median(ready) if ready else None,
               'scope': 'play.main entry to first input; excludes interpreter/imports and shutdown/export'}
    (root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    if ready:
        print(f'Median time to input: {statistics.median(ready):.3f} s ({len(ready)} successful runs)')
    return 0 if len(ready) == args.repeat else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError) as error:
        print(f'Profiler failed: {error}', file=sys.stderr)
        sys.exit(1)
