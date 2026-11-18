"""Limits per address over real sockets (README, "Limits per address"): the built server with small limits applied to
loopback. Connections at once per address, the request rate (429), a block after too many unknown paths and its end,
a request that drips its bytes is cut off, and the admin's signed call (keygen, call) with kick and ban through it.
Then the drip mutant (MUTANT_GATE_DRIP) must fail its check.

usage: test_limits.py build/coop-world"""
import json, os, socket, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def request(sock, path, method='GET', body=b''):
    sock.sendall(f'{method} {path} HTTP/1.1\r\nHost: x\r\nContent-Length: {len(body)}\r\n\r\n'.encode() + body)
    data = b''
    while b'\r\n\r\n' not in data:
        chunk = sock.recv(4096)
        if not chunk: return 0, b''
        data += chunk
    head, rest = data.split(b'\r\n\r\n', 1)
    length = 0
    for row in head.split(b'\r\n')[1:]:
        name, _, value = row.partition(b':')
        if name.strip().lower() == b'content-length': length = int(value)
    while len(rest) < length:
        chunk = sock.recv(4096)
        if not chunk: break
        rest += chunk
    return int(head.split()[1]), rest


def connect(port):
    s = socket.create_connection(('127.0.0.1', port), timeout=5); return s


def served(port, path='/k3y/coop/policy'):
    """One request on a new connection: its status, or 0 when the server closed it without an answer."""
    try:
        with connect(port) as s: return request(s, path)[0]
    except OSError:
        return 0


def start(exe, port, data, extra=()):
    p = subprocess.Popen([exe, '--port', str(port), '--data', data, '--prefix', 'k3y', '--limit-loopback',
                          '--ip-connections', '3', '--ip-rate', '5', '--ip-burst', '10', '--ip-strikes', '5',
                          '--ip-block', '3', '--request-seconds', '2', *extra],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for _ in range(100):
        try: socket.create_connection(('127.0.0.1', port), timeout=0.2).close(); return p
        except OSError: time.sleep(0.05)
    p.kill(); raise SystemExit('server did not start')


def drip_cut(port):
    """True when the server ends a request that drips one byte every 0.4 s within the request time (2 s here)."""
    s = connect(port); s.settimeout(0.2)
    began = time.time(); text = b'GET /k3y/coop/policy HTTP/1.1\r\nHost: x\r\nX-Slow: ' + b'a' * 200
    for b in text:
        try:
            s.sendall(bytes([b]))
            if s.recv(1) == b'': s.close(); return time.time() - began < 4.5
        except socket.timeout: pass
        except OSError: s.close(); return time.time() - began < 4.5
        time.sleep(0.2)
        if time.time() - began > 6: break
    s.close(); return False


def main():
    exe = os.path.abspath(sys.argv[1])
    failures = []
    def check(ok, what):
        print(('ok   ' if ok else 'FAIL ') + what, flush=True)
        if not ok: failures.append(what)
    with tempfile.TemporaryDirectory() as d:
        port = free_port(); server = start(exe, port, os.path.join(d, 'world.json'),
                                           ['--policy', 'public'])
        try:
            # Connections at once: three held, the fourth is closed unanswered; one freed lets the next in.
            # (The start's readiness probe holds a place until its thread has seen it close: wait for that.)
            time.sleep(1)
            held = [connect(port) for _ in range(3)]
            codes = [request(h, '/k3y/coop/policy')[0] for h in held]
            check(codes == [200] * 3, f'three connections from one address are served ({codes})')
            fourth = served(port)
            check(fourth == 0, f'a fourth connection from one address is closed ({fourth})')
            held.pop().close(); time.sleep(0.2)
            check(served(port) == 200, 'a freed place takes the next connection')
            for h in held: h.close()
            time.sleep(1.5)
            # Rate: the burst of 10 on one keep-alive connection, then 429 and the connection ends.
            with connect(port) as s:
                statuses = []
                for _ in range(20):
                    code, _ = request(s, '/k3y/coop/policy'); statuses.append(code)
                    if code != 200: break
            check(statuses.count(200) >= 9 and statuses[-1] == 429, f'over the rate: 429 ({statuses})')
            time.sleep(2.5)
            check(served(port) == 200, 'the rate comes back')
            # The admin's signed call, on the same server: keygen, then a call the owner key makes and one it may not.
            key = os.path.join(d, 'owner.key')
            made = subprocess.run([exe, 'keygen', '--out', key], capture_output=True, text=True)
            check(made.returncode == 0 and len(made.stdout.strip()) == 64 and (os.stat(key).st_mode & 0o777) == 0o600,
                  'keygen writes a private key file and prints the public half')
            again = subprocess.run([exe, 'keygen', '--out', key], capture_output=True, text=True)
            check(again.returncode == 1, 'keygen never overwrites a key')
            call = lambda *a: subprocess.run([exe, 'call', '--key', key, '--port', str(port), '--prefix', 'k3y', *a],
                                             capture_output=True, text=True)
            not_owner = call('GET', 'owner/players')
            check(not_owner.returncode == 3, 'a key that is not the owner\'s is refused')
            time.sleep(2.5)
        finally:
            server.kill(); server.wait()
        # The same world with that key as the owner's.
        pub = made.stdout.strip()
        server = start(exe, port, os.path.join(d, 'world.json'), ['--owner', pub])
        try:
            players = call('GET', 'owner/players')
            check(players.returncode == 0 and json.loads(players.stdout)['players'] == [], 'the owner\'s key lists the players')
            unknown = call('POST', 'owner/ban', '{"player":"zygo_nobody","ban":"yes"}')
            check(unknown.returncode == 3, 'a ban of nobody is refused')
            wrong = subprocess.run([exe, 'call', '--key', key, '--port', str(port), '--prefix', 'nope', 'GET', 'owner/players'],
                                   capture_output=True, text=True)
            check(wrong.returncode == 3, 'a wrong prefix finds no server')
            time.sleep(2.5)
            # Unknown paths: the fifth 404 in a minute blocks the address for 3 s (the wrong prefix above was the
            # first), then it is let in again.
            codes = [served(port, '/guess%d/coop/policy' % i) for i in range(4)]
            check(codes == [404] * 4, f'unknown paths answer 404 ({codes})')
            check(served(port) == 0, 'then the address is blocked')
            time.sleep(3.5)
            check(served(port) == 200, 'the block ends')
            check(drip_cut(port), 'a request that drips its bytes is cut off')
            log = ''
        finally:
            server.kill(); log = server.communicate()[0]
        check('blocked: too many requests for paths' in log, 'the block is logged')
        # The mutant: without the request deadline the drip is not cut off.
        mutant = os.path.join(d, 'coop-world-drip')
        mono = [str(ROOT / '../vendor/monocypher' / f) for f in ('monocypher.c', 'monocypher-ed25519.c')]
        subprocess.run(['g++', '-std=c++17', '-O1', '-pthread', '-DMUTANT_GATE_DRIP',
                        *[str(ROOT / f) for f in ('main.cpp', 'front.cpp', 'api.cpp', 'link.cpp', 'world.cpp', 'relay.cpp',
                                                  'identity.cpp', 'signatures.cpp', 'challenges.cpp', '../mod/route.cpp')], *mono, '-o', mutant], check=True)
        server = start(mutant, port, os.path.join(d, 'world2.json'))
        try:
            survived = drip_cut(port)
        finally:
            server.kill(); server.wait()
        check(not survived, 'KILLED MUTANT_GATE_DRIP')
    if failures: print(f'{len(failures)} limits checks failed'); sys.exit(1)
    print('limits over sockets: ok')


main()
