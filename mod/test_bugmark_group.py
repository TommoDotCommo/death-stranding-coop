"""F2 in group play (the relay's broadcast mode), in the fake world through the real server: four copies of the real mod
(fake.so, each with a log folder of its own) in group configs with the slot every group player has, the real coop-world
with a log folder of its own, and two coop-swarm stand-ins near the first player. One F2 of Ann's gives "BUGMARK <n>" and
its flight file in her log, "BUGMARK <n> from Ann" and a flight file in the log of every player near her (Bob, Cee and
the stand-ins), nothing in the far player's (Dee), and one "bugmark <n> from <relay id> (Ann) at <UTC ms>" line in the
server's log. A second F2 within 2 s is marked in her own log only (the rate). Every log's chain holds.

  python3 test_bugmark_group.py WORLD SWARM                     the test, then its mutant build
  python3 test_bugmark_group.py WORLD SWARM LIBRARY PROPERTY    one property (marks) against another build
"""
import ctypes as c
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from test_others_broadcast import Game, KEY, free_port, oracle as broadcast_oracle, others, run
import socket

ROOT=Path(__file__).resolve().parent
def oracle(ok,what):
    if not ok:raise AssertionError('bugmark-group oracle: '+str(what))
def verify_chain(path):
    chain=2166136261
    for expected,line in enumerate(path.read_text(encoding='utf-8').splitlines()):
        number,utc,recorded,text=line.split(' ',3)
        oracle(int(number)==expected and re.fullmatch('[0-9a-f]{8}',recorded),('chain line format',path.name))
        for byte in f'{number} {utc} {text}'.encode():chain=((chain^byte)*16777619)&0xffffffff
        oracle(chain==int(recorded,16),('continued FNV chain',path.name))
def logs(folder,component):
    return sorted((folder/'DeathStrandingCoop'/'logs').glob('*_%s_p*.log'%component))
def marks(text):return re.findall(r'^\d+ \d+ [0-9a-f]{8} (BUGMARK \d+(?: from .*)?)$',text,re.M)

