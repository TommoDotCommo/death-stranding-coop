"""The other players on the game's map (mapicons.h, config player_map) in the fake world through the real server and relay: a
copy of the real mod (fake.so) with its notice token, and stand-in players (coop-swarm) on the relay. The stand-ins' places,
in game axes and with their area, reach what the map update reads; they go when the stand-ins leave; with the server's rule
map=off nobody is listed.

  python3 test_map.py WORLD SWARM
"""
import ctypes as c
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import test_strand_notices as strand
from test_testmode import Game,oracle
class Server(strand.Server):
    def __init__(self,world,folder,port,relay,*rules):
        self.world,self.folder,self.port=world,Path(folder),port;self.data,self.trace=self.folder/'world.json',self.folder/'calls.log';self.process=None
        self.start('--name','North','--relay-port',str(relay),'--relay-key',strand.PREFIX,*rules)
def free_port(kind=socket.SOCK_STREAM):
    s=socket.socket(socket.AF_INET,kind);s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def game(folder,server,name):
    server.sign_in(name);t=server.notice_token(name);f=Path(folder)/name;f.mkdir()
    g=Game(f,Path(__file__).resolve().parent/'build/fake.so',dict(nickname=name,player_map=True,strand_token=t,online_server='http://127.0.0.1:%d/%s/auth/ds'%(server.port,strand.PREFIX)))
    g.lib.mod_test_map.argtypes=[c.c_char_p,c.c_uint];g.lib.mod_test_map.restype=c.c_uint;return g
def icons(g):
    b=c.create_string_buffer(4000);g.lib.mod_test_map(b,4000);return [tuple(float(v) for v in i.split(',')) for i in b.value.decode().split(';') if i]
def pump(g,seconds,until):
    for _ in range(int(seconds/.05)):
        g.pump(.05)
        if until():return True
    return until()
def swarm(tool,relay,count,seconds):
    return subprocess.Popen([tool,'--relay','127.0.0.1:%d'%relay,'--key',strand.PREFIX,'--count',str(count),'--centre','100','7','-50','--radius','5','--mode','broadcast','--area','200','--seconds',str(seconds)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def main(world,tool):
    with tempfile.TemporaryDirectory() as folder:
        relay=free_port(socket.SOCK_DGRAM);server=Server(world,folder,free_port(),relay);ann=game(folder,server,'Ann');crowd=swarm(tool,relay,3,6)
        try:
            state=lambda:ann.status()['player_map']
            oracle(pump(ann,8,lambda:state()['on'] and len(icons(ann))==3),('stand-ins not on the map',state(),icons(ann)))
            for x,y,z,area in icons(ann):oracle(abs(x-100)<40 and abs(y+50)<40 and abs(z-7)<40 and area==200,('place or area',x,y,z,area))
            crowd.wait(timeout=20);oracle(pump(ann,14,lambda:icons(ann)==[]),('left, still shown',icons(ann)))
        finally:
            crowd.kill();ann.close();server.stop()
    with tempfile.TemporaryDirectory() as folder:
        relay=free_port(socket.SOCK_DGRAM);server=Server(world,folder,free_port(),relay,'--policy-set','map=off');ann=game(folder,server,'Ann');crowd=swarm(tool,relay,2,5)
        try:
            state=lambda:ann.status()['player_map']
            oracle(pump(ann,4,lambda:state()['reads']>=2),('no reads',state()));oracle(not state()['on'] and icons(ann)==[],('rule off',state(),icons(ann)))
        finally:
            crowd.kill();ann.close();server.stop()
    print('players on the map (fake world): the stand-ins reach the map with place and area, go when they leave, and a server with map=off lists nobody')
if __name__=='__main__':main(str(Path(sys.argv[1]).resolve()),str(Path(sys.argv[2]).resolve()))
