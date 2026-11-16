"""Other players in the relay's broadcast mode (config relay_mode "broadcast", crowd.h), in the fake world through the
real server: three copies of the real mod (fake.so: fake game memory; the relay's datagrams handed over by this test
instead of a socket) in group configs, each with a UDP socket of this test's own to the real coop-world, and stand-ins
from coop-swarm --mode broadcast around them. Checked from each game's status: bodies made and shown, the names over
them (the stand-ins' too), gear and carried cases with others_gear and others_carry and none without, one player
leaving and coming back, the stand-ins leaving, and that a game's traffic does not grow with the players near it.

  python3 test_others_broadcast.py WORLD SWARM                     the test, then its mutant build
  python3 test_others_broadcast.py WORLD SWARM LIBRARY PROPERTY    one property (crowd) against another build
"""
import ctypes as c
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parent
PIN='9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b'
KEY='test'
class Context(c.Structure):
    _fields_=[('size',c.c_uint32),('version',c.c_uint32),('thread_id',c.c_uint32),('depth',c.c_uint32),
              ('caller_rva',c.c_uint64),('network_enforced',c.c_uint32),('reserved',c.c_uint32)]
def oracle(ok,what):
    if not ok:raise AssertionError('others-broadcast oracle: '+str(what))
def free_port(kind=socket.SOCK_STREAM):
    s=socket.socket(socket.AF_INET,kind);s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p

class Game:
    """One copy of the mod in its own folder, in a group config (no partner) with the relay in broadcast mode. The test
    is its relay socket: what the client sends goes out of `sock`, what arrives there is handed back."""
    def __init__(self,folder,library,name,position,player,slot,relay,extra=None):
        self.name,self.player,self.root=name,player,Path(folder)/name
        self.root.mkdir();shutil.copyfile(library,self.root/'mod.so')
        lib=self.lib=c.CDLL(str(self.root/'mod.so'))
        lib.mod_init.argtypes=[c.c_char_p];lib.mod_game_thread_tick.argtypes=[c.POINTER(Context)]
        lib.mod_status.argtypes=[c.c_void_p,c.c_uint];lib.mod_last_error.restype=c.c_char_p
        lib.mod_test_thread_id.restype=c.c_uint32;lib.mod_test_frame_caller.restype=c.c_uint64
        lib.mod_test_set_pose.argtypes=[c.POINTER(c.c_double),c.POINTER(c.c_float),c.c_int]
        lib.mod_test_relay_deliver.argtypes=[c.c_char_p,c.c_uint]
        lib.mod_test_relay_drain.argtypes=[c.c_void_p,c.c_uint];lib.mod_test_relay_drain.restype=c.c_uint
        lib.mod_test_set_gear.argtypes=[c.c_uint];lib.mod_test_set_carry.argtypes=[c.c_char_p]
        config=dict(slot=slot,server='127.0.0.1',nickname=name,provider='game',session=46,required=True,game_thread=True,puppet_probe=True,
                    discovery_sha256=PIN,partner_tag=True,others=8,relay='127.0.0.1:%d'%relay,relay_key=KEY,player_id=player,relay_mode='broadcast')
        config.update(extra or {})
        (self.root/'mod.json').write_text(json.dumps(config))
        oracle(lib.mod_init(str(self.root/'mod.json').encode())==1,lib.mod_last_error())
        self.ctx=Context(c.sizeof(Context),1,lib.mod_test_thread_id(),1,lib.mod_test_frame_caller(),1,0)
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);self.sock.connect(('127.0.0.1',relay));self.sock.setblocking(False)
        self.place(position)
    def place(self,position):
        self.position=position
        self.lib.mod_test_set_pose((c.c_double*3)(*position),(c.c_float*9)(1,0,0,0,1,0,0,0,1),0)
    def core(self):return (self.position[0],self.position[2],self.position[1])   # the wire's axes: x, y up, z
    def pump(self):
        oracle(self.lib.mod_game_thread_tick(c.byref(self.ctx))==1,self.lib.mod_last_error())
        raw=c.create_string_buffer(2048)
        while True:
            n=self.lib.mod_test_relay_drain(raw,2048)
            if not n:break
            self.sock.send(raw.raw[:n])
        while True:
            try:d=self.sock.recv(2048)
            except BlockingIOError:break
            self.lib.mod_test_relay_deliver(d,len(d))
    def status(self):
        cap=self.lib.mod_status(None,0)
        for _ in range(8):
            out=c.create_string_buffer(cap);required=self.lib.mod_status(out,cap)
            if required<=cap:return json.loads(out.value)
            cap=required*2
        raise AssertionError('status kept growing')
    def wear(self,shown):self.lib.mod_test_set_gear(shown)                    # bit 0: the backpack, bit 1: the wrist cuff
    def carry(self,spec):oracle(self.lib.mod_test_set_carry(spec.encode())==1,('carry spec',spec))
    def log(self):return next(self.root.glob('mod-*.log')).read_text()
    def close(self):self.lib.mod_shutdown();self.sock.close()

