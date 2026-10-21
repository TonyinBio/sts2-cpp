#!/usr/bin/env python3
"""Verify a canonical actions.json/states.json pair against the reference backend."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

from play import PlaySession
from reference_runtime import (REPO, JsonProcess, action_request, check_scope,
                               reference_build, require, start_request, strict_json,
                               validate, wire_json)


class ReplayMismatch(RuntimeError):
    """Reference execution succeeded but a recorded checkpoint differs."""


def load_pair(actions_path, states_path, validator):
    raw = actions_path.read_bytes()
    actions, states = strict_json(raw), strict_json(states_path.read_bytes())
    validate(validator, 'actions', actions)
    validate(validator, 'states', states, actions)
    require(states['actions_sha256'] == hashlib.sha256(raw).hexdigest(),
            'Action file digest mismatch')
    check_scope(actions)
    require('clock' in actions['setup'],
            'Recording has no reproducible clock input; record a new session')
    return actions, states


def compare_checkpoint(validator, expected, actual, boundary):
    result = subprocess.run([str(validator)], input=wire_json({
        'kind': 'compare', 'expected': wire_json(expected), 'actual': wire_json(actual)
    }) + '\n', capture_output=True, text=True, timeout=30)
    require(result.returncode == 0, f'Comparator failed: {result.stderr}')
    response = strict_json(result.stdout)
    require(response.get('valid') is True, f'Comparator failed: {response.get("error")}')
    if not response['equal']:
        raise ReplayMismatch(f'{boundary}: {wire_json(response["difference"])}')


def replay(host, actions, states, provenance, validator):
    # Keep provenance independent of the recording. Expected states never enter
    # initialization or execution, including the logical clock configuration.
    require(all(states['reference'].get(key) == value for key, value in provenance.items()),
            'Reference provenance mismatch')
    session = PlaySession(host, actions, provenance, validator, None, 1)
    require(host.receive().get('type') == 'ready', 'Missing reference ready message')
    state, session.identity = session.response_state(host.send(start_request(actions)), 'initial')
    require(dict(provenance, **session.identity) == states['reference'], 'Reference runtime identity mismatch')
    compare_checkpoint(validator, states['initial'], state, 'initial')
    checkpoints = {entry['action_id']: entry['state'] for entry in states['transitions']}
    for entry in actions['actions']:
        boundary = entry['id']
        try:
            response = host.send(action_request(actions, entry['action'], state, boundary))
            state, _ = session.response_state(response, boundary)
        except (RuntimeError, ValueError, KeyError, IndexError) as error:
            raise RuntimeError(f'Reference execution at {boundary}: {error}') from error
        if boundary in checkpoints:
            compare_checkpoint(validator, checkpoints[boundary], state, boundary)
    return len(actions['actions']), len(checkpoints) + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--actions', type=Path, required=True)
    parser.add_argument('--states', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    args = parser.parse_args()
    validator = args.validator.resolve()
    actions, states = load_pair(args.actions, args.states, validator)
    logs = args.logs.resolve()
    logs.mkdir(parents=True, exist_ok=True)
    with reference_build(args.game_dir, logs) as (command, env, provenance):
        host = JsonProcess(command, env, logs / 'replay', 30, log_protocol=False)
        try:
            count, checkpoints = replay(host, actions, states, provenance, validator)
        finally:
            host.close()
    print(f'Reference self-replay passed: {count} actions, {checkpoints} full checkpoints')


if __name__ == '__main__':
    try:
        main()
    except ReplayMismatch as error:
        print(f'Recording divergence: {error}', file=sys.stderr)
        sys.exit(2)
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
        print(f'Reference verification failed: {error}', file=sys.stderr)
        sys.exit(1)
