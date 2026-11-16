"""Identity keys over real sockets, game-free: the launcher's core (the functions its --dev-direct --dev-start
--group-host and --group-join paths run, with --prepare-only stopping before a game) against the built coop-world on
loopback. The host's server is started with the command line the launcher makes for public rules (with the host's key
as the owner's); the host's game and a joiner's are given a sign-in ticket and a relay token; both sign in with them
as the accounts of their keys; nothing signs in by a name or a Steam ticket; an unsigned call, a call signed by another
key, a call sent twice or changed after signing are refused; the owner's calls take the host's key only; and the relay
answers a HELLO with the player's token for his own id, and nothing with the group's key or another id. Arguments: the
launcher's test program, coop-world."""
import http.client,json,os,socket,struct,subprocess,sys,tempfile
helper,world=sys.argv[1:3]
def check(ok,what):
    if not ok:
        sys.stderr.write('identity oracle: %s\n'%what);sys.exit(1)
def free(port):
    try:
        for kind in (socket.SOCK_STREAM,socket.SOCK_DGRAM):
            # As the server binds (SO_REUSEADDR): a port whose earlier connections are still closing is free for it.
            s=socket.socket(socket.AF_INET,kind);s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind(('0.0.0.0',port));s.close()
        return True
    except OSError:return False
def run(*args):
    r=subprocess.run([helper,*args],capture_output=True,text=True,timeout=60)
    check(r.returncode==0,'the helper ran: %r %s'%(args,r.stderr))
    out={}
    for line in r.stdout.splitlines():
        key,_,value=line.partition(' ');out.setdefault(key,[]).append(value)
    return out
def one(d,key):return d[key][0]
slot=next(n for n in range(15) if free(49100+n))
port,secret=49100+slot,'305419896';prefix='w'+secret
TICKET='14000000aabbccddeeff00117856341201001001'+'0'*40
def hello(player,key,sequence=1):
    return struct.pack('<4sHBBQ32sIIIIB3xiii32s',b'DSR1',1,1,0,player,key,sequence,0,0,0,1,0,0,0,b'py')
