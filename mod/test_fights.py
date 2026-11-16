"""Fights with visitors (config host_fights, join_fights; fights.h) in the fake world through the real server: copies of
the real mod (fake.so: fake game memory, a fake catcher manager and gazers, mod_test_bts_*; the relay's datagrams
handed over by this test) in group configs in broadcast mode, each with a UDP socket of this test's own to the real
coop-world, started with --fight-visitors 2. Checked from each game's status: a catcher fight in the owner's game is
offered and joined by two visitors, each visitor's game shows it with a fight of its own at the owner's life, a
visitor's hit and the owner's own come off one health bar on the owner's terms, a gazer a visitor destroys goes in the
owner's world and in nobody else's, the cap leaves a third player out, a game with a fight of its own and a game without
the settings take no part, a visitor never announces the fight it shows, and when the owner's game goes away the
visitors are released with nothing more written in their worlds (no end command, their gazers and cleared spheres as
they were).

  python3 test_fights.py WORLD                     the test, then its mutant builds
  python3 test_fights.py WORLD LIBRARY PROPERTY    one property (visit) against another build
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
    if not ok:raise AssertionError('fights oracle: '+str(what))
def free_port(kind=socket.SOCK_STREAM):
    s=socket.socket(socket.AF_INET,kind);s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p

class Game:
    """One copy of the mod in a group config in broadcast mode; the test is its relay socket."""
    def __init__(self,folder,library,name,position,player,slot,relay,extra=None):
        self.name,self.player,self.root,self.closed=name,player,Path(folder)/name,False
        self.root.mkdir();shutil.copyfile(library,self.root/'mod.so')
        lib=self.lib=c.CDLL(str(self.root/'mod.so'))
        lib.mod_init.argtypes=[c.c_char_p];lib.mod_game_thread_tick.argtypes=[c.POINTER(Context)]
        lib.mod_status.argtypes=[c.c_void_p,c.c_uint];lib.mod_last_error.restype=c.c_char_p
        lib.mod_test_thread_id.restype=c.c_uint32;lib.mod_test_frame_caller.restype=c.c_uint64
        lib.mod_test_set_pose.argtypes=[c.POINTER(c.c_double),c.POINTER(c.c_float),c.c_int]
        lib.mod_test_relay_deliver.argtypes=[c.c_char_p,c.c_uint]
        lib.mod_test_relay_drain.argtypes=[c.c_void_p,c.c_uint];lib.mod_test_relay_drain.restype=c.c_uint
        lib.mod_test_bts_start.argtypes=[c.c_uint];lib.mod_test_bts_hit.argtypes=[c.c_uint,c.c_float];lib.mod_test_bts_end.argtypes=[c.c_int]
        lib.mod_test_bts_gazer.argtypes=[c.c_uint64,c.c_float];lib.mod_test_bts_kill.argtypes=[c.c_uint64]
        lib.mod_test_bts_gazer_life.argtypes=[c.c_uint64];lib.mod_test_bts_gazer_life.restype=c.c_float
        config=dict(slot=slot,server='127.0.0.1',nickname=name,provider='game',session=46,required=True,game_thread=True,puppet_probe=True,
                    discovery_sha256=PIN,others=8,relay='127.0.0.1:%d'%relay,relay_key=KEY,player_id=player,relay_mode='broadcast')
        config.update(extra or {})
        (self.root/'mod.json').write_text(json.dumps(config))
        oracle(lib.mod_init(str(self.root/'mod.json').encode())==1,lib.mod_last_error())
        self.ctx=Context(c.sizeof(Context),1,lib.mod_test_thread_id(),1,lib.mod_test_frame_caller(),1,0)
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);self.sock.connect(('127.0.0.1',relay));self.sock.setblocking(False)
        lib.mod_test_set_pose((c.c_double*3)(*position),(c.c_float*9)(1,0,0,0,1,0,0,0,1),0)
        for k in (0x501,0x502):lib.mod_test_bts_gazer(k,300)   # the same gazers in every world
    def pump(self):
        if self.closed:return
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
    def fights(self):return self.status()['fights']
    def bts(self):return self.status()['fake_bts']
    def life(self):l=self.bts()['lives'];return l[0] if l else None
    def log(self):return next(self.root.glob('mod-*.log')).read_text()
    def close(self):
        if not self.closed:self.closed=True;self.lib.mod_shutdown();self.sock.close()

def run(games,seconds,until=None):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        for g in games:g.pump()
        if until and until():return True
        time.sleep(.01)
    return until is None
def near(a,b):return a is not None and abs(a-b)<=.06

def visit(library,world):
    port,relay=free_port(),free_port(socket.SOCK_DGRAM)
    server=subprocess.Popen([world,'--port',str(port),'--relay-port',str(relay),'--relay-key',KEY,'--fight-visitors','2'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    games=[]
    with tempfile.TemporaryDirectory() as folder:
        try:
            oracle('listening' in server.stdout.readline(),'coop-world did not start')
            both=dict(host_fights=True,join_fights=True)
            o=Game(folder,library,'Ona',(0,0,0),1,0,relay,dict(host_fights=True))
            a=Game(folder,library,'Abe',(6,0,0),2,1,relay,both)          # a visitor that also hosts its own fights
            b=Game(folder,library,'Bea',(0,8,0),3,2,relay,dict(join_fights=True))
            own=Game(folder,library,'Cy',(9,9,0),4,3,relay,dict(join_fights=True))
            off=Game(folder,library,'Dot',(3,3,0),5,4,relay)
            games=[o,a,b,own,off]
            own.lib.mod_test_bts_start(1);own.lib.mod_test_bts_hit(0,25)   # Cy's game runs a fight of its own
            oracle(run(games,15,lambda:all(g.status()['others']['bodies']==4 for g in games)),('the games never saw each other',[g.status()['others'] for g in games]))
            oracle(all(g.fights()['on'] for g in (o,a,b,own)) and not off.fights()['on'],('fights not ready',[g.fights() for g in games]))
            # A catcher fight starts in Ona's game: offered, joined by Abe and Bea; each shows it with a fight of its own.
            o.lib.mod_test_bts_start(1)
            oracle(run(games,15,lambda:all(g.fights()['joined'] for g in (a,b)) and all(g.bts()['active'] for g in (a,b))),
                   ('the visitors never joined',o.fights(),a.fights(),b.fights(),a.log()[-1500:]))
            oracle(o.fights()['role']=='owner' and a.fights()['role']=='visitor' and b.fights()['role']=='visitor',('roles',o.fights(),a.fights(),b.fights()))
            oracle(run(games,5,lambda:o.fights()['visitors']==2),('the owner never heard of two visitors',o.fights()))
            for g in (a,b):oracle(g.bts()['commands']==[0] and near(g.life(),100),('the shown fight',g.name,g.bts()))
            # Abe hosts fights too, and never announces the one he shows.
            run(games,1.5)
            oracle(a.fights()['hosted']==0 and a.fights()['role']=='visitor' and a.fights()['joined'],('a visitor announced the fight it shows',a.fights()))
            # One health bar on the owner's terms.
            a.lib.mod_test_bts_hit(0,10)
            oracle(run(games,10,lambda:near(o.life(),90) and near(b.life(),90) and near(a.life(),90)),('a visitor\'s hit',o.bts(),a.bts(),b.bts(),o.fights(),a.fights()))
            o.lib.mod_test_bts_hit(0,20)
            oracle(run(games,10,lambda:near(o.life(),70) and near(b.life(),70) and near(a.life(),70)),('the owner\'s hit',o.bts(),a.bts(),b.bts()))
            # A gazer Abe destroys in his world: destroyed in Ona's, in nobody else's.
            a.lib.mod_test_bts_kill(0x501)
            oracle(run(games,10,lambda:o.lib.mod_test_bts_gazer_life(0x501)==0),('the owner never destroyed the visitor\'s gazer',o.bts(),o.fights()))
            oracle(o.bts()['destroyed']==[0x501] and all(g.lib.mod_test_bts_gazer_life(0x501)==300 for g in (b,own,off)),('gazers',[g.bts() for g in games]))
            # Reports reach the owner only; the owner's state the visitors only.
            oracle(o.fights()['reports_taken']>0 and a.fights()['reports_taken']==0 and b.fights()['reports_taken']==0,('reports',o.fights(),a.fights(),b.fights()))
            oracle(o.fights()['states_taken']==0 and own.fights()['states_taken']==0,('states',o.fights(),own.fights()))
            # Cy's own fight comes first; Dot has no settings: neither takes part, nothing is written in their worlds.
            for g in (own,off):oracle(g.bts()['commands']==[] and g.bts()['writes']==0,('a world that took no part',g.name,g.bts()))
            oracle(own.fights()['own_first']>=1 and own.fights()['role']=='none' and near(own.life(),75),('own first',own.fights(),own.bts()))
            # Cy's own fight ends: offered again, but the fight has two visitors, the server's cap: Cy stays out.
            own.lib.mod_test_bts_end(1)
            run(games,4)
            oracle(own.fights()['role']=='none' and own.fights()['visits']==0 and own.bts()['commands']==[],('the cap',own.fights(),own.bts()))
            # Ona's game goes away: both visitors are released; nothing more is written in their worlds.
            writes={g.name:g.bts()['writes'] for g in (a,b)}
            o.close();games=[a,b,own,off]
            oracle(run(games,10,lambda:all(g.fights()['role']=='none' for g in (a,b))),('the visitors never released',a.fights(),b.fights()))
            a.lib.mod_test_bts_hit(0,5)
            run(games,2)
            for g in (a,b):
                x=g.bts()
                oracle(x['commands']==[0] and x['active'] and x['cleared']==0 and x['unkillable']==0 and x['destroyed']==[],('a released visitor\'s world',g.name,x))
                oracle(x['writes']==writes[g.name],('written after the release',g.name,x['writes'],writes[g.name]))
            oracle(near(a.life(),65) and b.lib.mod_test_bts_gazer_life(0x501)==300,('his own fight now',a.bts(),b.bts()))
            log=a.log();oracle('FIGHT joining the fight of player 1' in log and 'FIGHT the visit ends' in log,'the log of the visit')
        finally:
            for g in games:g.close()
            server.kill();server.wait()

PROPERTIES=dict(visit=visit)
MUTANTS=['fake-fight-visitor-hosts.so','fake-fight-gone-clears.so','fake-fight-settings.so']
if __name__=='__main__':
    world=str(Path(sys.argv[1]).resolve())
    if len(sys.argv)==4:
        PROPERTIES[sys.argv[3]](Path(sys.argv[2]).resolve(),world)
    else:
        for name,check in PROPERTIES.items():check(ROOT/'build/fake.so',world)
        # The mutants: a visitor that announces the fight it shows; the shown fight ended (a cleared sphere written) when
        # the owner goes; join_fights ignored (a game without it joins).
        for m in MUTANTS:
            r=subprocess.run([sys.executable,'-B',__file__,world,str(ROOT/'build'/m),'visit'],capture_output=True,text=True,timeout=300)
            assert r.returncode==1 and 'fights oracle' in r.stderr,('SURVIVED '+m,r.returncode,r.stderr[-800:])
            print('KILLED '+m)
        print('fights with visitors (fake world through coop-world): offered, joined to the cap, one health bar, gazers, own fights first, the owner leaving passed')
