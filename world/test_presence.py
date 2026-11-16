"""Presence across linked servers with the built server: two coop-world processes on loopback, linked with
--link-presence, one knowing the other's relay and the other learning it. Players written here byte by byte (the layout
in core/broadcast.h) on each: Bea in the north and Carl in the south see each other, walking, under ids of the other
server, with their names followed by their servers' names; Fay far away in the north is never sent. Then the north
stops: Carl loses Bea within the link's silence. Then a new north and two swarms of stand-ins (coop-swarm --mode
broadcast) with the same ids on both servers: every stand-in sees the four of its own server and the four of the other,
whose names, read as the mod reads them, end with " · north"; and thirty stand-ins in the north on a link of 20000
bytes a second stay within it. Options that are refused first."""
import math,os,re,select,socket,struct,subprocess,sys,tempfile,time
exe,swarm=sys.argv[1],sys.argv[2]
GAME,BUILD=13300582,bytes(range(0xa0,0xb0))
KEY='key-of-north-and-south'
DOT=' · '
def free_port(kind=socket.SOCK_STREAM):
    s=socket.socket(socket.AF_INET,kind);s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def ms():return time.monotonic()*1000
def until(what,seconds):
    end=time.time()+seconds
    while time.time()<end:
        if what():return True
        time.sleep(.05)
    return what()
def refused(args):
    r=subprocess.run([exe,*args],capture_output=True,text=True,timeout=20);return r.returncode==2

# ---- The name state as the mod posts it (mod/crowd.h, mod/nametag.cpp): one 27-byte event of type 10 per 9 bytes.
def name_blob(text):
    s=text.encode()[:36];total=(len(s)+8)//9;out=bytes([max(total,1)])
    for k in range(max(total,1)):
        c=(s[9*k:9*k+9]+bytes(9))[:9]
        item=int.from_bytes(c[:4],'little');ref=k|total<<4|int.from_bytes(c[4:9],'little')<<8 if total else 0
        out+=struct.pack('<BIQ',10,item if total else 0,ref)+bytes(14)
    return out
def name_of(blob):
    n=blob[0];assert len(blob)==1+27*n,len(blob);text=b''
    for i in range(n):
        kind,item,ref=struct.unpack_from('<BIQ',blob,1+27*i);assert kind==10 and (ref&15)==i and (ref>>4&15) in (0,n)
        text+=(item.to_bytes(4,'little')+(ref>>8).to_bytes(5,'little')).replace(b'\0',b'')
    return text.decode()
assert name_of(name_blob('Bea'))=='Bea' and name_of(name_blob(''))==''

# ---- Options that are refused: the server does not start.
for wrong in (['--link','n','--link-key',KEY,'--link-presence','yes'],                                      # no relay here
              ['--relay-port','2','--link','n','--link-key',KEY,'--link-presence','maybe'],
              ['--relay-port','2','--link','n','--link-key',KEY,'--link-relay','127.0.0.1:9'],           # an address without presence
              ['--relay-port','2','--link','n','--link-key',KEY,'--link-presence','yes','--link-relay','localhost:9'],
              ['--relay-port','2','--link','n','--link-key',KEY,'--link-presence','yes','--link-relay','127.0.0.1:0'],
              ['--relay-port','2','--link','n','--link-key',KEY,'--link-presence','yes','--link-presence-budget','1000'],
              ['--relay-port','2','--link','n','--link-key',KEY,'--link-presence','yes','--link-presence-players','0'],
              ['--relay-port','2','--link-presence','yes']):
    assert refused(['--port','1',*wrong]),wrong

class Server:
    def __init__(self,folder,name,*args):
        self.name,self.port,self.relay=name,free_port(),free_port(socket.SOCK_DGRAM);self.log=os.path.join(folder,name+'.log');self.args=args;self.p=None
    def start(self,*links):
        if os.path.exists(self.log):os.remove(self.log)
        self.p=subprocess.Popen([exe,'--port',str(self.port),'--relay-port',str(self.relay),'--prefix','abc','--log',self.log,*self.args,*links],stdout=subprocess.DEVNULL)
        assert until(lambda:'coop-world relay speaks' in self.text(),10),self.text()
    def stop(self):
        if self.p:self.p.kill();self.p.wait();self.p=None
    def text(self):return open(self.log,encoding='utf-8').read() if os.path.exists(self.log) else ''

