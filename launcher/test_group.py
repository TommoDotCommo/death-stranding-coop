"""Group play over real sockets, game-free: the server command line the launcher makes starts the built coop-world
with its relay, the sign-in addresses the launcher writes for the host and for a joiner are two players there, and
stand-in players (mod/swarm.cpp) holding the key and session number of the launcher's mod config find each other
through it. Arguments: the launcher's test program, coop-world, coop-swarm."""
import http.client,json,os,re,socket,subprocess,sys,tempfile
helper,world,swarm=sys.argv[1:4]
def check(ok,what):
    if not ok:
        sys.stderr.write('group server oracle: %s\n'%what);sys.exit(1)
def free(port):
    try:
        for kind in (socket.SOCK_STREAM,socket.SOCK_DGRAM):
            s=socket.socket(socket.AF_INET,kind);s.bind(('0.0.0.0',port));s.close()
        return True
    except OSError:return False
slot=next(n for n in range(15) if free(49100+n))
TICKET='14000000aabbccddeeff00117856341201001001'+'0'*40
with tempfile.TemporaryDirectory(prefix='ds-launcher-group-') as d:
    lines=subprocess.run([helper,'--group',str(49000+slot),'305419896',os.path.join(d,'world.json')],capture_output=True,text=True,check=True).stdout.splitlines()
    args=[l[4:] for l in lines if l.startswith('arg ')];said=dict(l.split(' ',1) for l in lines if not l.startswith('arg '))
    port=49100+slot
    check('--relay-port' in args and '--relay-key' in args,'the group server is started with its relay')
    p=subprocess.Popen([world,*args],stdout=subprocess.PIPE,text=True)
    try:
        first,second=p.stdout.readline(),p.stdout.readline()
        check('listening on 0.0.0.0:%d'%port in first and 'relay on 0.0.0.0:%d'%port in second,'server and relay listen on the port the launcher names: '+first+second)
        check(said['relay']=='127.0.0.1:%d'%port,'the host game is given the relay on loopback')
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        def sign_in(address,name):
            path=address[len('http://127.0.0.1:%d'%port):];check(address.startswith('http://127.0.0.1:%d/'%port),'sign-in address '+address)
            c.request('GET',path+'?provider=steam&display_name='+name+'&code='+TICKET);r=c.getresponse();return r.status,json.loads(r.read() or b'{}')
        status,host=sign_in(said['host'],'Host');check(status==200,'the host signs in at the address in its config')
        status,join=sign_in(said['join'],'Joiner');check(status==200,'a joiner on the same PC signs in at the address in its config')
        check(host['user']['id']!=join['user']['id'] and join['user']['providerId']=='p78','host and joiner on one PC (one Steam account) are two players')
        c.request('GET','/auth/ds?provider=steam&display_name=X&code='+TICKET);r=c.getresponse();r.read();check(r.status==404,'nobody signs in without the group key')
        c.close()
        done=subprocess.run([swarm,'--relay',said['relay'],'--key',said['key'],'--session',said['session'],'--count','3','--centre','0','0','0','--radius','5','--seconds','3','--require-connected'],
                            capture_output=True,text=True,timeout=60)
        last=(done.stdout.strip().splitlines() or [''])[-1]
        check(done.returncode==0 and re.match(r'SWARM done online=3/3 near=6 sessions=6 connected=6 ',last),'three players with the launcher\'s key and session meet through the relay: '+last)
        bad=subprocess.run([swarm,'--relay',said['relay'],'--key','w1','--session',said['session'],'--count','2','--centre','0','0','0','--radius','5','--seconds','1.5','--require-connected'],
                           capture_output=True,text=True,timeout=60)
        check(bad.returncode==1 and 'online=0/2' in bad.stdout,'another key is not answered')
    finally:
        p.kill();p.wait()
print('group server socket test passed (port %d)'%port)
