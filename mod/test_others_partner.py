"""The partner and other players at once, in the fake world: the real mod (fake.so: fake game memory, datagrams
handed over by this test instead of sockets) runs the two-player link between A and B while A also holds sessions,
bodies and name labels for the other players C and D. This test is the relay: it tells each game who is near and
carries the others' datagrams by player id.

  python3 test_others_partner.py                    the test
  python3 test_others_partner.py LIBRARY PROPERTY   one property (together, no_relay, dressed) against another build: the mutants
"""
import ctypes as c
import json
from pathlib import Path
import shutil
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parent
PIN='9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b'
SESSION=46
class Context(c.Structure):
    _fields_=[('size',c.c_uint32),('version',c.c_uint32),('thread_id',c.c_uint32),('depth',c.c_uint32),
              ('caller_rva',c.c_uint64),('network_enforced',c.c_uint32),('reserved',c.c_uint32)]
def oracle(ok,what):
    if not ok:raise AssertionError('others-partner oracle: '+str(what))

class Game:
    """One copy of the mod in its own folder (a library file of its own, so each has its own globals)."""
    def __init__(self,folder,library,name,position,player=0,partner=None,others=4,relay=True,tag=True,extra=None):
        self.name,self.player,self.root=name,player,Path(folder)/name
        self.root.mkdir();shutil.copyfile(library,self.root/'mod.so')
        lib=self.lib=c.CDLL(str(self.root/'mod.so'))
        lib.mod_init.argtypes=[c.c_char_p];lib.mod_game_thread_tick.argtypes=[c.POINTER(Context)]
        lib.mod_status.argtypes=[c.c_void_p,c.c_uint];lib.mod_last_error.restype=c.c_char_p
        lib.mod_test_thread_id.restype=c.c_uint32;lib.mod_test_frame_caller.restype=c.c_uint64
        lib.mod_test_set_pose.argtypes=[c.POINTER(c.c_double),c.POINTER(c.c_float),c.c_int]
        lib.mod_test_others_roster.argtypes=[c.c_uint64,c.c_int]
        lib.mod_test_others_deliver.argtypes=[c.c_uint64,c.c_char_p,c.c_uint]
        lib.mod_test_others_drain.argtypes=[c.POINTER(c.c_uint64),c.c_void_p,c.c_uint];lib.mod_test_others_drain.restype=c.c_uint
        lib.mod_test_set_gear.argtypes=[c.c_uint];lib.mod_test_set_carry.argtypes=[c.c_char_p]
        config=dict(role='host',peer='127.0.0.1',slot=0,peer_slot=1,nickname=name,provider='game',session=SESSION,
            required=True,game_thread=True,puppet_probe=True,discovery_sha256=PIN,partner='off')
        if partner:config.update(role=partner[0],slot=partner[1],peer_slot=1-partner[1],partner='own',partner_tag=tag)
        if others:config.update(others=others)
        if others and relay:config.update(relay='127.0.0.1:8090',relay_key='test',player_id=player)
        config.update(extra or {})
        (self.root/'mod.json').write_text(json.dumps(config))
        oracle(lib.mod_init(str(self.root/'mod.json').encode())==1,lib.mod_last_error())
        self.ctx=Context(c.sizeof(Context),1,lib.mod_test_thread_id(),1,lib.mod_test_frame_caller(),1,0)
        self.position=position;self.place(position)
    def place(self,position):
        self.position=position
        self.lib.mod_test_set_pose((c.c_double*3)(*position),(c.c_float*9)(1,0,0,0,1,0,0,0,1),0)
    def tick(self):oracle(self.lib.mod_game_thread_tick(c.byref(self.ctx))==1,self.lib.mod_last_error())
    def status(self):
        cap=self.lib.mod_status(None,0)
        for _ in range(8):
            out=c.create_string_buffer(cap);required=self.lib.mod_status(out,cap)
            if required<=cap:return json.loads(out.value)
            cap=required*2
        raise AssertionError('status kept growing')
    def partner_out(self):
        packet=(c.c_ubyte*128)();out=[]
        while self.lib.mod_test_drain(packet):out.append(bytes(packet))
        return out
    def partner_in(self,datagram):self.lib.mod_test_deliver((c.c_ubyte*128).from_buffer_copy(datagram))
    def others_out(self):
        to=c.c_uint64();raw=c.create_string_buffer(2048);out=[]
        while True:
            n=self.lib.mod_test_others_drain(c.byref(to),raw,2048)
            if not n:return out
            out.append((to.value,raw.raw[:n]))
    def others_in(self,sender,datagram):self.lib.mod_test_others_deliver(sender,datagram,len(datagram))
    def roster(self,player,enter):self.lib.mod_test_others_roster(player,1 if enter else 0)
    def wear(self,shown):self.lib.mod_test_set_gear(shown)                    # bit 0: the backpack, bit 1: the wrist cuff
    def carry(self,spec):oracle(self.lib.mod_test_set_carry(spec.encode())==1,('carry spec',spec))
    def log(self):return next(self.root.glob('mod-*.log')).read_text()
    def close(self):self.lib.mod_shutdown()

