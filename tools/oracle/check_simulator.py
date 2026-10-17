#!/usr/bin/env python3
"""Generate legal combat action traces from sts2-cli and check every C++ transition."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys

from record import (FEATURES, REPO, JsonProcess, atomic_write, reference_build,
                    require, validate)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--episodes', type=int, default=32)
    parser.add_argument('--policy-seed', type=int, default=0)
    parser.add_argument('--output-dir', type=Path, default=REPO / 'build/parity-campaign')
    parser.add_argument('--replay', type=Path, default=REPO / 'build/debug/parity_replay')
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    args = parser.parse_args()
    require(args.episodes > 0, 'Episodes must be positive')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    policy = random.Random(args.policy_seed)
    total = 0
    outcomes = {'victory': 0, 'defeat': 0}
    with reference_build(args.game_dir, output) as (command, env, provenance):
        for episode in range(args.episodes):
            name = f'episode-{episode:04}'
            actions = {'schema_version': 1, 'profile': 'actions-v1', 'recording_id': name,
                'state_profile': 'combat-v1', 'required_features': FEATURES,
                'setup': {'kind': 'seeded_combat', 'seed': f'CPP-PARITY-{args.policy_seed}-{episode}',
                          'character': 'Ironclad', 'ascension': 0, 'encounter': 'SHRINKER_BEETLE_WEAK'},
                'actions': []}
            host = JsonProcess(command, env, output / name, 30)
            try:
                require(host.receive().get('type') == 'ready', 'Reference not ready')
                response = host.send({'cmd': 'oracle_start', 'request_id': 'initial',
                                      'setup': actions['setup'], 'required_features': FEATURES})
                require(response.get('type') == 'oracle_state' and response.get('request_id') == 'initial',
                        'Invalid initial response')
                initial = response['state']
                identity = {key: response[key] for key in ('assembly_mvid', 'runtime_version')}
                state = initial
                transitions = []
                for index in range(128):
                    if state['boundary'] == 'combat_end':
                        break
                    cards = {card['instance_id']: card for card in state['cards']}
                    legal = [{'kind': 'end_turn'}]
                    for card_id in state['piles']['hand']:
                        card = cards[card_id]
                        if card['cost']['amount'] <= state['player']['energy']:
                            action = {'kind': 'play_card', 'card_instance': card_id}
                            if card['model_id'] != 'CARD.DEFEND_IRONCLAD':
                                action['target'] = 'enemy_0'
                            legal.append(action)
                    # Include end-turn-only defeat episodes and attack-priority victory
                    # episodes, alongside random policies that exercise unusual orderings.
                    if episode % 8 == 0:
                        chosen = legal[0]
                    elif episode % 8 == 1:
                        attacks = [a for a in legal[1:] if 'target' in a]
                        chosen = attacks[0] if attacks else legal[0]
                    else:
                        chosen = policy.choice(legal)
                    action_id = f'action-{index:03}'
                    actions['actions'].append({'id': action_id, 'action': chosen})
                    response = host.send({'cmd': 'oracle_action', 'request_id': action_id, 'action': chosen})
                    require(response.get('type') == 'oracle_state' and response.get('request_id') == action_id,
                            f'{name}: invalid response at {action_id}')
                    require(all(response.get(k) == v for k, v in identity.items()), 'Runtime identity changed')
                    state = response['state']
                    transitions.append({'action_id': action_id, 'state': state})
                require(state['boundary'] == 'combat_end', f'{name}: policy did not terminate')
                require(host.send({'cmd': 'quit'}).get('type') == 'quit_result', 'Reference shutdown failed')
                host.process.wait(timeout=30)
                require(host.process.returncode == 0, 'Reference exited unsuccessfully')
            finally:
                host.close()
            raw = json.dumps(actions, indent=2) + '\n'
            result = {'schema_version': 1, 'profile': 'states-v1', 'recording_id': name,
                'actions_sha256': hashlib.sha256(raw.encode()).hexdigest(),
                'reference': dict(provenance, **identity), 'initial': initial, 'transitions': transitions}
            # Preserve each complete reference case before invoking C++, so failures are replayable.
            validate(args.validator.resolve(), 'states', result, actions)
            action_path = output / f'{name}.actions.json'
            action_path.write_text(raw)
            state_path = output / f'{name}.states.json'
            atomic_write(state_path, result)
            checked = subprocess.run([str(args.replay.resolve()), str(action_path), str(state_path)],
                                     text=True, capture_output=True, timeout=60)
            require(checked.returncode == 0, f'{name}: {checked.stderr.strip()} (saved in {output})')
            total += len(transitions)
            outcomes[state['combat']['outcome']] += 1
            print(f'{name}: {len(transitions)} transitions match ({state["combat"]["outcome"]})', flush=True)
    print(f'PASS: {args.episodes} episodes, {total} transitions, outcomes={outcomes}')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
        print(f'Parity campaign failed: {error}', file=sys.stderr)
        sys.exit(1)
