"""Route challenges in the fake world (docs/ROUTES.md): the real mod (fake.so, fake game memory), one game at a time,
each with a data folder of its own. Ann presses K (the recording key, through the pad script), walks 60 m over a small
hill and presses K again: the route string is in her log and in routes/recorded-*.txt. Then the same string runs as a
challenge (config "route") in three more games: Bob walks it inside the corridor (300 m above the recorded heights,
and 2 m to the side: the corridor is flat) and finishes; Cee steps 15 m aside and waits out the grace: failed, left
the corridor; Dee jumps 35 m in one tick: failed, teleported. Each run writes its result file. Checked from the mod's
status (the route state), its log (ROUTE lines) and the fake info log (notices_shown). A string that is not a
challenge refuses the start.

  python3 test_route_world.py                      the test, then its mutant build (fake-route-axes.so must fail)
  python3 test_route_world.py LIBRARY PROPERTY     one property (route) against another build
"""
import ctypes as c
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from test_others_partner import Game

ROOT=Path(__file__).resolve().parent
def oracle(ok,what):
    if not ok:raise AssertionError('route-world oracle: '+str(what))

def game(folder,library,name,state,extra):
    os.environ['XDG_STATE_HOME']=str(state)
    return Game(folder,library,name,(0,0,5),partner=None,others=0,extra=dict(dict(notices=True,virtual_pad=True),**extra))
def log(g):return next(g.root.glob('mod-*.log')).read_text()
def routes(state):return state/'DeathStrandingCoop'/'routes'

def walk(g,path,speed,tail=0.0):
    """Moves the porter along `path` (game axes: x, y on the ground, z up) at `speed` m/s in real time, ticking."""
    for (x0,y0,z0),(x1,y1,z1) in zip(path,path[1:]):
        d=((x1-x0)**2+(y1-y0)**2)**.5;began=time.monotonic()
        while True:
            f=min(1.0,(time.monotonic()-began)*speed/d) if d else 1.0
            g.place((x0+(x1-x0)*f,y0+(y1-y0)*f,z0+(z1-z0)*f));g.tick();time.sleep(.01)
            if f>=1:break
    hold(g,tail)