class Player:
    def __init__(s,player,name,position,relay):
        s.id,s.name,s.position=player,name,position;s.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.sock.connect(('127.0.0.1',relay));s.sock.setblocking(False)
        s.sequence=s.stream=s.ack=s.published=s.stamp=0;s.sent={};s.offset=None;s.near={};s.nextHello=0;s.stated=False
        s.entries={};s.names={};s.left=[]
    def place(s,now):
        a=now/1000*.75;x,y,z=s.position;return (x+2*math.cos(a),y,z+2*math.sin(a))
    def hello(s,now):
        s.sequence+=1;t0=int(now);s.sent[s.sequence]=t0;x=[int(round(v*100)) for v in s.place(now)]
        s.sock.send(struct.pack('<4sHBBQ32sIIIIB3xiii32s',b'DSR1',2,1,0,s.id,b'abc',s.sequence,s.stream,s.ack,0,1,*x,s.name.encode())+struct.pack('<I16sQ',GAME,BUILD,t0))
        s.nextHello=now+500
    def tick(s,now):
        if now>=s.nextHello:s.hello(now)
        if s.offset is None:return
        if not s.stated:
            blob=name_blob(s.name);s.sock.send(struct.pack('<4sHBBQBBHI',b'DSR1',2,7,0,s.id,1,0,len(blob),1)+blob);s.stated=True
        s.published+=1;s.stamp=max(s.stamp+1,int(now+s.offset));x=[int(round(v*100)) for v in s.place(now)]
        animation=bytes([1,1,0,1,40])+struct.pack('<f',.25)+bytes([0,0,0])
        s.sock.send(struct.pack('<4sHBBQIBBHQ',b'DSR1',2,5,0,s.id,s.published,3,0,len(animation),s.stamp)+struct.pack('<HBBiii',0,3,64,*x)+bytes(36)+animation)
    def bye(s):s.sock.send(struct.pack('<4sHBBQ32s',b'DSR1',2,10,0,s.id,b'abc'))
    def receive(s,now):
        while True:
            try:b=s.sock.recv(2000)
            except BlockingIOError:return
            kind=b[6]
            if kind==2:
                w=struct.unpack('<QIIHHHHQQ4IHHHHH2x',b[8:])
                if w[2]!=s.stream:s.stream,s.ack,s.near=w[2],0,{}
                if w[1] in s.sent and s.sent[w[1]]==w[7] and s.offset is None:s.offset=w[8]-(w[7]+now)/2
            elif kind==4:
                stream,first,count=struct.unpack_from('<IIB',b,8);o=20
                for i in range(count):
                    event,player,n=struct.unpack_from('<BQB',b,o);nick=b[o+10:o+10+n].decode();o+=10+n
                    if stream!=s.stream or first+i!=s.ack+1:continue
                    s.ack+=1
                    if event==1:s.near[player]=nick
                    elif s.near.pop(player,None) is not None:s.left.append((now,player))
                s.nextHello=now
            elif kind==6:
                count=b[16];o=20
                for i in range(count):
                    source,sequence,stamp,parts,_,size,generation,_=struct.unpack_from('<QIQBBHHH',b,o);o+=28+(52 if parts&1 else 0)+(size if parts&2 else 0)
                    s.entries.setdefault(source,[]).append((now,parts))
            elif kind==8:
                source,k,_,size,version=struct.unpack_from('<QBBHI',b,8)
                if k==1 and version:s.names[source]=name_of(b[44:44+size])
def play(players,seconds,stop=lambda:False):
    end=ms()+seconds*1000;bySocket={p.sock.fileno():p for p in players};nextTick=ms()
    while ms()<end and not stop():
        now=ms()
        if now>=nextTick:
            for p in players:p.tick(now)
            nextTick+=50
        ready,_,_=select.select([p.sock for p in players],[],[],max(0,nextTick-ms())/1000)
        now=ms()
        for r in ready:bySocket[r.fileno()].receive(now)
def swarms(north,south,seconds,count_north=4,extra=()):
    common=['--mode','broadcast','--key','abc','--radius','3','--seconds',str(seconds),'--names']
    a=subprocess.Popen([swarm,'--relay','127.0.0.1:%d'%north.relay,'--count',str(count_north),'--centre','0','0','20',*common,*extra],stdout=subprocess.PIPE,text=True,encoding='utf-8')
    b=subprocess.Popen([swarm,'--relay','127.0.0.1:%d'%south.relay,'--count','4','--centre','0','0','21',*common],stdout=subprocess.PIPE,text=True,encoding='utf-8')
    out=a.communicate(timeout=seconds+30)[0],b.communicate(timeout=seconds+30)[0];return out

