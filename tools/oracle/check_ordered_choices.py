#!/usr/bin/env python3
"""Compare explicit public card inputs with independently replayed native models.

This is completed atomic model evidence. It does not claim native UI callbacks.
The zero-step clock keeps full native timestamps identical while one historical
selection submission maps to several explicit public inputs. No fields are omitted.
"""
import argparse
from contextlib import contextmanager
import hashlib
import shutil
from pathlib import Path
import reference_runtime as reference
from check_consecutive_combats import compare
from check_ordered_powers import native_start, native_step
from play import new_actions, resolve
from reference_runtime import JsonProcess, require, wire_json


def cases():
    for card in ('PURITY', 'ARMAMENTS', 'HEADBUTT', 'BURNING_PACT', 'TRUE_GRIT'):
        for upgraded in (False, True):
            yield card.lower() + ('-plus' if upgraded else '') + '-empty', [(card, upgraded)], [('play', card)]
    yield 'purity-ordered', [('PURITY', False)] + [('DEFEND_IRONCLAD', False)] * 4, [('play', 'PURITY'), ('select', [2, 0, 1])]
    yield 'purity-zero', [('PURITY', True), ('STRIKE_IRONCLAD', False)], [('play', 'PURITY'), ('select', [])]
    yield 'armaments-upgrade', [('ARMAMENTS', False), ('STRIKE_IRONCLAD', False), ('DEFEND_IRONCLAD', False)], [('play', 'ARMAMENTS'), ('select', [1])]
    yield 'armaments-all', [('ARMAMENTS', True), ('STRIKE_IRONCLAD', False), ('DEFEND_IRONCLAD', False)], [('play', 'ARMAMENTS')]
    yield 'armaments-ineligible', [('ARMAMENTS', False), ('STRIKE_IRONCLAD', True), ('DEFEND_IRONCLAD', True)], [('play', 'ARMAMENTS')]
    yield 'armaments-one', [('ARMAMENTS', False), ('STRIKE_IRONCLAD', False)], [('play', 'ARMAMENTS')]
    yield 'headbutt-pile', [('HEADBUTT', False)] + [('DEFEND_IRONCLAD', False)] * 2, [('play', 'DEFEND_IRONCLAD'), ('play', 'DEFEND_IRONCLAD'), ('play', 'HEADBUTT'), ('select', [1])]
    yield 'headbutt-forced', [('HEADBUTT', True), ('DEFEND_IRONCLAD', False)], [('play', 'DEFEND_IRONCLAD'), ('play', 'HEADBUTT')]
    for upgraded in (False, True):
        yield 'burning-pact-forced' + ('-plus' if upgraded else ''), [('BURNING_PACT', upgraded), ('STRIKE_IRONCLAD', False)], [('play', 'BURNING_PACT')]
        yield 'true-grit-forced' + ('-plus' if upgraded else ''), [('TRUE_GRIT', upgraded), ('STRIKE_IRONCLAD', False)], [('play', 'TRUE_GRIT')]
        yield 'burning-pact' + ('-plus' if upgraded else ''), [('BURNING_PACT', upgraded)] * 8, [('play', 'BURNING_PACT'), ('select', [2])]
        yield 'true-grit' + ('-plus' if upgraded else ''), [('TRUE_GRIT', upgraded), ('STRIKE_IRONCLAD', False), ('DEFEND_IRONCLAD', False)], [('play', 'TRUE_GRIT')] + ([('select', [1])] if upgraded else [])


def capture(command, env, logs, setup, deck, script, name):
    host = JsonProcess(command, env, logs, 60, log_protocol=False)
    operations, states = [], []
    try:
        initial = native_start(host, setup, deck)
        states.append(initial['state'])
        for kind, payload in script:
            if kind == 'select':
                action = {'kind': 'select_cards', 'indices': payload}
            else:
                state = states[-1]
                hand = state['combat']['players'][0]['piles']['Hand']
                index = next(i for i, ref in enumerate(hand) if resolve(state, ref)['saved']['id'] == 'CARD.' + payload)
                action = {'kind': 'play_card', 'card_index': index}
                if payload in ('HEADBUTT', 'STRIKE_IRONCLAD'):
                    action['target_index'] = 0
            operation = {'cmd': 'action', 'action': action}
            states.append(native_step(host, operation))
            operations.append(operation)
        return initial, operations, states
    finally:
        host.close()


