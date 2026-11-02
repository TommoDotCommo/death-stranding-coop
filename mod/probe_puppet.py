#!/usr/bin/env python3
"""Native Windows, supervised one-shot puppet experiment in a disposable copy.

--arm writes only a process-stamped command beside mod.json, after two seconds
of fresh stationary poses. Process access is QUERY_LIMITED_INFORMATION|VM_READ:
no remote threads, process writes, injected input, or calls into the game.
"""
import argparse
import ctypes as c
import json
import math
import os
from pathlib import Path
import struct
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'redirect'))
from read_thread_trace import WindowsReader,identity_from_log


def command_bytes(pid,start):return f'spawn {pid}-{start:016x}\n'.encode('ascii')

def distance(a,b):return math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))


def pose_blob(blob):
    if len(blob)!=60:raise ValueError('short entity transform')
    values=struct.unpack('<3d9f',blob)
    if not all(math.isfinite(x) for x in values):raise ValueError('non-finite entity transform')
    return {'position':list(values[:3]),'rotation':list(values[3:])}


def validate_event(event,actor,local):
    if not int(event['actor'],16) or event['actor']==event['local_entity']:
        raise ValueError('actor is null or aliases local player')
    if not actor or not local:raise ValueError('external pose read unavailable')
    if distance(actor['position'],event['requested_position'])>.25:
        raise ValueError('external actor pose differs from requested transform')
    if distance(actor['position'],local['position'])<2:
        raise ValueError('clone is not independently separated from local player')
    if not event['local_after_available'] or event['local_after_entity']!=event['local_entity']:
        raise ValueError('local player changed during engine call')
    if distance(local['position'],event['local_after_position'])>.25:
        raise ValueError('external local pose changed after engine call')


def read(reader,address,size):
    if not 0<address<(1<<47)-size:raise ValueError('noncanonical read address')
    data=c.create_string_buffer(size);copied=c.c_size_t()
    if not reader.api.ReadProcessMemory(reader.handle,address,data,size,c.byref(copied)) or copied.value!=size:
        raise OSError(c.get_last_error(),'external game-state read failed')
    return data.raw


def private_path(path):
    for item in (path,*path.parents):
        if item.is_symlink() or getattr(item.stat(),'st_file_attributes',0)&0x400:
            raise ValueError('reparse/symlink path refused')
        if item.is_file() and item.stat().st_nlink!=1:raise ValueError('shared command/config path refused')


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--log',type=Path,required=True)
    p.add_argument('--arm',action='store_true')
    p.add_argument('--timeout',type=float,default=120)
    a=p.parse_args(argv)
    if os.name!='nt':p.error('native Windows Python required')
    if not math.isfinite(a.timeout) or not 5<=a.timeout<=300:p.error('timeout must be 5..300 seconds')
    audit=a.log.absolute();private_path(audit)
    if audit.parent.name.casefold()!='redirect-data':p.error('expected a disposable redirect-data audit log')
    pid,start,address=identity_from_log(audit)
    text=audit.read_text(encoding='utf-16-le')
    if '\nNETWORK\tenforce\t' not in '\n'+text or '\nGAME_THREAD_ARMED\t' not in '\n'+text:
        raise ValueError('enforced network and armed game-thread bootstrap required')
    directory=audit.parent/'mod';config=directory/'mod.json';private_path(config)
    cfg=json.loads(config.read_text())
    if not cfg.get('game_thread') or not cfg.get('puppet_probe') or cfg.get('self_tick') or not cfg.get('required'):
        raise ValueError('explicit required game_thread/puppet_probe config required')
    stamp=f'{pid}-{start:016x}';presence=directory/f'presence-{stamp}.jsonl';modlog=directory/f'mod-{stamp}.log'
    reader=WindowsReader(pid,start)
    report={'pid':pid,'process_start':hex(start),'armed':False,'events':[],'external':[],'removed_verified':False}
    try:
        header=read(reader,address,80);base=struct.unpack_from('<Q',header,32)[0]
        frequency=c.c_longlong();reader.api.QueryPerformanceFrequency(c.byref(frequency))
        def now_us():
            value=c.c_longlong();reader.api.QueryPerformanceCounter(c.byref(value));return value.value*1000000//frequency.value
        stable=[];last=0;seen=0;deadline=time.monotonic()+a.timeout
        while time.monotonic()<deadline:
            if a.arm and not report['armed'] and presence.exists():
                if presence.stat().st_size>32*1024*1024:raise ValueError('presence log exceeds cap')
                lines=[line for line in presence.read_text().splitlines(keepends=True) if line.endswith('\n')]
                row=json.loads(lines[-1]) if lines else None
                if row and row['t_us']!=last:
                    last=row['t_us'];local=row['local']
                    if 0<=now_us()-last<1000000 and local.get('available') and local.get('speed',1)<.05:
                        stable.append(row);stable=stable[-12:]
                    else:stable=[]
                    if len(stable)>=8 and last-stable[0]['t_us']>=1800000:
                        world,=struct.unpack('<Q',read(reader,base+0x7bc4e70,8))
                        if not world:raise ValueError('entity world manager unavailable')
                        command=directory/'puppet.spawn'
                        if command.exists():
                            private_path(command)
                            # Only canonicalize this same process's refused CRLF
                            # request. Other processes' or already canonical
                            # commands are preserved; replacement has no gap.
                            if command.read_bytes()!=command_bytes(pid,start).replace(b'\n',b'\r\n'):
                                raise ValueError('existing command preserved; inspect before retrying')
                        private_path(directory)
                        temporary=directory/('puppet.spawn-'+stamp+'.tmp')
                        with temporary.open('xb') as f:f.write(command_bytes(pid,start))
                        os.replace(temporary,command);report['armed']=True
                        print('PUPPET armed for stationary local player',file=sys.stderr,flush=True)
            if modlog.exists():
                events=[json.loads(line[7:]) for line in modlog.read_text().splitlines(keepends=True) if line.startswith('PUPPET {') and line.endswith('\n')]
                for event in events[seen:]:
                    report['events'].append(event);seen+=1
                    print('PUPPET event '+event['event'],file=sys.stderr,flush=True)
                    if event['event'] in ('created','moved'):
                        actor=int(event['actor'],16);local=int(event['local_entity'],16)
                        actual=pose_blob(read(reader,actor+0xc8,60));original=pose_blob(read(reader,local+0xc8,60))
                        validate_event(event,actual,original)
                        mover,model=struct.unpack('<QQ',read(reader,actor+0xb0,16))
                        # +b0 is mover, +b8 is model (the tuple names below are explicit).
                        report['external'].append({'event':event['event'],'actor':actual,'local':original,
                            'mover':hex(mover),'model':hex(model)})
                    if event['event']=='resource_rejected':
                        raise ValueError('resource quarantined: native Sam clone lacks DSPlayerController; no creator called')
                    if event['event']=='failed' or event['event'].startswith('world_changed'):
                        raise ValueError('in-process probe refused; inspect saved event evidence')
                    if event['event']=='remove_requested':
                        if len(report['external'])!=2:raise ValueError('creation and movement must both be read independently')
                        report['ok']=True;print(json.dumps(report,indent=2));return 0
            time.sleep(.05)
        raise TimeoutError('no completed one-shot experiment before timeout')
    except BaseException as error:
        report['ok']=False;report['error']=str(error);print(json.dumps(report,indent=2));return 1
    finally:reader.close()


if __name__=='__main__':raise SystemExit(main())
