#!/usr/bin/env python3
"""Exercise the standalone recording core; no GL driver or Windows DLL is tested."""
from pathlib import Path
import json, subprocess, tempfile, importlib.util, shutil
root = Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('capture_verify',root/'tools/chams_capture_verify.py')
verifier=importlib.util.module_from_spec(spec);spec.loader.exec_module(verifier)
with tempfile.TemporaryDirectory() as tmp:
    temp = Path(tmp)
    exe = temp / 'capture-test'
    subprocess.run(['g++','-std=c++17','-O2','-pthread',str(root/'tests/chams_capture_core_test.cpp'), str(root/'source/chams_capture.cpp'),'-o',str(exe)], check=True)
    subprocess.run([str(exe),str(temp)],check=True,timeout=90)
    def load(name):
        path=temp/name
        summary=json.loads((path/'summary.json').read_text())
        records=[]
        for f in sorted(path.glob('capture-*.jsonl')):
            with f.open() as src:
                for line in src:
                    records.append(json.loads(line))
        return summary,records
    s,rows=load('long')
    assert s['complete'] and not s['dropped'] and s['written']==len(rows)
    assert [x['seq'] for x in rows] == list(range(1,len(rows)+1))
    assert rows[0]['event']=='session_start' and rows[-1]['event']=='session_end'
    assert len({(x['worker'],x['index']) for x in rows if 'worker' in x}) == 400000
    assert len({x['thread'] for x in rows if 'worker' in x}) == 4
    draws=[x for x in rows if x['event']=='draw' and 'value' in x]
    assert [(x['context'],x['frame'],x['value']) for x in draws] == [(1,1,1),(2,1,2),(1,1,3),(3,1,4)]
    assert any(x['event']=='present' and not x['success'] for x in rows)
    for context in range(4,8):
        frames = [x for x in rows if x['context']==context and 'round' in x]
        assert len(frames)==2000
        assert all(x['frame']==x['round']+1 for x in frames)
        assert [x['round'] for x in frames]==[i for i in range(1000) for _ in range(2)]
    assert s['files'] > 1 and s['elapsed_ns'] > 0 and s['enqueue_ns'] > 0
    print('synthetic long:',s['draws'],'draw records,',s['files'],'parts,',round(s['elapsed_ns']/1e9,3),'s elapsed,',round(s['enqueue_ns']/s['attempted'],1),'ns mean producer enqueue time (includes lock wait)')
    simulated=verifier.verify(temp/'simulated')
    assert not simulated['problems'] and simulated['summary']['frames']==72000 and simulated['summary']['draws']==144000
    print('PASS: 20 minutes of logical 60 Hz frames simulated (not wall-clock gameplay)')
    surface_summary,surface_rows=load('surfaces'); assert surface_summary['complete']
    assert not verifier.verify(temp/'surfaces')['problems']
    surface_draws=[(e['surface'], e['frame'], e['value']) for e in surface_rows if e['event']=='draw']
    assert surface_draws == [(1,1,11),(1,2,12),(2,1,21),(1,2,13)]
    begin=next(e for e in surface_rows if e['event']=='draw_begin')
    end=next(e for e in surface_rows if e['event']=='draw' and e.get('value')==13)
    assert (begin['context'],begin['surface'],begin['frame'],begin['draw_id']) == (end['context'],end['surface'],end['frame'],end['draw_id'])
    sessions=list((temp/'runtime').glob('session-*')); assert len(sessions)==1
    runtime_rows=[json.loads(line) for f in sorted(sessions[0].glob('capture-*.jsonl')) for line in f.read_text().splitlines()]
    assert [x['event'] for x in runtime_rows].index('draw') < [x['event'] for x in runtime_rows].index('session_end')
    assert any(x['event']=='frame_end' and x['reason']=='session_stop_incomplete' for x in runtime_rows)
    s,rows=load('overflow')
    assert not s['complete'] and s['dropped'] > 0 and s['written']==len(rows)
    assert s['oversized'] >= 1 and s['queue_full'] > 0
    assert s['dropped'] == s['oversized']+s['queue_full']+s['io_dropped']+s['allocation_failed']
    assert s['attempted']==s['written']+s['dropped']
    assert verifier.verify(temp/'overflow')['problems']
    corrupt=temp/'corrupt'; shutil.copytree(temp/'surfaces',corrupt)
    with next(corrupt.glob('capture-*.jsonl')).open('ab') as f: f.write(b'{broken')
    assert verifier.verify(corrupt)['problems']
    assert all(a['seq']<b['seq'] for a,b in zip(rows,rows[1:]))
    print('PASS: capture core long-session, concurrency, context/frame correlation, rotation, overflow and record integrity')
    if (temp/'io-error').exists():
        e=json.loads((temp/'io-error'/'summary.json').read_text())
        assert not e['complete'] and e['dropped'] > 0
        print('PASS: forced asynchronous I/O error is reported')
    print('simulated capture events:',s['attempted'],'dropped:',s['dropped'])
