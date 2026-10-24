#!/usr/bin/env python3
"""Rebuild a planning inventory from preserved audits; never run game transitions."""
import collections
import gzip
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
PLAN = ROOT / 'docs/planning'
FIXTURE = ROOT / 'tests/parity/fixtures/reference/run-victory-1234'
INPUTS = [
    PLAN / 'victory-mechanics/scope-roots.json',
    PLAN / 'victory-mechanics/inventory.json',
    *[PLAN / 'victory-semantics' / (name + '.json') for name in ['cards', 'inventory', 'encounters']],
    *[PLAN / 'public-visibility-audit' / (name + '.md') for name in ['combat', 'prompts', 'invariants', 'adapter']],
    *[PLAN / 'instance-lifetimes' / (name + '.json') for name in ['cards-inventory', 'entities', 'history-dispatch', 'rooms-continuations']],
    FIXTURE / 'dense/states.json.gz', FIXTURE / 'dense/actions.json.gz',
    FIXTURE / 'verification.json',
]

def load(path):
    data = path.read_bytes()
    return json.loads(gzip.decompress(data) if path.suffix == '.gz' else data)

def write(name, value):
    (OUT / name).write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')

def rel(path):
    return str(path.relative_to(ROOT))

def leaf_shapes(value, path='', result=None):
    result = result if result is not None else set()
    if isinstance(value, dict):
        if not value:
            result.add((path, 'empty_object'))
        for key, child in value.items():
            leaf_shapes(child, path + '/' + key.replace('~', '~0').replace('/', '~1'), result)
    elif isinstance(value, list):
        if not value:
            result.add((path, 'empty_array'))
        for child in value:
            leaf_shapes(child, path + '/*', result)
    else:
        kind = 'null' if value is None else 'boolean' if isinstance(value, bool) else 'number' if isinstance(value, (int, float)) else 'string'
        result.add((path, kind))
    return result

