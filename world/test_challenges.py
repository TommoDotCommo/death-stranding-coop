"""Route challenges through the real coop-world over HTTP (challenges.h, ../docs/ROUTES.md "On the server"), with a
challenge string and a track made here by an encoder of this test's own (a second reading of the DSR1 and DST1 formats):
a challenge is published, listed, a run is counted on the board and a teleporting run refused, and all of it is still
there after the server is started again on the same world file.

  python3 test_challenges.py WORLD
"""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zlib

ALPHABET='0123456789ABCDEFGHJKMNPQRSTVWXYZ'
def base32(data):
    bits=''.join(f'{b:08b}' for b in data);bits+='0'*(-len(bits)%5)
    chars=[ALPHABET[int(bits[i:i+5],2)] for i in range(0,len(bits),5)]
    return '-'.join(''.join(chars[i:i+5]) for i in range(0,len(chars),5))
def var(z):
    out=bytearray()
    while True:
        c=z&0x7f;z>>=7
        out.append(c|(0x80 if z else 0))
        if not z:return bytes(out)
def zig(v):return var((v<<1) if v>=0 else ((-(v+1))<<1)|1)
def crc(b):return zlib.crc32(b).to_bytes(4,'little')
def route(name,points,width=10):
    b=bytearray([1,0,width,10,10,5,0])+(0).to_bytes(2,'little')+bytes([len(name)])+name.encode()+bytes([0])
    sx,sy,sz=(round(v*10) for v in points[0])
    for v in (sx,sy,sz):b+=v.to_bytes(4,'little',signed=True)
    b+=len(points).to_bytes(2,'little');ox,oy,oz=sx/10,sy/10,sz/10;prev=(0,0,0)
    for x,y,z in points[1:]:
        cell=(round((x-ox)/.25),round((z-oz)/.25),round((y-oy)/.5))
        for a,p in zip(cell,prev):b+=zig(a-p)
        prev=cell
    b+=crc(bytes(b));return 'DSR1-'+base32(bytes(b)),int.from_bytes(crc(bytes(b[:-4])),'little')
def track(route_id,claimed,points,origin):
    b=bytearray([1])+claimed.to_bytes(4,'little')+route_id.to_bytes(4,'little')+len(points).to_bytes(2,'little');pm=px=pz=py=0
    for ms,(x,y,z) in points:
        m=(ms+50)//100;cx,cz,cy=round((x-origin[0])/.5),round((z-origin[2])/.5),round(y-origin[1])
        b+=var((m-pm)<<1)+zig(cx-px)+zig(cz-pz)+zig(cy-py);pm,px,pz,py=m,cx,cz,cy
    b+=crc(bytes(b));return 'DST1-'+base32(bytes(b))

def free_port():
    s=socket.socket();s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def call(port,method,name,body=None):
    data=None if body is None else json.dumps(body).encode()
    req=urllib.request.Request(f'http://127.0.0.1:{port}/coop/{name}',data=data,method=method,headers={'Content-Type':'application/json'})
    try:
        with urllib.request.urlopen(req,timeout=5) as r:return r.status,json.loads(r.read())
    except urllib.error.HTTPError as e:return e.code,json.loads(e.read() or b'{}')
def start(world,port,data):
    p=subprocess.Popen([world,'--port',str(port),'--data',str(data)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    seen=[]
    for _ in range(5):
        seen.append(p.stdout.readline())
        if 'listening' in seen[-1]:return p
    p.kill();raise AssertionError(('coop-world did not start',seen))
def stop(p):
    p.terminate()
    try:p.wait(timeout=10)
    except subprocess.TimeoutExpired:p.kill();p.wait()

def main(world):
    points=[(0,0,0),(150,20,0),(300,0,0)]
    text,route_id=route('Py ridge',points)
    run=[(0,(0,0,0)),(200,(5,0,0))]+[(1000+2000*i,(12+12*i,0,1)) for i in range(25)]
    start_ms,finish_ms=1000,1000+2000*24          # out of the start zone at x=12, in the finish zone at x=300
    good=track(route_id,finish_ms-start_ms,run,points[0])
    jump=track(route_id,finish_ms-start_ms,run[:6]+[(run[6][0],(900,0,1))]+run[7:],points[0])
    with tempfile.TemporaryDirectory() as folder:
        data=Path(folder)/'world.json';port=free_port();server=start(world,port,data)
        try:
            status,out=call(port,'POST','challenge-publish',{'player':'ann','route':text,'name':'Ann'})
            assert status==200 and out['id']=='c1',(status,out)
            status,out=call(port,'GET','challenges')
            c=out['challenges'][0];assert c['name']=='Py ridge' and c['author']=='Ann' and c['length_m']==300 and c['route']==text,c
            status,out=call(port,'POST','challenge-result',{'player':'bob','challenge':'c1','track':good,'name':'Bob'})
            assert status==200 and out['place']==1,(status,out)
            status,out=call(port,'POST','challenge-result',{'player':'cee','challenge':'c1','track':jump,'name':'Cee'})
            assert status==403 and 'teleported' in out['message'],(status,out)
        finally:stop(server)
        saved=json.loads(data.read_text());assert saved['challenges']['list'][0]['id']=='c1',saved.get('challenges')
        server=start(world,port,data)
        try:
            status,out=call(port,'GET','challenges');c=out['challenges'][0]
            assert c['board'][0]['name']=='Bob' and c['board'][0]['ms']==finish_ms-start_ms and c['runs']==1,c
            status,out=call(port,'POST','challenge-publish',{'player':'ann','route':route('Second',[(0,0,0),(50,0,0)])[0]})
            assert status==200 and out['id']=='c2',(status,out)
        finally:stop(server)
    print('challenges over HTTP: published, listed, a run counted and a teleport refused, kept across a restart')

if __name__=='__main__':main(str(Path(sys.argv[1]).resolve()))