def group_marks(library,world,swarm):
    with tempfile.TemporaryDirectory() as folder:
        root=Path(folder);port,relay=free_port(),free_port(socket.SOCK_DGRAM)
        env=dict(os.environ,XDG_STATE_HOME=str(root/'state-world'))
        server=subprocess.Popen([world,'--port',str(port),'--relay-port',str(relay),'--relay-key',KEY],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
        games=[];stand=None;stop=root/'stop';before=os.environ.get('XDG_STATE_HOME');states={}
        began=int(time.time()*1000)
        try:
            oracle('listening' in server.stdout.readline(),'coop-world did not start')
            # Group play: every player has the launcher's slot for the group's port (0 here), so the slot cannot tell
            # their marks apart.
            for name,at,player in [('Ann',(10,20,30),1),('Bob',(13,20,30),2),('Cee',(10,26,30),3),('Dee',(10,20,3000),4)]:
                states[name]=root/('state-'+name);os.environ['XDG_STATE_HOME']=str(states[name])
                games.append(Game(folder,library,name,at,player,0,relay,{'virtual_pad':True}))
            a,b,cc,d=games
            broadcast_oracle(run(games,15,lambda:all(others(g.status())['bodies']==2 for g in (a,b,cc))),('the games never saw each other',[others(g.status()) for g in games]))
            oracle(others(d.status())['bodies']==0,'Dee is far away')
            x,y,z=a.core()
            stand=subprocess.Popen([swarm,'--relay','127.0.0.1:%d'%relay,'--key',KEY,'--mode','broadcast','--count','2','--centre',str(x),str(y),str(z),'--radius','6',
                                    '--speed','0','--first-id','1001','--seconds','60','--stop',str(stop)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            oracle(run(games,15,lambda:others(a.status())['bodies']==4),('the stand-ins never came near Ann',others(a.status())))
            # Two F2 presses 750 ms apart: both in Ann's log, the first to the server and the players near her.
            a.lib.mod_test_run_stamp.restype=c.c_char_p
            stamp=a.lib.mod_test_run_stamp().decode()
            (a.root/'pad.script').write_text(f'pad {stamp}\nkey F2 250\nwait 500\nkey F2 250\nwait 250\n')
            run(games,4)
            stop.write_text('stop');out=stand.communicate(timeout=30)[0];stand=None
        finally:
            if stand:stand.kill();stand.wait()
            for g in games:g.close()
            server.terminate()
            try:server.wait(timeout=10)
            except subprocess.TimeoutExpired:server.kill();server.wait()
            if before is None:os.environ.pop('XDG_STATE_HOME',None)
            else:os.environ['XDG_STATE_HOME']=before
        ended=int(time.time()*1000)
        found={}
        for name,state in states.items():
            files=logs(state,'game');oracle(len(files)==1,('one game log',name,files))
            found[name]=(files[0],files[0].read_text(encoding='utf-8'))
            verify_chain(files[0])
        own=marks(found['Ann'][1])
        oracle(len(own)==2 and all(re.fullmatch(r'BUGMARK \d+',m) for m in own),('Ann marks both presses in her own log',own))
        first,second=(int(m.split()[1]) for m in own)
        oracle(second==first+1 and first&0xfffff==1,('the group numbering: her prefix, then the counter',first,second))
        oracle(found['Ann'][0].with_suffix(f'.flight-{first}.txt').read_text().startswith(f'BUGMARK {first}\n'),'Ann\'s flight file')
        for name in ('Bob','Cee'):
            got=marks(found[name][1])
            oracle(got==[f'BUGMARK {first} from Ann'],('one mark from Ann in a near player\'s log',name,got,found[name][1][-1500:]))
            flight=found[name][0].with_suffix(f'.flight-{first}.txt').read_text()
            oracle(flight.startswith(f'BUGMARK {first} from Ann\n') and 'STATE ' in flight,('a flight file of his own',name,flight[:200]))
        oracle(marks(found['Dee'][1])==[] and not list(found['Dee'][0].parent.glob('*.flight-*.txt')),('the far player is told nothing',marks(found['Dee'][1])))
        oracle(f'BUGMARK {second} not sent to the server: at most one every 2 s' in found['Ann'][1],'the second press is held back by the rate')
        # The server's own log: exactly one line, under Ann's relay id, at a UTC time within the run.
        files=logs(root/'state-world','world');oracle(len(files)==1,('one server log',files))
        verify_chain(files[0])
        lines=re.findall(r'^\d+ \d+ [0-9a-f]{8} (bugmark .*)$',files[0].read_text(encoding='utf-8'),re.M)
        oracle(len(lines)==1,('one bugmark line in the server log',lines))
        m=re.fullmatch(r'bugmark (\d+) from 0000000000000001 \(Ann\) at (\d+)',lines[0])
        oracle(m and int(m.group(1))==first and began<=int(m.group(2))<=ended,('the server\'s line',lines[0],began,ended))
        # The stand-ins near Ann were sent it too, once each.
        heard=re.findall(r'^SWARM mark id=(\d+) from=1 name="Ann" mark=(\d+)$',out,re.M)
        oracle(sorted(heard)==[('1001',str(first)),('1002',str(first))] and 'marks=2' in out.splitlines()[-1],('the stand-ins near Ann',heard,out[-600:]))

PROPERTIES=dict(marks=group_marks)
if __name__=='__main__':
    world,swarm=str(Path(sys.argv[1]).resolve()),str(Path(sys.argv[2]).resolve())
    if len(sys.argv)==5:
        PROPERTIES[sys.argv[4]](Path(sys.argv[3]).resolve(),world,swarm)
    else:
        for name,check in PROPERTIES.items():check(ROOT/'build/fake.so',world,swarm)
        # The mutant: F2 in group play marks only the player's own log (the mark never goes to the server).
        r=subprocess.run([sys.executable,'-B',__file__,world,swarm,str(ROOT/'build/fake-mark-group.so'),'marks'],capture_output=True,text=True,timeout=300)
        assert r.returncode==1 and 'oracle' in r.stderr,('SURVIVED fake-mark-group.so',r.returncode,r.stderr[-800:])
        print('KILLED fake-mark-group.so')
        print('F2 in group play (fake world through coop-world): the mark in every near player\'s log, the stand-ins\', the server\'s, none far, the rate passed')
