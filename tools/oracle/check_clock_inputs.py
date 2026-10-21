#!/usr/bin/env python3
"""Verify canonical source clocks at zero, non-unit steps and the UTC upper bound."""
import argparse
from pathlib import Path
from play import PlaySession, RecordingWriter, new_actions
from reference_runtime import REPO, JsonProcess, reference_build, wire_json
from replay import load_pair, replay


def check(game_dir, output, validator):
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'
    logs.mkdir()
    with reference_build(game_dir, logs) as (command, env, provenance):
        for name, epoch, step in [('clock-zero', 0, 0), ('clock-step', 1700000123, 7),
                                  ('clock-limit', 253402300799, 0)]:
            destination = output / name
            destination.mkdir()
            actions = new_actions('ORACLECHECK01', name, run=False)
            actions['setup']['clock'].update(epoch_seconds=epoch, seconds_per_action=step)
            host = JsonProcess(command, env, logs / name, 30, log_protocol=False)
            session = PlaySession(host, actions, provenance, validator, RecordingWriter(destination, validator), 1)
            try:
                session.start()
                session.step({'kind': 'end_turn'})
                session.step({'kind': 'end_turn'})
                session.publish(final=True)
                (destination / 'reference.json').write_text(wire_json(dict(provenance, **session.identity)) + '\n')
            finally:
                host.close()
            actions, states = load_pair(destination / 'current/actions.json', destination / 'current/states.json', validator)
            host = JsonProcess(command, env, logs / f'{name}-replay', 30, log_protocol=False)
            try:
                replay(host, actions, states, provenance, validator)
            finally:
                host.close()
            print(f'{name}: reference self-replay passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    args = parser.parse_args()
    check(args.game_dir, args.output_dir.resolve(), args.validator.resolve())
