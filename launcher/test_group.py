"""Group play over real sockets, game-free: the server command line the launcher makes starts the built coop-world
with its relay and the host's choice of rules, the sign-in addresses the launcher writes for the host and for a joiner
are two players there, and stand-in players (mod/swarm.cpp) holding the key and session number of the launcher's mod
config find each other through it. Then the launcher's side of the server's rules (strand.h) on the same server: the
policy read, the joiner's choices remembered and sent, his objects listed, offered and withdrawn; and the joins that
are refused (a wrong key, nobody listening, choices a full server does not take) or go on with defaults (a server
from before the launcher's API, stood in for by a small HTTP server here). Arguments: the launcher's test program,
coop-world, coop-swarm."""
import http.client,http.server,json,os,re,socket,subprocess,sys,tempfile,threading
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
def free_port():
    s=socket.socket();s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
slot=next(n for n in range(15) if free(49100+n))
TICKET='14000000aabbccddeeff00117856341201001001'+'0'*40
def strand(*args):
    r=subprocess.run([helper,*args],capture_output=True,text=True,timeout=60)
    check(r.returncode==0,'the strand helper ran: %r %s'%(args,r.stderr))
    out={}
    for line in r.stdout.splitlines():
        key,_,value=line.partition(' ');out.setdefault(key,[]).append(value)
    return out
