"""The built server over a real socket: sign-in, a gateway call, keep-alive, the secret prefix, the saved file; the server
policy: an upload that waits for the owner is handed to nobody else until he accepts it, and the policy commands."""
import http.client,json,os,socket,subprocess,sys,tempfile,time
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
    port=free_port();p=start(port,'--policy','public','--policy-set','build=everyone',path=held)
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
    # --idle-exit: a server nobody asks anything leaves by itself.
    port=free_port();p=subprocess.Popen([exe,'--port',str(port),'--idle-exit','1'],stdout=subprocess.PIPE,text=True)
    try:assert p.wait(timeout=20)==0
    finally:p.kill()
print('server socket test passed')
