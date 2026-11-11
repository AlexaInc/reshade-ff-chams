#!/usr/bin/env python3
"""Audit recorded observability, not an identity or time-proximity correlator.

Input: a directory of existing capture-*.jsonl parts plus summary.json. This
reads rendering records only. It cannot count unseen calls, GPU completion,
renderer generations or guest-host handoffs that the capture did not observe.
"""
import argparse, collections, json, pathlib

EXPLICIT_GUEST_KEYS = frozenset({'renderer_instance_id', 'renderer_generation', 'owner_id',
                                 'owner_generation', 'verified_owner_class', 'guest_submission_id',
                                 'transport_correlation_id'})

def analyze(directory):
    paths=sorted(directory.glob('capture-*.jsonl'))
    if not paths: raise ValueError('no capture parts')
    summary=json.loads((directory/'summary.json').read_text())
    events=collections.Counter(); contexts=collections.Counter(); draws_by_thread_context=collections.Counter()
    fields=collections.Counter(); syncs={}; draw_ids=set(); begins=set(); seq=0; gaps=0; records=0
    for path in paths:
        with path.open(encoding='utf-8') as f:
            for line in f:
                e=json.loads(line); current=e['seq']
                if current!=seq+1: gaps+=1
                seq=current; records+=1; event=e['event']; events[event]+=1
                fields.update(e.keys()); contexts[e['context']]+=1
                if event=='draw_begin': begins.add(e['draw_id'])
                if event=='draw':
                    draw_ids.add(e['draw_id']); draws_by_thread_context[(e['thread'],e['context'])]+=1
                if event=='gl_sync' and e.get('sync_id',0):
                    s=syncs.setdefault(e['sync_id'],{'contexts':set(),'threads':set(),'apis':set()})
                    s['contexts'].add(e['context']); s['threads'].add(e['thread']); s['apis'].add(e.get('api'))
    multi_ctx=sum(len(s['contexts'])>1 for s in syncs.values())
    multi_thread=sum(len(s['threads'])>1 for s in syncs.values())
    return {
      'records':records, 'last_sequence':seq, 'sequence_discontinuities':gaps, 'summary_complete':summary.get('complete'),
      'reported_drops':summary.get('dropped'), 'partial_frames':summary.get('partial_frames'),
      'events':dict(sorted(events.items())), 'contexts':dict(sorted(contexts.items())),
      'draws_by_recorded_thread_and_context':{f'{t}/{c}':n for (t,c),n in sorted(draws_by_thread_context.items())},
      'draw_records':events['draw'], 'distinct_draw_ids':len(draw_ids),
      'begin_ids_without_recorded_draw':len(begins-draw_ids), 'draw_ids_without_begin':len(draw_ids-begins),
      'explicit_guest_identity_field_counts':{k:fields[k] for k in sorted(EXPLICIT_GUEST_KEYS)},
      'host_caller_samples':events['host_caller_sample'],
      'nonzero_sync_labels':len(syncs), 'sync_labels_observed_in_multiple_contexts':multi_ctx,
      'sync_labels_observed_on_multiple_threads':multi_thread,
      'limitations':[
        'Absence of the listed fields is absence in this capture, not proof an emulator cannot transport an identity.',
        'Existing sync labels are best-effort local observations, not proof of GPU completion or a guest-host renderer handoff.',
        'Renderer create/update/destroy/reuse, verified ownership, object lifetime, and asynchronous guest-to-host joins were not instrumented and cannot be counted.',
        'Thread and context numbers are session-local recorder labels, not OS thread IDs or guest process identifiers.',
        'A partially recorded or unhooked draw stream limits all coverage claims.'
      ]}

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('directory',type=pathlib.Path); ap.add_argument('--output',type=pathlib.Path)
    a=ap.parse_args(); data=analyze(a.directory); text=json.dumps(data,indent=2)+'\n'
    if a.output:a.output.write_text(text,encoding='utf-8')
    else:print(text,end='')
if __name__=='__main__':main()
