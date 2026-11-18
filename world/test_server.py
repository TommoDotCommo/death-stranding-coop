"""The built server over a real socket: sign-in, a gateway call, keep-alive, the secret prefix, the saved file; the server
policy: an upload that waits for the owner is handed to nobody else until he accepts it, one the policy does not let be
shared is answered and held for its builder, and the policy commands; which objects a game is handed (newest first, spread
apart, the same ones again after newer ones came and after a restart); the launcher's API on the running server; paver
materials between players (roads paved together), kept over a restart and taken back by remove-player."""
import http.client,json,math,os,socket,subprocess,sys,tempfile,time
exe=sys.argv[1]
def free_port():
    s=socket.socket();s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
TICKET='14000000aabbccddeeff00117856341201001001'+'0'*40
with tempfile.TemporaryDirectory() as d:
    data=os.path.join(d,'world.json')
    def start(port,*more,path=None):
        p=subprocess.Popen([exe,'--port',str(port),'--data',path or data,'--prefix','abc',*more],stdout=subprocess.PIPE,text=True)
        for _ in range(4):
            if 'listening' in p.stdout.readline():return p
        p.kill();raise AssertionError('server did not start')
    port=free_port();p=start(port)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        def call(method,path,body=None,token=None):
            c.request(method,path,body=body,headers={'Authorization':'Bearer '+token} if token else {})
            r=c.getresponse();return r.status,json.loads(r.read() or b'{}')
        assert call('GET','/auth/ds?code='+TICKET)[0]==404
        status,auth=call('GET','/abc/auth/ds?provider=steam&display_name=Ann&code='+TICKET);assert status==200,status
        token=auth['session']['token'];assert auth['session']['gateway']=='http://127.0.0.1:%d/abc/gate'%port,auth
        assert call('GET','/abc/gate/e/getVersion',token=token)==(200,{'version':'0.12','major':0,'minor':12})
        assert call('GET','/abc/gate/e/getVersion')[0]==401
        status,obj=call('POST','/abc/gate/e/createObject','{"data":"{"t":"l","q":226,"px":1,"py":2,"pz":3}"}',token);assert status==200 and obj['l']==[1,2,3],obj
        c.close()
        raw=socket.create_connection(('127.0.0.1',port));raw.sendall(b'GET /abc/gate/e/getVersion HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n');assert raw.recv(100)==b'';raw.close()
        saved=json.load(open(data));assert len(saved['objects'])==1 and saved['players'][0]['name']=='Ann',saved
    finally:
        p.kill();p.wait()
    port=free_port();p=start(port)   # a restart keeps the world and the session
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        c.request('POST','/abc/gate/e/getQpidObjects',body='{"ids":["%s"]}'%obj['id'],headers={'Authorization':'Bearer '+token})
        r=c.getresponse();assert r.status==200 and len(json.loads(r.read())['objects'])==1
    finally:
        p.kill();p.wait()
    # The policy: on a public server an upload waits for the owner, and nobody else is handed it until he accepts it.
    run=lambda *args:subprocess.run([exe,*args],capture_output=True,text=True,timeout=30)
    for wrong in (['--policy','club'],['--policy-set','limit=x'],['--policy-set','colour=red']):
        r=run('--port','1',*wrong);assert r.returncode==2,(wrong,r.stdout,r.stderr)
    held=os.path.join(d,'policy.json')
    port=free_port();p=start(port,'--policy','public','--policy-set','build=everyone','--policy-set','identity=none',path=held)   # identity keys: test_identity.py
    try:
        assert 'policy public:' in p.stdout.readline()
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        def gate(token,name,body):
            c.request('POST','/abc/gate/e/'+name,body=json.dumps(body),headers={'Authorization':'Bearer '+token});r=c.getresponse();return r.status,json.loads(r.read() or b'{}')
        def sign_in(name):
            c.request('GET','/abc/k/%s/auth/ds?provider=steam&display_name=%s&code=00'%(name,name));r=c.getresponse();auth=json.loads(r.read());return auth['session']['token'],auth['user']['id']
        (ann,ann_id),(bob,_)=sign_in('ann'),sign_in('bob')
        status,made=gate(ann,'createObject',{'t':'l','q':226,'px':1,'py':2,'pz':3});assert status==200,made
        assert gate(bob,'findQpidObjects',{'q':226})[1]['n']['oa']==[] and gate(bob,'getQpidObjects',{'ids':[made['id']]})[1]['objects']==[]
        assert len(gate(ann,'getQpidObjects',{'ids':[made['id']]})[1]['objects'])==1
        c.close()
    finally:
        p.kill();p.wait()
    r=run('review','--data',held);assert r.returncode==0 and made['id'] in r.stdout and 'waits for the owner' in r.stdout,r.stdout
    r=run('players','--data',held);assert r.returncode==0 and ann_id in r.stdout and 'waiting 1' in r.stdout,r.stdout
    r=run('policy','--data',held);assert r.returncode==0 and 'preset public' in r.stdout and 'build          everyone (preset: members)' in r.stdout,r.stdout
    r=run('review','--data',held,'--accept',made['id']);assert r.returncode==0,r.stderr
    port=free_port();p=start(port,'--policy','friends',path=held)   # an existing world keeps its preset
    try:
        assert 'policy public:' in p.stdout.readline()
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        assert [o['id'] for o in gate(bob,'findQpidObjects',{'q':226})[1]['n']['oa']]==[made['id']]
        c.close()
    finally:
        p.kill();p.wait()
    # Withdrawn on a public server: the ladder stays as a remnant; the owner then removes all of Ann's.
    r=run('withdraw','--data',held,'--player',ann_id,'--all');assert r.returncode==0 and '1 stay as remnants, 0 removed' in r.stdout,r.stdout
    r=run('remove-player','--data',held,'--player',ann_id);assert r.returncode==0 and 'removed 1 objects' in r.stdout,r.stdout
    assert json.load(open(held))['objects']==[]
    r=run('player-set','--data',held,'--player',ann_id,'offer_builds=ask');assert r.returncode==0,r.stderr
    assert json.load(open(held))['players'][0]['offer_builds']=='ask'
    before=open(held).read()
    for wrong,code in ((['withdraw','--data',held,'--player',ann_id],2),(['player-set','--data',held,'--player',ann_id],2),(['review','--data',held,'--accept','a','--reject','b'],2),
                       (['remove-player','--data',held],2),(['policy','--data',held,'--set','limit=-1'],1),(['withdraw','--data',held,'--player','zygo_nobody','--all'],1),
                       (['review','--data',held,'--accept','lnothing00000'],1),(['player-set','--data',held,'--player',ann_id,'member=maybe'],1)):
        r=run(*wrong);assert r.returncode==code and open(held).read()==before,(wrong,r.returncode,r.stdout,r.stderr)
    # build=nobody: the upload is answered as any other and held for its builder; once the policy allows it, he offers it.
    port=free_port();held2=os.path.join(d,'held.json');p=start(port,'--policy-set','build=nobody',path=held2)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        (ann,ann_id),(bob,_)=sign_in('ann'),sign_in('bob')
        status,kept=gate(ann,'createObject',{'t':'l','q':226,'px':1,'py':2,'pz':3});assert status==200 and kept['id'][0]=='l' and len(kept['id'])==13 and kept['l']==[1,2,3],(status,kept)
        assert gate(bob,'findQpidObjects',{'q':226})[1]['n']['oa']==[] and gate(bob,'getQpidObjects',{'ids':[kept['id']]})[1]['objects']==[]
        assert len(gate(ann,'getQpidObjects',{'ids':[kept['id']]})[1]['objects'])==1
        c.close()
    finally:
        p.kill();p.wait()
    saved=json.load(open(held2))['objects'];assert len(saved)==1 and saved[0]['share']=='held' and saved[0]['held']=='build',saved
    r=run('review','--data',held2);assert r.returncode==0 and 'held for its builder only: nobody builds on this server' in r.stdout,r.stdout
    r=run('offer','--data',held2,'--player',ann_id,'--object',kept['id']);assert r.returncode==1 and 'stays held' in r.stderr,(r.stdout,r.stderr)
    r=run('policy','--data',held2,'--set','build=everyone');assert r.returncode==0,r.stderr
    r=run('offer','--data',held2,'--player',ann_id,'--all');assert r.returncode==0 and 'offered: 1 objects' in r.stdout,r.stdout
    port=free_port();p=start(port,path=held2)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        assert [o['id'] for o in gate(bob,'findQpidObjects',{'q':226})[1]['n']['oa']]==[kept['id']]
        c.close()
    finally:
        p.kill();p.wait()
    # Which objects a game is handed: the asked number, the newest first, none closer than 20 m to another of its kind, and
    # the same ones again when newer ones come, also after a restart (kept in the world file).
    curated=os.path.join(d,'curated.json');port=free_port();p=start(port,path=curated)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        (ann,_),(bob,bob_id)=sign_in('ann'),sign_in('bob')
        build=lambda x,z:gate(ann,'createObject',{'t':'e','q':226,'px':x,'py':0,'pz':z,'exp':2})[1]['id']
        grid=[build((i%4)*400,(i//4)*400) for i in range(16)]   # a 4 m grid, in centimetres
        time.sleep(1.1);newer=[build(100000,0),build(110000,0)]
        def ask():
            answer=gate(bob,'findQpidObjects',{'q':226,'o':{'ct':{'e':3,'l':2}}})[1]['n']['oa'];return [o['id'] for o in answer],{o['id']:o['l'] for o in answer}
        first,places=ask();assert len(first)==3 and set(newer)<=set(first) and len(set(first)&set(grid))==1,(first,newer)   # one of a 12 m grid
        assert all(math.dist(places[a],places[b])>=2000 for a in first for b in first if a!=b),places
        time.sleep(1.1);later=[build(200000,0),build(210000,0)]
        assert ask()[0]==first and not set(later)&set(ask()[0])
        c.close()
    finally:
        p.kill();p.wait()
    saved=json.load(open(curated))['picks'];assert [(k['player'],k['area'],k['ids']) for k in saved]==[(bob_id,226,first)],saved
    port=free_port();p=start(port,path=curated)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        assert ask()[0]==first
        assert gate(ann,'deleteObject',{'id':first[0]})[0]==200
        again=ask()[0];assert again[:2]==first[1:] and first[0] not in again and again[2] in later,again
        c.close()
    finally:
        p.kill();p.wait()
    # The launcher's API on the running server: the policy in words, a player's choices before his game signs in, his
    # objects with their state, his offer and withdrawal; saved like any other change.
    port=free_port();api_world=os.path.join(d,'api.json');p=start(port,'--policy','public','--policy-set','build=everyone','--policy-set','identity=none',path=api_world)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        def coop(method,call,body=None,player=None):
            c.request(method,'/abc/coop/'+call+('?player='+player if player else ''),body=None if body is None else json.dumps(body))
            r=c.getresponse();return r.status,json.loads(r.read() or b'{}')
        c.request('GET','/coop/policy');r=c.getresponse();r.read();assert r.status==404
        status,policy=coop('GET','policy');assert status==200 and policy['preset']=='public' and len(policy['settings'])==17,policy
        build=[s for s in policy['settings'] if s['key']=='build'][0];assert build['value']=='everyone' and build['preset']=='members' and build['text'].startswith('What any player'),build
        status,me=coop('GET','me',player='ann');assert status==200 and me['known'] is False,me
        status,me=coop('POST','choices',{'player':'ann','offer_builds':'ask','offer_old':'no'});assert status==200 and me['known'] and me['applies']['offer_builds']=='ask',me
        assert coop('POST','choices',{'player':'ann','member':'yes'})[0]==400
        (ann,ann_id),(bob,_)=sign_in('ann'),sign_in('bob');assert ann_id==me['player']
        status,made=gate(ann,'createObject',{'t':'l','q':226,'px':1,'py':2,'pz':3});assert status==200
        status,mine=coop('GET','mine',player='ann');assert status==200 and [(o['id'],o['state'],o.get('waiting_for')) for o in mine['objects']]==[(made['id'],'waiting','you')],mine
        assert coop('POST','offer',{'player':'ann'})[0]==400 and coop('POST','withdraw',{'player':'bob','object':made['id']})[0]==403
        status,done=coop('POST','offer',{'player':'ann','object':made['id']});assert status==200 and done['offered']==1 and done['me']['waiting_owner']==1,done
        assert gate(bob,'findQpidObjects',{'q':226})[1]['n']['oa']==[]
        c.request('GET','/abc/coop/withdraw?player=ann',body=json.dumps({'player':'ann','all':True}));r=c.getresponse();r.read();assert r.status==404
        c.close()
    finally:
        p.kill();p.wait()
    saved=json.load(open(api_world));assert saved['players'][0]['offer_builds']=='ask' and saved['objects'][0]['share']=='review',saved
    r=run('review','--data',api_world,'--accept',made['id']);assert r.returncode==0,r.stderr
    port=free_port();p=start(port,path=api_world)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        assert coop('GET','mine',player='ann')[1]['objects'][0]['state']=='shared'
        status,done=coop('POST','withdraw',{'player':'ann','all':True});assert status==200 and done['remnants']==1 and done['me']['remnants']==1,done
        assert coop('GET','mine',player='ann')[1]['objects'][0]['state']=='remnant' and [o['id'] for o in gate(bob,'findQpidObjects',{'q':226})[1]['n']['oa']]==[made['id']]
        c.close()
    finally:
        p.kill();p.wait()
    assert json.load(open(api_world))['objects'][0]['remnant'] is True
    # What the game is told: the mod's read with its notice token (a header, never written by --trace), the player's own
    # objects only, from where it left off, and again after a restart (a first read there: every one).
    told=os.path.join(d,'told.json');trace=os.path.join(d,'calls.log')
    port=free_port();p=start(port,'--policy-set','build=members','--name','North','--trace',trace,path=told)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        def read(token,since=''):
            c.request('GET','/abc/coop/notices'+('?since='+since if since else ''),headers={'X-Coop-Token':token});r=c.getresponse();return r.status,json.loads(r.read() or b'{}')
        status,given=coop('POST','notice-token',{'player':'ann'});assert status==200 and len(given['token'])>=16,given
        token=given['token'];(ann,_),(bob,_)=sign_in('ann'),sign_in('bob')
        status,first=read(token);assert status==200 and first['full'] and first['name']=='North' and first['objects']==[],first
        status,made=gate(ann,'createObject',{'t':'l','q':226,'px':1,'py':2,'pz':3});assert status==200
        assert gate(bob,'createObject',{'t':'e','q':226,'px':9,'py':2,'pz':3})[0]==200
        status,after=read(token,first['version']);assert status==200 and not after['full'] and after['objects']==[{'id':made['id'],'kind':'ladder','state':'held','why':'member'}],after
        assert read(token,after['version'])[1]['objects']==[] and read('wrong')[0]==403
        c.request('GET','/abc/coop/notices?token='+token);r=c.getresponse();r.read();assert r.status==403
        c.close()
    finally:
        p.kill();p.wait()
    traced=[row for row in open(trace) if '?token=' not in row];assert '/coop/notices' in ''.join(traced) and not any(token in row for row in traced),traced   # (the test's own address with it aside)
    r=run('player-set','--data',told,'--player',json.load(open(told))['players'][0]['id'],'member=yes');assert r.returncode==0,r.stderr
    r=run('review','--data',told,'--accept-player',json.load(open(told))['players'][0]['id']);assert r.returncode==0,r.stderr
    port=free_port();p=start(port,path=told)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        status,again=read(token,after['version']);assert status==200 and again['full'] and again['objects']==[{'id':made['id'],'kind':'ladder','state':'shared','why':''}],again
        c.close()
    finally:
        p.kill();p.wait()
    # Roads paved together: what Ann puts into a paver reaches Bob as the game asks for it (the body as the game wraps it),
    # added up and kept over a restart; the owner's remove-player on the stopped world takes it back.
    paved=os.path.join(d,'paved.json');port=free_port();p=start(port,path=paved)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        (ann,ann_id),(bob,_)=sign_in('ann'),sign_in('bob')
        def devote(token,ci,ri,n):
            c.request('POST','/abc/gate/e/devoteHighwayResources',body='{"data":"{"ph":[{"ci":%d,"ri":%d,"n":%d}]}"}'%(ci,ri,n),headers={'Authorization':'Bearer '+token})
            r=c.getresponse();return r.status,json.loads(r.read() or b'{}')
        assert devote(ann,2031,2,300)==(200,{'e':0}) and devote(ann,2031,2,100)[0]==200 and devote(ann,2031,9,1)[0]==400
        asked={'cds':[{'cid':2031,'lgd':1700000000},{'cid':2032,'lgd':0}],'rids':[0,2,3]}
        status,got=gate(bob,'getHighwayResources',asked);assert status==200 and got=={'pr':[{'id':'2031_2','cid':2031,'rid':2,'n':400,'un':0}],'cs':[{'cid':2031,'cs':[{'u':ann_id,'l':0}]}],'ul':0},got
        assert gate(bob,'sendHighwayLikes',{'hls':[{'u':ann_id,'l':2}]})[0]==200 and gate(ann,'getHighwayResources',asked)[1]['ul']==2
        c.close()
    finally:
        p.kill();p.wait()
    assert json.load(open(paved))['highway']==[{'player':ann_id,'cid':2031,'rid':2,'n':400}]
    port=free_port();p=start(port,path=paved)
    try:
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
        assert gate(bob,'getHighwayResources',asked)[1]['pr'][0]['n']==400
        c.close()
    finally:
        p.kill();p.wait()
    r=run('remove-player','--data',paved,'--player',ann_id);assert r.returncode==0,r.stderr
    assert 'highway' not in json.load(open(paved)) and 'highway_likes' not in json.load(open(paved))
    # --idle-exit: a server nobody asks anything leaves by itself.
    port=free_port();p=subprocess.Popen([exe,'--port',str(port),'--idle-exit','1'],stdout=subprocess.PIPE,text=True)
    try:assert p.wait(timeout=20)==0
    finally:p.kill()
print('server socket test passed')
