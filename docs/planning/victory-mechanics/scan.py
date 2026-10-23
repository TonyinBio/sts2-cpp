import argparse,json,gzip,collections,hashlib
from pathlib import Path
parser=argparse.ArgumentParser(description='Scan observed victory actions and states; no semantic closure is inferred.')
parser.add_argument('--input-dir',type=Path,required=True)
parser.add_argument('--output-dir',type=Path,required=True)
args=parser.parse_args()
base=args.input_dir
a=json.loads(gzip.decompress((base/'actions.json.gz').read_bytes()));doc=json.loads(gzip.decompress((base/'states.json.gz').read_bytes()));states=[doc['initial']]+[x['state'] for x in doc['transitions']]
assert len(states)==697 and len(a['actions'])==696
out=args.output_dir
out.mkdir(parents=True,exist_ok=False)
def desc(c):
 if not c:return None
 c=c.get('saved',c)
 return {k:c.get(k) for k in ('id','current_upgrade_level','enchantment','floor_added_to_deck','props') if k in c}
def key(c):return json.dumps(desc(c),sort_keys=True)
def ids(obj,prefix):
 ret=[]
 if isinstance(obj,dict):
  for k,v in obj.items():
   if isinstance(v,str) and v.startswith(prefix):ret.append(v)
   else:ret.extend(ids(v,prefix))
 elif isinstance(obj,list):
  for v in obj:ret.extend(ids(v,prefix))
 return ret
sets=collections.defaultdict(set);pointers=collections.defaultdict(dict);variants=collections.defaultdict(dict);action_rows=[];played=collections.Counter();played_variants=collections.Counter();errors=[];choices=[];sessions=[];prevroom=None
for n,st in enumerate(states):
 boundary='initial' if n==0 else a['actions'][n-1]['id'];combat=st['combat'];flow=st['run_flow'] or {};room=st['current_room'] or {}
 def add(category,items,path):
  for model in items:
   sets[category].add(model);pointers[category].setdefault(model,{'boundary':boundary,'pointer':path})
 for pi,p in enumerate(st['players']):
  for cat,field,prefix in [('owned_cards','deck','CARD.'),('owned_relics','relics','RELIC.'),('owned_potions','potions','POTION.')]:
   add(cat,ids(p[field],prefix),f'/players/{pi}/{field}')
  for c in p['deck']:variants['owned_card_variants'].setdefault(key(c),{'card':desc(c),'boundary':boundary})
  add('owned_enchantments',ids(p['deck'],'ENCHANTMENT.'),f'/players/{pi}/deck')
 if combat:
  for cat,field,prefix in [('combat_cards','cards','CARD.'),('powers','powers','POWER.'),('monsters','creatures','MONSTER.'),('orbs','orbs','ORB.'),('afflictions','afflictions','AFFLICTION.')]:add(cat,ids(combat[field],prefix),f'/combat/{field}')
  add('combat_enchantments',ids(combat['cards'],'ENCHANTMENT.'),'/combat/cards')
  for c in combat['cards'].values():variants['combat_card_variants'].setdefault(key(c),{'card':desc(c),'boundary':boundary})
 for cat,prefix in [('offered_cards','CARD.'),('offered_relics','RELIC.'),('offered_potions','POTION.'),('offered_enchantments','ENCHANTMENT.')]:add(cat,ids(flow,prefix),'/run_flow')
 if room.get('event_id'):add('entered_events',[room['event_id']],'/current_room/event_id')
 if room.get('encounter_id'):add('entered_encounters',[room['encounter_id']],'/current_room/encounter_id')
 rk=(st['current_act_index'],room.get('room_type'),room.get('event_id'),room.get('encounter_id'),json.dumps(st['visited_map_coords'],sort_keys=True))
 if rk!=prevroom:
  sessions.append({'boundary':boundary,'act':st['current_act_index'],'room':room,'visited_map_coords':st['visited_map_coords']});prevroom=rk
