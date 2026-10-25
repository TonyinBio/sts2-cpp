"""Shared reference build, protocol, and validation support for interactive recording."""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
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
    request = {'kind': kind, 'value': wire_json(value) if kind in ('snapshot', 'snapshot_once') else value}
    if actions is not None:
        request['actions'] = actions
    if isinstance(validator, ValidationSession):
        response = validator.host.send(request)
    else:
        result = subprocess.run([str(validator)], input=wire_json(request) + '\n',
                                capture_output=True, text=True, timeout=30)
        require(result.returncode == 0, f'Validator failed: {result.stderr}')
        response = strict_json(result.stdout)
    require(response.get('valid') is True, f'Invalid {kind}: {response.get("error")}')


def check_scope(actions):
    if actions['setup']['kind'] == 'seeded_run':
        require(actions['required_features'] == ['run_flow_v1'], 'Seeded runs require run_flow_v1')
        return
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
    def __init__(self, command, env, log_prefix, timeout, log_protocol=True):
        self.timeout = timeout
        self.lines = queue.Queue()
        self.stdout_log = open(str(log_prefix) + '.stdout.log', 'w')
        self.stderr_log = open(str(log_prefix) + '.stderr.log', 'w')
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True, env=env, start_new_session=True)
        def drain_stdout():
            try:
                for line in self.process.stdout:
                    if log_protocol or not line.lstrip().startswith(('{', '[')):
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
            if value.get('type') == 'error':
                if value.get('stack_trace'):
                    self.stderr_log.write(value['stack_trace'] + '\n')
                    self.stderr_log.flush()
                raise RuntimeError(f'Reference error: {value.get("message", value)}')
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


class ValidationSession:
    """One validator process and its content cache, scoped to one recording."""
    def __init__(self, executable, log_prefix, timeout=30):
        self.executable, self.log_prefix, self.timeout = executable, log_prefix, timeout
        self.host = None

    def __enter__(self):
        self.host = JsonProcess([str(self.executable)], os.environ, self.log_prefix,
                                self.timeout, log_protocol=False)
        return self

    def __exit__(self, *exc):
        self.host.close()


