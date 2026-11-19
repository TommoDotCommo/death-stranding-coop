"""The server owner's calls over real sockets, game-free: the launcher's core (owner::run, which its --dev-direct
--owner-... flags and its Server owner window use) against the built coop-world on loopback, started with the command
line the launcher makes for public rules and the host's key as the owner's. Two joiners build (held: only members'
builds are shared); the owner lists the players (key short, counts, likes) and what waits (builder, kind, area), makes
one a member, accepts all of his, rejects all of the other's and nobody else's, accepts one object (the other is
handed three of them, and his own rejected one back, as the world is the server's), changes a rule on
the running server (the words of /coop/policy), and removes all of a player's objects (his account stays). A joiner's
key is refused as the owner, and an owner call sent twice (the same nonce) is refused. Arguments: the launcher's test
program, coop-world."""
import http.client,json,os,socket,subprocess,sys,tempfile
helper,world=sys.argv[1:3]
def check(ok,what):
    if not ok:
        sys.stderr.write('owner oracle: %s\n'%what);sys.exit(1)
def free(port):
    try:
        for kind in (socket.SOCK_STREAM,socket.SOCK_DGRAM):
            s=socket.socket(socket.AF_INET,kind);s.bind(('0.0.0.0',port));s.close()
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
with tempfile.TemporaryDirectory(prefix='ds-launcher-owner-') as d:
    hostKey,annKey,bobKey=(os.path.join(d,n+'.key') for n in ('host','ann','bob'))
    host,ann,bob=(run('--keyed-keys',k) for k in (hostKey,annKey,bobKey))
    def owner(kind,*rest,key=hostKey):
        r=subprocess.run([helper,'--owner-run',str(49000+slot),secret,kind,*rest,key],capture_output=True,text=True,timeout=60)
        check(r.returncode==0,'the owner helper ran: %s'%r.stderr);return json.loads(r.stdout)
    args=[l for l in run('--keyed-host',str(49000+slot),secret,os.path.join(d,'world.json'),hostKey)['arg']]
    p=subprocess.Popen([world,*args],stdout=subprocess.PIPE,text=True)
    try:
        lines=[p.stdout.readline() for _ in range(5)]
        check(any('owner key '+one(host,'pub')[:16] in l for l in lines),'the server knows its owner: %r'%lines)
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        def call(method,path,body=None,headers={}):
            c.request(method,path,body=body,headers=headers);r=c.getresponse();return r.status,json.loads(r.read() or b'{}')
        # Two joiners: their games sign in with their tickets and build; what they build is held (public: members only).
        ids={}
        for name,key in (('Ann',annKey),('Bob',bobKey)):
            ini=os.path.join(d,name+'.ini');open(ini,'w').write('[launcher]\nname=%s\n'%name)
            j=run('--keyed-join','127.0.0.1',str(port),secret,ini,key);check(j.get('start')==['1'],'%s joins: %r'%(name,j))
            status,auth=call('GET',one(j,'online')[len('http://127.0.0.1:%d'%port):]+'?provider=steam&display_name=%s&code=%s'%(name,TICKET))
            check(status==200,'%s signs in'%name);ids[name]=(auth['user']['id'],auth['session']['token'])
        def build(name,x):
            status,made=call('POST','/%s/gate/e/createObject'%prefix,json.dumps({'t':'l','q':226,'px':x,'py':2,'pz':3}),{'Authorization':'Bearer '+ids[name][1]})
            check(status==200,'%s builds'%name);return made['id']
        a1,a2,b1=build('Ann',10000),build('Ann',30000),build('Bob',50000)
        # The owner's list: the players with their keys short and their counts, and what waits with builder, kind and area.
        r=owner('list');check(r['ok'] and r['server'] and r['action']=='list','the owner lists the server: %r'%r)
        rows={x['id']:x for x in r['players']}
        check(rows[ids['Ann'][0]]['key']==one(ann,'short') and rows[ids['Ann'][0]]['held']==2 and not rows[ids['Ann'][0]]['member'] and rows[ids['Ann'][0]]['name']=='Ann','Ann\'s row: %r'%rows)
        waits={(w['id'],w['name'],w['kind'],w['area'],w['held'],w['waiting_for']) for w in r['waiting']}
        check(waits=={(a1,'Ann','ladder',226,'member','owner'),(a2,'Ann','ladder',226,'member','owner'),(b1,'Bob','ladder',226,'member','owner')},'what waits: %r'%waits)
        # A player's key is not the owner's.
        r=owner('list',key=annKey);check(not r['ok'] and r['message'].startswith('Refused: only the owner'),'a joiner\'s key is refused as the owner: %r'%r)
        r=owner('member',ids['Bob'][0],'yes',key=annKey);check(not r['ok'] and r['message'].startswith('Refused: only the owner'),'a joiner cannot make anyone a member: %r'%r)
        # Members, the review queue: all of Ann's accepted once she is a member, all of Bob's rejected and nobody else's.
        r=owner('member',ids['Ann'][0],'yes');check(r['ok'] and {x['id']:x for x in r['players']}[ids['Ann'][0]]['member'],'Ann made a member: %r'%r)
        a3=build('Ann',70000)   # a member's upload now waits for the owner's review
        r=owner('accept-player',ids['Ann'][0]);rows={x['id']:x for x in r['players']}
        check(r['ok'] and rows[ids['Ann'][0]]['shared']==3 and rows[ids['Ann'][0]]['held']==0,'all of Ann\'s accepted: %r'%rows[ids['Ann'][0]])
        a4=build('Ann',90000)
        r=owner('reject-player',ids['Bob'][0]);rows={x['id']:x for x in r['players']}
        check(r['ok'] and rows[ids['Bob'][0]]['kept']==1 and rows[ids['Bob'][0]]['held']==0 and [w['id'] for w in r['waiting']]==[a4],'all of Bob\'s rejected, Ann\'s still waits: %r'%r)
        r=owner('accept',a4);check(r['ok'] and r['waiting']==[] and {x['id']:x for x in r['players']}[ids['Ann'][0]]['shared']==4,'one object accepted: %r'%r)
        # Bob's game is handed Ann's (three of four: per_builder), and likes one: the likes in the owner's list.
        status,found=call('POST','/%s/gate/e/findQpidObjects'%prefix,json.dumps({'q':226}),{'Authorization':'Bearer '+ids['Bob'][1]})
        handed={o['id'] for o in found['n']['oa']};told={o['id']:o['c'] for o in found['n']['oa']}
        check(status==200 and len(handed&{a1,a2,a3,a4})==3 and handed<={a1,a2,a3,a4,b1},'what Bob is handed (public: at most three of one builder): %r'%handed)
        # Public rules make the world the server's (policy world=server): Bob's own ladder, though rejected for the
        # others, stands in his world, handed back told as another builder's (neither his id nor Ann's).
        check(b1 in handed and told[b1] not in (ids['Bob'][0],ids['Ann'][0]),'Bob\'s own handed back, not as his: %r'%told)
        call('POST','/%s/gate/e/sendLike'%prefix,json.dumps({'oid':a1,'lp':2,'lp2':0,'lt':''}),{'Authorization':'Bearer '+ids['Bob'][1]})
        check({x['id']:x for x in owner('list')['players']}[ids['Ann'][0]]['likes']==2,'likes in the owner\'s list')
        # The rules on the running server: a setting changed, the preset's value beside it, the server's words.
        r=owner('policy','-','withdrawal=go');s={x['key']:x for x in r['policy']['settings']}
        check(r['ok'] and s['withdrawal']['value']=='go' and s['withdrawal']['preset']=='stay' and 'removed for everyone' in s['withdrawal']['text'],'a rule changed live: %r'%s.get('withdrawal'))
        r=owner('policy','-','limit=-1');check(not r['ok'] and r['message'].startswith('Refused'),'a value that is none: refused, nothing changed: %r'%r['message'])
        # Removing all of a player's objects: gone for everyone, his account stays.
        r=owner('remove',ids['Ann'][0]);rows={x['id']:x for x in r['players']}
        check(r['ok'] and 'Removed 4 objects' in r['message'] and ids['Ann'][0] in rows and rows[ids['Ann'][0]]['shared']==0,'all of Ann\'s removed, her account kept: %r'%r)
        status,found=call('POST','/%s/gate/e/findQpidObjects'%prefix,json.dumps({'q':226}),{'Authorization':'Bearer '+ids['Bob'][1]})
        check([o['id'] for o in found['n']['oa']]==[b1],'Bob is handed none of them now, only his own: %r'%found['n']['oa'])
        # An owner call sent again with the same nonce is refused, and so is one signed by a joiner.
        body=json.dumps({'player':ids['Bob'][0],'member':'yes'},separators=(',',':'))
        s=run('--keyed-signed',str(port),prefix,'POST','owner/member',body,hostKey);target,headers=one(s,'target'),dict(x.split(' ',1) for x in s['header'])
        check(call('POST',target,body,headers)[0]==200,'the owner\'s signed call')
        check(call('POST',target,body,headers)[0]==401,'the same call again (a nonce used before) is refused')
        s=run('--keyed-signed',str(port),prefix,'POST','owner/member',body,annKey);target,headers=one(s,'target'),dict(x.split(' ',1) for x in s['header'])
        check(call('POST',target,body,headers)[0]==403,'a joiner\'s signed call is not the owner\'s')
        c.close()
    finally:
        p.kill();p.wait()
print('owner socket test passed (port %d)'%port)
