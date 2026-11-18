"""Test mode (testmode.h, config test_mode) in the fake world: the real mod (fake.so: fake game memory, the game's info log
and its crate requests recorded) in a group config with test_mode on.

- gauges: blood and stamina in the porter's status block (fake memory) are set to their maximum on the game thread; not
  while a game event runs, not while he is down or caught; again once that is over. Without test_mode nothing is written.
  The info log says once that test mode is on.
- materials: the key (U, through the virtual pad) asks the game for a set of crates (the crates known for the build) two
  metres behind the porter, and the info log says so; a second press within 5 s asks for nothing; one after 5 s does.
The cap of 20 crates on the ground is the unit test's (testmode_test.cpp): a set a press, one set every 5 s, would take
the fake world over a minute and a half to fill.

  python3 test_testmode.py                     the test, then its mutant builds side by side
  python3 test_testmode.py LIBRARY PROPERTY    one property against another build
"""
import ctypes as c
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import mutants

ROOT=Path(__file__).resolve().parent
PIN='9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b'
ON='Test mode is on: unlimited stamina and blood; U drops material crates.'
DROPPED='Test mode: materials dropped.'
RESINS=0x0ae5c9ac
class Context(c.Structure):
    _fields_=[('size',c.c_uint32),('version',c.c_uint32),('thread_id',c.c_uint32),('depth',c.c_uint32),
              ('caller_rva',c.c_uint64),('network_enforced',c.c_uint32),('reserved',c.c_uint32)]
def oracle(ok,what):
    if not ok:raise AssertionError('test-mode oracle: '+str(what))

class Game:
    """One copy of the mod in its own folder, in a group config (no partner, no relay), in the world at (10, 20, 30)
    facing +y."""
    def __init__(self,folder,library,extra):
        self.root=Path(folder)/'game';self.root.mkdir();shutil.copyfile(library,self.root/'mod.so')
        lib=self.lib=c.CDLL(str(self.root/'mod.so'))
        lib.mod_init.argtypes=[c.c_char_p];lib.mod_game_thread_tick.argtypes=[c.POINTER(Context)]
        lib.mod_status.argtypes=[c.c_void_p,c.c_uint];lib.mod_last_error.restype=c.c_char_p
        lib.mod_test_thread_id.restype=c.c_uint32;lib.mod_test_frame_caller.restype=c.c_uint64;lib.mod_test_run_stamp.restype=c.c_char_p
        lib.mod_test_set_pose.argtypes=[c.POINTER(c.c_double),c.POINTER(c.c_float),c.c_int]
        lib.mod_test_set_gauges.argtypes=[c.c_float]*4;lib.mod_test_gauges.argtypes=[c.POINTER(c.c_float)]
        lib.mod_test_dialog_answer.argtypes=[c.c_uint];lib.mod_test_set_vitals.argtypes=[c.c_int,c.c_int];lib.mod_test_set_event.argtypes=[c.c_int]
        config=dict(slot=0,server='127.0.0.1',nickname='Ann',provider='game',session=46,required=True,game_thread=True,puppet_probe=True,
                    virtual_pad=True,discovery_sha256=PIN)
        config.update(extra)
        (self.root/'mod.json').write_text(json.dumps(config))
        oracle(lib.mod_init(str(self.root/'mod.json').encode())==1,lib.mod_last_error())
        self.ctx=Context(c.sizeof(Context),1,lib.mod_test_thread_id(),1,lib.mod_test_frame_caller(),1,0)
        # Facing +y: row 0..2 of the rotation are right, forward, up (the transform's forward is rotation[3], [4]).
        lib.mod_test_set_pose((c.c_double*3)(10,20,30),(c.c_float*9)(1,0,0,0,1,0,0,0,1),0)
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
    def gauges(self):
        four=(c.c_float*4)();self.lib.mod_test_gauges(four);return tuple(round(v,3) for v in four)
    def low(self):self.lib.mod_test_set_gauges(40,100,30,90)
    def script(self,text):(self.root/'pad.script').write_text('pad %s\n%s'%(self.lib.mod_test_run_stamp().decode(),text))
    def log(self):return next(self.root.glob('mod-*.log')).read_text()
    def close(self):self.lib.mod_shutdown()

def gauges(library):
    """Blood and stamina kept full through the gate; nothing without test_mode."""
    with tempfile.TemporaryDirectory() as folder:
        game=Game(folder,library,dict(test_mode={}))
        try:
            game.low();full=(100.0,100.0,90.0,90.0)
            oracle(game.pump(5,lambda:game.gauges()==full),('not kept full',game.gauges(),game.status()['test_mode']))
            # A game event (cutscene, private room): nothing written while it runs; full again once it is over.
            game.lib.mod_test_set_event(1);game.pump(.5);game.low();game.pump(1)
            oracle(game.gauges()==(40.0,100.0,30.0,90.0),('written in a game event',game.gauges()))
            game.lib.mod_test_set_event(0)
            oracle(game.pump(5,lambda:game.gauges()==full),('not full after the event',game.gauges()))
            # Down, then caught by BTs: nothing written.
            for dead,caught in ((1,0),(0,1)):
                game.lib.mod_test_set_vitals(dead,caught);game.pump(.3);game.low();game.pump(1)
                oracle(game.gauges()==(40.0,100.0,30.0,90.0),('written while down or caught',dead,caught,game.gauges()))
            game.lib.mod_test_set_vitals(0,0)
            oracle(game.pump(5,lambda:game.gauges()==full),('not full when up again',game.gauges()))
            status=game.status()
            oracle(status['test_mode']['on'] and status['test_mode']['blood_writes']>=3 and status['test_mode']['stamina_writes']>=3,status['test_mode'])
            game.pump(3)   # the notice queue's gap
            oracle(status['notices_shown'].count(ON)==1 or game.status()['notices_shown'].count(ON)==1,('told once',game.status()['notices_shown']))
            oracle(game.status()['notices_shown'].count(ON)==1,('told more than once',game.status()['notices_shown']))
            log=game.log();oracle('TESTMODE on: materials on U' in log and 'battery on (the porter' in log and 'TESTMODE gauges kept full' in log,log[-1500:])
        finally:game.close()
    with tempfile.TemporaryDirectory() as folder:
        game=Game(folder,library,{})
        try:
            game.low();game.pump(2)
            oracle(game.gauges()==(40.0,100.0,30.0,90.0),('written without test_mode',game.gauges()))
            s=game.status();oracle(not s['test_mode']['on'] and ON not in s['notices_shown'],s['test_mode'])
        finally:game.close()

