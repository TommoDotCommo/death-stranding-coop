"""The built server's relay over real UDP sockets on loopback: a HELLO written here byte by byte (the layout in
core/relay.h) is answered, anything without the key is not, and a swarm of stand-in players (mod/swarm.cpp) finds each
other through it and connects every pair with the real protocol core."""
import re,socket,struct,subprocess,sys
exe,swarm=sys.argv[1],sys.argv[2]
def free_port(kind=socket.SOCK_STREAM):
    s=socket.socket(socket.AF_INET,kind);s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def hello(player,key,sequence=1,position=(0,0,0)):
    return struct.pack('<4sHBBQ32sIIIIB3xiii32s',b'DSR1',1,1,0,player,key,sequence,0,0,0,1,*[int(x*100) for x in position],b'py')
def refused(args):
    r=subprocess.run([exe,*args],capture_output=True,text=True,timeout=20);return r.returncode==2
# The relay never starts without a key beyond loopback, with distances the wrong way round, or with a key the wire cannot carry.
assert refused(['--port','1','--bind','192.0.2.1','--relay-port','2'])
assert refused(['--port','1','--relay-port','2','--relay-enter','300','--relay-leave','200'])
assert refused(['--port','1','--relay-port','2','--relay-key','not a key'])
assert refused(['--port','1','--relay-port','70000'])
port,relay=free_port(),free_port(socket.SOCK_DGRAM)
p=subprocess.Popen([exe,'--port',str(port),'--relay-port',str(relay),'--prefix','abc','--relay-enter','30','--relay-leave','40'],stdout=subprocess.PIPE,text=True)
try:
    lines=[p.stdout.readline(),p.stdout.readline()]
    assert 'listening' in lines[0] and 'relay on 127.0.0.1:%d'%relay in lines[1] and '30 m to 40 m' in lines[1],lines
    u=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);u.settimeout(.5);u.connect(('127.0.0.1',relay))
    def silent():
        try:u.recv(2000)
        except socket.timeout:return True
        return False
    # Without the key (the prefix, as none was given), garbage, a message meant for players, a HELLO cut short: no answer.
    for datagram in [hello(7,b'wrong'),hello(7,b''),b'\x00'*112,b'DSR1'+b'\xff'*200,hello(7,b'abc')[:-1],hello(7,b'abc')+b'\x00',
                     struct.pack('<4sHBBQ',b'DSR1',1,5,0,1001)+b'x'*128]:
        u.send(datagram)
    assert silent()
    # With it: WELCOME, 32 bytes, with the id, the HELLO's sequence, a stream number and the server's distances.
    u.send(hello(7,b'abc',sequence=5,position=(1000,0,0)));w=u.recv(2000);assert len(w)==32,len(w)
    magic,version,kind,zero,player,sequence,stream,enter,leave,timeout,near=struct.unpack('<4sHBBQIIHHHH',w)
    assert (magic,version,kind,zero,player,sequence,enter,leave,timeout,near)==(b'DSR1',1,2,0,7,5,30,40,10,0) and stream!=0,w
    # The same id from another socket: refused (24 bytes, reason 2), and the first socket stays the player.
    v=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);v.settimeout(.5);v.connect(('127.0.0.1',relay))
    v.send(hello(7,b'abc',sequence=9));r=v.recv(2000);assert len(r)==24 and r[6]==3 and r[20]==2 and struct.unpack_from('<QI',r,8)==(7,9),r
    # Eight stand-in players on a 10 m circle: every one of the 56 sessions connects through the relay and samples flow.
    done=subprocess.run([swarm,'--relay','127.0.0.1:%d'%relay,'--key','abc','--count','8','--centre','100','50','-200','--radius','10','--seconds','4','--require-connected'],
                        capture_output=True,text=True,timeout=60)
    last=done.stdout.strip().splitlines()[-1];m=re.match(r'SWARM done online=8/8 near=56 sessions=56 connected=56 accepted=(\d+)$',last)
    assert done.returncode==0 and m and int(m.group(1))>500,(done.returncode,done.stdout[-600:],done.stderr[-300:])
    # Two groups 100 m apart, with the server's 30 m: sessions inside each group only (2 x 4 x 3), and with --only just the ones named.
    a=subprocess.Popen([swarm,'--relay','127.0.0.1:%d'%relay,'--key','abc','--count','4','--centre','0','0','0','--radius','5','--seconds','4','--first-id','2001'],stdout=subprocess.PIPE,text=True)
    b=subprocess.run([swarm,'--relay','127.0.0.1:%d'%relay,'--key','abc','--count','4','--centre','100','0','0','--radius','5','--seconds','3','--first-id','3001','--require-connected'],capture_output=True,text=True,timeout=60)
    assert b.returncode==0 and 'near=12 sessions=12 connected=12' in b.stdout.strip().splitlines()[-1],b.stdout[-400:]
    out=a.communicate(timeout=30)[0];assert 'near=12 sessions=12 connected=12' in out.strip().splitlines()[-1],out[-400:]
    a=subprocess.Popen([swarm,'--relay','127.0.0.1:%d'%relay,'--key','abc','--count','1','--centre','0','0','0','--radius','0','--seconds','4','--first-id','2001'],stdout=subprocess.PIPE,text=True)
    b=subprocess.run([swarm,'--relay','127.0.0.1:%d'%relay,'--key','abc','--count','5','--centre','0','0','0','--radius','8','--seconds','3','--first-id','3001','--only','2001','--require-connected'],capture_output=True,text=True,timeout=60)
    assert b.returncode==0 and 'near=25 sessions=5 connected=5' in b.stdout.strip().splitlines()[-1],b.stdout[-400:]
    a.communicate(timeout=30)
    # The wrong key: nobody is answered, and the tool says so with its exit code.
    bad=subprocess.run([swarm,'--relay','127.0.0.1:%d'%relay,'--key','other','--count','2','--centre','0','0','0','--radius','5','--seconds','1.5','--require-connected'],capture_output=True,text=True,timeout=60)
    assert bad.returncode==1 and 'online=0/2' in bad.stdout,bad.stdout[-300:]
finally:
    p.kill();p.wait()
print('relay socket test passed')