def hold(g,seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:g.tick();time.sleep(.01)

HILL=[(0,0,5),(20,0,15),(40,0,15),(60,0,5)]

def route_property(library):
    before=os.environ.get('XDG_STATE_HOME')
    try:
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            # Ann records: K, a walk over the hill, K.
            state=root/'state-Ann';a=game(folder,library,'Ann',state,{})
            try:
                a.lib.mod_test_run_stamp.restype=c.c_char_p;stamp=a.lib.mod_test_run_stamp().decode()
                hold(a,.3)
                (a.root/'pad.script').write_text(f'pad {stamp}\nkey K 200\nwait 300\n')
                hold(a,.8);oracle(a.status()['route']['recording'],('K did not start recording',a.status()['route'],log(a)[-800:]))
                walk(a,HILL,10,.3)
                (a.root/'pad.script').write_text(f'pad {stamp}\nkey K 200\nwait 300\n')
                hold(a,1.6)
                s=a.status();text=log(a)
            finally:a.close()
            oracle(not s['route']['recording'] and s['route']['state']=='none',('recording did not stop',s['route']))
            m=re.search(r'ROUTE recorded (\d+) points, (\d+) m: (DSR1-[0-9A-Z-]+)',text)
            oracle(m,('no recorded route in the log',text[-1500:]))
            points,length,route=int(m.group(1)),int(m.group(2)),m.group(3)
            oracle(2<=points<=10 and 58<=length<=62,('the recorded route',points,length))
            files=list(routes(state).glob('recorded-*.txt'))
            oracle(len(files)==1 and files[0].read_text().strip()==route,('the recording file',files))
            oracle(any(n.startswith('Recording a route') for n in s['notices_shown']) and any(re.match(r'Route recorded: (5[89]|6[012]) m\. ',n) for n in s['notices_shown']),('notices',s['notices_shown']))

            # Bob runs it inside the corridor: 300 m higher than recorded and 2 m aside, faster than he recorded it.
            state=root/'state-Bob';b=game(folder,library,'Bob',state,{'route':route})
            try:
                hold(b,.5);oracle(b.status()['route']['state']=='ready',('Bob is not at the start',b.status()['route']))
                walk(b,[(0,0,5),(20,2,315),(40,-2,315),(60,0,305)],8,.3)
                s=b.status();text=log(b)
            finally:b.close()
            r=s['route'];oracle(r['state']=='finished',('Bob did not finish',r,text[-1500:]))
            oracle(4000<=r['elapsed_ms']<=6500,('his time',r))
            oracle(re.search(r'ROUTE started: Route \d{8}-\d{4} started\.',text) and re.search(r'ROUTE finished: .* finished in 0:0\d\.\d\.',text),('the log lines',text[-1500:]))
            oracle(any('finished in 0:0' in n for n in s['notices_shown']),('the finish notice',s['notices_shown']))
            m=re.search(r'ROUTE result finished (\d+) ms track=(DST1-[0-9A-Z-]+)',text);oracle(m and int(m.group(1))==r['elapsed_ms'],('the result line',text[-800:]))
            files=list(routes(state).glob('result-*.json'));oracle(len(files)==1,('one result file',files))
            res=json.loads(files[0].read_text())
            oracle(res['route']==route and res['outcome']=='finished' and res['ms']==r['elapsed_ms'] and res['track']==m.group(2) and res['reason']=='',('the result file',res))

            # Cee steps 15 m aside half way and stays there.
            state=root/'state-Cee';cc=game(folder,library,'Cee',state,{'route':route})
            try:
                hold(cc,.3);walk(cc,[(0,0,5),(30,0,15),(30,15,15)],8,6.5)
                s=cc.status();text=log(cc)
            finally:cc.close()
            r=s['route'];oracle(r['state']=='failed' and r['reason']=='left the corridor',('Cee was not failed',r,text[-1500:]))
            oracle(any(n.startswith('Leaving the corridor: 5 s. Corridor: ') for n in s['notices_shown']),('the leaving notice',s['notices_shown']))
            oracle(re.search(r'ROUTE failed: .* failed: left the corridor\.',text) and 'ROUTE result failed' in text,('the log lines',text[-1200:]))
            res=json.loads(next(routes(state).glob('result-*.json')).read_text());oracle(res['outcome']=='failed' and res['reason']=='left the corridor',res)

            # Dee jumps 35 m between two ticks.
            state=root/'state-Dee';d=game(folder,library,'Dee',state,{'route':route})
            try:
                hold(d,.3);walk(d,[(0,0,5),(20,0,15)],8,.2)
                d.place((55,0,7));d.tick();hold(d,.5)
                s=d.status();text=log(d)
            finally:d.close()
            oracle(s['route']['state']=='failed' and s['route']['reason']=='teleported',('Dee was not failed',s['route'],text[-1200:]))

            # A string that is not a good challenge refuses the start, with the reason.
            os.environ['XDG_STATE_HOME']=str(root/'state-Eve')
            try:
                game(folder,library,'Eve',root/'state-Eve',{'route':'DSR1-AAAAA-AAAAA'});oracle(False,'a damaged challenge started')
            except AssertionError as e:
                oracle('incomplete' in str(e),('the reason',str(e)))
    finally:
        if before is None:os.environ.pop('XDG_STATE_HOME',None)
        else:os.environ['XDG_STATE_HOME']=before

PROPERTIES=dict(route=route_property)
if __name__=='__main__':
    os.chdir(ROOT)
    if len(sys.argv)==3:
        PROPERTIES[sys.argv[2]](Path(sys.argv[1]).resolve())
    else:
        for name,check in PROPERTIES.items():check(ROOT/'build/fake.so')
        # The mutant: positions fed to the route in the game's axes (z up) instead of the presence axes.
        r=subprocess.run([sys.executable,'-B',__file__,str(ROOT/'build/fake-route-axes.so'),'route'],capture_output=True,text=True,timeout=300)
        assert r.returncode==1 and 'oracle' in r.stderr,('SURVIVED fake-route-axes.so',r.returncode,r.stderr[-800:])
        print('KILLED fake-route-axes.so')
        print('Route challenges in the fake world: recorded with K, run inside (finished), outside (failed), with a jump (teleported); the result files')
