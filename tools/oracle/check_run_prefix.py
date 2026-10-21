#!/usr/bin/env python3
"""Record and self-replay the seeded Neow-to-map prefix with every full state."""
import argparse
from pathlib import Path
from play import PlaySession, RecordingWriter, new_actions
from reference_runtime import REPO, JsonProcess, reference_build, require, strict_json, wire_json
from replay import load_pair, replay


def check(game_dir, logs, output, validator):
    logs.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=False)
    with reference_build(game_dir, logs) as (command, env, provenance):
        host = JsonProcess(command, env, logs / 'prefix', 30, log_protocol=False)
        try:
            session = PlaySession(host, new_actions('ORACLECHECK01', 'neow-map-prefix', run=True),
                                  provenance, validator, RecordingWriter(output, validator), 1)
            session.start()
            require(session.state['run_flow']['room']['options'][0]['relic']['id'] == 'RELIC.GOLDEN_PEARL', 'Pinned prefix changed')
            session.step({'kind': 'choose_option', 'option_index': 0})
            session.step({'kind': 'proceed'})
            session.publish(final=True)
            (output / 'reference.json').write_text(wire_json(dict(provenance, **session.identity)) + '\n')
        finally:
            host.close()
        actions, states = load_pair(output / 'current/actions.json', output / 'current/states.json', validator)
        host = JsonProcess(command, env, logs / 'verify', 30, log_protocol=False)
        try:
            replay(host, actions, states, provenance, validator)
        finally:
            host.close()
    print('Canonical Neow/map prefix recorded and reference self-replay passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    args = parser.parse_args()
    check(args.game_dir, args.logs.resolve(), args.output_dir.resolve(), args.validator.resolve())