@contextmanager
def selection_build(game_dir, logs):
    original = reference.inject
    def inject(source):
        original(source)
        shutil.copy2(Path(__file__).parent / 'adapters/SelectionDefinitionsAudit.cs', source)
        program = source / 'Program.cs'
        program.write_text(program.read_text().replace('case "start_run":',
            'case "selection_definitions": return sim.AuditSelectionDefinitions(cmd);\ncase "start_run":', 1))
    reference.inject = inject
    try:
        with reference.reference_build(game_dir, logs) as built:
            yield built
    finally:
        reference.inject = original


def check(game_dir, output, simulator, validator, only=None, definitions_output=None, fixture_output=None):
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'; logs.mkdir()
    setup = new_actions('CHOICE18', 'ordered-choices', run=False)['setup']
    setup['clock']['seconds_per_action'] = 0
    with selection_build(game_dir, logs) as (command, env, provenance):
        if definitions_output:
            host = JsonProcess(command, env, logs / 'definitions', 60, log_protocol=False)
            try:
                identity = native_start(host, setup, [{'id': 'CARD.PURITY', 'upgraded': False}])
                result = host.send({'cmd': 'selection_definitions'})
                require('cards' in result, f'Native definitions failed: {result}')
                definitions_output.write_text(wire_json({'reference': dict(provenance, **{key: identity[key] for key in ('assembly_mvid', 'runtime_version')}), 'cards': result['cards']}) + '\n')
            finally:
                host.close()
        comparator = JsonProcess([str(validator)], None, logs / 'comparator', 60, log_protocol=False)
        try:
            for name, cards, script in cases():
                if only and name not in only:
                    continue
                destination = output / name; destination.mkdir()
                deck = [{'id': 'CARD.' + card, 'upgraded': upgraded} for card, upgraded in cards]
                initial, operations, states = capture(command, env, logs / (name + '-capture'), setup, deck, script, name)
                _, repeated, replayed = capture(command, env, logs / (name + '-self-replay'), setup, deck, script, name)
                require(repeated == operations, 'Independent action construction differs')
                for index, (expected, actual) in enumerate(zip(states, replayed, strict=True)):
                    compare(comparator, expected, actual, f'{name} native replay {index}')
                scenario = {'profile': 'ordered-choices-atomic-model-v1', 'setup': setup, 'scenario_deck': deck, 'operations': operations}
                identity = {key: initial[key] for key in ('assembly_mvid', 'runtime_version')}
                for filename, value in (('scenario.json', scenario), ('states.json', states), ('reference.json', dict(provenance, **identity))):
                    (destination / filename).write_text(wire_json(value) + '\n')
                cpp = JsonProcess([str(simulator)], None, logs / (name + '-cpp'), 60, log_protocol=False)
                public_operations, correspondence = [], []
                try:
                    reply = cpp.send({'cmd': 'start', 'setup': setup, 'scenario_deck': deck})
                    require(reply.get('ok'), f'{name} C++ initial: {reply}')
                    compare(comparator, states[0], reply['state'], f'{name} C++ initial')
                    for index, operation in enumerate(operations):
                        action = operation['action']
                        start = len(public_operations)
                        if action['kind'] == 'select_cards':
                            inputs = [{'cmd': 'action', 'action': {'kind': 'select_card', 'candidate_index': candidate}} for candidate in action['indices']]
                            if not reply['selection']['automatic_at_maximum']:
                                inputs.append({'cmd': 'action', 'action': {'kind': 'confirm_selection'}})
                        else:
                            inputs = [operation]
                        for public in inputs:
                            reply = cpp.send(public); public_operations.append(public)
                            require(reply.get('ok'), f'{name} C++ partial: {reply}')
                        (destination / 'cpp-last.json').write_text(wire_json(reply['state']) + '\n')
                        compare(comparator, states[index + 1], reply['state'], f'{name} C++ atomic boundary {index + 1}')
                        correspondence.append({'native_action': index, 'public_action_begin': start, 'public_action_end': len(public_operations)})
                    (destination / 'cpp-final.json').write_text(wire_json(reply['state']) + '\n')
                finally:
                    cpp.close()
                (destination / 'public-actions.json').write_text(wire_json({'operations': public_operations, 'correspondence': correspondence}) + '\n')
                hashes = {file: hashlib.sha256((destination / file).read_bytes()).hexdigest() for file in ('scenario.json', 'states.json', 'reference.json', 'public-actions.json')}
                (destination / 'verification.json').write_text(wire_json({'native_model_self_replay': 'passed', 'cpp_exact_atomic_comparison': 'passed', 'native_partial_callbacks': 'not_exercised', 'boundaries': len(states), 'sha256': hashes}) + '\n')
                print(f'{name}: {len(states)} native model boundaries, exact C++ partial-input result', flush=True)
        finally:
            comparator.close()
    if fixture_output:
        captured = []
        for path in sorted(output.glob('*/verification.json')):
            root = path.parent
            captured.append({'name': root.name, **{key: reference.strict_json((root / filename).read_bytes()) for key, filename in (
                ('scenario', 'scenario.json'), ('states', 'states.json'), ('public_actions', 'public-actions.json'),
                ('reference', 'reference.json'), ('verification', 'verification.json'))}})
        require(captured, 'No verified scenarios to bundle')
        fixture_output.write_text(wire_json({'profile': 'ordered-choices-atomic-model-v1', 'cases': captured}) + '\n')


