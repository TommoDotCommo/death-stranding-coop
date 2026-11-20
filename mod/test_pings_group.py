"""Pings and likes in group play (group_pings, the relay's broadcast mode), in the fake world through the real server:
four copies of the real mod (fake.so, each with a log folder of its own) and the real coop-world. Ann, Bob and Cee stand
a few metres apart, Dee far away. Ann's B: "PING sent" in her log and her ping on her own HUD; in Bob's and Cee's logs
"PING from Ann", a HUD mark at her place with a ping's id, and the info-log line naming her ("Ann pinged a place ...");
nothing at Dee's. Ann's L: a like for the nearest player (Bob, 3 m), given in his game ("LIKE received from Ann", the
info log's "Ann sent you a like."), none at Cee's. Dee's L: nobody within 25 m, nothing sent. Every log's chain holds.

  python3 test_pings_group.py WORLD                     the test, then its mutant build
  python3 test_pings_group.py WORLD LIBRARY PROPERTY    one property (pings) against another build
"""
import ctypes as c
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile

from test_others_broadcast import Game, KEY, free_port, oracle as broadcast_oracle, others, run
from test_bugmark_group import logs, verify_chain

ROOT=Path(__file__).resolve().parent
def oracle(ok,what):
    if not ok:raise AssertionError('pings-group oracle: '+str(what))
def pings(s):return s['group_pings']

def group_pings(library,world):
    with tempfile.TemporaryDirectory() as folder:
        root=Path(folder);port,relay=free_port(),free_port(socket.SOCK_DGRAM)
        env=dict(os.environ,XDG_STATE_HOME=str(root/'state-world'))
        server=subprocess.Popen([world,'--port',str(port),'--relay-port',str(relay),'--relay-key',KEY],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
        games=[];before=os.environ.get('XDG_STATE_HOME');states={}
        try:
            oracle('listening' in server.stdout.readline(),'coop-world did not start')
            for name,at,player in [('Ann',(10,20,30),1),('Bob',(13,20,30),2),('Cee',(10,26,30),3),('Dee',(10,20,3000),4)]:
                states[name]=root/('state-'+name);os.environ['XDG_STATE_HOME']=str(states[name])
                games.append(Game(folder,library,name,at,player,0,relay,{'virtual_pad':True,'group_pings':True}))
            a,b,cc,d=games
            broadcast_oracle(run(games,15,lambda:all(others(g.status())['bodies']==2 for g in (a,b,cc))),('the games never saw each other',[others(g.status()) for g in games]))
            oracle(all(pings(g.status())['on'] for g in games),('group pings on',[pings(g.status()) for g in games]))
            # Ann: B, then L 600 ms later (one signal every 500 ms). Dee: L with nobody near.
            for g,script in ((a,'key B 250\nwait 600\nkey L 250\nwait 250\n'),(d,'key L 250\nwait 250\n')):
                g.lib.mod_test_run_stamp.restype=c.c_char_p
                (g.root/'pad.script').write_text(f'pad {g.lib.mod_test_run_stamp().decode()}\n'+script)
            oracle(run(games,8,lambda:pings(b.status())['likes_given']==1 and pings(cc.status())['pings_shown']==1),
                   ('the ping and the like never arrived',[pings(g.status()) for g in games]))
            run(games,2)   # anything more that would arrive
            got={g.name:g.status() for g in games}
        finally:
            for g in games:g.close()
            server.terminate()
            try:server.wait(timeout=10)
            except subprocess.TimeoutExpired:server.kill();server.wait()
            if before is None:os.environ.pop('XDG_STATE_HOME',None)
            else:os.environ['XDG_STATE_HOME']=before
        text={}
        for name,state in states.items():
            files=logs(state,'game');oracle(len(files)==1,('one game log',name,files))
            text[name]=files[0].read_text(encoding='utf-8');verify_chain(files[0])
        p={n:pings(s) for n,s in got.items()}
        # Ann: one ping and one like sent; her own ping on her HUD, at her place (game axes).
        oracle(p['Ann']['pings_sent']==1 and p['Ann']['likes_sent']==1 and p['Ann']['pings_shown']==0,('Ann sent one of each',p['Ann']))
        oracle('PING sent at ' in text['Ann'] and 'LIKE sent to Bob' in text['Ann'],('Ann\'s log',text['Ann'][-1500:]))
        hud=p['Ann']['hud'];oracle(len(hud)==1 and [round(x) for x in hud[0]['at']]==[10,20,30] and hud[0]['id']>>48==0x5047,('her own ping on her HUD',hud))
        # Bob and Cee: her ping shown once, on the HUD at her place, with a line naming her.
        for name,line in (('Bob','Ann pinged a place here.'),('Cee','Ann pinged a place 6 m away.')):
            q=p[name];hud=q['hud']
            oracle(q['pings_shown']==1 and len(re.findall(r' PING from Ann at ',text[name]))==1,('one ping from Ann',name,q,text[name][-1500:]))
            oracle(len(hud)==1 and [round(x) for x in hud[0]['at']]==[10,20,30] and hud[0]['id']==p['Ann']['hud'][0]['id'],('her ping on his HUD',name,hud,p['Ann']['hud']))
            oracle(line in got[name]['notices_shown'],('the info log names her',name,got[name]['notices_shown']))
        # The like: Bob's (the nearest), given once; none at Cee's.
        oracle(p['Bob']['likes_given']==1 and 'LIKE received from Ann' in text['Bob'] and 'Ann sent you a like.' in got['Bob']['notices_shown'],('Bob is given her like',p['Bob'],got['Bob']['notices_shown']))
        oracle(p['Cee']['likes_given']==0 and 'LIKE received' not in text['Cee'],('Cee is not',p['Cee']))
        # Dee, far away: nothing of Ann's; his own L finds nobody.
        oracle(p['Dee']['pings_shown']==0 and p['Dee']['hud']==[] and p['Dee']['likes_sent']==0 and 'PING from' not in text['Dee'],('the far player',p['Dee']))
        oracle('LIKE not sent: nobody within 25 m' in text['Dee'] and 'Nobody is near enough to like.' in got['Dee']['notices_shown'],('nobody near Dee',text['Dee'][-800:]))

PROPERTIES=dict(pings=group_pings)
if __name__=='__main__':
    world=str(Path(sys.argv[1]).resolve())
    if len(sys.argv)==4:
        PROPERTIES[sys.argv[3]](Path(sys.argv[2]).resolve(),world)
    else:
        for name,check in PROPERTIES.items():check(ROOT/'build/fake.so',world)
        # The mutant: B in group play sends nothing.
        r=subprocess.run([sys.executable,'-B',__file__,world,str(ROOT/'build/fake-pings-group.so'),'pings'],capture_output=True,text=True,timeout=300)
        assert r.returncode==1 and 'oracle' in r.stderr,('SURVIVED fake-pings-group.so',r.returncode,r.stderr[-800:])
        print('KILLED fake-pings-group.so')
        print('Pings and likes in group play (fake world through coop-world): the ping on the near players\' HUD and info log, the like given to the nearest, nothing far')
