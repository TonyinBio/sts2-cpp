"""Extract lifetime examples from stored evidence; no simulation or parity proof."""
import gzip
import hashlib
import json
from pathlib import Path

here = Path(__file__).resolve().parent
repo = here.parents[2]
fixture = repo / 'tests/parity/fixtures/reference/run-victory-1234/dense'
actions_raw = gzip.decompress((fixture / 'actions.json.gz').read_bytes())
states_raw = gzip.decompress((fixture / 'states.json.gz').read_bytes())
actions = json.loads(actions_raw)
states = json.loads(states_raw)
assert hashlib.sha256(actions_raw).hexdigest() == states['actions_sha256']
transitions = states['transitions']
assert len(transitions) == len(actions['actions']) == 696
samples = []
for index, transition in enumerate(transitions):
    assert transition['action_id'] == actions['actions'][index]['id']
    state = transition['state']
    choice = state.get('pending_choice') or {}
    inventory = (state.get('combat') or {}).get('retained_inventory_models', {})
    for pointer in choice.get('sources', []):
        prefix = '/combat/retained_inventory_models/'
        if not pointer.startswith(prefix):
            continue
        model = inventory[pointer[len(prefix):]]
        next_transition = transitions[index + 1] if index + 1 < len(transitions) else None
        next_inventory = ((next_transition['state'].get('combat') or {}).get('retained_inventory_models', {})
                          if next_transition else {})
        samples.append({
            'after_action': transition['action_id'],
            'action': actions['actions'][index]['action'],
            'source_pointer': pointer,
            'model_id': model['saved']['id'],
            'source_slot_index': model['saved']['slot_index'],
            'removed_from_state': model['has_been_removed_from_state'],
            'offered_candidates': choice.get('candidates'),
            'next_action': actions['actions'][index + 1] if next_transition else None,
            'pointer_present_in_next_export_inventory': pointer[len(prefix):] in next_inventory,
            'interpretation_limit': 'Absence in later export is not native object destruction or permission to retire storage.',
        })
report = {
    'evidence_kind': 'stored recording extraction, not a new oracle experiment',
    'actions_sha256': hashlib.sha256(actions_raw).hexdigest(),
    'states_decompressed_sha256': hashlib.sha256(states_raw).hexdigest(),
    'boundaries_scanned': len(transitions) + 1,
    'retained_choice_inventory_sources': samples,
    'limits': 'Ordinary JSON is used for inventory extraction only; this is not the exact state comparator. Export labels are local to adapter scope and do not identify native global object lifetimes.',
}
(here / 'recording-examples.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'Scanned {len(transitions)+1} stored boundaries; extracted {len(samples)} retained choice-source examples')
