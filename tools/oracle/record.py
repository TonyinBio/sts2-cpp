#!/usr/bin/env python3
"""Record actions-v1 input against a disposable, pinned sts2-cli reference build."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

REPO = Path(__file__).resolve().parents[2]
FEATURES = ['basic_combat', 'ironclad_starter_shrinker_v1']
GAME_SHA256 = '0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9'
REFERENCE_REVISION = '084d1aa3d8e118ca7ce8d8774ad16d6be9c92367'
DLLS = ('sts2.dll', 'SmartFormat.dll', 'SmartFormat.ZString.dll', 'Sentry.dll',
        'Sentry.Godot.dll', 'Steamworks.NET.dll', 'MonoMod.Backports.dll',
        'MonoMod.ILHelpers.dll', '0Harmony.dll', 'System.IO.Hashing.dll')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def strict_json(text):
    from native_run import exact_json
    return exact_json(text)


def wire_json(value):
    from native_run import exact_dump
    return exact_dump(value)


def validate(validator, kind, value, actions=None):
    request = {'kind': kind, 'value': wire_json(value) if kind == 'snapshot' else value}
    if actions is not None:
        request['actions'] = actions
    result = subprocess.run([str(validator)], input=wire_json(request) + '\n',
                            capture_output=True, text=True, timeout=30)
    require(result.returncode == 0, f'Validator failed: {result.stderr}')
    response = strict_json(result.stdout)
    require(response.get('valid') is True, f'Invalid {kind}: {response.get("error")}')


def check_scope(actions):
    require(actions['required_features'] == FEATURES, f'Required features must be {FEATURES}')
    setup = actions['setup']
    require(setup['character'] == 'Ironclad' and setup['ascension'] == 0 and
            setup['encounter'] == 'SHRINKER_BEETLE_WEAK',
            'Recorder supports Ironclad A0 / SHRINKER_BEETLE_WEAK only')
    for entry in actions['actions']:
        require(entry['action']['kind'] in ('play_card', 'end_turn'),
                f'{entry["id"]}: selection actions are not supported by this exporter profile')


class JsonProcess:
    """Single-flight line protocol with deadlines and continuously drained stderr."""
    def __init__(self, command, env, log_prefix, timeout):
        self.timeout = timeout
        self.lines = queue.Queue()
        self.stdout_log = open(str(log_prefix) + '.stdout.log', 'w')
        self.stderr_log = open(str(log_prefix) + '.stderr.log', 'w')
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True, env=env)
        def drain_stdout():
            try:
                for line in self.process.stdout:
                    self.stdout_log.write(line)
                    self.stdout_log.flush()
                    self.lines.put(line)
            finally:
                self.lines.put(None)
        def drain_stderr():
            for line in self.process.stderr:
                self.stderr_log.write(line)
                self.stderr_log.flush()
        self.threads = [threading.Thread(target=drain_stdout, daemon=True),
                        threading.Thread(target=drain_stderr, daemon=True)]
        for thread in self.threads:
            thread.start()

    def receive(self):
        deadline = time.monotonic() + self.timeout
        while True:
            remaining = deadline - time.monotonic()
            require(remaining > 0, 'Reference response timed out')
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty as error:
                raise RuntimeError('Reference response timed out') from error
            require(line is not None, f'Reference exited unexpectedly ({self.process.poll()})')
            if not line.strip():
                continue
            try:
                value = strict_json(line)
            except (ValueError, RuntimeError):
                # Some shipped DLL diagnostics use stdout, outside the JSON protocol.
                if line.lstrip().startswith(('{', '[')):
                    raise RuntimeError(f'Malformed reference JSON: {line[:200]}')
                continue
            require(isinstance(value, dict), 'Reference response must be an object')
            require(value.get('type') != 'error', f'Reference error: {value.get("message", value)}')
            return value

    def send(self, command):
        require(self.process.poll() is None, 'Reference process has exited')
        self.process.stdin.write(wire_json(command) + '\n')
        self.process.stdin.flush()
        return self.receive()

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=10)
        for thread in self.threads:
            thread.join(timeout=10)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr,
                       self.stdout_log, self.stderr_log):
            stream.close()


def inject(source):
    sim = source / 'RunSimulator.cs'
    text = sim.read_text()
    require(text.count('public class RunSimulator') == 1, 'Unexpected simulator declaration')
    anchor = 'private Dictionary<string, object?> DetectDecisionPoint()\n    {'
    require(text.count(anchor) == 1, 'Unexpected decision-point implementation')
    text = text.replace('public class RunSimulator', 'public partial class RunSimulator', 1)
    # Avoid UI preview calculations and post-combat reward generation in recording mode.
    text = text.replace(anchor, anchor + '\n        if (_oracleActive) return new() { ["type"] = "oracle_ack" };', 1)
    sim.write_text(text)
    program = source / 'Program.cs'
    text = program.read_text()
    require(text.count('case "start_run":') == 1, 'Unexpected command dispatcher')
    program.write_text(text.replace('case "start_run":',
        'case "oracle_start": return sim.OracleStart(cmd);\n'
        '            case "oracle_action": return sim.OracleAction(cmd);\n'
        '            case "start_run":', 1))
    for name in ('RecordingSession.cs', 'NativeRunRoundTrip.cs'):
        shutil.copy2(Path(__file__).parent / 'adapters' / name, source)


@contextlib.contextmanager
def reference_build(game_dir, log_dir):
    game = game_dir.resolve()
    if not (game / 'sts2.dll').is_file():
        candidates = list(game.glob('data_*/sts2.dll'))
        require(len(candidates) == 1, 'Pass the game directory or directory containing sts2.dll')
        game = candidates[0].parent
    require(hashlib.sha256((game / 'sts2.dll').read_bytes()).hexdigest() == GAME_SHA256,
            'Unsupported game DLL hash; exporter must be audited before accepting another build')
    submodule = REPO / 'sts2-cli'
    revision = subprocess.check_output(['git', '-C', str(submodule), 'rev-parse', 'HEAD'], text=True).strip()
    require(revision == REFERENCE_REVISION, 'Unsupported sts2-cli revision')
    require(not subprocess.check_output(
        ['git', '-C', str(submodule), 'status', '--porcelain', '--untracked-files=normal'], text=True).strip(),
        'Reference submodule has modifications or untracked source files')
    with tempfile.TemporaryDirectory(prefix='sts2-recorder-') as temporary:
        root = Path(temporary)
        shutil.copytree(submodule, root, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('.git', 'bin', 'obj', 'lib', 'logs',
                                                     'saves', '__pycache__'))
        lib = root / 'lib'
        lib.mkdir()
        for name in DLLS:
            shutil.copy2(game / name, lib / name)
        source = root / 'src/Sts2Headless'
        inject(source)
        # Hash actual adapter sources, schemas, runner, and copied managed dependencies.
        digest = hashlib.sha256()
        inputs = [p for p in (root / 'src').rglob('*') if p.suffix in ('.cs', '.csproj')]
        for path in sorted(inputs):
            digest.update(path.relative_to(root).as_posix().encode() + b'\0' + path.read_bytes())
        for path in [Path(__file__), *sorted((REPO / 'tests/parity/schema').glob('*.json')),
                     REPO / 'docs/specs/schemas/run-parity-v1.schema.json',
                     REPO / 'docs/specs/schemas/serializable-run-v20.schema.json',
                     *sorted(lib.glob('*.dll'))]:
            digest.update(path.name.encode() + b'\0' + path.read_bytes())
        env = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT='1',
                   DOTNET_CLI_HOME=str(log_dir / 'dotnet-home'), STS2_LIB=str(lib),
                   STS2_GAME_DIR=str(lib))
        build = subprocess.run(['dotnet', 'build', str(source / 'Sts2Headless.csproj'), '--nologo'],
                               capture_output=True, text=True, env=env, timeout=120)
        (log_dir / 'build.log').write_text(build.stdout + build.stderr)
        require(build.returncode == 0, f'Reference build failed; see {log_dir / "build.log"}')
        yield (['dotnet', str(source / 'bin/Debug/net9.0/Sts2Headless.dll')], env,
               {'revision': revision, 'game_dll_sha256': GAME_SHA256, 'adapter_sha256': digest.hexdigest()})


def record_session(command, env, actions, validator, log_prefix, timeout):
    host = JsonProcess(command, env, log_prefix, timeout)
    try:
        require(host.receive().get('type') == 'ready', 'Missing reference ready message')
        response = host.send({'cmd': 'oracle_start', 'request_id': 'initial',
                              'setup': actions['setup'], 'required_features': actions['required_features']})
        def state(response, request_id):
            require(response.get('type') == 'oracle_state' and response.get('request_id') == request_id,
                    f'Mismatched response for {request_id}')
            snapshot = response['state']
            validate(validator, 'snapshot', snapshot)
            return snapshot
        initial = state(response, 'initial')
        identity = {key: response[key] for key in ('assembly_mvid', 'runtime_version')}
        transitions = []
        previous = initial
        for index, entry in enumerate(actions['actions']):
            require(previous['boundary_kind'] != 'run_complete', f'{entry["id"]}: action after combat end')
            request_id = f'action-{index}'
            try:
                response = host.send({'cmd': 'oracle_action', 'request_id': request_id, 'action': entry['action']})
            except (RuntimeError, OSError) as error:
                raise RuntimeError(f'Action {index} ({entry["id"]}): {error}') from error
            require(all(response.get(key) == value for key, value in identity.items()), 'Reference identity changed')
            previous = state(response, request_id)
            transitions.append({'action_id': entry['id'], 'state': previous})
        require(host.send({'cmd': 'quit'}).get('type') == 'quit_result', 'Reference shutdown failed')
        host.process.wait(timeout=timeout)
        require(host.process.returncode == 0, 'Reference exited unsuccessfully')
        return {'initial': initial, 'transitions': transitions}, identity
    finally:
        host.close()


def atomic_write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, prefix=path.name + '.',
                                         suffix='.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(wire_json(value) + '\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('actions', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    parser.add_argument('--verify-repeat', action='store_true')
    parser.add_argument('--timeout', type=float, default=30)
    args = parser.parse_args()
    require(args.timeout > 0, 'Timeout must be positive')
    require(args.actions.resolve() != args.output.resolve(), 'Output must not overwrite the action input')
    validator = args.validator.resolve()
    require(validator.is_file(), 'Build the parity_validate CMake target first, or pass --validator')
    raw = args.actions.read_bytes()
    actions = strict_json(raw)
    validate(validator, 'actions', actions)
    check_scope(actions)
    output = args.output.resolve()
    logs = output.parent / (output.name + '.logs')
    logs.mkdir(parents=True, exist_ok=True)
    with reference_build(args.game_dir, logs) as (command, env, provenance):
        states, identity = record_session(command, env, actions, validator, logs / 'run1', args.timeout)
        if args.verify_repeat:
            repeated, repeated_identity = record_session(command, env, actions, validator, logs / 'run2', args.timeout)
            require(states == repeated and identity == repeated_identity, 'Same-input reference replay diverged')
    result = {'schema_version': 1, 'profile': 'states-v1', 'recording_id': actions['recording_id'],
              'actions_sha256': hashlib.sha256(raw).hexdigest(), 'reference': dict(provenance, **identity), **states}
    validate(validator, 'states', result, actions)
    atomic_write(output, result)
    print(f'Recorded {len(result["transitions"])} transitions plus initial state: {output}')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
        print(f'Recording failed: {error}', file=sys.stderr)
        sys.exit(1)
