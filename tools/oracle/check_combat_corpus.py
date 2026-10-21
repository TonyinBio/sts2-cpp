#!/usr/bin/env python3
"""Capture dense canonical combat recordings and independently self-replay them."""
import argparse
from pathlib import Path
from play import PlaySession, RecordingWriter, new_actions
from reference_runtime import REPO, JsonProcess, reference_build, require, strict_json, wire_json
from replay import load_pair, replay


def check(game_dir, output, validator):
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'
    logs.mkdir()
    with reference_build(game_dir, logs) as (command, env, provenance):
        for scenario in ('combat-victory-dense', 'combat-block-death'):
            destination = output / scenario
            destination.mkdir()
            host = JsonProcess(command, env, logs / scenario, 30, log_protocol=False)
            session = PlaySession(host, new_actions('ORACLECHECK01', scenario, run=False), provenance,
                                  validator, RecordingWriter(destination, validator), 1)
            try:
                session.start()
                if scenario == 'combat-victory-dense':
                    fixture = strict_json((REPO / 'tests/parity/fixtures/ironclad-shrinker-victory.actions.json').read_bytes())
                    for entry in fixture['actions']:
                        session.step(entry['action'])
                else:
                    session.step({'kind': 'play_card', 'card_instance': 'card_0'})
                    session.step({'kind': 'end_turn'})
                    session.step({'kind': 'play_card', 'card_instance': 'card_5'})
                    for _ in range(20):
                        if session.finished():
                            break
                        session.step({'kind': 'end_turn'})
                require(session.finished(), 'Combat did not reach its natural end')
                session.publish(final=True)
                # Obtained directly from the running backend, independent of the
                # expected-state file consumed by the simulator replay command.
                (destination / 'reference.json').write_text(wire_json(dict(provenance, **session.identity)) + '\n')
            finally:
                host.close()
            actions, states = load_pair(destination / 'current/actions.json', destination / 'current/states.json', validator)
            host = JsonProcess(command, env, logs / f'{scenario}-replay', 30, log_protocol=False)
            try:
                count, checkpoints = replay(host, actions, states, provenance, validator)
            finally:
                host.close()
            print(f'{scenario}: {count} actions, {checkpoints} complete checkpoints; reference self-replay passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    args = parser.parse_args()
    check(args.game_dir, args.output_dir.resolve(), args.validator.resolve())