with tempfile.TemporaryDirectory(prefix='ds-launcher-group-') as d:
    lines=subprocess.run([helper,'--group',str(49000+slot),'305419896',os.path.join(d,'world.json'),'public'],capture_output=True,text=True,check=True).stdout.splitlines()
    args=[l[4:] for l in lines if l.startswith('arg ')];said=dict(l.split(' ',1) for l in lines if not l.startswith('arg '))
    port=49100+slot
    check('--relay-port' in args and '--relay-key' in args,'the group server is started with its relay')
    p=subprocess.Popen([world,*args],stdout=subprocess.PIPE,text=True)
    try:
        first,second=p.stdout.readline(),p.stdout.readline()
        check('listening on 0.0.0.0:%d'%port in first and 'relay on 0.0.0.0:%d'%port in second,'server and relay listen on the port the launcher names: '+first+second)
        p.stdout.readline();check(p.stdout.readline().startswith('policy public:'),'a new world has the rules the host chose')
        check(said['relay']=='127.0.0.1:%d'%port,'the host game is given the relay on loopback')
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        def sign_in(address,name):
            path=address[len('http://127.0.0.1:%d'%port):];check(address.startswith('http://127.0.0.1:%d/'%port),'sign-in address '+address)
            c.request('GET',path+'?provider=steam&display_name='+name+'&code='+TICKET);r=c.getresponse();return r.status,json.loads(r.read() or b'{}')
        status,host=sign_in(said['host'],'Host');check(status==200,'the host signs in at the address in its config')
        status,join=sign_in(said['join'],'Joiner');check(status==200,'a joiner on the same PC signs in at the address in its config')
        check(host['user']['id']!=join['user']['id'] and join['user']['providerId']=='p78','host and joiner on one PC (one Steam account) are two players')
        c.request('GET','/auth/ds?provider=steam&display_name=X&code='+TICKET);r=c.getresponse();r.read();check(r.status==404,'nobody signs in without the group key')
        done=subprocess.run([swarm,'--relay',said['relay'],'--key',said['key'],'--session',said['session'],'--count','3','--centre','0','0','0','--radius','5','--seconds','3','--require-connected'],
                            capture_output=True,text=True,timeout=60)
        last=(done.stdout.strip().splitlines() or [''])[-1]
        check(done.returncode==0 and re.match(r'SWARM done online=3/3 near=6 sessions=6 connected=6 ',last),'three players with the launcher\'s key and session meet through the relay: '+last)
        bad=subprocess.run([swarm,'--relay',said['relay'],'--key','w1','--session',said['session'],'--count','2','--centre','0','0','0','--radius','5','--seconds','1.5','--require-connected'],
                           capture_output=True,text=True,timeout=60)
        check(bad.returncode==1 and 'online=0/2' in bad.stdout,'another key is not answered')
        # The server's rules, before the joiner's game connects: read, his choices remembered and sent for the
        # account his game signs in as (on this PC the name in its address, never the host's Steam account).
        account=said['joinaccount'];check(account==join['user']['providerId'],'the launcher acts as the account the joiner\'s game signs in as: '+account)
        key=said['key'];ini=os.path.join(d,'launcher.ini');open(ini,'w').write('[launcher]\nname=Joiner\n')
        j=strand('--strand','127.0.0.1',str(port),key,account,ini)
        check(j['fetched']==['0'] and j['start']==['1'],'the rules read and the join goes on: %r'%j)
        policy=json.loads(j['policy'][0]);check(policy['preset']=='public' and [s['value'] for s in policy['settings'] if s['key']=='build']==['members'],'the policy as the server has it')
        kept=open(ini).read();check('[launcher]\nname=Joiner\n' in kept and '[server-%s]\noffer_builds=yes\noffer_old=no\n'%policy['server'] in kept,'the choices remembered for this server: '+kept)
        j=strand('--strand','127.0.0.1',str(port),key,account,ini,'ask','yes')
        check(j['start']==['1'] and open(ini).read().count('offer_builds=')==1 and 'offer_builds=ask\noffer_old=yes' in open(ini).read(),'other choices replace the remembered ones')
        c.request('GET','/%s/coop/me?player=%s'%(key,account));r=c.getresponse();me=json.loads(r.read())
        check(r.status==200 and me['choices']=={'offer_builds':'ask','offer_old':'yes'} and me['player']==join['user']['id'],'the server has his choices: %r'%me)
        # My strand on this server: what he built there, with its state; his offer and his withdrawal.
        c.request('POST','/%s/gate/e/createObject'%key,body=json.dumps({'t':'l','q':226,'px':1,'py':2,'pz':3}),headers={'Authorization':'Bearer '+join['session']['token']})
        r=c.getresponse();made=json.loads(r.read());check(r.status==200,'the joiner\'s game builds')
        rows=strand('--strand-mine','127.0.0.1',str(port),key,account)
        check(rows.get('row',[''])[0].startswith(made['id']+' held 0 ladder  area 226  held: only members build on this server (then your answer)') and '1 held by the server' in rows['me'][0],'the strand view: %r'%rows)
        done=strand('--strand-act','127.0.0.1',str(port),key,account,'offer',made['id'])
        check(done['refused']==['Refused: %s stays held: only members build on this server.'%made['id']],'an offer the rules do not allow is refused in the server\'s words: %r'%done)
        check(strand('--strand-act','127.0.0.1',str(port),key,host['user']['providerId'],'withdraw',made['id']).get('refused'),'nobody withdraws another player\'s object')
        done=strand('--strand-act','127.0.0.1',str(port),key,account,'withdraw','all')
        check(done['done']==['Withdrawn: 1 removed, 0 stay as remnants.'] and 'row' not in strand('--strand-mine','127.0.0.1',str(port),key,account),'withdrawn: %r'%done)
        # Refused joins: a key the server does not know, nobody listening.
        j=strand('--strand','127.0.0.1',str(port),'w1',account,ini);check(j['fetched']==['2'] and j['start']==['0'],'a wrong key: not joined: %r'%j)
        j=strand('--strand','127.0.0.1',str(free_port()),key,account,ini);check(j['fetched']==['3'] and j['start']==['0'] and 'did not answer' in j['message'][0],'nobody listening: not joined: %r'%j)
        c.close()
    finally:
        p.kill();p.wait()
    # A full server does not take a new player's choices: the game is not started.
    solo_port=free_port();solo=subprocess.Popen([world,'--port',str(solo_port),'--prefix','wsolo','--data',os.path.join(d,'solo.json'),'--policy','solo'],stdout=subprocess.PIPE,text=True)
    try:
        check('listening' in solo.stdout.readline(),'the solo server starts')
        c=http.client.HTTPConnection('127.0.0.1',solo_port,timeout=5);c.request('GET','/wsolo/k/owner/auth/ds?provider=steam&display_name=Owner&code=00');r=c.getresponse();r.read();c.close()
        j=strand('--strand','127.0.0.1',str(solo_port),'wsolo','p78',ini)
        check(j['fetched']==['0'] and j['start']==['0'] and 'all its players' in j['message'][0],'choices a full server does not take: not joined: %r'%j)
        check(strand('--strand','127.0.0.1',str(solo_port),'wsolo','owner',ini)['start']==['1'],'its own player joins')
    finally:
        solo.kill();solo.wait()
    # A server from before the launcher's API: it answers the game (401 without a session) and has no /coop/.
    class Old(http.server.BaseHTTPRequestHandler):
        protocol_version='HTTP/1.1'
        def do_GET(self):
            status=401 if self.path.startswith('/wold/gate/') else 404
            body=b'{"status":"%d"}'%status;self.send_response(status);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
        def log_message(self,*args):pass
    old=http.server.HTTPServer(('127.0.0.1',0),Old);threading.Thread(target=old.serve_forever,daemon=True).start()
    try:
        before=open(ini).read()
        j=strand('--strand','127.0.0.1',str(old.server_address[1]),'wold','p78',ini)
        check(j['fetched']==['1'] and j['start']==['1'] and 'does not show its rules' in j['message'][0] and open(ini).read()==before,'an older server: said so, joined with its defaults: %r'%j)
        j=strand('--strand','127.0.0.1',str(old.server_address[1]),'wnew','p78',ini);check(j['fetched']==['2'] and j['start']==['0'],'and with a key it does not know: not joined')
    finally:
        old.shutdown()
print('group server socket test passed (port %d)'%port)
