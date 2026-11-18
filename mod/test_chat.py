"""Text chat (chat.h, config chat) in the fake world through the real server: two copies of the real mod (fake.so) with
online_server and the notice tokens this test takes from the real coop-world. What one types (the lab's puppet.chat, the
game's telop shows it) reaches the other's info log under the player's name, and its own; Esc sends nothing; a server
whose rules say chat=off tells the player and takes no line; six lines at once: the sixth is refused and told.

  python3 test_chat.py WORLD            the test, then its mutant build
  python3 test_chat.py WORLD LIBRARY    the test against another build
"""
import ctypes as c
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import test_strand_notices as strand
from test_testmode import Game,oracle
ROOT=Path(__file__).resolve().parent
class Server(strand.Server):
    def __init__(self,world,folder,port,*rules):
        self.world,self.folder,self.port=world,Path(folder),port;self.data,self.trace=self.folder/'world.json',self.folder/'calls.log';self.process=None;self.start('--name','North',*rules)
def free_port():
    s=socket.socket();s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def game(folder,library,server,name,**extra):
    server.sign_in(name);t=server.notice_token(name);f=Path(folder)/name;f.mkdir()
    g=Game(f,library,dict(extra,nickname=name,chat=True,strand_token=t,online_server='http://127.0.0.1:%d/%s/auth/ds'%(server.port,strand.PREFIX)))
    g.lib.mod_test_telop.argtypes=[c.c_char_p,c.c_uint];g.lib.mod_test_telop.restype=c.c_uint;return g
def command(g,text):(g.root/'puppet.chat').write_text('%s %s\n'%(text,g.lib.mod_test_run_stamp().decode()))
def telop(g):
    b=c.create_string_buffer(400);g.lib.mod_test_telop(b,400);return b.value.decode()
def pump(games,seconds,until):
    for _ in range(int(seconds/.05)):
        for g in games:g.pump(.025)
        if until():return True
    return until()
shown=lambda g:g.status()['notices_shown']
chat=lambda g:g.status()['chat']
def say(g,others,word):
    command(g,'open');oracle(pump([g]+others,3,lambda:chat(g)['typing']),('not typing',chat(g)))
    command(g,'type '+word);oracle(pump([g]+others,3,lambda:telop(g)==word.replace('_',' ')+'_'),('telop',telop(g)))
    command(g,'enter');oracle(pump([g]+others,3,lambda:not chat(g)['typing']),('still typing',chat(g)))
def run(world,library):
    with tempfile.TemporaryDirectory() as folder:
        server=Server(world,folder,free_port());ann=game(folder,library,server,'Ann');bob=game(folder,library,server,'Bob');both=[ann,bob]
        try:
            oracle(pump(both,6,lambda:chat(ann)['reads'] and chat(bob)['reads'] and chat(ann)['on']),('no reads',chat(ann)))
            say(ann,[bob],'hello_there');line='Ann: hello there'
            oracle(pump(both,8,lambda:line in shown(bob) and line in shown(ann)),('line not told',shown(bob),shown(ann)))
            oracle(telop(ann)=='',('telop left up',telop(ann)))
            command(bob,'open');pump(both,2,lambda:chat(bob)['typing']);command(bob,'type never');pump(both,1,lambda:telop(bob)=='never_');command(bob,'esc')
            oracle(pump(both,3,lambda:not chat(bob)['typing']),'esc');pump(both,3,lambda:False);oracle(not any('never' in l for l in shown(ann)),shown(ann))
            for i in range(6):say(ann,[bob],'m%d'%i)
            oracle(pump(both,10,lambda:chat(ann)['refused']>=1 and any('too many' in l for l in shown(ann))),('sixth line',chat(ann),shown(ann)))
        finally:
            for g in both:g.close()
            server.stop()
    with tempfile.TemporaryDirectory() as folder:
        server=Server(world,folder,free_port(),'--policy-set','chat=off');ann=game(folder,library,server,'Ann')
        try:
            oracle(pump([ann],6,lambda:chat(ann)['reads']>0),'no read');oracle(not chat(ann)['on'],chat(ann))
            command(ann,'open');oracle(pump([ann],6,lambda:'Text chat is off on this server.' in shown(ann)),shown(ann));oracle(not chat(ann)['typing'],chat(ann))
        finally:ann.close();server.stop()
def keys(world,library):
    """While typing, the letters are not the mod's keys: U drops no crates; after it, U does."""
    with tempfile.TemporaryDirectory() as folder:
        server=Server(world,folder,free_port());ann=game(folder,library,server,'Ann',test_mode={})
        try:
            oracle(pump([ann],6,lambda:chat(ann)['reads']>0),'no read');sets=lambda:ann.status()['test_mode']['sets']
            command(ann,'open');oracle(pump([ann],3,lambda:chat(ann)['typing']),'not typing')
            ann.script('key U 200\nwait 300\n');pump([ann],1.5,lambda:False);oracle(sets()==0,('U acted while typing',sets()))
            command(ann,'esc');oracle(pump([ann],3,lambda:not chat(ann)['typing']),'esc')
            pump([ann],1.2,lambda:False);ann.script('key U 200\nwait 300\n');oracle(pump([ann],3,lambda:sets()==1),('U after typing',sets()))
        finally:ann.close();server.stop()
if __name__=='__main__':
    world=str(Path(sys.argv[1]).resolve())
    if len(sys.argv)==3:keys(world,Path(sys.argv[2]).resolve());run(world,Path(sys.argv[2]).resolve())
    else:
        keys(world,ROOT/'build/fake.so');run(world,ROOT/'build/fake.so')
        r=subprocess.run([sys.executable,'-B',__file__,world,str(ROOT/'build/fake-chat-keys.so')],capture_output=True,text=True,timeout=200)
        assert r.returncode==1 and 'test-mode oracle' in r.stderr,('SURVIVED fake-chat-keys.so',r.returncode,r.stderr[-500:])
        print('text chat (fake world): a line typed reaches both info logs under its name, Esc sends nothing, the rate limit and a server with chat off are told')