def run(games,seconds,until=None):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        for g in games:g.pump()
        if until and until():return True
        time.sleep(.01)
    return until is None
def labels(s):return sorted(s['label_names'])
def others(s):return s['others']

def crowd(library,world,swarm):
    """Ann and Bob show gear and cases, Cee has neither setting; three stand-ins walk a circle around them."""
    port,relay=free_port(),free_port(socket.SOCK_DGRAM)
    server=subprocess.Popen([world,'--port',str(port),'--relay-port',str(relay),'--relay-key',KEY],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    games=[];stand=None
    with tempfile.TemporaryDirectory() as folder:
        stop=Path(folder)/'stop'
        try:
            oracle('listening' in server.stdout.readline(),'coop-world did not start')
            show=dict(others_gear=True,others_carry=2)
            a=Game(folder,library,'Ann',(10,20,30),1,0,relay,show)
            b=Game(folder,library,'Bob',(13,20,30),2,1,relay,show)
            cc=Game(folder,library,'Cee',(10,26,30),3,2,relay)
            games=[a,b,cc]
            a.wear(2);b.wear(1);cc.wear(3)                                     # Ann the cuff, Bob the backpack (the cases hang on it), Cee both
            b.carry('501:1:3');cc.carry('503:1:4')
            oracle(all(others(g.status())['mode']=='broadcast' and others(g.status())['on'] for g in games),'not in broadcast mode')
            # The three games find each other: two bodies each, shown, named.
            oracle(run(games,15,lambda:all(others(g.status())['bodies']==2 and others(g.status())['shown']==2 for g in games) and labels(a.status())==['Bob','Cee']),
                   ('the games never saw each other',[others(g.status()) for g in games],a.status()['label_names'],a.log()[-1500:]))
            oracle(labels(b.status())==['Ann','Cee'] and labels(cc.status())==['Ann','Bob'],('names',b.status()['label_names'],cc.status()['label_names']))
            # Each label over its own body (2.1 m above the feet).
            at={n['name']:n['at'] for n in a.status()['labels']}
            for g in (b,cc):oracle(abs(at[g.name][0]-g.position[0])<1 and abs(at[g.name][1]-g.position[1])<1 and abs(at[g.name][2]-g.position[2]-2.1)<.5,('label not over its body',g.name,at[g.name],g.position))
            # Three stand-ins on a 9 m circle around Ann, in the same mode.
            x,y,z=a.core()
            stand=subprocess.Popen([swarm,'--relay','127.0.0.1:%d'%relay,'--key',KEY,'--mode','broadcast','--count','3','--centre',str(x),str(y),str(z),'--radius','9',
                                    '--speed','0','--first-id','1001','--seconds','120','--stop',str(stop)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            sams=['Sam 1001','Sam 1002','Sam 1003']
            # Five others for Ann; the name labels go to the four nearest (Bob 3 m, Cee 6 m, two of the stand-ins at 9 m),
            # with the names the stand-ins posted.
            oracle(run(games,15,lambda:others(a.status())['bodies']==5 and others(a.status())['shown']==5 and len(labels(a.status()))==4 and not 'Porter' in labels(a.status())),
                   ('the stand-ins never shown',others(a.status()),a.status()['label_names'],a.log()[-1500:]))
            l=labels(a.status());oracle('Bob' in l and 'Cee' in l and len([n for n in l if n in sams])==2,('labels',l))
            oracle(run(games,5,lambda:others(cc.status())['bodies']==5),('Cee does not see everybody',others(cc.status())))
            # Gear and cases: Bob's cuff and his three cases on his body; Cee sends none and takes none (dropped).
            def dressed():
                s=a.status();by=s['gear_shown']['by']
                return by.get('2')==1 and all(v==0 for k,v in by.items() if k!='2') and s['cases_shown']['by']=={'2':3}
            oracle(run(games,25,dressed),('gear or cases never as sent',a.status()['gear_shown'],a.status()['cases_shown'],a.log()[-2500:]))
            oracle(run(games,10,lambda:b.status()['gear_shown']['by'].get('1')==1),('Bob never dressed Ann',b.status()['gear_shown']))
            sc=cc.status();oracle(sc['gear_shown']['pieces']==0 and sc['cases_shown']['cases']==0 and sc['gear_shown']['sets']==0 and sc['cases_shown']['sets']==0
                                  and sc['cases_shown']['dropped_events']>0,('Cee took something without the settings',sc['gear_shown'],sc['cases_shown']))
            s=a.status();o=others(s)
            oracle(o['refused']==0 and o['malformed']==0 and s['pieces_orphaned']==0,('Ann refused something',o))
            # One client, whoever is near: Ann's datagrams to the server do not grow with five players (pairs: a session each).
            before=o['sent'];began=time.monotonic();run(games,2);rate=(others(a.status())['sent']-before)/(time.monotonic()-began)
            oracle(rate<40,('Ann sends too much for one client',rate))
            # Bob walks away (beyond the server's 200 m): his body, gear, cases and label go; then he comes back.
            b.place((13,20,1000))
            oracle(run(games,10,lambda:others(a.status())['bodies']==4 and 'Bob' not in labels(a.status()) and a.status()['cases_shown']['by']=={} and '2' not in a.status()['gear_shown']['by']),
                   ('Bob never went',others(a.status()),a.status()['label_names'],a.status()['gear_shown'],a.status()['cases_shown']))
            oracle(a.status()['pieces_orphaned']==0,'a body was removed with gear copies on it')
            b.place((13,20,30))
            oracle(run(games,20,lambda:others(a.status())['bodies']==5 and 'Bob' in labels(a.status()) and dressed()),
                   ('Bob came back without his gear or cases',others(a.status()),a.status()['label_names'],a.status()['gear_shown'],a.status()['cases_shown']))
            # The stand-ins stop (BYE): their bodies go; the two games stay.
            stop.write_text('stop');out=stand.communicate(timeout=30)[0];stand=None
            oracle('SWARM done mode=broadcast online=3/3' in out,('the stand-ins',out[-600:]))
            oracle(run(games,10,lambda:others(a.status())['bodies']==2 and labels(a.status())==['Bob','Cee']),('the stand-ins never left',others(a.status()),a.status()['label_names']))
            log=a.log();oracle('OTHERS ready (broadcast)' in log and 'OTHERS player 2 left' in log and log.count('OTHERS player 2 is near')==2,'the log of who came and went')
            oracle(a.status()['pieces_orphaned']==0,'a body was removed with gear copies on it')
        finally:
            if stand:stand.kill();stand.wait()
            for g in games:g.close()
            server.kill();server.wait()

PROPERTIES=dict(crowd=crowd)
if __name__=='__main__':
    world,swarm=str(Path(sys.argv[1]).resolve()),str(Path(sys.argv[2]).resolve())
    if len(sys.argv)==5:
        PROPERTIES[sys.argv[4]](Path(sys.argv[3]).resolve(),world,swarm)
    else:
        for name,check in PROPERTIES.items():check(ROOT/'build/fake.so',world,swarm)
        # The mutant: relay_mode "broadcast" ignored (the pairs mode runs): nobody near is ever shown.
        r=subprocess.run([sys.executable,'-B',__file__,world,swarm,str(ROOT/'build/fake-relay-mode.so'),'crowd'],capture_output=True,text=True,timeout=300)
        assert r.returncode==1 and 'others-broadcast oracle' in r.stderr,('SURVIVED fake-relay-mode.so',r.returncode,r.stderr[-800:])
        print('KILLED fake-relay-mode.so')
        print('others in broadcast mode (fake world through coop-world): names, gear and cases per player, the stand-ins, leaving and coming back passed')