def main():
    roots_by_category = load(INPUTS[0])
    roots = {model: category for category, models in roots_by_category.items() for model in models}
    observed = load(INPUTS[1])
    first = collections.defaultdict(list)
    for group, entries in observed['first_evidence'].items():
        for model, evidence in entries.items():
            first[model].append({'kind': group, **evidence})
    audits = [load(PLAN / 'victory-semantics' / (name + '.json')) for name in ['cards', 'inventory', 'encounters']]
    source_records = []
    for name, audit in zip(['cards', 'inventory', 'encounters'], audits):
        for key in ['records', 'models', 'required_nonroot_power_records', 'support_records']:
            for index, record in enumerate(audit.get(key, [])):
                source_records.append((name, key, index, record))
    models = []
    for model, category in sorted(roots.items()):
        matches = [(name, key, index, record) for name, key, index, record in source_records if record.get('id', record.get('model_id')) == model]
        assert len(matches) == 1, (model, len(matches))
        name, key, index, record = matches[0]
        requirements = {k: v for k, v in record.items() if k in {
            'legal_branches', 'upgrade', 'regression_scenarios', 'regressions',
            'regression_requirements', 'state_branch_citations', 'hp_and_parameter_citations',
            'rng_citations', 'fixed_model_dependencies', 'fixed_semantic_dependencies',
            'required_dependency_types', 'random_outcomes', 'random_output_boundary',
            'effect_order', 'semantics', 'semantic_summary', 'mutable_state',
            'declared_mutable_storage', 'variant_members', 'constructor_parameters',
        }}
        models.append({
            'case_family': 'MODEL-' + model, 'model_id': model, 'category': category,
            'audit_pointer': f'docs/planning/victory-semantics/{name}.json#/{key}/{index}',
            'native_source': record.get('source'),
            'native_source_sha256': record.get('source_sha256', record.get('sha256')),
            'route_evidence': first[model], 'requirements': requirements,
            'branch_case_rule': 'Split every cited reachable predicate into true/false cases and every authored option/move/parameter into a case; preserve base/upgrade, zero/one/many candidates, thresholds and reset/retention cases where applicable. Establish reachable single-player preconditions before capture.',
            'required_evidence': ['native alternate-outcome state/RNG comparison', 'ordering-sensitive shared-case comparison', 'C++ legal/invalid input and restore/clone regression'],
            'implementation': 'not_assessed_here',
            'reference_verification': 'route_presence_only; alternate cases pending',
            'interaction_verification': 'pending; no passing claim',
        })
    assert len(models) == 224
    supporting = []
    for name, key, index, record in source_records:
        model = record.get('id', record.get('model_id', record.get('class_name')))
        if model not in roots:
            supporting.append({'audit_pointer': f'docs/planning/victory-semantics/{name}.json#/{key}/{index}', 'record': record, 'status': 'source_specified; runtime and C++ comparison pending'})
    write('models.json', {'evidence_kind': 'Source-backed planning obligations, not executed cases or exhaustive runtime coverage', 'root_count': len(models), 'category_counts': {k: len(v) for k, v in roots_by_category.items()}, 'models': models, 'supporting_records': supporting, 'extra_semantic_system_edges': audits[0]['extra_semantic_system_edges']})

    states_doc = load(FIXTURE / 'dense/states.json.gz')
    actions = load(FIXTURE / 'dense/actions.json.gz')['actions']
    states = [('initial', states_doc['initial'])] + [(t['action_id'], t['state']) for t in states_doc['transitions']]
    assert len(states) == 697 and len(actions) == 696
    assert [a['id'] for a in actions] == [boundary for boundary, _ in states[1:]]
    shapes = set()
    rooms = collections.Counter()
    for _, state in states:
        leaf_shapes(state, result=shapes)
        rooms[state['current_room']['room_type']] += 1
    write('fields.json', {
        'surface': 'Privileged dense export; shape inventory only, not a complete-state or public-field certification',
        'comparison': 'Retain historical exact comparison unchanged for every exported field on the 697-boundary route. No wildcard-ignore or normalization is approved by this audit.',
        'graphical_candidate': {'pointer': '/map_drawings', 'value_counts': dict(collections.Counter(json.dumps(s['map_drawings']) for _, s in states)), 'policy': 'Pure annotation candidate, null in every saved state; no non-null rendering evidence and no comparator change. Native icons conveying gameplay remain structured semantic requirements.'},
        'boundary_count': len(states), 'room_boundary_counts': dict(rooms),
        'top_level_fields': sorted({k for _, s in states for k in s}),
        'leaf_shapes': [{'path_pattern': p, 'kind': k} for p, k in sorted(shapes)],
        'limits': 'Array wildcard paths are inventory notation only. Strings, number representations, array order, null versus empty and identity references still require exact comparator semantics. This scanner is not the comparator.',
    })
    examples = []
    for index in [18, 294, 384, 385, 611, 612, 646, 647, 650, 696]:
        boundary, state = states[index]
        assert boundary == f'action-{index:06d}'
        examples.append({'boundary': boundary, 'action': actions[index - 1]['action'], 'room': state['current_room'], 'boundary_kind': state['boundary_kind'], 'pending_choice_present': state.get('pending_choice') is not None, 'run_ending_reason': state.get('run_ending_reason')})
    write('route-examples.json', {'examples': examples, 'interpretation': 'Stored anchors to inspect; existence proves neither an alternate branch nor an independent public golden.'})

    public = []
    for filename, prefix in [('combat.md', '| combat-'), ('prompts.md', '| P'), ('invariants.md', '| NI-')]:
        path = PLAN / 'public-visibility-audit' / filename
        for line, text in enumerate(path.read_text().splitlines(), 1):
            if not text.startswith(prefix):
                continue
            cells = [c.strip() for c in text.strip('|').split('|')]
            if filename == 'prompts.md' and not (len(cells[0]) > 2 and cells[0][1:3].isdigit()):
                continue
            public.append({'case': cells[0], 'source': rel(path), 'line': line, 'requirements': cells[1:], 'native_capture': 'pending', 'C++_comparison': 'pending', 'scope_rule': 'Only reachable observed-plus-fixed single-player mechanics gate Ironclad; examples outside that closure are deferred. P15 graphical editor/modifier controls do not become an in-run milestone requirement. Bundle/relic/dynamic-pile paths require a supported native caller. Droplet and Ambergris examples do not automatically add those model effects to scope.'})
    assert len(public) == 34, len(public)
    write('public-cases.json', {'case_count': len(public), 'cases': public, 'cross_cutting': ['independent native public path, not filtered snapshot', 'stable identity correspondence, not numeric native/C++ ID equality', 'missing/hidden/empty/zero distinctions', 'read/inspect/advance purity at every supported stop', 'meaningful semantic ordering and reveal before/during/after', 'partial selection toggles are inputs, not pure reads']})
    write('inputs.json', {'inputs': [{'path': rel(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in INPUTS], 'counts': {'roots': len(models), 'supporting_records': len(supporting), 'public_case_families': len(public), 'leaf_shapes': len(shapes), 'boundaries': len(states)}, 'limits': 'Integrity and linkage, not proof of semantic completeness, live native feasibility or simulator parity.'})
    lines = ['# Per-model evidence index', '', 'Generated from the preserved semantic audits. Every row requires native alternate-branch evidence and C++ regressions; route presence is not branch certification.', '', '| Model | Category | Source-backed requirements | Route evidence |', '| --- | --- | --- | --- |']
    for row in models:
        pointer = row['audit_pointer'].replace('docs/planning/victory-semantics/', '../victory-semantics/')
        evidence = ', '.join(sorted({item['boundary'] for item in row['route_evidence']})) or 'See scope inventory; no first-evidence entry'
        lines.append(f"| {row['model_id']} | {row['category']} | [audit record]({pointer}) | {evidence} |")
    (OUT / 'models.md').write_text('\n'.join(lines) + '\n')
    print(json.dumps({'roots': len(models), 'supporting_records': len(supporting), 'public_cases': len(public), 'leaf_shapes': len(shapes), 'boundaries': len(states)}))

if __name__ == '__main__':
    main()