def materials(library):
    """U drops a set behind the porter; one set every 5 s."""
    with tempfile.TemporaryDirectory() as folder:
        game=Game(folder,library,dict(test_mode={}))
        try:
            game.low();game.pump(1)
            # Three presses: at once, 1.5 s later (refused: within 5 s), 6.5 s after the first (a set again).
            game.script('key U 200\nwait 1300\nkey U 200\nwait 4800\nkey U 200\nwait 300\n')
            game.pump(9)
            s=game.status();drops=s['fake_drops'];t=s['test_mode']
            oracle(len(drops)>=2 and len(drops)%2==0 and t['sets']==2 and t['refused']==1,('one set every 5 s',t,drops))
            oracle(RESINS in [d['item'] for d in drops],('resins in a set',drops))
            for d in drops:oracle(abs(d['at'][1]-18)<1e-6 and abs(d['at'][2]-30.5)<1e-6,('two metres behind him, half a metre up',d))
            oracle(t['on_ground']==len(drops),('his crates on the ground',t))
            game.pump(3)
            oracle(game.status()['notices_shown'].count(DROPPED)==2,('told once a set',game.status()['notices_shown']))
            log=game.log();oracle(log.count('TESTMODE materials dropped')==2 and 'TESTMODE materials refused: one set every 5 s' in log,log[-1500:])
            # Picked up: it stops counting.
            game.lib.mod_test_take_crate(0);game.pump(.6)
            oracle(game.status()['test_mode']['on_ground']==len(drops)-1,('a crate picked up still counts',game.status()['test_mode']))
        finally:game.close()

def menu(library):
    """F3 opens the switches where test mode is on; a switch turned off stops that gauge being kept, at once and only that one."""
    with tempfile.TemporaryDirectory() as folder:
        game=Game(folder,library,dict(test_mode={}))
        try:
            full=(100.0,100.0,90.0,90.0);game.low()
            oracle(game.pump(5,lambda:game.gauges()==full),('not kept full',game.gauges()))
            menu=lambda:game.status()['test_mode']['menu']
            oracle(game.pump(3,lambda:menu()['ready']),('no menu',menu()))
            game.script('key F3 200\nwait 300\n')
            oracle(game.pump(3,lambda:menu()['open']),('F3 did not open it',menu()))
            # Row 0 on the first switch: stamina off. Its gauge is left alone from then on; blood is still kept.
            game.lib.mod_test_dialog_answer(0)
            oracle(game.pump(3,lambda:menu()['switches']==1 and menu()['open']),('not switched and reopened',menu()))
            oracle(game.status()['test_mode']['keep']=={'stamina':False,'blood':True,'battery':True},game.status()['test_mode']['keep'])
            game.pump(.3);game.low();game.pump(1)
            g=game.gauges();oracle(g[0]==100.0 and g[2]==30.0,('stamina off, blood on',g))
            # Close: nothing else changes and the dialogue stays gone.
            game.lib.mod_test_dialog_answer(2);game.pump(.5)
            oracle(not menu()['open'] and menu()['opens']==2,('closed',menu()))
            oracle(game.status()['test_mode']['keep']['stamina'] is False,'the switch went back by itself')
            log=game.log();oracle('DEBUGMENU on: F3' in log and 'DEBUGMENU opened' in log and 'DEBUGMENU switched 0 off' in log and 'DEBUGMENU closed' in log,log[-1200:])
        finally:game.close()
    with tempfile.TemporaryDirectory() as folder:   # no test mode, no menu
        game=Game(folder,library,{})
        try:
            game.script('key F3 200\nwait 300\n');game.pump(1.5)
            m=game.status()['test_mode']['menu'];oracle(not m['ready'] and not m['opens'],('a menu without test mode',m))
        finally:game.close()

PROPERTIES=dict(gauges=gauges,materials=materials,menu=menu)
MUTANTS=[('fake-testmode-event.so','gauges'),('fake-testmode-rate.so','materials'),('fake-testmode-menu.so','menu')]
if __name__=='__main__':
    if len(sys.argv)==3:
        PROPERTIES[sys.argv[2]](Path(sys.argv[1]).resolve())
    else:
        for name,check in PROPERTIES.items():check(ROOT/'build/fake.so')
        def one(job):
            build,prop=job
            r=subprocess.run([sys.executable,'-B',__file__,str(ROOT/'build'/build),prop],capture_output=True,text=True,timeout=300)
            assert r.returncode==1 and 'test-mode oracle' in r.stderr,('SURVIVED '+build,r.returncode,r.stderr[-800:])
            return build
        for build in mutants.parallel(one,MUTANTS):print('KILLED '+build)
        print('test mode (fake world): blood and stamina kept full on the game thread, not in an event, not down or caught, nothing without test_mode; told once; a set of crates behind the porter on U, one set every 5 s, told each time passed')
