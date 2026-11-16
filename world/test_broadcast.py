"""The built server's broadcast mode (envelope version 2) over real UDP sockets on loopback, with 26 players written
here byte by byte (the layout in core/broadcast.h): 16 in a group, 8 on a ring 80 m around it and 2 far away, each
publishing its presence and an animation sample 20 times a second. Every FRAME fits the budget, every entry is from a
player near, players within the full distance get every update with its animation, players beyond it one in the
interval and no animation, entries share datagrams, a STATE reaches everyone near and nobody else, an update naming
another player goes nowhere, a pairs player in the group is near nobody, and a server that pins the build refuses
another one. Prints what each player received a second. Then 100 stand-ins (coop-swarm --mode broadcast) walking in one
place, on a server that shows each player at most 16 of the players near him and on one without a cap: what one
receives a second, and that with the cap he is sent the updates of 16 players and no more."""
import collections,math,select,socket,struct,subprocess,sys,time
exe=sys.argv[1];swarm=sys.argv[2] if len(sys.argv)>2 else None
GAME,BUILD=13300582,bytes(range(0xa0,0xb0))
FULL,INTERVAL,ANIMATION,BUDGET,FLUSH=30,200,50,600,25
def free_port(kind=socket.SOCK_STREAM):
    s=socket.socket(socket.AF_INET,kind);s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def refused(args):
    r=subprocess.run([exe,*args],capture_output=True,text=True,timeout=20);return r.returncode==2
def cm(position):return [int(round(x*100)) for x in position]
def hello(player,sequence,stream,ack,position,t0,build=BUILD,version=2):
    b=struct.pack('<4sHBBQ32sIIIIB3xiii32s',b'DSR1',version,1,0,player,b'abc',sequence,stream,ack,0,1,*cm(position),b'P%d'%player)
    return b+struct.pack('<I16sQ',GAME,build,t0) if version==2 else b
# The presence block: flags, action, phase, then the position in centimetres (the server reads those two), the rest zero.
def presence(position,flags=0):return struct.pack('<HBBiii',flags,3,64,*cm(position))+bytes(36)
SAMPLE=bytes([1,1,0,1,40])+struct.pack('<f',.25)+bytes([0,0,0])   # the core's animation payload: one bool held, one float
def publish(player,sequence,stamp,position,animation=SAMPLE):
    return struct.pack('<4sHBBQIBBHQ',b'DSR1',2,5,0,player,sequence,1|(2 if animation else 0),0,len(animation),stamp)+presence(position)+animation
def state(player,kind,version,payload):return struct.pack('<4sHBBQBBHI',b'DSR1',2,7,0,player,kind,0,len(payload),version)+payload
def ms():return time.monotonic()*1000

# Options out of range: the server does not start.
for wrong in (['--broadcast-budget','255'],['--broadcast-budget','1201'],['--broadcast-interval','5001'],['--broadcast-flush','201'],['--broadcast-rate','5'],
              ['--broadcast-full','70000'],['--broadcast-build','13300582:00'],['--broadcast-build','x:'+'0'*32],['--broadcast-build','13300582:'+'g'*32]):
    assert refused(['--port','1','--relay-port','2']+wrong),wrong

class Player:
    def __init__(s,player,position,relay):
        s.id,s.position=player,position;s.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.sock.connect(('127.0.0.1',relay));s.sock.setblocking(False)
        s.sequence=s.stream=s.ack=s.published=s.stamp=0;s.sent={};s.offset=None;s.near={};s.nextHello=0;s.welcome=None
        s.frames=[];s.entries=collections.defaultdict(list);s.shares=[];s.bytes=s.datagrams=0;s.counting=False;s.strangers=0
    def hello(s,now):
        s.sequence+=1;t0=int(now);s.sent[s.sequence]=t0;s.sock.send(hello(s.id,s.sequence,s.stream,s.ack,s.position,t0));s.nextHello=now+500
    def publish(s,now):
        if s.offset is None or not s.near:return
        s.published+=1;s.stamp=max(s.stamp+1,int(now+s.offset));s.sock.send(publish(s.id,s.published,s.stamp,s.position))
    def receive(s,now):
        while True:
            try:b=s.sock.recv(2000)
            except BlockingIOError:return
            assert len(b)<=1200 and b[:4]==b'DSR1' and b[4:6]==b'\x02\x00' and b[7]==0,b[:8]
            if s.counting:s.bytes+=len(b);s.datagrams+=1
            kind=b[6]
            if kind==2:
                assert len(b)==76;w=struct.unpack('<QIIHHHHQQ4IHHHHH2x',b[8:]);s.welcome=w
                assert w[0]==s.id and w[13:18]==(FULL,ANIMATION,INTERVAL,w[16],BUDGET),w
                if w[2]!=s.stream:s.stream,s.ack,s.near=w[2],0,{}
                if w[1] in s.sent and s.sent[w[1]]==w[7]:
                    candidate=w[8]-(w[7]+now)/2
                    s.offset=candidate if s.offset is None else s.offset
            elif kind==4:
                stream,first,count=struct.unpack_from('<IIB',b,8);o=20
                for i in range(count):
                    event,player,n=struct.unpack_from('<BQB',b,o);nick=b[o+10:o+10+n];o+=10+n
                    if stream!=s.stream or first+i!=s.ack+1:continue
                    s.ack+=1
                    if event==1:s.near[player]=nick
                    else:s.near.pop(player,None)
                s.nextHello=now   # acknowledge at once
            elif kind==6:
                server,count=struct.unpack_from('<QB',b,8);o=20;entries=[]
                for i in range(count):
                    source,sequence,stamp,parts,_,size,generation,_=struct.unpack_from('<QIQBBHHH',b,o);o+=28
                    flags=struct.unpack_from('<H',b,o)[0] if parts&1 else 0
                    o+=(52 if parts&1 else 0)+(size if parts&2 else 0);entries.append((source,sequence,stamp,parts,size,generation,flags))
                    if source not in s.near:s.strangers+=1
                assert o==len(b),(o,len(b))
                if s.counting:
                    s.frames.append((len(b),count))
                    for e in entries:s.entries[e[0]].append((now,)+e[1:])
            elif kind==8:
                source,kind_,_,size,version,generation=struct.unpack_from('<QBBHIH',b,8);s.shares.append((source,kind_,version,b[44:44+size]))
            else:raise AssertionError('unexpected type %d'%kind)

