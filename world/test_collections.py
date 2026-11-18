"""Collections (../docs/COLLECTIONS.md) on the built server over real sockets: two stand-in games sign in as the game
does (Ann and Bob, /k/NAME), Ann's game uploads a ladder, a cairn and a bridge, her launcher makes a collection of them,
and Bob's launcher imports it at the original place on a second server and here on the first: his game says where it is
with a HELLO to the relay (written byte by byte, core/relay.h), and the server takes his place from it. Each time his
game's findQpidObjects (what build_received builds from) is handed the imported structures, at the right places, told
as built by another player (his collection builder, which lookup names "bob · collection"), never by his own id: his
game sets objects of its own player that its save lacks to Fail. On the first server Ann's game is handed his too.
Then the same run against a server built with MUTANT_COLLECTION_CREATOR_SELF (the games told that he built them)
must fail.

  python3 test_collections.py COOP-WORLD"""
import http.client,json,os,socket,struct,subprocess,sys,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent
PREFIX='w77'
def oracle(ok,what):
    if not ok:raise AssertionError('collections oracle: '+str(what))
def free_port(kind=socket.SOCK_STREAM):
    s=socket.socket(socket.AF_INET,kind);s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def hello(player,key,sequence,position):
    # Version 1 HELLO: the position in centimetres in the relay's axes (x, up, y).
    return struct.pack('<4sHBBQ32sIIIIB3xiii32s',b'DSR1',1,1,0,player,key,sequence,0,0,0,1,*[int(round(x*100)) for x in position],b'bob')