def inject(source):
    sim = source / 'RunSimulator.cs'
    text = sim.read_text()
    require(text.count('public class RunSimulator') == 1, 'Unexpected simulator declaration')
    anchor = 'private Dictionary<string, object?> DetectDecisionPoint()\n    {'
    require(text.count(anchor) == 1, 'Unexpected decision-point implementation')
    text = text.replace('public class RunSimulator', 'public partial class RunSimulator', 1)
    # Avoid UI preview calculations and post-combat reward generation in recording mode.
    text = text.replace(anchor, anchor + '\n        if (_oracleActive) return new() { ["type"] = "oracle_ack" };', 1)
    require(text.count('CardSelectCmd.UseSelector(_cardSelector);') == 2,
            'Unexpected native card selector setup')
    require(text.count('sim._pendingBundles = bundles;') == 1,
            'Unexpected bundle selector setup')
    text = text.replace("CardSelectCmd.UseSelector(_cardSelector);", "CardSelectCmd.UseSelector(_oracleRunMode ? (MegaCrit.Sts2.Core.TestSupport.ICardSelector)_runSelector : _cardSelector);")
    text = text.replace("sim._pendingBundles = bundles;", "sim._runBundleOwner = RunOwnerContext.Value ?? sim._runActionOwner;\n        sim._pendingBundles = bundles;")
    # Native queued actions can post from timer threads. Serialize the disposable
    # CLI context's queue without holding its lock while invoking callbacks.
    context_start = text.index('internal class InlineSynchronizationContext : SynchronizationContext')
    context_end = text.index('\n/// <summary>', context_start)
    text = text[:context_start] + """internal class InlineSynchronizationContext : SynchronizationContext
{
    private readonly Queue<(SendOrPostCallback, object?)> _queue = new();
    private readonly object _gate = new();
    private bool _executing;
    public override void Post(SendOrPostCallback callback, object? state)
    {
        lock (_gate) {
            _queue.Enqueue((callback, state));
            if (_executing) return;
            _executing = true;
        }
        while (true) {
            (SendOrPostCallback callback, object? state) next;
            lock (_gate) {
                if (_queue.Count == 0) { _executing = false; return; }
                next = _queue.Dequeue();
            }
            try { next.callback(next.state); }
            catch { lock (_gate) { _executing = false; } throw; }
        }
    }
    public override void Send(SendOrPostCallback callback, object? state) => callback(state);
    public void Pump() => Post(_ => { }, null);
}
""" + text[context_end:]
    # Reuse upstream event formatting without its implicit transitions or pumping.
    event_start = text.index('    private Dictionary<string, object?> EventChoiceState(')
    event_end = text.index('    private Dictionary<string, object?> RestSiteState(', event_start)
    event = text[event_start:event_end]
    body = event[event.index('        var options = currentOptions'):]
    event = """    private Dictionary<string, object?> RecordingEventState(EventRoom eventRoom)
    {
        var localEvent = MegaCrit.Sts2.Core.Runs.RunManager.Instance.EventSynchronizer.GetLocalEvent();
        Require(localEvent != null && !localEvent.IsFinished, "Event must settle before display");
        var currentOptions = localEvent.CurrentOptions;
""" + body
    text = text[:event_end] + event + text[event_end:]
    require(text.count('["potions"] = player.Potions?.Select') == 1, 'Unexpected potion display')
    text = text.replace('["potions"] = player.Potions?.Select', '["potions"] = player.PotionSlots?.Select', 1)
    sim.write_text(text)
    stub = source.parent / 'GodotStubs/Types.cs'
    stubs = stub.read_text()
    require(stubs.count('public static Callable From(Action action)') == 1, 'Unexpected Callable stub')
    stubs = stubs.replace('public static Callable From(Action action)',
        'public static Callable From<T>(Func<T> function) => new(function);\n    public static Callable From(Action action)', 1)
    stubs = stubs.replace('public void Call(params Variant[] args) => (_delegate as Action)?.Invoke();',
        'public void Call(params Variant[] args) { if (args.Length == 0) _delegate?.DynamicInvoke(); else (_delegate as Action)?.Invoke(); }', 1)
    stub.write_text(stubs)

    # Harmony resolves the original debug-audio IL before installing its no-op.
    # Supply the pinned Godot signatures even though playback is suppressed.
    audio_stub = source.parent / 'GodotStubs/UI.cs'
    audio = audio_stub.read_text()
    require(audio.count('public float VolumeDb { get; set; }') == 1, 'Unexpected audio stub')
    audio = audio.replace('public float VolumeDb { get; set; }',
        'public float VolumeDb { get; set; }\n'
        '    public float VolumeLinear { get; set; }\n'
        '    public float PitchScale { get; set; }\n'
        '    public StringName Bus { get; set; } = "";', 1)
    # Test Subject's phase-three color callback resolves this method even when
    # there is no creature node and the graphical branch will be skipped.
    require(audio.count('public Color SelfModulate { get; set; } = Color.White;') == 1,
            'Unexpected CanvasItem stub')
    audio = audio.replace('public Color SelfModulate { get; set; } = Color.White;',
        'public Color SelfModulate { get; set; } = Color.White;\n'
        '    public void SetSelfModulate(Color color) => SelfModulate = color;', 1)
    audio_stub.write_text(audio)
    core_stub = source.parent / 'GodotStubs/Core.cs'
    core = core_stub.read_text()
    # ReattachPower's final-segment death effect references this signature even
    # when no graphical creature nodes exist. The runtime resolves the whole
    # method before it can take the headless early return.
    require(core.count('public Node? GetParent() => _parent;') == 1, 'Unexpected Node stub')
    core = core.replace('public Node? GetParent() => _parent;',
        'public Node? GetParent() => _parent;\n'
        '    public int GetIndex(bool includeInternal = false) => _parent?._children.IndexOf(this) ?? 0;', 1)
    require(core.count('public class GodotObject\n{') == 1, 'Unexpected Godot object stub')
    core_stub.write_text(core.replace('public class GodotObject\n{',
        'public class GodotObject\n{\n'
        '    public Error Connect(StringName signal, Callable callable, uint flags = 0) => Error.Ok;', 1))

    program = source / 'Program.cs'
    text = program.read_text()
    require(text.count('case "start_run":') == 1, 'Unexpected command dispatcher')
    program.write_text(text.replace('case "start_run":',
        'case "oracle_display": return sim.OracleDisplay(cmd);\n'
        '            case "oracle_start": return sim.OracleStart(cmd);\n'
        '            case "oracle_action": return sim.OracleAction(cmd);\n'
        '            case "oracle_run_start": return sim.OracleRunStart(cmd);\n'
        '            case "oracle_run_action": return sim.OracleRunAction(cmd);\n'
        '            case "oracle_run_state": return sim.OracleRunState(cmd);\n'
        '            case "oracle_power_scenario": return sim.OraclePowerScenario(cmd);\n'
        '            case "start_run":', 1))
    for name in ('RecordingSession.cs', 'NativeRunRoundTrip.cs', 'RunRecordingSession.cs', 'CombatModelFields.cs', 'RecordingDisplay.cs', 'RecordingClock.cs'):
        shutil.copy2(Path(__file__).parent / 'adapters' / name, source)

    contract = strict_json((Path(__file__).parent / 'combat-field-contract.json').read_bytes())
    require(contract['game_dll_sha256'] == GAME_SHA256, 'Combat field contract targets another DLL')
    schema = strict_json((REPO / 'data/schemas/run-parity-v1.schema.json').read_bytes())
    # Embed the checked contract; no runtime dependency on the checkout or build audit output.
    (source / 'CombatModelContract.g.cs').write_text(
        'namespace Sts2Headless; internal static class CombatModelContract {\n'
        + 'internal const string Members = ' + json.dumps(wire_json(contract['members'])) + ';\n'
        + 'internal const string Schema = ' + json.dumps(wire_json(schema['$defs']['ModelFields']['properties'])) + ';\n}\n')