port,relay=free_port(),free_port(socket.SOCK_DGRAM)
p=subprocess.Popen([exe,'--port',str(port),'--relay-port',str(relay),'--prefix','abc','--broadcast-full',str(FULL),'--broadcast-interval',str(INTERVAL),
                    '--broadcast-animation',str(ANIMATION),'--broadcast-budget',str(BUDGET),'--broadcast-flush',str(FLUSH)],stdout=subprocess.PIPE,text=True)
try:
    lines=[p.stdout.readline() for _ in range(3)]
    assert 'speaks pairs and broadcast (every update within 30 m, beyond it one in 200 ms, animation within 50 m, frames of 600 bytes within 25 ms, at most the policy\'s near_cap players near shown to each (32 now), fights with at most 9 visitors)' in lines[2],lines
    group=[Player(1000+i,(10*math.cos(i*math.pi/8),0,10*math.sin(i*math.pi/8)),relay) for i in range(16)]
    ring=[Player(2000+i,(80*math.cos(i*math.pi/4),0,80*math.sin(i*math.pi/4)),relay) for i in range(8)]
    apart=[Player(3000,(5000,0,0),relay),Player(3001,(5005,0,0),relay)]
    players=group+ring+apart;bySocket={pl.sock.fileno():pl for pl in players}
    # A pairs player (version 1) in the middle of the group: the server answers it, and it is near nobody.
    pairs=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);pairs.settimeout(.5);pairs.connect(('127.0.0.1',relay))
    start=ms();nextPublish=start;stateSent=spoofSent=False
    while ms()-start<5000:
        now=ms()
        if now>=nextPublish:
            for pl in players:pl.publish(now)
            nextPublish+=50
        for pl in players:
            if now>=pl.nextHello:pl.hello(now)
        if not stateSent and now-start>2000:
            group[0].sock.send(state(group[0].id,1,1,b'Ann'));stateSent=True
        if not spoofSent and now-start>2500:   # the far player names the first of the group
            apart[0].sock.send(publish(group[0].id,0x40000000,int(now+apart[0].offset),group[0].position));spoofSent=True
        counting=1500<=now-start<4500
        for pl in players:pl.counting=counting
        ready,_,_=select.select([pl.sock for pl in players],[],[],max(0,min(nextPublish,min(pl.nextHello for pl in players))-ms())/1000)
        now=ms()
        for r in ready:bySocket[r.fileno()].receive(now)
    pairs.send(hello(9,1,0,0,(0,0,0),0,version=1));w=pairs.recv(2000);assert len(w)==32 and w[4]==1 and struct.unpack_from('<H',w,30)[0]==0,w
    try:pairs.recv(2000);assert False,'a ROSTER to the pairs player'
    except socket.timeout:pass
    # Everyone online; the group sees the group and the ring; the two far players see only each other.
    ids=lambda team:{pl.id for pl in team}
    for pl in players:assert pl.welcome and pl.offset is not None,pl.id
    for pl in group:assert set(pl.near)==ids(group+ring)-{pl.id},(pl.id,sorted(pl.near))
    for pl in apart:assert set(pl.near)==ids(apart)-{pl.id},(pl.id,sorted(pl.near))
    for pl in ring:assert ids(group)<=set(pl.near) and not ids(apart)&set(pl.near),pl.id
    # Every FRAME within the budget, every entry from a player near; several entries to a datagram.
    for pl in players:
        assert pl.strangers==0 and all(size<=BUDGET for size,_ in pl.frames),(pl.id,pl.strangers,max(pl.frames or [(0,0)]))
    frames=sum(len(pl.frames) for pl in group);entries=sum(n for pl in group for _,n in pl.frames)
    assert entries/frames>=3,(entries,frames)
    # Within 30 m: every update (3 s at 20 a second), with the animation. Beyond it: one in 200 ms, presence only.
    for pl in group:
        for other in group:
            if other is pl:continue
            got=pl.entries[other.id];assert 50<=len(got)<=62 and all(e[3]&2 and e[4]==len(SAMPLE) for e in got),(pl.id,other.id,len(got))
        for other in ring:
            got=pl.entries[other.id];assert 12<=len(got)<=17 and all(e[3]==1 for e in got),(pl.id,other.id,len(got))
            assert all(b[2]-a[2]>=INTERVAL-15 for a,b in zip(got,got[1:])),(pl.id,other.id,[e[2] for e in got])
        assert not ids(apart)&set(pl.entries)
    for pl in ring:
        for other in group:got=pl.entries[other.id];assert 12<=len(got)<=17 and all(e[3]==1 for e in got),(pl.id,other.id,len(got))
    # The first one's name went to the 23 players near him, once each, and to nobody else.
    for pl in players:
        mine=[x for x in pl.shares if x[0]==group[0].id]
        assert mine==([(group[0].id,1,1,b'Ann')] if pl.id in group[0].near else []),(pl.id,mine)
    # The update the far player sent in the first one's name went nowhere.
    assert not any(e[1]==0x40000000 for pl in players for v in pl.entries.values() for e in v)
    received=sum(pl.bytes for pl in group)/len(group)/3;datagrams=sum(pl.datagrams for pl in group)/len(group)/3
    print('broadcast socket test: a player in a group of 16 with 8 more beyond 30 m receives %.0f datagrams and %.0f bytes a second (%.1f entries a frame, frames of at most %d bytes)'
          %(datagrams,received,entries/frames,max(size for pl in group for size,_ in pl.frames)))
