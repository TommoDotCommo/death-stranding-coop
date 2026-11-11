"""The built server over a real socket: sign-in, a gateway call, keep-alive, the secret prefix, the saved file."""
import http.client,json,os,socket,subprocess,sys,tempfile,time
exe=sys.argv[1]
def free_port():
    s=socket.socket();s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
TICKET='14000000aabbccddeeff00117856341201001001'+'0'*40
with tempfile.TemporaryDirectory() as d:
    data=os.path.join(d,'world.json')
    def start(port):
        p=subprocess.Popen([exe,'--port',str(port),'--data',data,'--prefix','abc'],stdout=subprocess.PIPE,text=True)
        for _ in range(3):
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
print('server socket test passed')
