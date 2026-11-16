"""Linked servers with the built server: the offline commands on world files (export, import twice, origins, forget, a
file that is refused), then two servers on loopback that take from each other with filters: new, changed and removed
objects follow, nothing comes back to where it was built, the wrong key gets nothing, requests in the wrong form are
refused, and either side catches up after it was away."""
import http.client,json,os,socket,subprocess,sys,tempfile,time
exe=sys.argv[1]
def free_port():
    s=socket.socket();s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def run(*args):
    r=subprocess.run([exe,*args],capture_output=True,text=True,timeout=30);return r.returncode,r.stdout+r.stderr
def until(what,seconds=20):
    end=time.time()+seconds
    while time.time()<end:
        if what():return True
        time.sleep(.1)
    return False
class Server:
    def __init__(self,folder,name,*args,prefix=''):
        self.port=free_port();self.data=os.path.join(folder,name+'.json');self.log=os.path.join(folder,name+'.log');self.args=args;self.prefix=prefix;self.p=None
    def start(self):
        self.p=subprocess.Popen([exe,'--port',str(self.port),'--data',self.data,'--log',self.log,*(['--prefix',self.prefix] if self.prefix else []),*self.args],stdout=subprocess.DEVNULL)
        def up():
            try:socket.create_connection(('127.0.0.1',self.port),timeout=1).close();return True
            except OSError:return False
        assert until(up),'server did not start'
    def stop(self):
        if self.p:self.p.kill();self.p.wait();self.p=None
    def call(self,method,path,body=None,headers=None):
        c=http.client.HTTPConnection('127.0.0.1',self.port,timeout=10)
        c.request(method,('/'+self.prefix if self.prefix else '')+path,body=body,headers=headers or {});r=c.getresponse();text=r.read();c.close()
        return r.status,json.loads(text or b'{}')
    def sign_in(self,name):
        status,auth=self.call('GET','/k/%s/auth/ds?provider=steam&display_name=%s&code=00'%(name,name));assert status==200,status;return auth['session']['token']
    def gate(self,token,name,body):
        status,answer=self.call('POST','/gate/e/'+name,json.dumps(body),{'Authorization':'Bearer '+token});assert status==200,(name,status,answer);return answer
    def make(self,token,kind,area):return self.gate(token,'createObject',{'t':kind,'q':area,'px':1,'py':2,'pz':3})['id']
    def found(self,token,area):return sorted(o['t'] for o in self.gate(token,'findQpidObjects',{'q':area})['n']['oa'])
    def saved(self):return json.load(open(self.data))
    def logged(self,text):return os.path.exists(self.log) and text in open(self.log).read()
KEY='key-of-a-and-b'
# A server with a link that has no key, or half a link, does not start; nor do the offline commands with half their options.
for wrong in (['--link','n'],['--link','n','--link-key','short'],['--link','n','--link-key','has space in it'],['--link-key',KEY],['--link','n','--link-key',KEY,'--link-url','http://example.org:80'],
              ['--link','n','--link-key',KEY,'--link-url','10.0.0.1:80'],['--link','n','--link-key',KEY,'--link-import','all'],['--link','n','--link-key',KEY,'--link-export','types=ladders'],
              ['--link','n','--link-key',KEY,'--link-export',''],['--link','n','--link-key',KEY,'--link','m','--link-key',KEY],['--link','n','--link-key',KEY,'--link-every','0'],
              ['--link','bad name','--link-key',KEY]):
    code,text=run('--port','1',*wrong);assert code==2,(wrong,code,text)
for wrong in (['export'],['export','--data','x'],['import','--data','x'],['forget','--data','x'],['forget','--data','x','--server','!'],['export','--data','x','--out','y','--filter','colour=red'],
              ['export','--data','x','--out','y','--prune'],['origins'],['explode','--data','x']):
    code,text=run(*wrong);assert code==2,(wrong,code,text)
