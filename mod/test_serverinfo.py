"""The server page (serverinfo.h, config server_info) in the fake world: the real mod with a small HTTP server standing in
for the players' server. P reads the policy under the server's prefix and opens the dialogue with the name, who is
online and the rules; Next page turns; without an answer it says so; without server_info the key does nothing.

  python3 test_serverinfo.py                     the test, then its mutant build
  python3 test_serverinfo.py LIBRARY             the test against another build
"""
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import ctypes as c
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from test_testmode import Game,oracle

ROOT=Path(__file__).resolve().parent
POLICY=dict(format=1,server='s',name='Bridges',preset='friends',players=dict(count=5,limit=16),online=dict(count=2,names=['Ann','Bob']),
            settings=[dict(text='Rule %d.'%i) for i in range(7)])
class Answer(BaseHTTPRequestHandler):
    seen=[];protocol_version='HTTP/1.1'
    def do_GET(self):
        Answer.seen.append(self.path);ok=self.path=='/s3cret/coop/policy'
        body=json.dumps(POLICY if ok else {}).encode()
        self.send_response(200 if ok else 404);self.send_header('Content-Length',str(len(body)));self.send_header('Connection','close');self.end_headers();self.wfile.write(body)
    def log_message(self,*a):pass
def body(game):
    out=c.create_string_buffer(2000);game.lib.mod_test_dialog_body(out,2000);return out.value.decode()
def press(game):game.script('key P 200\nwait 300\n')
def run(library):
    server=ThreadingHTTPServer(('127.0.0.1',0),Answer);port=server.server_address[1];threading.Thread(target=server.serve_forever,daemon=True).start()
    url='http://127.0.0.1:%d/s3cret/auth/ds'%port
    with tempfile.TemporaryDirectory() as folder:
        game=Game(folder,library,dict(server_info=True,online_server=url))
        try:
            info=lambda:game.status()['server_info']
            oracle(game.pump(3,lambda:info()['ready']),('no page',info()))
            press(game);oracle(game.pump(5,lambda:info()['open']),('P did not open it',info(),Answer.seen))
            oracle(info()['answered'] and info()['pages']==3,('not read',info(),Answer.seen))
            t=body(game);oracle('Bridges (friends rules)' in t and 'Online now: 2\nAnn, Bob' in t,t)
            game.lib.mod_test_dialog_answer(0);oracle(game.pump(3,lambda:info()['open'] and info()['page']==1),('no next page',info()))
            t=body(game);oracle('Rules 1 of 2' in t and 'Rule 0.' in t,t)
            game.lib.mod_test_dialog_answer(1);oracle(game.pump(3,lambda:not info()['open']),('not closed',info()))
            server.shutdown();server.server_close()    # gone: the page still opens and says so
            game.pump(1.3)   # the key's cooldown
            press(game);oracle(game.pump(8,lambda:info()['open']),('nothing without a server',info(),game.log()[-900:]))
            oracle('did not answer' in body(game) and not info()['answered'],body(game))
        finally:game.close()
    with tempfile.TemporaryDirectory() as folder:
        game=Game(folder,library,{})
        try:
            press(game);game.pump(1.5);s=game.status()['server_info'];oracle(not s['ready'] and not s['opens'],('a page nobody asked for',s))
        finally:game.close()
if __name__=='__main__':
    if len(sys.argv)==2:run(Path(sys.argv[1]).resolve())
    else:
        run(ROOT/'build/fake.so')
        r=subprocess.run([sys.executable,'-B',__file__,str(ROOT/'build/fake-serverinfo-base.so')],capture_output=True,text=True,timeout=120)
        assert r.returncode==1 and 'test-mode oracle' in r.stderr,('SURVIVED fake-serverinfo-base.so',r.returncode,r.stderr[-600:])
        print('server page (fake world): the policy read under the prefix, name, who is online and rules on pages, no answer told, nothing without server_info; its mutant build fails')