@contextlib.contextmanager
def reference_build(game_dir, log_dir, extra_sources=()):
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
        for path in [Path(__file__), *extra_sources, *sorted((REPO / 'data/schemas').glob('*.json')),
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


def start_request(actions):
    run = actions['setup']['kind'] == 'seeded_run'
    return {'cmd': 'oracle_run_start' if run else 'oracle_start', 'request_id': 'initial',
            'setup': actions['setup'], 'required_features': actions['required_features']}


def action_request(actions, action, state, request_id):
    if actions['setup']['kind'] != 'seeded_run':
        return {'cmd': 'oracle_action', 'request_id': request_id, 'action': action}
    kind = action['kind']
    args = {key: value for key, value in action.items() if key != 'kind'}
    if kind == 'play_card':
        hand = state['combat']['players'][0]['piles']['Hand']
        ref = '/combat/cards/' + args.pop('card_instance')
        require(ref in hand, 'Recorded card is not in hand')
        args['card_index'] = hand.index(ref)
    if kind == 'select_cards' and 'card_instances' in args:
        choice = state.get('pending_choice')
        require(choice is not None and 'min_select' in choice, 'No pending combat card selection')
        candidates = [ref.rsplit('/', 1)[1] for ref in choice['candidates']]
        identities = args.pop('card_instances')
        require(all(identity in candidates for identity in identities), 'Selected card is not an offered candidate')
        args['indices'] = [candidates.index(identity) for identity in identities]
    if 'target' in args:
        target = args.pop('target')
        require(target.startswith('enemy_') and target[6:].isdigit(), 'Unsupported target identity')
        ref = '/combat/creatures/' + target
        def alive(pointer):
            value = state
            for token in pointer[1:].split('/'):
                token = token.replace('~1', '/').replace('~0', '~')
                value = value[int(token)] if isinstance(value, list) else value[token]
            return value['monster']['current_hp'] > 0
        enemies = [pointer for pointer in state['combat']['enemies'] if alive(pointer)]
        require(ref in enemies, 'Recorded target is not present or alive')
        args['target_index'] = enemies.index(ref)
    return {'cmd': 'oracle_run_action', 'request_id': request_id, 'action': kind, 'args': args}


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
