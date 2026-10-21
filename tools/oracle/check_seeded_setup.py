#!/usr/bin/env python3
"""Capture fully validated starting states across all characters and ascensions."""
import argparse
from pathlib import Path
from play import DEFAULT_CLOCK
from reference_runtime import REPO, JsonProcess, atomic_write, reference_build, require, validate


def check(game_dir, validator, logs, output):
    logs.mkdir(parents=True, exist_ok=True)
    cases = []
    with reference_build(game_dir, logs) as (command, env, provenance):
        for character in ('Ironclad', 'Silent', 'Regent', 'Necrobinder', 'Defect'):
            for ascension in (0, 4, 10):
                for seed in ('ORACLECHECK01', 'SETUPVERIFY02'):
                    label = f'{character}-{ascension}-{seed}'
                    host = JsonProcess(command, env, logs / label, 30, log_protocol=False)
                    try:
                        require(host.receive()['type'] == 'ready', 'Missing ready response')
                        response = host.send(dict(cmd='oracle_run_start', request_id='initial',
                            setup=dict(kind='seeded_run', character=character, ascension=ascension, seed=seed, clock=DEFAULT_CLOCK)))
                        validate(validator, 'snapshot', response['state'])
                        cases.append(dict(character='CHARACTER.' + character.upper(), ascension=ascension,
                                          seed=seed, expected=response['state']))
                    finally:
                        host.close()
            print(f'Captured {character} starting states', flush=True)
        atomic_write(output, dict(reference=provenance, cases=cases))
    print(f'Captured {len(cases)} full native starting states: {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    parser.add_argument('--output', type=Path, default=REPO / 'tests/fixtures/seeded-setup-v1.json')
    args = parser.parse_args()
    check(args.game_dir, args.validator.resolve(), args.logs.resolve(), args.output.resolve())
