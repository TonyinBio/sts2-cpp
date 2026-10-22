#!/usr/bin/env python3
"""Record natural seeded map travel through the supported Shrinker encounter."""
import argparse
from pathlib import Path
from play import PlaySession, RecordingWriter, new_actions, resolve
from reference_runtime import REPO, JsonProcess, reference_build, require, wire_json
from replay import load_pair, replay
from upstream_play import RecordingTransport


def check(game_dir, output, validator):
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'
    logs.mkdir()
    with reference_build(game_dir, logs) as (command, env, provenance):
        for scenario in ('run-shrinker-victory', 'run-shrinker-death', 'run-shrinker-interface-skip'):
            directory = output / scenario
            directory.mkdir()
            host = JsonProcess(command, env, logs / scenario, 30, log_protocol=False)
            session = PlaySession(host, new_actions('KERNELRUN14', scenario, run=True), provenance,
                                  validator, RecordingWriter(directory, validator), 1)
            try:
                session.start()
                require(session.state['run_flow']['room']['options'][0]['relic']['id'] == 'RELIC.GOLDEN_PEARL', 'Neow offer changed')
                session.step({'kind': 'choose_option', 'option_index': 0})
                session.step({'kind': 'proceed'})
                node = session.state['run_flow']['room']['choices'][0]
                session.step({'kind': 'select_map_node', 'col': node['col'], 'row': node['row']})
                require(session.state['current_room']['encounter_id'] == 'ENCOUNTER.SHRINKER_BEETLE_WEAK', 'Seeded encounter changed')
                for _ in range(100):
                    combat = session.state['combat']
                    if combat is None or not combat['turn_control']['is_in_progress']:
                        break
                    action = {'kind': 'end_turn'}
                    if not scenario.endswith('death'):
                        player = combat['players'][0]
                        for ref in player['piles']['Hand']:
                            card = resolve(session.state, ref)
                            if card['saved']['id'] in ('CARD.STRIKE_IRONCLAD', 'CARD.BASH') and card['energy_cost']['base'] <= player['energy']:
                                action = {'kind': 'play_card', 'card_instance': ref.rsplit('/', 1)[1], 'target': 'enemy_0'}
                                break
                    session.step(action)
                require(session.state['combat'] is None, 'Combat did not end naturally')
                if not scenario.endswith('death'):
                    require(session.state['run_ending_reason'] is None, 'Combat victory unexpectedly ended run')
                    # Retain the first reward-offer boundary as the next feature's
                    # reference input, without using the transport's auto-collection.
                    before_gold = session.state['players'][0]['gold']
                    session.step({'kind': 'proceed'})
                    flow = session.state['run_flow']
                    require(flow['pending_choice'] is None and len(flow['reward_sets']) == 1,
                            'Reference silently selected rewards instead of offering them')
                    require(session.state['players'][0]['gold'] == before_gold and not session.state['players'][0]['potions'],
                            'Reference collected gold or potions before an explicit action')
                    if scenario.endswith('interface-skip'):
                        deck = session.state['players'][0]['deck']
                        bridge = RecordingTransport(session)
                        bridge._settle_interface()
                        require(session.state['run_flow']['pending_choice']['kind'] == 'card_reward',
                                'Interactive transport did not offer the card reward')
                        display = bridge({'cmd': 'action', 'action': 'skip_card_reward'})
                        require(display['decision'] == 'map_select', 'Skipped card reward was offered again')
                        require(session.state['players'][0]['deck'] == deck, 'Skipping added a reward card')
                        require(not session.state['run_flow']['reward_sets'], 'Skipped reward set remained active')
                        require(any(e['action']['kind'] == 'skip_rewards' for e in session.actions['actions']),
                                'Closing the skipped reward set was not recorded')
                    else:
                        _check_explicit_rewards(session, host)
                else:
                    require(session.state['run_ending_reason'] == 'death', 'Run did not end in death')
                session.publish(final=True)
                (directory / 'reference.json').write_text(wire_json(dict(provenance, **session.identity)) + '\n')
            finally:
                host.close()
            actions, states = load_pair(directory / 'current/actions.json', directory / 'current/states.json', validator)
            host = JsonProcess(command, env, logs / f'{scenario}-replay', 30, log_protocol=False)
            try:
                count, checkpoints = replay(host, actions, states, provenance, validator)
            finally:
                host.close()
            print(f'{scenario}: {count} actions, {checkpoints} full checkpoints; reference self-replay passed')


def _check_explicit_rewards(session, host):
    session.step({'kind': 'take_reward', 'index': 0})
    session.step({'kind': 'take_reward', 'index': 1})
    session.step({'kind': 'take_reward', 'index': 2})
    require(session.state['run_flow']['pending_choice']['can_skip'], 'Native Skip alternative was lost')
    display = host.send({'cmd': 'oracle_display'})
    require(display['decision'] == 'card_reward' and display['can_skip'],
            'Interactive reward display rejected the native Skip alternative')
    after_display = host.send({'cmd': 'oracle_run_state', 'request_id': 'reward-display'})['state']
    require(after_display == session.state, 'Reward display mutated the pending reward')
    session.step({'kind': 'skip_card_reward'})
    session.step({'kind': 'take_reward', 'index': 2})
    session.step({'kind': 'reward_alternative', 'index': 0})
    require(session.state['run_flow']['pending_choice'] is None and len(session.state['run_flow']['reward_sets']) == 1,
            'Skipping the card must leave the reward set available')
    session.step({'kind': 'take_reward', 'index': 2})
    session.step({'kind': 'select_card_reward', 'card_index': 0})
    session.step({'kind': 'discard_potion', 'potion_index': 0})
    session.step({'kind': 'proceed'})
    require(session.state['run_flow']['room']['kind'] == 'map', 'Combat rewards did not return to map')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--validator', type=Path, default=REPO / 'build/debug/parity_validate')
    args = parser.parse_args()
    check(args.game_dir, args.output_dir.resolve(), args.validator.resolve())
