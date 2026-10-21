"""Exact comparisons and a disposable native save serialization probe."""
import contextlib
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from reference_runtime import DLLS, GAME_SHA256, REFERENCE_REVISION, REPO, require


def exact_json(text):
    def pairs(entries):
        result = {}
        for key, value in entries:
            if key in result:
                raise ValueError(f'Duplicate JSON key: {key}')
            result[key] = value
        return result

    def invalid(value):
        raise ValueError(f'Invalid JSON number: {value}')

    return json.loads(text, parse_float=Decimal, parse_constant=invalid,
                      object_pairs_hook=pairs)


def exact_dump(value, *, sort_keys=False):
    """Write decimal tokens exactly when constructing a modified test input."""
    if isinstance(value, Decimal):
        if not value.is_finite():
            raise ValueError('JSON decimals must be finite')
        return str(value)
    if isinstance(value, dict):
        keys = sorted(value) if sort_keys else value.keys()
        return '{' + ','.join(json.dumps(k) + ':' + exact_dump(value[k], sort_keys=sort_keys)
                              for k in keys) + '}'
    if isinstance(value, list):
        return '[' + ','.join(exact_dump(v, sort_keys=sort_keys) for v in value) + ']'
    return json.dumps(value, ensure_ascii=False, allow_nan=False)


_ABSENT = object()


def differences(expected, actual, path=''):
    """Compare every field, preserving arrays, exact numbers, and missing vs null."""
    if isinstance(expected, dict) and isinstance(actual, dict):
        result = []
        for key in sorted(expected.keys() | actual.keys()):
            token = key.replace('~', '~0').replace('/', '~1')
            result.extend(differences(expected.get(key, _ABSENT), actual.get(key, _ABSENT),
                                      path + '/' + token))
        return result
    if isinstance(expected, list) and isinstance(actual, list):
        result = []
        for index in range(max(len(expected), len(actual))):
            result.extend(differences(expected[index] if index < len(expected) else _ABSENT,
                                      actual[index] if index < len(actual) else _ABSENT,
                                      path + '/' + str(index)))
        return result
    # Python's True == 1 must not conceal a changed JSON type.
    numeric = (int, Decimal)
    equal = (type(expected) is type(actual) or
             (type(expected) in numeric and type(actual) in numeric)) and expected == actual
    if equal:
        return []
    return [{'path': path or '/', 'expected': expected, 'actual': actual}]


def assert_unchanged(expected, actual):
    changes = differences(expected, actual)
    if changes:
        def describe(value):
            return '<missing>' if value is _ABSENT else repr(value)
        detail = '\n'.join(f'{c["path"]}: {describe(c["expected"])} -> {describe(c["actual"])}'
                           for c in changes[:20])
        raise AssertionError(f'{len(changes)} changed native field(s):\n{detail}')


@contextlib.contextmanager
def reference_build(game_dir, logs):
    game = Path(game_dir).resolve()
    if not (game / 'sts2.dll').is_file():
        candidates = list(game.glob('data_*/sts2.dll'))
        require(len(candidates) == 1, 'Pass the game directory or directory containing sts2.dll')
        game = candidates[0].parent
    require(hashlib.sha256((game / 'sts2.dll').read_bytes()).hexdigest() == GAME_SHA256,
            'Unsupported game DLL: native round-trip tests are pinned to v0.111.0')
    submodule = REPO / 'sts2-cli'
    revision = subprocess.check_output(['git', '-C', str(submodule), 'rev-parse', 'HEAD'],
                                       text=True).strip()
    require(revision == REFERENCE_REVISION, 'Unsupported sts2-cli revision')
    require(not subprocess.check_output(['git', '-C', str(submodule), 'status', '--porcelain'],
                                        text=True).strip(), 'Reference submodule has local changes')
    logs = Path(logs).resolve()
    logs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sts2-native-roundtrip-') as directory:
        root = Path(directory)
        shutil.copytree(submodule, root, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('.git', 'bin', 'obj', 'lib', 'logs',
                                                     'saves', '__pycache__'))
        lib = root / 'lib'
        lib.mkdir()
        for name in DLLS:
            shutil.copy2(game / name, lib / name)
        source = root / 'src/Sts2Headless'
        shutil.copy2(Path(__file__).parent / 'adapters' / 'NativeRunRoundTrip.cs', source)
        program = source / 'Program.cs'
        text = program.read_text()
        anchor = 'case "start_run":'
        require(text.count(anchor) == 1, 'Unexpected reference command dispatcher')
        program.write_text(text.replace(anchor,
            'case "native_run_roundtrip": return NativeRunRoundTrip.Run(cmd);\n            ' + anchor, 1))
        env = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT='1',
                   DOTNET_CLI_HOME=str(logs / 'dotnet-home'), STS2_LIB=str(lib), STS2_GAME_DIR=str(lib))
        build = subprocess.run(['dotnet', 'build', str(source / 'Sts2Headless.csproj'), '--nologo'],
                               env=env, capture_output=True, text=True, timeout=120)
        (logs / 'build.log').write_text(build.stdout + build.stderr)
        require(build.returncode == 0, f'Build failed; see {logs / "build.log"}')
        yield ['dotnet', str(source / 'bin/Debug/net9.0/Sts2Headless.dll')], env
