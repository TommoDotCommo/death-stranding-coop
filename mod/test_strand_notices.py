"""Strand notices (strand_notices.h) in the fake world through the real server: the real mod (fake.so: fake game memory, the
game's info log recorded) in a group config with online_server, strand_notices and the notice token this test takes
from the real coop-world, while the test uploads as the player's game would. A server named North shares members'
builds only. Checked from the game's status (the lines its info log took, in order): nothing for what was there before
the game started; a held ladder told once, in the right words; a held cairn told once; nothing for another player's
upload; after the owner made the player a member, accepted the ladder and started the server again, "now shared" for the
ladder and nothing again for the cairn; nothing for an upload that is shared at once. Without strand_notices: no line
and no read of the server at all. With offer_strand alone (partner notices on): the server is read with the token for
its word on the player's older strand (yes, then no after his choice changes in the launcher's API), and nothing about
his builds is told.

  python3 test_strand_notices.py WORLD                       the test, then its three mutant builds side by side
  python3 test_strand_notices.py WORLD LIBRARY PROPERTY      one property against another build
"""
import ctypes as c
import http.client
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import mutants

ROOT=Path(__file__).resolve().parent
PIN='9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b'
PREFIX='w46'
HELD_LADDER="Your ladder is not shared on North: only members' builds are shared here."
HELD_CAIRN="Your cairn is not shared on North: only members' builds are shared here."
SHARED_LADDER='Your ladder is now shared on North.'
class Context(c.Structure):
    _fields_=[('size',c.c_uint32),('version',c.c_uint32),('thread_id',c.c_uint32),('depth',c.c_uint32),
              ('caller_rva',c.c_uint64),('network_enforced',c.c_uint32),('reserved',c.c_uint32)]
def oracle(ok,what):
    if not ok:raise AssertionError('strand-notices oracle: '+str(what))
def free_port():
    s=socket.socket();s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p