with tempfile.TemporaryDirectory(prefix='ds-launcher-identity-') as d:
    hostKey,joinKey,otherKey=(os.path.join(d,n+'.key') for n in ('host','join','other'))
    host,joiner,other=(run('--keyed-keys',k) for k in (hostKey,joinKey,otherKey))
    check(one(host,'pub')==one(run('--keyed-keys',hostKey),'pub') and len(one(host,'pub'))==64,'the key is made once and kept')
    check(len({one(host,'pub'),one(joiner,'pub'),one(other,'pub')})==3,'every install has its own key')
    args=[l for l in run('--keyed-host',str(49000+slot),secret,os.path.join(d,'world.json'),hostKey)['arg']]
    check(args[args.index('--owner')+1]==one(host,'pub') and args[args.index('--policy')+1]=='public','the host\'s server: public rules, the host\'s key as the owner\'s')
    p=subprocess.Popen([world,*args],stdout=subprocess.PIPE,text=True)
    try:
        lines=[p.stdout.readline() for _ in range(5)]
        check(any('identity keys' in l for l in lines) and any('owner key '+one(host,'pub')[:16] in l for l in lines),'the server takes keys and knows its owner: %r'%lines)
        # The host's game and a joiner's (on this PC: never a name in the address).
        h=run('--keyed-host-config',str(49000+slot),secret,os.path.join(d,'world.json'),hostKey)
        check(h.get('keyed')==['1'] and one(h,'account')==one(host,'account'),'the host\'s launcher reads the policy as its key: %r'%h)
        check('/t/' in one(h,'online') and '/k/' not in one(h,'online') and one(h,'key')!=prefix and one(h,'player')==one(host,'relay'),'the host\'s game: a ticket, a relay token, the relay id of its key: %r'%h)
        ini=os.path.join(d,'launcher.ini');open(ini,'w').write('[launcher]\nname=Joiner\n')
        j=run('--keyed-join','127.0.0.1',str(port),secret,ini,joinKey)
        check(j.get('start')==['1'] and j.get('keyed')==['1'] and one(j,'account')==one(joiner,'account'),'the joiner joins as his key: %r'%j)
        check('/t/' in one(j,'online') and '/k/' not in one(j,'online') and one(j,'player')==one(joiner,'relay') and one(j,'key') not in (prefix,one(h,'key')),'the joiner\'s game: his own ticket and token: %r'%j)
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        def get(path,headers={}):
            c.request('GET',path,headers=headers);r=c.getresponse();return r.status,json.loads(r.read() or b'{}')
        def post(path,body,headers={}):
            c.request('POST',path,body=body,headers=headers);r=c.getresponse();return r.status,json.loads(r.read() or b'{}')
        def sign_in(address,name):return get(address[len('http://127.0.0.1:%d'%port):]+'?provider=steam&display_name='+name+'&code='+TICKET)
        status,hs=sign_in(one(h,'online'),'Host');check(status==200 and hs['user']['providerId']==one(host,'account'),'the host signs in with his ticket, as his key: %r'%hs)
        status,js=sign_in(one(j,'online'),'Joiner');check(status==200 and js['user']['providerId']==one(joiner,'account') and js['user']['id']!=hs['user']['id'],'the joiner too, as another player')
        # Nobody signs in by a name in the address or a Steam ticket; a made-up ticket is no ticket.
        for path in ('/%s/k/host/auth/ds'%prefix,'/%s/k/%s/auth/ds'%(prefix,one(joiner,'account')),'/%s/auth/ds'%prefix,'/%s/t/0123456789abcdef0123456789abcdef/auth/ds'%prefix):
            status,_=get(path+'?provider=steam&display_name=X&code='+TICKET);check(status==403,'no sign-in at '+path)
        token=js['session']['token']
        status,made=post('/%s/gate/e/createObject'%prefix,json.dumps({'t':'l','q':226,'px':1,'py':2,'pz':3}),{'Authorization':'Bearer '+token})
        check(status==200,'the joiner\'s game builds with its session')
        # The API: unsigned, signed by another key, sent twice, changed after signing: refused.
        acct=one(joiner,'account')
        check(get('/%s/coop/me?player=%s'%(prefix,acct))[0]==401 and post('/%s/coop/choices'%prefix,json.dumps({'player':acct,'offer_builds':'no'}))[0]==401,'unsigned calls are refused')
        def signed(key,method,call,body):
            s=run('--keyed-signed',str(port),prefix,method,call,body,key);return one(s,'target'),dict(x.split(' ',1) for x in s['header'])
        body=json.dumps({'player':acct,'offer_builds':'no'},separators=(',',':'))
        target,headers=signed(joinKey,'POST','choices',body)
        status,me=post(target,body,headers);check(status==200 and me['choices']['offer_builds']=='no' and me['player']==js['user']['id'],'his own signed call: %r'%me)
        check(post(target,body,headers)[0]==401,'the same call again is refused')
        target,headers=signed(joinKey,'POST','choices',body);check(post(target,body.replace('"no"','"yes"'),headers)[0]==401,'a call changed after signing is refused')
        target,headers=signed(otherKey,'POST','choices',body);check(post(target,body,headers)[0]==403,'another key does not act for him')
        target,headers=signed(joinKey,'GET','mine?player='+acct,'');status,mine=get(target,headers)
        check(status==200 and [o['id'] for o in mine['objects']]==[made['id']],'his objects, to his signed call')
        # The owner's calls: the host's key only.
        target,headers=signed(hostKey,'GET','owner/players','');status,players=get(target,headers)
        check(status==200 and len(players['players'])==2,'the owner lists the players: %r'%players)
        target,headers=signed(joinKey,'GET','owner/players','');check(get(target,headers)[0]==403,'a player is not the owner')
        body=json.dumps({'set':['limit=5']});target,headers=signed(hostKey,'POST','owner/policy',body);status,policy=post(target,body,headers)
        check(status==200 and [s['value'] for s in policy['settings'] if s['key']=='limit']==['5'],'the owner changes the rules on the running server')
        c.close()
        # The relay: the player's token for his own id, nothing else.
        u=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);u.settimeout(.6);u.connect(('127.0.0.1',port))
        def answer(datagram):
            u.send(datagram)
            try:return u.recv(2000)
            except socket.timeout:return b''
        jid,jkey,hid=int(one(j,'player')),one(j,'key').encode(),int(one(h,'player'))
        check(answer(hello(jid,prefix.encode()))==b'','the group\'s key is not answered (an older mod refuses cleanly: no answer)')
        check(answer(hello(hid,jkey))==b'','his token does not speak for another id')
        w=answer(hello(jid,jkey,sequence=2));check(len(w)==32 and struct.unpack_from('<Q',w,8)[0]==jid,'his token for his id: WELCOME')
    finally:
        p.kill();p.wait()
print('identity socket test passed (port %d)'%port)
