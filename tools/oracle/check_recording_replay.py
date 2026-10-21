#!/usr/bin/env python3
"""Live canonical self-replay regression: combat victory and sparse run death."""
import argparse
from pathlib import Path

from play import PlaySession, RecordingWriter, new_actions
from reference_runtime import REPO, JsonProcess, reference_build, require, strict_json
from replay import load_pair, replay
from upstream_play import RecordingTransport


def check(game_dir, validator, directory):
    directory.mkdir(parents=True, exist_ok=False)
    logs = directory / 'logs'
    logs.mkdir()
    with reference_build(game_dir, logs) as (command, env, provenance):
        for scenario in ('combat-victory', 'run-death'):
            destination = directory / scenario
            destination.mkdir()
            host = JsonProcess(command, env, logs / scenario, 30, log_protocol=False)
            actions = new_actions('ORACLECHECK01', scenario, run=scenario == 'run-death')
            session = PlaySession(host, actions, provenance, validator,
                                  RecordingWriter(destination, validator), 3)
            try:
                session.start()
                if scenario == 'combat-victory':
                    fixture = strict_json((REPO / 'tests/parity/fixtures/ironclad-shrinker-victory.actions.json').read_bytes())
                    for entry in fixture['actions']:
                        session.step(entry['action'])
                    require(session.finished(), 'Combat fixture did not finish')
                    require(session.state['run_ending_reason'] is None, 'Combat victory ended run')
                else:
                    # Use the actual transport once when recording; replay must
                    # execute its explicit automatic proceed action only once.
                    transport = RecordingTransport(session)
                    transport({'cmd': 'action', 'action': 'choose_option', 'args': {'option_index': 0}})
                    session.step({'kind': 'select_map_node', 'col': 0, 'row': 1})
                    for _ in range(50):
                        if session.finished():
                            break
                        session.step({'kind': 'end_turn'})
                    require(session.state['run_ending_reason'] == 'death', 'Run did not reach natural death')
                session.publish(final=True)
            finally:
                host.close()
            actions, states = load_pair(destination / 'current/actions.json', destination / 'current/states.json', validator)
            # Replay twice with independent processes; no expected state is used
            # as clock input or initialization data.
            for attempt in range(2):
                host = JsonProcess(command, env, logs / f'{scenario}-replay-{attempt}', 30, log_protocol=False)
                try:
                    count, checkpoints = replay(host, actions, states, provenance, validator)
                finally:
                    host.close()
            print(f'{scenario}: {count} actions, {checkpoints} full checkpoints, two independent replays passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    args = parser.parse_args()
    check(args.game_dir, args.validator.resolve(), args.output_dir.resolve())