with tempfile.TemporaryDirectory() as d:
    path=lambda name:os.path.join(d,name)
    # ---- Offline: two world files and a file between them.
    one=Server(d,'one');one.start()
    try:
        ann=one.sign_in('ann');bob=one.sign_in('bob')
        ladder=one.make(ann,'l',226);one.make(ann,'r',226);one.make(ann,'e',300);one.make(bob,'l',226)
    finally:one.stop()
    code,text=run('export','--data',one.data,'--out',path('all.json'));assert code==0 and text.startswith('exported 4 objects of server '),text
    one_id=text.split()[-1];assert one_id==one.saved()['server_id']
    code,text=run('export','--data',one.data,'--out',path('ladders.json'),'--filter','types=ladder;owners=ann');assert code==0 and text.startswith('exported 1 objects'),text
    assert run('export','--data',path('nothing.json'),'--out',path('x.json'))[0]==1
    exported=json.load(open(path('all.json')));assert exported['server']==one_id and len(exported['objects'])==4 and {o['name'] for o in exported['objects']}=={'ann','bob'}
    assert 'key' not in json.dumps(exported['objects']) and 'sessions' not in exported   # no account keys, no session tokens
    two=path('two.json')
    code,text=run('import','--data',two,'--in',path('all.json'),'--filter','areas=226');assert code==0 and 'added 3,' in text and 'not passing the filter 1' in text,text
    first=open(two).read();world=json.loads(first)
    assert len(world['objects'])==3 and all(o['origin']['server']==one_id and o['origin']['via']==one_id and o['id']!=o['origin']['id'] and o['id'][0]==o['post']['t'] and len(o['id'])==13 for o in world['objects'])
    assert world['server_id']!=one_id
    code,text=run('import','--data',two,'--in',path('all.json'),'--filter','areas=226');assert code==0 and 'added 0, changed 0, removed 0, as before 3' in text,text
    assert open(two).read()==first                                               # the same file twice changes nothing
    code,text=run('import','--data',two,'--in',path('ladders.json'),'--prune');assert code==0 and 'removed 2' in text and 'as before 1' in text,text
    code,text=run('origins','--data',two);assert code==0 and '0 objects built here, 1 imported' in text and 'from server %s: 1 objects'%one_id in text,text
    assert run('import','--data',one.data,'--in',path('all.json'))[0]==1         # a server does not import its own export
    before=open(two).read()
    for name,content in (('garbage.json','not json at all'),('empty.json',''),('list.json','[]'),('kind.json',json.dumps(dict(exported,kind='other'))),
                         ('object.json',json.dumps(dict(exported,objects=exported['objects']+[{'id':'x'}]))),
                         ('huge.json',json.dumps(dict(exported,objects=[dict(exported['objects'][0],post={'t':'l','q':1,'st':'x'*70000})])))):
        open(path(name),'w').write(content)
        code,text=run('import','--data',two,'--in',path(name));assert code==1 and 'nothing changed' in text and open(two).read()==before,(name,code,text)
    assert run('import','--data',two,'--in',path('missing.json'))[0]==1
    code,text=run('forget','--data',two,'--server','0000ffff');assert code==0 and text.startswith('forgot 0 objects'),text
    code,text=run('forget','--data',two,'--server',one_id);assert code==0 and text.startswith('forgot 1 objects'),text
    assert json.load(open(two))['objects']==[]
    # ---- Live: A gives B its ladders and ropes and takes everything; B gives everything and takes area 226 only.
    a=Server(d,'a',prefix='abc');b=Server(d,'b')
    a.args=['--link','b','--link-key',KEY,'--link-url','http://127.0.0.1:%d'%b.port,'--link-export','types=l,r','--link-import','all','--link-every','1']
    b.args=['--link','a','--link-key',KEY,'--link-url','http://127.0.0.1:%d/abc'%a.port,'--link-export','all','--link-import','areas=226','--link-every','1']
    c=Server(d,'c');c.args=['--link','a','--link-key','not-the-right-key','--link-url','http://127.0.0.1:%d/abc'%a.port,'--link-import','all','--link-every','1']
    a.start();b.start();c.start()
    try:
        ann=a.sign_in('ann');bea=b.sign_in('bea')
        ladder=a.make(ann,'l',226);a.make(ann,'r',227);a.make(ann,'e',226);cairn=b.make(bea,'e',226)
        assert until(lambda:b.found(bea,226)==['l']),b.found(bea,226)          # the ladder; not the cairn (A does not give those)
        assert b.found(bea,227)==[]                                              # nor the rope (B does not take that area)
        assert until(lambda:a.found(ann,226)==['e']),a.found(ann,226)          # B's cairn at A
        imported=[o for o in b.saved()['objects'] if 'origin' in o];assert len(imported)==1 and imported[0]['origin']['id']==ladder and imported[0]['id']!=ladder and imported[0]['id'][0]=='l',imported
        assert imported[0]['origin']['server']==a.saved()['server_id']
        # The wrong key gets nothing, and the server it asked says so in its log.
        assert until(lambda:c.logged('link a: no answer')) and a.logged('link request refused: no link has that key')
        assert 'objects' not in c.saved() or c.saved()['objects']==[]
        # Requests no linked server would send.
        ask=json.dumps({'format':1,'server':'someOtherServer','have':'','after':''});right={'Authorization':'Link '+KEY}
        assert a.call('POST','/link/pull',ask,right)[0]==200
        assert a.call('POST','/link/pull',ask)[0]==404 and a.call('POST','/link/pull',ask,{'Authorization':'Link wrong-key-here'})[0]==404 and a.call('GET','/link/pull',None,right)[0]==404
        assert a.call('POST','/link/pull',ask,{'Authorization':'Bearer '+ann})[0]==404
        for body in ('garbage','{}','[1,2',json.dumps({'format':1,'server':'x'*5000,'have':'','after':''}),json.dumps({'format':1,'server':'someOtherServer','have':'','after':'','extra':1})):
            assert a.call('POST','/link/pull',body,right)[0]==400,body
        raw=socket.create_connection(('127.0.0.1',a.port));raw.sendall(b'POST /abc/link/pull HTTP/1.1\r\nAuthorization: Link '+KEY.encode()+b'\r\nContent-Length: 99999999\r\n\r\n');assert raw.recv(100)==b'';raw.close()
        # Nothing loops: after a few more rounds each side still has its own objects and one copy of what it took.
        time.sleep(3)
        count=lambda s:(len([o for o in s.saved()['objects'] if 'origin' not in o]),len([o for o in s.saved()['objects'] if 'origin' in o]))
        assert count(a)==(3,1) and count(b)==(1,1),(count(a),count(b))
        # Removed at A: gone at B. A new one while B is away: B has it soon after it is back, with what it had.
        a.gate(ann,'deleteObject',{'id':ladder});assert until(lambda:b.found(bea,226)==[]),b.found(bea,226)
        b.stop();second=a.make(ann,'l',226);third=a.make(ann,'l',226);b.start()
        assert until(lambda:len(b.found(bea,226))==2),b.found(bea,226)
        assert count(b)==(1,2),count(b)
        # A away: B keeps what it has. A back (the same world file, one object fewer): B follows.
        held=sorted(o['id'] for o in b.saved()['objects'])
        a.stop();time.sleep(2.5);assert sorted(o['id'] for o in b.saved()['objects'])==held and b.logged('link a: no answer')
        a.start();a.gate(ann,'deleteObject',{'id':second})
        assert until(lambda:len(b.found(bea,226))==1),b.found(bea,226)
        assert [o['origin']['id'] for o in b.saved()['objects'] if 'origin' in o]==[third]
        assert until(lambda:a.found(ann,226)==['e']) and count(a)==(3,1)
        # A linked server asking does not keep a server alive that should leave when no game asks.
        idle=Server(d,'idle');idle.args=['--idle-exit','2','--link','x','--link-key',KEY,'--link-export','all'];idle.start()
        end=time.time()+25
        while idle.p.poll() is None and time.time()<end:
            try:idle.call('POST','/link/pull',ask,right)
            except OSError:pass
            time.sleep(.3)
        assert idle.p.poll()==0,idle.p.poll()
        idle.p=None
    finally:
        for server in (a,b,c):server.stop()
        if 'idle' in dir():idle.stop()
print('linked servers socket test passed')
