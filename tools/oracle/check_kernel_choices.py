#!/usr/bin/env python3
"""Capture full native states for isolated Survivor continuation regressions."""
import argparse
from pathlib import Path
import shutil
import reference_runtime as reference
from play import DEFAULT_CLOCK


def check(game_dir, validator, logs, output):
    logs.mkdir(parents=True, exist_ok=True)
    original = reference.inject
    def inject(source):
        original(source)
        shutil.copy2(Path(__file__).parent / 'adapters/KernelCombatAudit.cs', source)
        program = source / 'Program.cs'
        program.write_text(program.read_text().replace('case "start_run":',
            'case "kernel_combat_fixture": return sim.AuditKernelCombat(cmd);\ncase "start_run":', 1))
    reference.inject = inject
    scenarios = []
    try:
        with reference.reference_build(game_dir, logs) as (command, env, provenance):
            for count in (1, 2, 3):
                deck = ['SURVIVOR', 'STRIKE_IRONCLAD', 'DEFEND_IRONCLAD'][:count]
                host = reference.JsonProcess(command, env, logs / str(count), 30, log_protocol=False)
                try:
                    reference.require(host.receive()['type'] == 'ready', 'Missing ready response')
                    host.send(dict(cmd='oracle_run_start', request_id='run', setup=dict(
                        character='Ironclad', ascension=0, seed='CHOICE', clock=DEFAULT_CLOCK)))
                    host.send(dict(cmd='set_player', deck=deck))
                    host.send(dict(cmd='enter_room', type='combat', encounter='SHRINKER_BEETLE_WEAK'))
                    initial = host.send(dict(cmd='kernel_combat_fixture', request_id='initial', enemy_hp=39))['state']
                    reference.validate(validator, 'snapshot', initial)
                    survivor = next(key for key, card in initial['combat']['cards'].items() if card['saved']['id'] == 'CARD.SURVIVOR')
                    actions = {'setup': {'kind': 'seeded_run'}}
                    state = host.send(reference.action_request(actions,
                        dict(kind='play_card', card_instance=survivor), initial, 'play'))['state']
                    reference.validate(validator, 'snapshot', state)
                    snapshots = [initial, state]
                    selection = None
                    if count == 3:
                        reference.require(state['pending_choice'] is not None, 'Missing Survivor prompt')
                        selection = state['pending_choice']['candidates'][0].rsplit('/', 1)[1]
                        state = host.send(reference.action_request(actions,
                            dict(kind='select_cards', card_instances=[selection]), state, 'select'))['state']
                        reference.validate(validator, 'snapshot', state)
                        snapshots.append(state)
                    else:
                        reference.require(state['pending_choice'] is None, 'Native game auto-selects at most one candidate')
                    scenarios.append(dict(seed='CHOICE', deck=deck, enemy_hp=39,
                                          survivor=survivor, selection=selection, states=snapshots))
                finally:
                    host.close()
            reference.atomic_write(output, dict(reference=provenance, scenarios=scenarios))
    finally:
        reference.inject = original
    print(f'Captured native Survivor cases (zero, one, two candidates): {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, default=reference.REPO / 'build/debug/parity_validate')
    parser.add_argument('--logs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    check(args.game_dir, args.validator.resolve(), args.logs.resolve(), args.output.resolve())
