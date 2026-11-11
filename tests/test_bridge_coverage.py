#!/usr/bin/env python3
import importlib.util,json,pathlib,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bridge_coverage',root/'tools/chams_bridge_coverage.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as d:
 p=pathlib.Path(d);(p/'summary.json').write_text(json.dumps({'complete':False,'dropped':2,'partial_frames':1}))
 rows=[
  {'seq':1,'event':'draw_begin','draw_id':1,'thread':3,'context':3},
  {'seq':2,'event':'gl_sync','sync_id':7,'api':'glFenceSync','thread':3,'context':3},
  {'seq':3,'event':'gl_sync','sync_id':7,'api':'glWaitSync','thread':1,'context':1},
  {'seq':4,'event':'draw','draw_id':1,'thread':3,'context':3,'renderer':1},
  {'seq':5,'event':'draw_begin','draw_id':2,'thread':3,'context':3},
 ]
 (p/'capture-000001.jsonl').write_text(''.join(json.dumps(e)+'\n' for e in rows))
 a=m.analyze(p)
 assert a['draw_records']==1 and a['begin_ids_without_recorded_draw']==1
 assert a['nonzero_sync_labels']==1 and a['sync_labels_observed_in_multiple_contexts']==1
 assert a['summary_complete'] is False and a['reported_drops']==2
 assert all(n==0 for n in a['explicit_guest_identity_field_counts'].values())
 assert a['host_caller_samples']==0
print('PASS: bridge coverage distinguishes observed syncs from unavailable guest identity (synthetic records)')
