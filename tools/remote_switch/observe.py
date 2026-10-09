"""Bounded observation only: never send input or classify a black screen as failure."""
import argparse
from datetime import datetime, timezone
import json
import http.client
from pathlib import Path
import re
import time
import uuid
from controller import Controller
from direct_files import DirectFiles
from transport import BotTransport
from diagnostics import Diagnostics
from credentials import http_key
from performance_data import parse as parse_performance
FAULT_NAMES={1:'GC fatal error',2:'Rendering thread exception',3:'DLL initialization aborted',
             4:'Unhandled exception',5:'Native runtime failure',6:'Fatal error dialog',7:'Fatal NT process exit'}

def fresh_fault(snapshot, previous, state):
    # Old closed files can survive exit. Require a newer console sample before
    # interpreting a retained failure flag; never infer a failure from black.
    return bool(snapshot and previous and snapshot['tick']>previous['tick'] and
                state.get('fatal_kind',0) in FAULT_NAMES)

def remote_unavailable(failures, first_failure, now, recovery_seconds):
    """Transient startup/service resets are not proof of a game crash."""
    return bool(failures >= 3 and first_failure is not None and
                now - first_failure >= recovery_seconds)

def summarize(folder, collection):
    candidates=[item for item in collection if item.get('snapshot')]
    if not candidates: return {'snapshot_status':'unknown'}
    selected=max(candidates,key=lambda item:(item['snapshot']['tick'],item['snapshot']['sequence']))
    text=(folder/selected['name']).read_text(encoding='utf-8')
    result={'snapshot':selected['snapshot']}
    for marker, allowed in (('STARTUP',('stage','total_s','stage_s','graphics_presents','graphics_successful_presents','window_frames')),
                            ('PROGRESS',('reads','read_mb','sd_reads','sd_mb','syscalls','frames','heap_used_mb','heap_free_mb'))):
        lines=[line for line in text.splitlines() if line.startswith('['+marker+']')]
        if lines:
            values=dict(re.findall(r'([a-z_]+)=(\d+)(?:\s|$)',lines[-1]))
            result[marker.lower()]={key:int(values[key]) for key in allowed if key in values}
    states=[line for line in text.splitlines() if line.startswith('[REMOTE-STATE]')]
    if states:
        values=dict(re.findall(r'([a-z_]+)=(\d+)(?:\s|$)',states[-1]))
        result['runtime_state']={key:int(value) for key,value in values.items()
                                 if key in ('active','stage','total_s','stage_s','graphics_successful_presents','window_frames','fatal_kind','fatal_age_s')}
        if result['runtime_state'].get('active') == 1:
            result['startup']=dict(result['runtime_state'])
    performance = parse_performance(text)
    if performance:
        result['performance'] = performance
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',required=True)
    parser.add_argument('--game',required=True)
    parser.add_argument('--seconds',type=int,default=600)
    parser.add_argument('--interval',type=int,default=2)
    parser.add_argument('--source',choices=('direct','http'),default='direct')
    parser.add_argument('--lan-key-file',type=Path)
    parser.add_argument('--recovery-seconds',type=int,default=120,
                        help='Read-only recovery window after a failed read (0..300)')
    parser.add_argument('--no-screenshots',action='store_true',
                        help='Collect logs only during startup; avoid GPU screenshot requests')
    args=parser.parse_args()
    if not 60<=args.seconds<=1800 or not 2<=args.interval<=60 or len(args.game)>128:
        parser.error('Observation bounds: 60..1800 seconds, 2..60 second interval, game name <=128 characters')
    if not 0<=args.recovery_seconds<=300: parser.error('recovery-seconds must be 0..300')
    transport=BotTransport(args.host,timeout=15)
    controller=Controller(transport)
    if controller.title() != '0548EABB35576000':
        parser.error('Autorun forwarder is not the active application')
    diagnostics = Diagnostics(args.host,http_key(args.lan_key_file),timeout=5) if args.source=='http' else None
    folder=Path(__file__).parent/'runs'/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-observe-')+uuid.uuid4().hex[:8])
    folder.mkdir()
    record={'environment':'physical Switch LAN','game':args.game,'started_utc':datetime.now(timezone.utc).isoformat(),
            'requested_seconds':args.seconds,'recovery_seconds':args.recovery_seconds,
            'screenshots_enabled':not args.no_screenshots,
            'source':args.source,'state':'observing','game_verdict':'Unknown','samples':[],'sends_input':False}
    def save(): (folder/'observation.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    save();print(folder,flush=True)
    started=time.monotonic();failures=0;previous=None;next_screenshot=0;first_failure=None
    while True:
        elapsed=time.monotonic()-started
        sample_folder=folder/f"sample-{len(record['samples']):03d}"
        sample_folder.mkdir()
        sample={'elapsed_seconds':round(elapsed,1)}
        try:
            collection=diagnostics.collect_live(sample_folder) if diagnostics else DirectFiles(transport).collect(sample_folder,live=True)
            sample.update(summarize(sample_folder,collection))
            snapshot=sample.get('snapshot')
            sample['fresh_snapshot']=bool(snapshot and previous and snapshot['tick']>previous['tick'])
            fault=fresh_fault(snapshot,previous,sample.get('runtime_state',{}))
            if snapshot: previous=snapshot
            if fault:
                record['state']='runtime-fault-detected'
                record['game_verdict']='Explicit runtime error; application may still be running'
                kind=sample['runtime_state']['fatal_kind']
                record['runtime_fault']={'kind':kind,'label':FAULT_NAMES[kind],
                                         'game_elapsed_seconds':sample['runtime_state'].get('total_s')}
                print('Explicit runtime failure: '+FAULT_NAMES[kind],flush=True)
            if not args.no_screenshots and (elapsed>=next_screenshot or elapsed>=args.seconds or fault):
                (sample_folder/'screen.jpg').write_bytes(controller.screenshot())
                sample['screenshot']='screen.jpg'
                next_screenshot=elapsed+30
            sample['status']='received';failures=0;first_failure=None
        except (OSError, ValueError, UnicodeError, http.client.HTTPException) as exc:
            sample['status']='remote-read-failed';sample['error_type']=type(exc).__name__;failures+=1
            if first_failure is None: first_failure=time.monotonic()
        record['samples'].append(sample);save()
        if record['state']=='runtime-fault-detected': break
        if remote_unavailable(failures,first_failure,time.monotonic(),args.recovery_seconds):
            record['state']='remote-unavailable';break
        if time.monotonic()-started>=args.seconds:
            record['state']='observation-window-complete';break
        time.sleep(max(0,min(args.interval,args.seconds-(time.monotonic()-started))))
    record['elapsed_seconds']=round(time.monotonic()-started,1);save()
    print(record['state'],flush=True)

if __name__=='__main__': main()