finally:
    p.kill();p.wait()
# A server that pins the build: another build is refused (REFUSED, reason 3), the pinned one is welcome.
port,relay=free_port(),free_port(socket.SOCK_DGRAM)
p=subprocess.Popen([exe,'--port',str(port),'--relay-port',str(relay),'--prefix','abc','--broadcast-build','%d:%s'%(GAME,BUILD.hex())],stdout=subprocess.PIPE,text=True)
try:
    lines=[p.stdout.readline() for _ in range(3)];assert 'one build' in lines[2],lines
    u=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);u.settimeout(2);u.connect(('127.0.0.1',relay))
    u.send(hello(7,1,0,0,(0,0,0),1,build=bytes(16)));r=u.recv(2000);assert len(r)==24 and r[4]==2 and r[6]==3 and r[20]==3,r
    u.send(hello(7,2,0,0,(0,0,0),1));w=u.recv(2000);assert len(w)==76 and w[6]==2,w
finally:
    p.kill();p.wait()
# 100 stand-ins walking within 10 m of each other: with a cap of 16 each is sent 16 players' updates, without one 99.
def crowd(near):
    port,relay=free_port(),free_port(socket.SOCK_DGRAM)
    p=subprocess.Popen([exe,'--port',str(port),'--relay-port',str(relay),'--prefix','abc','--relay-players','128','--broadcast-near',str(near)],stdout=subprocess.PIPE,text=True)
    try:
        lines=[p.stdout.readline() for _ in range(3)]
        assert ('at most %d players near shown to each'%near if near else 'every player near shown to each') in lines[2],lines
        r=subprocess.run([swarm,'--relay','127.0.0.1:%d'%relay,'--key','abc','--count','100','--centre','0','0','0','--radius','10','--mode','broadcast','--seconds','6'],
                         capture_output=True,text=True,timeout=60)
        assert r.returncode==0,r.stderr
        last=[l for l in r.stdout.splitlines() if l.startswith('SWARM t=')][-1]
        values=dict(x.split('=',1) for x in last.split()[1:])
        return int(values['near']),float(values['heard']),float(values['in']),float(values['in_bytes'])
    finally:
        p.kill();p.wait()
if swarm:
    capped,uncapped=crowd(16),crowd(0)
    print('broadcast socket test: 100 players in one place, each receives %.0f datagrams and %.0f bytes a second shown 16 of them, %.0f and %.0f shown all 99'
          %(capped[2],capped[3],uncapped[2],uncapped[3]))
    assert capped[0]==1600 and capped[1]==1600 and uncapped[0]==9900 and uncapped[1]>=9800,(capped,uncapped)
    # 16 players' presence at 20 a second (80-byte entries in FRAMEs of 1200 bytes): about 26 kB a second, whatever the crowd.
    assert capped[3]<=16*20*80*1.3 and capped[3]*4<uncapped[3],(capped,uncapped)
print('broadcast socket test passed')