class Server:
    def __init__(self,exe,folder,name):
        self.port,self.relay=free_port(),free_port(socket.SOCK_DGRAM)
        self.p=subprocess.Popen([exe,'--port',str(self.port),'--prefix',PREFIX,'--relay-port',str(self.relay),'--data',os.path.join(folder,name+'.json')],
                                stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        for _ in range(4):
            if 'listening' in self.p.stdout.readline():break
        else:raise AssertionError('coop-world did not start')
    def stop(self):self.p.kill();self.p.wait()
    def call(self,method,path,body=None,headers=None):
        h=http.client.HTTPConnection('127.0.0.1',self.port,timeout=10)
        try:
            h.request(method,'/'+PREFIX+path,body=json.dumps(body) if body is not None else None,headers=headers or {});r=h.getresponse();return r.status,json.loads(r.read() or b'{}')
        finally:h.close()
    def game(self,name):
        status,auth=self.call('GET','/k/%s/auth/ds?provider=steam&display_name=%s&code=00'%(name,name));oracle(status==200,auth);return auth['session']['token']
    def gate(self,session,call,body):
        status,out=self.call('POST','/gate/e/'+call,body,{'Authorization':'Bearer '+session});oracle(status==200,(call,status,out));return out
    def upload(self,session,kind,x,y,z,rz=0,area=226,**extra):
        body=dict(t=kind,x=9,y=7,m=22123,px=round(x*1e5),py=round(y*1e5),pz=round(z*1e5),rx=0,ry=0,rz=round(rz*1e5),exp=5,q=area,st='0',p=0,lp=0);body.update(extra)
        return self.gate(session,'createObject',body)['id']
    def handed(self,session,area=226):
        """What the game is handed for an area: the objects its received structures are built from."""
        return {o['id']:o for o in self.gate(session,'findQpidObjects',{'q':area,'o':{'ct':{'l':5,'e':5,'b':5}}})['n']['oa']}
    def api(self,call,body):return self.call('POST','/coop/'+call,body)
def place(o):return tuple(v/10**o['exp'] for v in o['l'])
def near(a,b,metres=0.05):return all(abs(x-y)<=metres for x,y in zip(a,b))
def run(exe):
    with tempfile.TemporaryDirectory(prefix='ds-collections-') as d:
        a,b=Server(exe,d,'north'),Server(exe,d,'south')
        try:
            ann=a.game('ann')
            ladder=a.upload(ann,'l',1000,2000,30,rz=45);cairn=a.upload(ann,'e',1030,2000,30,si={'r':1});bridge=a.upload(ann,'b',1000,2040,30,st='30',bi={'a':-111525})
            # Ann's launcher: a collection of the three, by their ids.
            status,made=a.api('collection',{'player':'ann','objects':[ladder,cairn,bridge],'name':'North camp'})
            oracle(status==200 and made['count']==3 and made['collection'].startswith('DSK1-'),made)
            text=made['collection']
            # Bob on the second server, at the original place: his game is handed all three, where Ann built them.
            bob=b.game('bob')
            status,got=b.api('import',{'player':'bob','collection':text,'place':'original'})
            oracle(status==200 and got['imported']==3 and got['source']=='North camp',got)
            own=b.upload(bob,'l',3000,2000,30);ownId=b.gate(bob,'getQpidObjects',{'ids':[own]})['objects'][0]['c']
            handed=b.handed(bob)
            oracle(set(got['ids'])<=set(handed) and own not in handed,('his game is handed what he imported, not what he built',got['ids'],list(handed)))
            builders={handed[i]['c'] for i in got['ids']}
            oracle(len(builders)==1 and ownId not in builders,('told another builder than his own id',builders,ownId))
            users=b.gate(bob,'lookup',{'ids':list(builders)})['users']
            oracle(len(users)==1 and users[0]['displayName']=='bob \u00b7 collection',('lookup names his collection builder',users))
            byKind={o['t']:o for o in handed.values()}
            oracle(near(place(byKind['l']),(1000,2000,30)) and near(place(byKind['e']),(1030,2000,30)) and near(place(byKind['b']),(1000,2040,30)),'at their places')
            oracle(byKind['l']['r'][2]==4500000 and byKind['b']['bi']=={'a':-111525} and byKind['b']['st']=='30' and byKind['e']['si']=={'r':1},'with their angles, level and blocks')
            # Bob on the first server, here: his game says where it is (relay id 4242, x 5000, y 7000, z 40: the relay's axes
            # are x, up, y). The server knows no object near him yet: it cannot tell the area.
            u=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);u.settimeout(1);u.connect(('127.0.0.1',a.relay))
            bob=a.game('bob')
            for seq in range(1,4):u.send(hello(4242,PREFIX.encode(),seq,(5000,40,7000)));time.sleep(0.6)
            status,got=a.api('import',{'player':'bob','collection':text,'place':'here','relay_id':'4242'})
            oracle(status==409 and 'original place' in got.get('message',''),(status,got))
            # One structure of another player near him gives the area (300); then here works, moved to his place.
            a.upload(a.game('cy'),'p',5100,7000,40,area=300)
            status,got=a.api('import',{'player':'bob','collection':text,'place':'here','relay_id':'4242'})
            oracle(status==200 and got['imported']==3 and got['anchor']['area']==300 and abs(got['anchor']['x']-5000)<0.05 and abs(got['anchor']['y']-7000)<0.05,(status,got))
            mine=a.handed(bob,300);oracle(set(got['ids'])<=set(mine),('his game is handed what he imported here',got['ids'],list(mine)))
            oracle(len({mine[i]['c'] for i in got['ids']})==1,'one collection builder for all he imported')
            ladderHere=[o for o in mine.values() if o['t']=='l' and o['id'] in got['ids']][0]
            # The anchor of a collection made from named objects is their middle (1010, 2013.33, 30): the ladder was 10 m and 13.33 m from it.
            oracle(near(place(ladderHere),(4990,6986.67,40)),('the ladder kept its place from the middle of the three',place(ladderHere)))
            # Ann's game is handed Bob's imports in that area, as any player's shared structures.
            oracle(set(got['ids'])<=set(a.handed(ann,300)),'the others are handed them too')
            # A string changed on the way makes nothing.
            changed=text[:-1]+('0' if text[-1]!='0' else '1')
            status,refused=a.api('import',{'player':'bob','collection':changed,'place':'original'})
            oracle(status==400,(status,refused))
        finally:
            a.stop();b.stop()
if __name__=='__main__':
    exe=sys.argv[1]
    run(exe)
    # The mutant: a server that tells the games the importer built them (so his game is not handed them) must fail.
    with tempfile.TemporaryDirectory(prefix='ds-collections-mutant-') as d:
        mutant=os.path.join(d,'coop-world-creator-self')
        mono=[str(ROOT/'../vendor/monocypher'/f) for f in ('monocypher.c','monocypher-ed25519.c')]
        subprocess.run(['g++','-std=c++17','-O1','-pthread','-DMUTANT_COLLECTION_CREATOR_SELF',
                        *[str(ROOT/f) for f in ('main.cpp','front.cpp','api.cpp','link.cpp','world.cpp','collections.cpp','collection.cpp','relay.cpp','identity.cpp','signatures.cpp','challenges.cpp','../mod/route.cpp')],*mono,'-o',mutant],check=True)
        try:run(mutant);survived=True
        except AssertionError as e:survived=False;print('KILLED MUTANT_COLLECTION_CREATOR_SELF ('+str(e)[:80]+')')
        oracle(not survived,'MUTANT_COLLECTION_CREATOR_SELF survived')
    print('collections over sockets passed: made, imported at the original place and here, handed to the importer as another builder, and to the others')
