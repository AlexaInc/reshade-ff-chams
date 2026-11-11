#!/usr/bin/env python3
"""Stream-check one ReShade Chams JSONL session, without reading game resources."""
import argparse, collections, hashlib, json, pathlib, sys

def verify(path):
    path=pathlib.Path(path); summary_path=path/'summary.json'; problems=[]
    try: summary=json.loads(summary_path.read_text(encoding='utf-8'))
    except (OSError,ValueError): summary={};problems.append('Missing or invalid summary.json; capture may be unfinished or truncated.')
    files=sorted(path.glob('capture-*.jsonl'))
    if not files: problems.append('No capture parts found.')
    seq=0; records=0; gaps=0; counts=collections.Counter(); open_draw={}; orphan=0; mismatched=0; first=None;last=None; hashes=[]
    for part in files:
        digest=hashlib.sha256()
        try:
            with part.open('rb') as f:
                for line in f:
                    digest.update(line);records+=1
                    try:
                        if not line.endswith(b'\n'): raise ValueError('missing newline')
                        event=json.loads(line)
                        if not isinstance(event,dict) or event.get('v')!=1: raise ValueError('schema')
                        n=event['seq']; name=event['event']
                        if not isinstance(n,int) or n<=seq: raise ValueError('sequence')
                    except (ValueError,KeyError,UnicodeDecodeError,TypeError):
                        problems.append(f'Invalid or truncated JSONL record in {part.name} at line {records}.');continue
                    gaps+=n-seq-1;seq=n; counts[name]+=1
                    if first is None: first=name
                    last=name
                    if name=='draw_begin':
                        k=event.get('draw_id'); open_draw[k]=(event.get('context'),event.get('surface'),event.get('frame'))
                    elif name=='draw' and 'draw_id' in event:
                        key=event['draw_id']; origin=open_draw.pop(key,None)
                        if origin is None: orphan+=1
                        elif origin!=(event.get('context'),event.get('surface'),event.get('frame')): mismatched+=1
        except OSError: problems.append(f'Cannot read {part.name}.')
        hashes.append((part.name,digest.hexdigest()))
    if first!='session_start': problems.append('Missing session_start marker.')
    if last!='session_end': problems.append('Missing session_end marker; stop may not have completed.')
    if summary and summary.get('written')!=records: problems.append('Summary written count does not match readable line count.')
    if summary and summary.get('files')!=len(files): problems.append('Summary part count does not match files supplied.')
    if gaps: problems.append(f'{gaps} missing sequence numbers (record loss or missing part).')
    if orphan or open_draw or mismatched: problems.append(f'Draw pairing: {orphan} missing starts, {len(open_draw)} missing ends, {mismatched} mismatched frame keys.')
    if summary.get('dropped',0): problems.append(f"Recorder reported {summary['dropped']} dropped events.")
    if summary.get('io_error'): problems.append('Recorder reported a writer error.')
    if summary.get('partial_frames',0): problems.append(f"Recorder reported {summary['partial_frames']} incomplete ending frames.")
    return {'summary':summary,'records_read':records,'event_counts':dict(counts),'missing_sequence_numbers':gaps,'unpaired_draws':len(open_draw),'problems':problems,'sha256':dict(hashes)}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('session',help='Session folder containing summary.json and capture-*.jsonl');args=ap.parse_args()
    result=verify(args.session);print(json.dumps(result,indent=2,sort_keys=True));return 1 if result['problems'] else 0
if __name__=='__main__':sys.exit(main())