def replay_fixture(fixture, output, simulator, validator):
    output.mkdir(parents=True, exist_ok=False)
    bundle = reference.strict_json(fixture.read_bytes())
    require(bundle['profile'] == 'ordered-choices-atomic-model-v1', 'Unknown choice fixture profile')
    comparator = JsonProcess([str(validator)], None, output / 'comparator', 60, log_protocol=False)
    try:
        for case in bundle['cases']:
            name = case['name']; scenario = case['scenario']; states = case['states']
            cpp = JsonProcess([str(simulator)], None, output / name, 60, log_protocol=False)
            try:
                reply = cpp.send({'cmd': 'start', 'setup': scenario['setup'], 'scenario_deck': scenario['scenario_deck']})
                require(reply.get('ok'), f'{name} initial: {reply}')
                compare(comparator, states[0], reply['state'], f'{name} initial')
                boundaries = {point['public_action_end']: point['native_action'] + 1 for point in case['public_actions']['correspondence']}
                for index, operation in enumerate(case['public_actions']['operations'], 1):
                    reply = cpp.send(operation)
                    require(reply.get('ok'), f'{name} public input {index}: {reply}')
                    if index in boundaries:
                        compare(comparator, states[boundaries[index]], reply['state'], f'{name} public boundary {index}')
                require(len(boundaries) == len(states) - 1, f'{name} incomplete boundary correspondence')
            finally:
                cpp.close()
            print(f'{name}: exact offline public-input replay', flush=True)
    finally:
        comparator.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--fixture-output', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--simulator', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--only', action='append')
    parser.add_argument('--definitions-output', type=Path)
    args = parser.parse_args()
    if args.fixture:
        replay_fixture(args.fixture, args.output_dir, args.simulator.resolve(), args.validator.resolve())
    else:
        require(args.game_dir is not None, '--game-dir required for native capture')
        check(args.game_dir, args.output_dir, args.simulator.resolve(), args.validator.resolve(), args.only, args.definitions_output, args.fixture_output)
