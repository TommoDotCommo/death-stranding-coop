"""Builder names with the built server (README, "Builder names"): two servers on loopback that take from each other, one
with a display name (--name North) and one without. A game on either is told its own server's players by name only,
and a builder from the other server as "Name · Home": the other server's own name, or, where it has none, the name of
the link to it; a long name is cut so the whole label stays within 24 characters and 32 bytes and the home still shows.
The name is in the policy the launcher reads and in the world file, every import's home too, and `origins` prints it.
Argument: coop-world."""
import http.client,json,os,socket,subprocess,sys,tempfile,time
exe=sys.argv[1]
DOT=' · '
def check(ok,what):
    if not ok:
        sys.stderr.write('names socket oracle: %s\n'%what);sys.exit(1)
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
    def __init__(self,folder,name):
        self.port=free_port();self.data=os.path.join(folder,name+'.json');self.p=None
    def start(self,*args):
        self.p=subprocess.Popen([exe,'--port',str(self.port),'--data',self.data,*args],stdout=subprocess.DEVNULL)
        def up():
            try:socket.create_connection(('127.0.0.1',self.port),timeout=1).close();return True
            except OSError:return False
        check(until(up),'the server started')
    def stop(self):
        if self.p:self.p.kill();self.p.wait();self.p=None
    def call(self,method,path,body=None,headers=None):
        c=http.client.HTTPConnection('127.0.0.1',self.port,timeout=10)
        c.request(method,path,body=body,headers=headers or {});r=c.getresponse();text=r.read();c.close()
        return r.status,json.loads(text or b'{}')
    def sign_in(self,key,name):
        status,auth=self.call('GET','/k/%s/auth/ds?provider=steam&display_name=%s&code=00'%(key,name));check(status==200,'sign-in');return auth['session']['token'],auth['user']['id']
    def gate(self,token,name,body):
        status,answer=self.call('POST','/gate/e/'+name,json.dumps(body),{'Authorization':'Bearer '+token});check(status==200,(name,status,answer));return answer
    def lookup(self,token,player):
        users=self.gate(token,'lookup',{'ids':[player]})['users'];return users[0]['displayName'] if len(users)==1 else None
for wrong in (' North','North ','abcdefghijklm','a\tb',''):
    code,text=run('--port','1','--name',wrong);check(code==2,'--name %r is refused: %s'%(wrong,text))
with tempfile.TemporaryDirectory() as d:
    north,south=Server(d,'north'),Server(d,'south')
    KEY='key-of-north-and-south'
    north.start('--name','North','--link','south','--link-key',KEY,'--link-url','http://127.0.0.1:%d'%south.port,'--link-export','all','--link-import','all','--link-every','1')
    south.start('--link','north','--link-key',KEY,'--link-url','http://127.0.0.1:%d'%north.port,'--link-export','all','--link-import','all','--link-every','1')
    try:
        ann,annId=north.sign_in('ann','Ann');zed,zedId=north.sign_in('zed','Zebediah-of-the-Eastern-Crater-Lake');bea,beaId=south.sign_in('bea','Bea')
        north.gate(ann,'createObject',{'t':'l','q':226,'px':10000,'py':2,'pz':3});north.gate(zed,'createObject',{'t':'e','q':226,'px':30000,'py':2,'pz':3})
        south.gate(bea,'createObject',{'t':'l','q':226,'px':20000,'py':2,'pz':3})
        check(until(lambda:len(south.gate(bea,'findQpidObjects',{'q':226})['n']['oa'])==2 and len(north.gate(ann,'findQpidObjects',{'q':226})['n']['oa'])==2),'the linked servers took each other\'s objects')
        # This server's own players: the name only, on the server with a name and on the one without.
        check(north.lookup(ann,annId)=='Ann' and south.lookup(bea,beaId)=='Bea','own players by name only')
        # From the other server: "Name · Home", the home its own name, or the link's name for a server that has none.
        got=south.lookup(bea,annId);check(got=='Ann'+DOT+'North','an imported builder on the server without a name: %r'%got)
        got=north.lookup(ann,beaId);check(got=='Bea'+DOT+'south','an imported builder from the server without a name: %r'%got)
        got=south.lookup(bea,zedId)
        check(got is not None and len(got.encode())<=32 and len(got)<=24 and got.endswith(DOT+'North') and got.startswith('Zebediah'),'a long name cut, the home kept: %r'%got)
        check(north.call('GET','/coop/policy')[1]['name']=='North' and south.call('GET','/coop/policy')[1]['name']=='','the server\'s name in its policy')
    finally:
        north.stop();south.stop()
    check(json.load(open(north.data))['name']=='North' and 'name' not in json.load(open(south.data)),'the name saved with the world')
    homes=[o['origin'].get('home') for o in json.load(open(south.data))['objects'] if 'origin' in o]
    check(sorted(homes)==['North','North'],'every import keeps its home: %r'%homes)
    code,text=run('origins','--data',south.data);check(code==0 and "home: North)" in text,'origins names the server: %s'%text)
    code,text=run('policy','--data',south.data,'--name','South');check(code==0 and 'server name: South' in text and json.load(open(south.data))['name']=='South','a name given to a stopped server: %s'%text)
    code,text=run('policy','--data',south.data,'--name',' x');check(code==1 and json.load(open(south.data))['name']=='South','a name that is none changes nothing: %s'%text)
print('names socket test passed')