class World:
    """The two-player link between a and b, and the relay for everybody with a player id."""
    def __init__(self,games,a=None,b=None):
        self.games,self.a,self.b=games,a,b;self.near=set();self.by_id={g.player:g for g in games if g.player}
        self.partner_carried=0;self.others_carried=0;self.last_partner={};self.last_others={}
    def enter(self,x,y):
        self.near.add(frozenset((x.player,y.player)));x.roster(y.player,True);y.roster(x.player,True)
    def leave(self,x,y):
        self.near.discard(frozenset((x.player,y.player)));x.roster(y.player,False);y.roster(x.player,False)
    def step(self):
        for g in self.games:g.tick()
        if self.a:
            for src,dst in ((self.a,self.b),(self.b,self.a)):
                for d in src.partner_out():
                    self.last_partner[src.name]=d;dst.partner_in(d);self.partner_carried+=dst is self.a
        for g in self.games:
            if not g.player:continue
            for to,d in g.others_out():
                # As the relay: only between players who are near each other.
                if frozenset((g.player,to)) in self.near and to in self.by_id:
                    if len(d)==128:self.last_others[(g.player,to)]=d
                    self.by_id[to].others_in(g.player,d);self.others_carried+=self.by_id[to] is self.games[0]
        time.sleep(.01)
    def run(self,seconds,until=None):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            self.step()
            if until and until():return True
        return until is None

def labels(status):return sorted(status['label_names'])
def partner_fine(s,why):
    oracle(s['connected'] and not s['failed'],(why,'partner link',s['connected'],s['failed']))
    oracle(s['wrong_accepted']==0 and s['session_refused']==0 and s['build_refused']==0 and s['version_refused']==0,
           (why,'the partner link refused something',{k:s[k] for k in ('wrong_accepted','session_refused','build_refused','version_refused')}))