class Server:
    """The real coop-world on loopback, its world in a file; the test is the players' games and their launchers."""
    def __init__(self,world,folder,port):
        self.world,self.folder,self.port=world,Path(folder),port
        self.data,self.trace=self.folder/'world.json',self.folder/'calls.log'
        self.process=None;self.start('--name','North','--policy-set','build=members')
    def start(self,*more):
        self.process=subprocess.Popen([self.world,'--port',str(self.port),'--prefix',PREFIX,'--data',str(self.data),'--trace',str(self.trace),*more],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        for _ in range(4):
            if 'listening' in self.process.stdout.readline():return
        raise AssertionError('coop-world did not start')
    def stop(self):self.process.kill();self.process.wait()
    def command(self,*args):
        r=subprocess.run([self.world,*args,'--data',str(self.data)],capture_output=True,text=True,timeout=30);oracle(r.returncode==0,(args,r.stdout,r.stderr))
    def call(self,method,path,body=None,headers=None):
        h=http.client.HTTPConnection('127.0.0.1',self.port,timeout=5)
        try:
            h.request(method,'/'+PREFIX+path,body=json.dumps(body) if body is not None else None,headers=headers or {});r=h.getresponse();return r.status,json.loads(r.read() or b'{}')
        finally:h.close()
    def sign_in(self,name):
        status,auth=self.call('GET','/k/%s/auth/ds?provider=steam&display_name=%s&code=00'%(name,name));oracle(status==200,auth);return auth['session']['token'],auth['user']['id']
    def notice_token(self,name):
        status,given=self.call('POST','/coop/notice-token',{'player':name});oracle(status==200,given);return given['token']
    def upload(self,session,kind,x):
        status,made=self.call('POST','/gate/e/createObject',{'t':kind,'q':226,'px':x,'py':2,'pz':3},{'Authorization':'Bearer '+session});oracle(status==200,made);return made['id']
    def reads(self):return sum('/coop/notices' in row for row in self.trace.read_text().splitlines()) if self.trace.exists() else 0

class Game:
    """One copy of the mod in its own folder, in a group config with the server's address as online_server."""
    def __init__(self,folder,library,name,port,extra):
        self.root=Path(folder)/name;self.root.mkdir();shutil.copyfile(library,self.root/'mod.so')
        lib=self.lib=c.CDLL(str(self.root/'mod.so'))
        lib.mod_init.argtypes=[c.c_char_p];lib.mod_game_thread_tick.argtypes=[c.POINTER(Context)]
        lib.mod_status.argtypes=[c.c_void_p,c.c_uint];lib.mod_last_error.restype=c.c_char_p
        lib.mod_test_thread_id.restype=c.c_uint32;lib.mod_test_frame_caller.restype=c.c_uint64
        lib.mod_test_set_pose.argtypes=[c.POINTER(c.c_double),c.POINTER(c.c_float),c.c_int]
        config=dict(slot=0,server='127.0.0.1',nickname=name,provider='game',session=46,required=True,game_thread=True,discovery_sha256=PIN,
                    online_server='http://127.0.0.1:%d/%s/k/%s/auth/ds'%(port,PREFIX,name.lower()))
        config.update(extra)
        (self.root/'mod.json').write_text(json.dumps(config))
        oracle(lib.mod_init(str(self.root/'mod.json').encode())==1,lib.mod_last_error())
        self.ctx=Context(c.sizeof(Context),1,lib.mod_test_thread_id(),1,lib.mod_test_frame_caller(),1,0)
        lib.mod_test_set_pose((c.c_double*3)(10,20,30),(c.c_float*9)(1,0,0,0,1,0,0,0,1),0)   # in the world
    def pump(self,seconds,until=None):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            oracle(self.lib.mod_game_thread_tick(c.byref(self.ctx))==1,self.lib.mod_last_error())
            if until and until():return True
            time.sleep(.02)
        return until is None
    def status(self):
        cap=self.lib.mod_status(None,0)
        for _ in range(8):
            out=c.create_string_buffer(cap);required=self.lib.mod_status(out,cap)
            if required<=cap:return json.loads(out.value)
            cap=required*2
        raise AssertionError('status kept growing')
    def shown(self):return self.status()['notices_shown']
    def log(self):return next(self.root.glob('mod-*.log')).read_text()
    def close(self):self.lib.mod_shutdown()

def told(library,world):
    """Ann's game with strand_notices and no partner notices (group play)."""
    with tempfile.TemporaryDirectory() as folder:
        port=free_port();server=Server(world,folder,port);game=None
        try:
            ann,_=server.sign_in('ann');bob,_=server.sign_in('bob')
            server.upload(ann,'l',1)                                                 # built before this game started
            game=Game(folder,library,'Ann',port,dict(strand_notices=True,strand_token=server.notice_token('ann')))
            oracle(game.pump(10,lambda:game.status()['strand']['answers']>=1),('no first answer',game.status()['strand'],game.log()[-800:]))
            oracle(game.shown()==[],('told what was there before',game.shown()))
            ladder=server.upload(ann,'l',30)
            oracle(game.pump(12,lambda:len(game.shown())>=1),('the held ladder was never told',game.status()['strand'],game.log()[-800:]))
            server.upload(bob,'l',60)                                                # another player's: never his business
            server.upload(ann,'e',90)
            oracle(game.pump(12,lambda:len(game.shown())>=2),('the held cairn was never told',game.shown()))
            game.pump(11)
            oracle(game.shown()==[HELD_LADDER,HELD_CAIRN],('held: once each, in these words',game.shown()))
            # The owner makes Ann a member and accepts her new ladder (not the cairn), with the server stopped; it starts again.
            server.stop()
            game.pump(3)
            players=json.loads(server.data.read_text())['players'];ann_id=next(p['id'] for p in players if p['key']=='ann')
            server.command('player-set','--player',ann_id,'member=yes');server.command('review','--accept',ladder)
            server.start()
            oracle(game.pump(40,lambda:len(game.shown())>=3),('"now shared" never came',game.status()['strand'],game.shown(),game.log()[-800:]))
            server.upload(ann,'l',120)                                               # a member's upload: shared at once
            game.pump(12)
            oracle(game.shown()==[HELD_LADDER,HELD_CAIRN,SHARED_LADDER],('after the restart: the ladder now shared, nothing else',game.shown()))
            s=game.status()['strand'];oracle(s['on'] and not s['refused'] and s['known']==4 and s['told']==3,s)
            log=game.log();oracle('STRAND notices on' in log and 'STRAND first answer: 1 objects' in log and log.count('NOTICE shown: strand')==3,log[-1200:])
        finally:
            if game:game.close()
            server.stop()

def off(library,world):
    """Without strand_notices (partner notices on, group play): no line, and the server is never read."""
    with tempfile.TemporaryDirectory() as folder:
        port=free_port();server=Server(world,folder,port);game=None
        try:
            ann,_=server.sign_in('ann')
            game=Game(folder,library,'Ann',port,dict(notices=True))
            game.pump(2);server.upload(ann,'l',30);game.pump(12)
            oracle(game.shown()==[] and not game.status()['strand']['on'],('told without strand_notices',game.shown(),game.status()['strand']))
            oracle(server.reads()==0,('the server was read without strand_notices',server.trace.read_text()[-600:]))
        finally:
            if game:game.close()
            server.stop()

def offered(library,world):
    """offer_strand without strand_notices (partner notices on): the server's word on his older strand is read, nothing told."""
    with tempfile.TemporaryDirectory() as folder:
        port=free_port();server=Server(world,folder,port);game=None
        try:
            ann,_=server.sign_in('ann')
            status,me=server.call('POST','/coop/choices',{'player':'ann','offer_old':'yes'});oracle(status==200 and me['applies']['offer_old']=='yes',me)
            game=Game(folder,library,'Ann',port,dict(notices=True,offer_strand=True,strand_token=server.notice_token('ann')))
            oracle(game.pump(10,lambda:game.status()['strand']['offer_old']=='yes'),('the server\'s yes was not read',game.status()['strand'],game.log()[-800:]))
            server.upload(ann,'l',30)                                                # held for her: strand_notices would tell it
            status,me=server.call('POST','/coop/choices',{'player':'ann','offer_old':'no'});oracle(status==200,me)
            oracle(game.pump(12,lambda:game.status()['strand']['offer_old']=='no'),('her no was not read',game.status()['strand']))
            game.pump(6)
            oracle(game.shown()==[],('told without strand_notices',game.shown()))
            s=game.status();oracle(s['strand']['on'] and s['strand']['answers']>=2 and not s['offer_strand']['on'] and s['offer_strand']['asked']==0,s['strand'])
            log=game.log();oracle('STRAND read on' in log and 'STRAND notices on' not in log and 'NOTICE shown: strand' not in log,log[-1200:])
        finally:
            if game:game.close()
            server.stop()

PROPERTIES=dict(told=told,off=off,offered=offered)
MUTANTS=[('fake-strand-once.so','told'),('fake-strand-partner.so','told'),('fake-strand-off.so','off'),('fake-offer-lines.so','offered')]
if __name__=='__main__':
    world=str(Path(sys.argv[1]).resolve())
    if len(sys.argv)==4:
        PROPERTIES[sys.argv[3]](Path(sys.argv[2]).resolve(),world)
    else:
        for name,check in PROPERTIES.items():check(ROOT/'build/fake.so',world)
        def one(job):
            build,prop=job
            r=subprocess.run([sys.executable,'-B',__file__,world,str(ROOT/'build'/build),prop],capture_output=True,text=True,timeout=300)
            assert r.returncode==1 and 'strand-notices oracle' in r.stderr,('SURVIVED '+build,r.returncode,r.stderr[-800:])
            return build
        for build in mutants.parallel(one,MUTANTS):print('KILLED '+build)
        print('strand notices (fake world through coop-world): held told once in words, now shared after a restart, nothing for shared or others, nothing when off, the server word on the older strand read and nothing told for offer_strand alone passed')
