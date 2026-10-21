#!/usr/bin/env python3
"""Extract immutable setup definitions and independent map regression cases."""
import argparse
from pathlib import Path
import shutil
import reference_runtime as reference


def check(game_dir, logs, definitions, maps):
    logs.mkdir(parents=True, exist_ok=True)
    original = reference.inject
    def inject(source):
        original(source)
        shutil.copy2(Path(__file__).parent / 'adapters/SetupDefinitionsAudit.cs', source)
        program = source / 'Program.cs'
        program.write_text(program.read_text().replace('case "start_run":',
            'case "setup_definitions": return sim.SetupDefinitions(cmd);\n'
            'case "setup_map_audit": return sim.SetupMapAudit(cmd);\ncase "start_run":', 1))
    reference.inject = inject
    try:
        with reference.reference_build(game_dir, logs) as (command, env, provenance):
            host = reference.JsonProcess(command, env, logs / 'setup', 30, log_protocol=False)
            try:
                reference.require(host.receive()['type'] == 'ready', 'Missing ready response')
                host.send(dict(cmd='oracle_run_start', request_id='boot', setup=dict(character='Ironclad', ascension=0, seed='SETUP')))
                data = host.send({'cmd': 'setup_definitions'})
                reference.require(data['type'] == 'setup_definitions', 'Unexpected definitions')
                reference.atomic_write(definitions, dict(data, reference=provenance))
                cases = []
                for act in data['acts']:
                    for ascension in (0, 1, 10):
                        for seed in (0, 1, 14408614567452956757):
                            response = host.send(dict(cmd='setup_map_audit', act=act['id'], ascension=ascension, numeric_seed=seed))
                            cases.append(dict(act=act['id'], ascension=ascension, numeric_seed=seed, expected=response))
                reference.atomic_write(maps, dict(reference=provenance, cases=cases))
            finally:
                host.close()
    finally:
        reference.inject = original
    print(f'Extracted definitions and {len(cases)} native map cases')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    parser.add_argument('--definitions', type=Path, default=reference.REPO / 'data/builds/v0.111.0/setup.json')
    parser.add_argument('--maps', type=Path, default=reference.REPO / 'tests/fixtures/maps-v1.json')
    args = parser.parse_args()
    check(args.game_dir, args.logs.resolve(), args.definitions.resolve(), args.maps.resolve())