def together(library):
    """A and B are partners; C and D come near A, leave and come back. A's partner link never notices."""
    with tempfile.TemporaryDirectory() as folder:
        a=Game(folder,library,'Ann',(10,20,30),player=1,partner=('host',0))
        b=Game(folder,library,'Bob',(13,20,30),partner=('client',1),others=0)
        cc=Game(folder,library,'Cee',(10,26,30),player=3)
        d=Game(folder,library,'Dee',(4,20,30),player=4)
        games=[a,b,cc,d];w=World(games,a,b)
        try:
            oracle(a.status()['others']['on'] and cc.status()['others']['on'] and not b.status()['others']['on'],'who has other players on')
            # The partner alone first: connected, the partner's body made, the name over it.
            oracle(w.run(8,lambda:a.status()['connected'] and b.status()['connected'] and labels(a.status())==['Bob']),
                   ('the partner pair never came up',a.status()['connected'],a.status()['label_names'],a.log()[-1500:]))
            s=a.status();partner_fine(s,'alone')
            oracle(s['others']==dict(s['others'],near=0,connected=0,bodies=0,received=0,sent=0),('others before anybody is near',s['others']))
            # C and D come near A (not each other).
            w.enter(a,cc);w.enter(a,d)
            oracle(w.run(10,lambda:a.status()['others']['bodies']==2 and labels(a.status())==['Bob','Cee','Dee']),
                   ('others never shown',a.status()['others'],a.status()['label_names'],a.log()[-1500:]))
            s=a.status();partner_fine(s,'with others')
            oracle(s['others']['near']==2 and s['others']['connected']==2 and s['others']['shown']==2,s['others'])
            oracle(s['others']['refused']==0 and s['others']['unknown']==0 and s['others']['malformed']==0,('others refused something',s['others']))
            oracle(cc.status()['others']['connected']==1 and d.status()['others']['connected']==1,'C and D each see A only')
            # Each label is over its own body: C stands 6 m along y from A, D 6 m along -x; 2.1 m above the feet.
            at={n['name']:n['at'] for n in s['labels']}
            for name,where in (('Cee',cc.position),('Dee',d.position),('Bob',b.position)):
                oracle(abs(at[name][0]-where[0])<1 and abs(at[name][1]-where[1])<1 and abs(at[name][2]-where[2]-2.1)<.5,('label not over its body',name,at[name],where))
            # The partner link carried on all the while: every datagram B sent arrived and was taken.
            before=s['accepted'];began=time.monotonic()
            def walk():b.place((13+1.5*(time.monotonic()-began),20,30))   # B walks, so its samples come at the full rate
            w.run(1.5,lambda:walk() or False);s=a.status();partner_fine(s,'while others walk')
            oracle(s['accepted']>before+10,('the partner stopped arriving',before,s['accepted']))
            oracle(s['received']==w.partner_carried,('the partner socket saw datagrams nobody sent it',s['received'],w.partner_carried))
            oracle(s['others']['received']==w.others_carried,('the others saw datagrams the relay never carried',s['others']['received'],w.others_carried))
            # C leaves: its session, body and label go; D and the partner stay as they are.
            w.leave(a,cc)
            oracle(w.run(5,lambda:a.status()['others']['bodies']==1 and labels(a.status())==['Bob','Dee']),('C never went',a.status()['others'],a.status()['label_names']))
            s=a.status();partner_fine(s,'after C left');oracle(s['others']['near']==1 and s['others']['connected']==1,s['others'])
            # A local game event: every body and every label is hidden; over: back, the same bodies.
            a.lib.mod_test_set_event(1);oracle(w.run(5,lambda:labels(a.status())==[]),('labels stayed during a game event',a.status()['label_names']))
            a.lib.mod_test_set_event(0);oracle(w.run(8,lambda:labels(a.status())==['Bob','Dee']),('labels did not come back',a.status()['label_names']))
            # C comes back: a session, a body and a label again.
            w.enter(a,cc)
            oracle(w.run(10,lambda:a.status()['others']['bodies']==2 and labels(a.status())==['Bob','Cee','Dee']),('C never came back',a.status()['others'],a.status()['label_names']))
            s=a.status();partner_fine(s,'after C came back');oracle(s['others']['refused']==0 and s['others']['unknown']==0,s['others'])
            log=a.log();oracle('OTHERS player 3 left' in log and log.count('OTHERS player 3 is near')==2,'the log of who came and went')
            # The partner's body was made once and never again; each other player's once per stay.
            oracle(log.count('made and parked')==4 and log.count('OTHERS BODY')>=0,('bodies made',log.count('made and parked')))
            return a,b,cc,d,w,s
        except BaseException:
            for g in games:g.close()
            raise

def routing(library):
    """A datagram of another player handed to the partner's socket, and the partner's handed to the others: refused."""
    a,b,cc,d,w,s=together(library)
    try:
        # One of C's datagrams for A, given to A as if it came from the partner's address: the partner's core takes
        # it for another session. The partner link goes on.
        w.run(.3);stray=w.last_others[(3,1)];before=a.status()
        a.partner_in(stray);w.run(.5);after=a.status()
        oracle(after['session_refused']==before['session_refused']+1 and after['wrong_accepted']==0 and after['connected'],
               ('a datagram of another player reached the partner core',before['session_refused'],after['session_refused'],after['wrong_accepted']))
        # One of B's datagrams, given to A as if the relay brought it from C, from D, and from somebody who is not near.
        stray=w.last_partner['Bob'];before=after['others']
        a.others_in(3,stray);a.others_in(4,stray);a.others_in(99,stray);w.run(.5);after=a.status()
        oracle(after['others']['refused']==before['refused']+2 and after['others']['unknown']==before['unknown']+1,
               ('the partner\'s datagram was taken by another player\'s session',before,after['others']))
        oracle(after['others']['connected']==2 and after['connected'] and labels(after)==['Bob','Cee','Dee'],'everybody still here after the strays')
    finally:
        for g in (a,b,cc,d):g.close()

