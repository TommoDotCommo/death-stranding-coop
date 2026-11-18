"""Collections (../docs/COLLECTIONS.md), the launcher's side on the built server over real sockets: Ann's game uploads a
ladder and a cairn; her launcher makes a collection of the two rows and writes it to a .dsk file; Bob's launcher loads
the file and imports it at the original place on a second server, where his game is then handed both. Ann's launcher
makes another of everything within 100 m of where her game says it is (a HELLO to the relay); Bob imports that one
here, from a chat line pasted into a file. A string changed on the way is refused by the launcher itself, before
anything is sent, and the server makes nothing.
Arguments: the launcher's test program (build/core-test), coop-world."""
import http.client,json,os,socket,struct,subprocess,sys,tempfile,time
helper,world=sys.argv[1:3]
PREFIX='w88'
def check(ok,what):
    if not ok:
        sys.stderr.write('launcher collections oracle: %s\n'%what);sys.exit(1)
def free_port(kind=socket.SOCK_STREAM):
    s=socket.socket(socket.AF_INET,kind);s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def hello(player,key,sequence,position):
    return struct.pack('<4sHBBQ32sIIIIB3xiii32s',b'DSR1',1,1,0,player,key,sequence,0,0,0,1,*[int(round(x*100)) for x in position],b'game')
class Server:
    def __init__(self,folder,name):
        self.port,self.relay=free_port(),free_port(socket.SOCK_DGRAM)
        self.p=subprocess.Popen([world,'--port',str(self.port),'--prefix',PREFIX,'--relay-port',str(self.relay),'--data',os.path.join(folder,name+'.json')],
                                stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        for _ in range(4):
            if 'listening' in self.p.stdout.readline():break
    def call(self,method,path,body=None,headers=None):
        h=http.client.HTTPConnection('127.0.0.1',self.port,timeout=10)
        try:h.request(method,'/'+PREFIX+path,body=json.dumps(body) if body is not None else None,headers=headers or {});r=h.getresponse();return r.status,json.loads(r.read() or b'{}')
        finally:h.close()
    def game(self,name):
        status,auth=self.call('GET','/k/%s/auth/ds?provider=steam&display_name=%s&code=00'%(name,name));check(status==200,auth);return auth['session']['token']
    def upload(self,session,kind,x,y,z,area=226):
        status,out=self.call('POST','/gate/e/createObject',dict(t=kind,x=9,y=7,m=22123,px=round(x*1e5),py=round(y*1e5),pz=round(z*1e5),rx=0,ry=0,rz=0,exp=5,q=area,st='0',p=0,lp=0),
                             {'Authorization':'Bearer '+session});check(status==200,out);return out['id']
    def handed(self,session,area=226):
        status,out=self.call('POST','/gate/e/findQpidObjects',{'q':area,'o':{'ct':{'l':5,'e':5}}},{'Authorization':'Bearer '+session});check(status==200,out)
        return {o['id']:o for o in out['n']['oa']}
    def launcher(self,*args):
        r=subprocess.run([helper,*args[:1],'127.0.0.1',str(self.port),PREFIX,*args[1:]],capture_output=True,text=True,timeout=60)
        check(r.returncode==0,'the launcher helper ran: %r %s'%(args,r.stderr))
        out={}
        for line in r.stdout.splitlines():
            key,_,value=line.partition(' ');out.setdefault(key,[]).append(value)
        return out
    def stop(self):self.p.kill();self.p.wait()
with tempfile.TemporaryDirectory(prefix='ds-launcher-collections-') as d:
    folder=os.path.join(d,'collections')
    a,b=Server(d,'north'),Server(d,'south')
    try:
        ann=a.game('ann');ladder=a.upload(ann,'l',1000,2000,30);cairn=a.upload(ann,'e',1030,2000,30)
        # The selected rows: the clipboard text and a .dsk file in the collections folder.
        made=a.launcher('--strand-collect','ann','0',folder,'North camp',ladder,cairn)
        check('collection' in made and made['count']==['2'] and made['file'][0].endswith('.dsk') and os.path.dirname(made['file'][0])==folder,made)
        text=made['collection'][0];check(open(made['file'][0]).read().strip()==text,'the file holds the string')
        check('2 structures (1 cairn, 1 ladder), named North camp' in made['said'][0],made['said'])
        # Bob on the second server, from the file, at the original place.
        bob=b.game('bob')
        got=b.launcher('--strand-import','bob','0','original',made['file'][0])
        check(got.get('imported')==['2'] and got.get('source')==['North camp'] and 'Your game is handed them' in got['said'][0],got)
        check(len(b.handed(bob))==2,'his game is handed both')
        # Near her game's place (relay id 31: x 1010, y 2000, z 30; the relay's axes are x, up, y).
        u=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);u.connect(('127.0.0.1',a.relay))
        refused=a.launcher('--strand-collect','ann','31',folder,'-','near:100')
        check('refused' in refused and 'start the game' in refused['refused'][0],refused)
        for seq in range(1,4):u.send(hello(31,PREFIX.encode(),seq,(1010,30,2000)));time.sleep(0.6)
        near=a.launcher('--strand-collect','ann','31',folder,'-','near:100')
        check(near.get('count')==['2'] and near['file'][0].endswith('.dsk') and os.path.basename(near['file'][0]).startswith('collection-'),near)
        # Bob on the first server, here, 400 m away (relay id 32), from a chat line with the string in it.
        bob=a.game('bob')
        for seq in range(1,4):u.send(hello(32,PREFIX.encode(),seq,(1410,30,2000)));time.sleep(0.6)
        pasted=os.path.join(d,'pasted.txt');open(pasted,'w').write('[18:02] ann: try this one '+near['collection'][0]+' :)\n')
        got=a.launcher('--strand-import','bob','32','here',pasted)
        check(got.get('imported')==['2'],got)
        places=sorted(round(o['l'][0]/1e5,1) for o in a.handed(bob).values())
        check(1400.0 in places and 1430.0 in places,('moved to his place',places))
        # A string changed on the way: refused by the launcher before it is sent; nothing is made.
        changed=near['collection'][0];changed=changed[:9]+('A' if changed[9]!='A' else 'B')+changed[10:]
        bad=os.path.join(d,'bad.dsk');open(bad,'w').write(changed+'\n')
        got=b.launcher('--strand-import','bob','0','original',bad)
        check('unreadable' in got and 'cannot be read' in got['unreadable'][0],got)
        check(len(b.handed(b.game('bob')))==2,'nothing more made')
    finally:
        a.stop();b.stop()
print('launcher collections: made from rows and near the game, saved, loaded, imported at the original place and here, a changed string refused')
