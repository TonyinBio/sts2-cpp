#!/usr/bin/env python3
"""Capture independent pinned-game RNG vectors for the C++ kernel regression."""
import argparse
from pathlib import Path
import shutil
import reference_runtime as reference


def check(game_dir, logs, output):
    logs.mkdir(parents=True, exist_ok=True)
    original = reference.inject
    def inject(source):
        original(source)
        shutil.copy2(Path(__file__).parent / 'adapters/KernelRngAudit.cs', source)
        program = source / 'Program.cs'
        program.write_text(program.read_text().replace('case "start_run":',
            'case "kernel_rng_audit": return sim.AuditKernelRng(cmd);\ncase "start_run":', 1))
    reference.inject = inject
    try:
        with reference.reference_build(game_dir, logs) as (command, env, provenance):
            host = reference.JsonProcess(command, env, logs / 'rng', 30, log_protocol=False)
            try:
                reference.require(host.receive()['type'] == 'ready', 'Missing ready response')
                response = host.send({'cmd': 'kernel_rng_audit'})
                reference.require(response['type'] == 'kernel_rng_audit', 'Invalid audit response')
                reference.atomic_write(output, dict(response, reference=provenance))
            finally:
                host.close()
    finally:
        reference.inject = original
    print(f'Captured game RNG/hash/sort vectors: {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    check(args.game_dir, args.logs.resolve(), args.output.resolve())