for i,e in enumerate(a['actions']):
 st,post=states[i],states[i+1];act=e['action'];kind=act['kind'];combat=st['combat'];flow=st['run_flow'] or {};room=flow.get('room') or {};pending=flow.get('pending_choice');row={'action_id':e['id'],'action':act,'pre_boundary':'initial' if i==0 else a['actions'][i-1]['id'],'act':st['current_act_index'],'room':st['current_room']}
 try:
  if kind=='play_card':
   label=act['card_instance'];c=combat['cards'][label];assert '/combat/cards/'+label in combat['players'][0]['piles']['Hand'];d=desc(c);row['played_card']=d;row['resolution_pointer']='/combat/cards/'+label;played[d['id']]+=1;played_variants[key(c)]+=1
  elif kind=='use_potion':row['used_potion']=next(p for p in st['players'][0]['potions'] if p['slot_index']==act['potion_index']);row['resolution_pointer']='/players/0/potions (slot_index matches action potion_index)'
  elif kind=='choose_option':row['chosen_option']=room['options'][act['option_index']];row['resolution_pointer']=f"/run_flow/room/options/{act['option_index']}"
  elif kind=='select_card_reward':row['selected_card_reward']=desc(pending['cards'][act['card_index']]);row['resolution_pointer']=f"/run_flow/pending_choice/cards/{act['card_index']}"
  elif kind in ('buy_card','buy_relic','buy_potion'):
   field={'buy_card':'cards','buy_relic':'relics','buy_potion':'potions'}[kind];idx=act[{'buy_card':'card_index','buy_relic':'relic_index','buy_potion':'potion_index'}[kind]];row['targeted_shop_item']=room[field][idx];row['resolution_pointer']=f'/run_flow/room/{field}/{idx}'
  elif kind=='pick_treasure':row['treasure']=room['relics'][act['index']]
  elif kind=='select_cards':
   if 'card_instances' in act:
    row['selected_cards']=[desc(combat['cards'][label]) for label in act['card_instances']];row['pending_sources']=[{'reference':ref,'model':desc(combat['cards'][ref.rsplit('/',1)[1]]) if ref.startswith('/combat/cards/') else desc(combat['retained_inventory_models'][ref.rsplit('/',1)[1]]) if ref.startswith('/combat/retained_inventory_models/') else None} for ref in st['pending_choice']['sources']];row['pending_constraints']=st['pending_choice']
   else:
    row['selected_cards']=[desc(pending['cards'][ix]) for ix in act['indices']];row['pending_constraints']={k:v for k,v in pending.items() if k!='cards'};row['flow_operations']=flow.get('operations')
  elif kind=='take_reward':row['reward_sets_before']=flow['reward_sets']
 except Exception as ex:errors.append({'action_id':e['id'],'error':repr(ex)})
 before=collections.Counter(key(c) for c in st['players'][0]['deck']);after=collections.Counter(key(c) for c in post['players'][0]['deck'])
 if before!=after:row['deck_descriptor_delta']={'added':[json.loads(k) for k,count in (after-before).items() for _ in range(count)],'removed':[json.loads(k) for k,count in (before-after).items() for _ in range(count)]}
 for field in ('relics','potions'):
  before=collections.Counter(json.dumps(x,sort_keys=True) for x in st['players'][0][field]);after=collections.Counter(json.dumps(x,sort_keys=True) for x in post['players'][0][field])
  if before!=after:row[field+'_descriptor_delta']={'added':[json.loads(k) for k,count in (after-before).items() for _ in range(count)],'removed':[json.loads(k) for k,count in (before-after).items() for _ in range(count)]}
 if kind.startswith('buy_'):
  row['purchase_observation']={'gold_before':st['players'][0]['gold'],'gold_after':post['players'][0]['gold'],'deck_changed':st['players'][0]['deck']!=post['players'][0]['deck'],'relics_changed':st['players'][0]['relics']!=post['players'][0]['relics'],'potions_changed':st['players'][0]['potions']!=post['players'][0]['potions']}
 if kind not in ('play_card','end_turn','select_map_node','proceed') or len(row)>5:choices.append(row)
 action_rows.append(row)
# Separate actual offers from generic flow model references (which include owned
# cards presented as upgrade/removal candidates).
actual_offers=collections.defaultdict(set)
for st in states:
 flow=st['run_flow'] or {}; room=flow.get('room') or {}
 for rs in flow.get('reward_sets',[]):
  for reward in rs['rewards']:
   for cat,prefix in [('cards','CARD.'),('relics','RELIC.'),('potions','POTION.')]:actual_offers[cat].update(ids(reward,prefix))
 if room.get('kind')=='shop':
  for cat,prefix in [('cards','CARD.'),('relics','RELIC.'),('potions','POTION.')]:actual_offers[cat].update(ids(room.get(cat,[]),prefix))
 if room.get('kind')=='event':
  for cat,prefix in [('cards','CARD.'),('relics','RELIC.'),('potions','POTION.')]:actual_offers[cat].update(ids(room.get('options',[]),prefix))
 if room.get('kind')=='treasure':actual_offers['relics'].update(ids(room.get('relics',[]),'RELIC.'))
actual_offers={k:sorted(v) for k,v in actual_offers.items()}
summary={'source' :'dense/actions.json.gz + dense/states.json.gz','action_count':696,'state_count':697,'action_counts':dict(collections.Counter(x['action']['kind'] for x in a['actions'])),'observed':{k:sorted(v) for k,v in sets.items()},'actual_reward_shop_event_treasure_offers':actual_offers,'used_potion_models':sorted({row['used_potion']['id'] for row in action_rows if 'used_potion' in row}),'first_evidence':pointers,'card_plays_by_model':dict(played),'card_play_variants':[{'card':json.loads(k),'plays':v} for k,v in played_variants.items()],'unresolved_actions':errors,'terminal':{k:states[-1][k] for k in ('boundary_kind','run_ending_reason','current_act_index')},'limits':['All states scanned; labels for card plays resolved only through matching pre-action combat/cards map and checked membership of pre-action Hand.','Owned/observed/offered IDs are not proof their effects ran; powers are observed stored effects, relic passive triggers need source audit.','Deck descriptor deltas include upgrades, enchantments, removals, transformations and acquisition; do not count all additions as acquisition.','Static setup queues, unlock lists and grab bags deliberately excluded from observed effect inventory.','This is an observed inventory, not audited transitive mechanic dependency closure.']}
for name,value in [('inventory.json',summary),('actions-resolved.json',action_rows),('noncombat-and-choice-actions.json',choices),('room-boundaries.json',sessions),('room-history.json',states[-1]['map_point_history']),('card-variants.json',{k:list(v.values()) for k,v in variants.items()})]:
 (out/name).write_text(json.dumps(value,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ('first_evidence','card_play_variants')},indent=2))