with tempfile.TemporaryDirectory() as d:
    north,south=Server(d,'north'),Server(d,'south')
    # The north knows the south's relay; the south learns the north's from its first HELLO with the right tag.
    north_link=['--link','south','--link-key',KEY,'--link-presence','yes','--link-relay','127.0.0.1:%d'%south.relay,'--link-presence-budget','20000']
    south_link=['--link','north','--link-key',KEY,'--link-presence','yes']
    north.start(*north_link);south.start(*south_link)
    try:
        assert 'link south: presence of broadcast players with the relay at 127.0.0.1:%d (at most 20000 bytes a second and 64 players each way)'%south.relay in north.text(),north.text()
        assert 'link north: presence of broadcast players with the relay at the address of its first HELLO' in south.text(),south.text()
        bea,fay,carl=Player(7,'Bea',(0,0,0),north.relay),Player(8,'Fay',(5000,0,0),north.relay),Player(9,'Carl',(10,0,0),south.relay)
        play([bea,fay,carl],5)
        assert 'link north: presence with server ' in south.text() and 'link south: presence with server ' in north.text()
        # Each sees the other once, under an id of his own server's, with his name and his server's name.
        assert len(carl.near)==1 and len(bea.near)==1 and not fay.near,(carl.near,bea.near,fay.near)
        (bea_there,nick),=carl.near.items();(carl_there,nick2),=bea.near.items()
        assert bea_there not in (7,8,9) and carl_there not in (7,8,9),(bea_there,carl_there)
        assert nick=='Bea'+DOT+'north' and nick2=='Carl'+DOT+'south',(nick,nick2)
        assert carl.names.get(bea_there)=='Bea'+DOT+'north' and bea.names.get(carl_there)=='Carl'+DOT+'south',(carl.names,bea.names)
        # Every update within 30 m, with its animation: about 20 a second for the last 2 s.
        t=ms();recent=[e for e in carl.entries[bea_there] if e[0]>t-2000];assert 34<=len(recent)<=44 and all(e[1]==3 for e in recent),len(recent)
        recent=[e for e in bea.entries[carl_there] if e[0]>t-2000];assert 34<=len(recent)<=44,len(recent)
        # Fay is near nobody of the south: never sent.
        assert 'Bea'+DOT+'north) of link north joined' in south.text() and 'Fay' not in south.text() and 'Carl'+DOT+'south) of link south joined' in north.text()
        # The north goes away: Carl loses Bea within the link's silence (3 s), and the log says so.
        north.stop();gone=ms()
        play([carl],5,stop=lambda:not carl.near)
        assert not carl.near and carl.left and 2500<=carl.left[-1][0]-gone<=4200,(carl.near,carl.left and carl.left[-1][0]-gone)
        assert until(lambda:'link north: presence lost (silent), 1 of its players gone' in south.text(),2),south.text()
        print('presence socket test: Bea and Carl across two servers, %d updates a second; Carl lost Bea %.1f s after the north stopped'%(len(recent)//2,(carl.left[-1][0]-gone)/1000))
        for p in (bea,fay,carl):p.bye();p.sock.close()
        # ---- A new north, and stand-ins with the same ids on both servers.
        north.start(*north_link)
        assert until(lambda:south.text().count('link north: presence with server ')==2,10),south.text()
        a,b=swarms(north,south,6)
        done=[l for l in b.splitlines() if l.startswith('SWARM done')];assert done and 'online=4/4 near=28 live=28' in done[0],b
        names=re.findall(r'SWARM name id=(\d+) "(.*)"',b)
        own=sorted(n for i,n in names if 1001<=int(i)<=1004);theirs=sorted(n for i,n in names if not 1001<=int(i)<=1004)
        assert own==['Sam 1001','Sam 1002','Sam 1003','Sam 1004'] and theirs==['Sam %d%snorth'%(i,DOT) for i in range(1001,1005)],names
        assert 'online=4/4 near=28 live=28' in a and len(re.findall(r'SWARM name id=\d+ "Sam \d+'+DOT+'south"',a))==4,a
        # Thirty stand-ins in the north near the four in the south: the link stays within its 20000 bytes a second.
        a,b=swarms(north,south,12,30)
        reports=[(int(x),int(y)) for x,y in re.findall(r'link south: presence, 5 s: sent \d+ datagrams, \d+ bytes \((\d+) a second, budget 20000\), \d+ updates, (\d+) left out',north.text())]
        assert reports and all(rate<=20000*1.25 for rate,_ in reports) and any(left>0 for _,left in reports),reports
        # Each of the four has 33 players near (3 of his own server, 30 of the north) and is shown 32 of them, the
        # near_cap of a friends server: the linked server's players count for it like the others, and all are live.
        done=[l for l in b.splitlines() if l.startswith('SWARM done')];assert done and 'online=4/4 near=128 live=128' in done[0],(done,reports)
        print('presence socket test: stand-ins with the same ids on both servers see 8 each; with 30 in the north the link sent %s bytes a second (budget 20000)'%[r for r,_ in reports])
    finally:
        north.stop();south.stop()
print('presence socket test passed')