def no_relay(library):
    """`others` on with no relay configured: no session object, nothing sent, nothing shown, the partner as ever."""
    with tempfile.TemporaryDirectory() as folder:
        a=Game(folder,library,'Ann',(10,20,30),player=1,partner=('host',0),relay=False)
        b=Game(folder,library,'Bob',(13,20,30),partner=('client',1),others=4,relay=False)
        w=World([a,b],a,b)
        try:
            # A roster and datagrams that turn up anyway (nothing sends them without a relay) fall on nothing.
            a.roster(3,True);a.others_in(3,bytes(128))
            oracle(w.run(8,lambda:a.status()['connected'] and b.status()['connected'] and labels(a.status())==['Bob']),'the partner pair never came up')
            w.run(1);s=a.status();partner_fine(s,'no relay')
            oracle(s['others']==dict(on=False,near=0,connected=0,bodies=0,shown=0,sent=0,received=0,accepted=0,refused=0,unknown=0,malformed=0),('others without a relay',s['others']))
            oracle(a.others_out()==[] and 'OTHERS' not in a.log() and 'RELAY' not in a.log(),'others said or sent something without a relay')
            oracle(s['received']==w.partner_carried,'the partner socket saw datagrams nobody sent it')
        finally:
            a.close();b.close()

def near(at,where,reach=1.5):return all(abs(at[i]-where[i])<reach for i in range(3))
def dressed(library):
    """What each player wears and carries is shown on that player's body: the partner's (key 0) and the other players'
    (their ids), each from that player's own events; the cases only for the nearest carrier (others_carry 1); a player
    without the settings takes and sends none of it."""
    show=dict(others_gear=True,others_carry=1)
    with tempfile.TemporaryDirectory() as folder:
        a=Game(folder,library,'Ann',(10,20,30),player=1,partner=('host',0),extra=dict(show,partner_gear=True,carry_visual=True))
        b=Game(folder,library,'Bob',(13,20,30),partner=('client',1),others=0,extra=dict(partner_gear=True,carry_visual=True))
        cc=Game(folder,library,'Cee',(10,26,30),player=3,extra=show)
        d=Game(folder,library,'Dee',(7,20,30),player=4,extra=show)
        e=Game(folder,library,'Eve',(10,12,30),player=5)                      # other players on, nothing more
        games=[a,b,cc,d,e];w=World(games,a,b)
        try:
            a.wear(1);b.wear(2);cc.wear(3);d.wear(1);e.wear(3)               # Bob the cuff, Cee both, Dee the backpack
            b.carry('777');cc.carry('501:1:3');d.carry('502:1:2');e.carry('503:1:4')
            w.enter(a,cc);w.enter(a,d);w.enter(a,e)
            oracle(w.run(10,lambda:a.status()['others']['bodies']==3 and a.status()['connected']),('bodies never made',a.status()['others']))
            s=a.status();oracle(all(s['gear_shown']['by'].get(k,0)==0 for k in ('3','4','5')),('gear before a set arrived',s['gear_shown']))
            # Every set arrives (a sender waits until the gear has held for 5 s) and every body is dressed with its own.
            want_gear={'0':1,'3':2,'4':1,'5':0}
            oracle(w.run(25,lambda:a.status()['gear_shown']['by']==want_gear and a.status()['cases_shown']['by']=={'0':1,'4':2}),
                   ('gear and cases never as sent',a.status()['gear_shown'],a.status()['cases_shown'],a.log()[-2500:]))
            s=a.status();partner_fine(s,'dressed')
            oracle(s['others']['refused']==0 and s['others']['unknown']==0 and s['others']['malformed']==0,('others refused something',s['others']))
            oracle(s['gear_shown']['sets']==2 and s['cases_shown']['sets']>=2,('sets taken',s['gear_shown'],s['cases_shown']))   # Cee's and Dee's; Eve sends none
            pieces=s['pieces'];oracle(len(pieces)==4 and all(p['visible'] for p in pieces),('pieces',pieces))
            def on(who,helpers):
                got=sorted(p['helper'] for p in s['pieces'] if near(p['at'],who.position))
                oracle(got==sorted(helpers),('gear on the wrong body',who.name,got,helpers,s['pieces']))
            on(b,['HLP_Hcuf']);on(cc,['HLP_Backpack','HLP_Hcuf']);on(d,['HLP_Backpack']);on(e,[])
            # The cases: the partner's one, and of the other players only the nearest carrier's (Dee, 3 m; Cee is 6 m away).
            def cases(who):return sorted(x['item'] for x in s['cases'] if near(x['at'],who.position))
            oracle(cases(b)==[777] and cases(d)==[502,502] and cases(cc)==[] and cases(e)==[] and len(s['cases'])==3,('cases',s['cases']))
            # Eve has neither setting: Ann's events reached her and were dropped; she shows nothing and sent nothing of it.
            se=e.status();oracle(se['gear_shown']['pieces']==0 and se['cases_shown']['cases']==0 and se['gear_shown']['sets']==0 and se['cases_shown']['sets']==0
                                 and se['cases_shown']['dropped_events']>0,('a game without the settings took something',se['gear_shown'],se['cases_shown']))
            # Cee sees Ann the same way (Ann shows the backpack and carries nothing).
            sc=cc.status();oracle(sc['gear_shown']['by']=={'1':1} and sc['cases_shown']['cases']==0,('Cee\'s view of Ann',sc['gear_shown'],sc['cases_shown']))
            # A local game event: every body is hidden, with it every piece; no case is drawn. Over: all back, nothing made twice.
            a.lib.mod_test_set_event(1)
            oracle(w.run(5,lambda:a.status()['cases']==[] and not any(p['visible'] for p in a.status()['pieces'])),('shown during a game event',a.status()['pieces'],a.status()['cases']))
            oracle(len(a.status()['pieces'])==4,'pieces were removed for a game event')
            a.lib.mod_test_set_event(0)
            oracle(w.run(8,lambda:len(a.status()['cases'])==3 and all(p['visible'] for p in a.status()['pieces'])),('not back after the game event',a.status()['pieces'],a.status()['cases']))
            # Dee leaves: her gear, her cases and her body go (the copies before the body); Cee (6 m) is the nearest
            # carrier now and the cases are hers; the partner's stay as they are.
            w.leave(a,d)
            oracle(w.run(10,lambda:a.status()['others']['bodies']==2 and a.status()['gear_shown']['by']=={'0':1,'3':2,'5':0} and a.status()['cases_shown']['by']=={'0':1,'3':3}),
                   ('Dee\'s gear or cases stayed, or Cee\'s never came',a.status()['others'],a.status()['gear_shown'],a.status()['cases_shown']))
            s=a.status();partner_fine(s,'after Dee left');on(b,['HLP_Hcuf']);on(cc,['HLP_Backpack','HLP_Hcuf'])
            oracle(len(s['pieces'])==3 and cases(cc)==[501,501,501] and cases(b)==[777] and cases(d)==[] and len(s['cases'])==4,('after Dee left',s['pieces'],s['cases']))
            # Dee comes back, 3 m nearer than Cee: her set again (sent on the new connection), and the cases are hers again.
            w.enter(a,d)
            oracle(w.run(25,lambda:a.status()['others']['bodies']==3 and a.status()['gear_shown']['by']=={'0':1,'3':2,'4':1,'5':0} and a.status()['cases_shown']['by']=={'0':1,'4':2}),
                   ('Dee came back without her gear or cases',a.status()['others'],a.status()['gear_shown'],a.status()['cases_shown']))
            s=a.status();on(d,['HLP_Backpack']);oracle(cases(d)==[502,502] and cases(cc)==[] and len(s['cases'])==3,('after Dee came back',s['cases']))
            # Cee leaves: her gear, her cases and her body go; Dee is the only carrier left; the partner's are untouched.
            w.leave(a,cc)
            oracle(w.run(10,lambda:a.status()['others']['bodies']==2 and a.status()['gear_shown']['by']=={'0':1,'4':1,'5':0} and a.status()['cases_shown']['by']=={'0':1,'4':2}),
                   ('Cee\'s gear or cases stayed',a.status()['others'],a.status()['gear_shown'],a.status()['cases_shown']))
            s=a.status();partner_fine(s,'after Cee left');on(b,['HLP_Hcuf']);on(d,['HLP_Backpack']);oracle(len(s['pieces'])==2,s['pieces'])
            # No body was ever removed while it still wore a copy.
            oracle(s['pieces_orphaned']==0,('a body was removed with gear copies still on it',s['pieces_orphaned']))
            # The partner link carried on all the while, and each side saw only its own datagrams.
            oracle(s['received']==w.partner_carried and s['others']['received']==w.others_carried,'a datagram went the wrong way')
        finally:
            for g in games:g.close()

PROPERTIES=dict(together=routing,no_relay=no_relay,dressed=dressed)   # `together` runs A, B, C and D once and ends with the strays
if __name__=='__main__':
    if len(sys.argv)==3:
        PROPERTIES[sys.argv[2]](Path(sys.argv[1]).resolve())
    else:
        for name,run in PROPERTIES.items():run(ROOT/'build/fake.so')
        print('others with the partner (fake world): together, strays, no relay, and gear and cases per player passed')
