"""The deployment without a server to rent (README.md in this folder, "Checks"): the Linux coop-world started on
loopback with exactly the command line of the unit install.sh writes (only the address, the port and the data path
changed), then what a tester's launcher and game do through it, and what the owner's coop-admin does to them.

  - the unit has every hardening line the README promises; a unit missing one is caught (mutant); systemd-analyze
    verify takes it where it is installed;
  - the invite the deploy prints reads back, through the launcher's own recogniser, as the server's key and port;
  - the server runs Public rules with identity keys: a sign-in by name is refused, a launcher's ticket signs in;
  - two players, each with his own key and relay token, see each other through the relay (coop-swarm, broadcast);
  - a stand-in that brings the group's key instead of a relay token is not answered (identity keys);
  - coop-admin lists the players, bans one (his relay presence ends, his launcher's calls and passes are refused),
    lifts the ban, kicks the other; a wrong prefix finds nothing;
  - no Steam id is written to the world file.

usage: test_vps.py COOP_WORLD_LINUX COOP_SWARM VPS_INVITE"""
import json, os, re, shutil, socket, subprocess, sys, tempfile, time, urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REQUIRED = ['User=coopworld', 'Restart=always', 'NoNewPrivileges=yes', 'ProtectSystem=strict', 'PrivateTmp=yes',
            'ReadWritePaths=/var/lib/coop-world', 'MemoryMax=', 'LimitNOFILE=', 'CapabilityBoundingSet=',
            'ProtectHome=yes', '--policy public', '--policy-set identity=keys', '--bind 0.0.0.0',
            '--relay-key w${COOP_KEY}', '--prefix w${COOP_KEY}', '--ip-connections', '--ip-rate', '--ip-strikes']
failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what, flush=True)
    if not ok: failures.append(what)


def missing(unit):
    return [r for r in REQUIRED if r not in unit]


def free_port():
    """A free port among the launcher's group ports, TCP and UDP."""
    for p in range(49100, 49115):
        try:
            t = socket.socket(); t.bind(('127.0.0.1', p)); u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.bind(('127.0.0.1', p))
            t.close(); u.close(); return p
        except OSError:
            continue
    raise SystemExit('no free port in 49100..49114')


def http(port, path, method='GET', body=None):
    req = urllib.request.Request(f'http://127.0.0.1:{port}{path}', data=body, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as r: return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def main():
    exe, swarm, invite_tool = (os.path.abspath(a) for a in sys.argv[1:4])
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        port = free_port()
        owner_key = d / 'owner.key'
        owner_pub = subprocess.run([exe, 'keygen', '--out', str(owner_key)], capture_output=True, text=True, check=True).stdout.strip()
        group = 3141592653
        conf = d / 'coop-world.conf'
        conf.write_text(f'COOP_PORT={port}\nCOOP_KEY={group}\nCOOP_NAME=Alpha\nCOOP_OWNER={owner_pub}\n')
        unit = subprocess.run(['sh', str(HERE / 'install.sh'), '--print-unit', '--conf', str(conf)], capture_output=True, text=True, check=True).stdout
        check(missing(unit) == [], f'the unit has every hardening line ({missing(unit)})')
        check(missing(unit.replace('ProtectSystem=strict\n', '')) == ['ProtectSystem=strict'], 'a unit without ProtectSystem=strict is caught')
        bad = d / 'bad.conf'; bad.write_text(conf.read_text().replace('COOP_NAME=Alpha', 'COOP_NAME=Al pha --trace /x'))
        refused = subprocess.run(['sh', str(HERE / 'install.sh'), '--print-unit', '--conf', str(bad)], capture_output=True, text=True)
        check(refused.returncode != 0 and refused.stdout == '', 'a configuration that would add options is refused')
        # systemd's own reading of the unit, where systemd-analyze is installed (the installed paths replaced by this test's).
        if shutil.which('systemd-analyze'):
            u = d / 'coop-world.service'
            u.write_text(unit.replace('/opt/coop-world/coop-world', exe).replace('EnvironmentFile=/etc/coop-world/coop-world.conf', f'EnvironmentFile={conf}'))
            v = subprocess.run(['systemd-analyze', 'verify', str(u)], capture_output=True, text=True)
            said = [l for l in (v.stdout + v.stderr).splitlines() if 'coop-world' in l]
            check(v.returncode == 0 and said == [], f'systemd-analyze verify takes the unit ({said})')
        else:
            print('skip systemd-analyze not installed', flush=True)
        # ExecStart as systemd runs it: continuation lines joined, ${VAR} from the configuration.
        text = re.search(r'^ExecStart=((?:.*\\\n)*.*)$', unit, re.M).group(1).replace('\\\n', ' ')
        env = dict(l.split('=', 1) for l in conf.read_text().splitlines())
        argv = re.sub(r'\$\{(\w+)\}', lambda m: env[m.group(1)], text).split()
        check(argv[0] == '/opt/coop-world/coop-world', 'the unit starts the installed binary')
        argv[0] = exe
        argv[argv.index('--bind') + 1] = '127.0.0.1'
        argv[argv.index('--data') + 1] = str(d / 'world.json')
        server = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        swarms = []
        try:
            for _ in range(100):
                try: socket.create_connection(('127.0.0.1', port), timeout=0.2).close(); break
                except OSError: time.sleep(0.05)
            prefix = f'w{group}'
            status, body = http(port, f'/{prefix}/coop/policy')
            policy = json.loads(body) if status == 200 else {}
            settings = {s['key']: s['value'] for s in policy.get('settings', [])}
            check(status == 200 and policy.get('preset') == 'public' and settings.get('identity') == 'keys' and policy.get('owner') is True,
                  'the server runs Public rules with identity keys and the owner key')
            # The invite the deploy prints, read back as the launcher reads it.
            code = subprocess.run([invite_tool, str(group), str(port), '203.0.113.7'], capture_output=True, text=True).stdout.strip()
            read = subprocess.run([invite_tool, '--read', code], capture_output=True, text=True).stdout.strip()
            check(code.startswith('DSG-') and read == f'key {group} port {port} route 3:203.0.113.7', f'the invite reads back ({code}: {read})')
            check(subprocess.run([invite_tool, str(group), str(port), '10.11.12.13'], capture_output=True).returncode == 2,
                  'an invite with a home (private) address is refused')
            # A sign-in by name (or a Steam ticket) is refused; a launcher's ticket signs in.
            check(http(port, f'/{prefix}/k/ann/auth/ds?provider=steam&display_name=&code=00')[0] == 403, 'a sign-in by name is refused')
            steam = http(port, f'/{prefix}/auth/ds?provider=steam&display_name=&code=7656119800000000')[0]
            check(steam in (400, 403), f'a Steam sign-in is refused ({steam})')
            check(http(port, f'/w1/coop/policy')[0] == 404, 'a wrong prefix finds nothing')
            players = {}
            for who in ('ann', 'bea'):
                key = d / f'{who}.key'
                pub = subprocess.run([exe, 'keygen', '--out', str(key)], capture_output=True, text=True, check=True).stdout.strip()
                call = lambda *a, key=key: subprocess.run([exe, 'call', '--key', str(key), '--port', str(port), '--prefix', prefix, *a],
                                                          capture_output=True, text=True)
                account = 'ed25519-' + pub
                ticket = call('POST', 'ticket', json.dumps({'player': account}))
                relay = call('POST', 'relay-token', json.dumps({'player': account}))
                check(ticket.returncode == 0 and relay.returncode == 0, f'{who}: the launcher takes a ticket and a relay token')
                t = json.loads(ticket.stdout)['ticket']; r = json.loads(relay.stdout)
                status, body = http(port, f'/{prefix}/t/{t}/auth/ds?provider=steam&display_name=&code=7656119812345678')
                check(status == 200, f'{who}: the game signs in with the ticket')
                players[who] = dict(call=call, account=account, token=r['token'], relay_id=r['relay_id'],
                                    id=json.loads(body)['user']['id'] if status == 200 else '')
            # Two players near each other through the relay, each with his own token (broadcast, as the mod).
            def stand_in(p, seconds, token=None, out=None):
                return subprocess.Popen([swarm, '--relay', f'127.0.0.1:{port}', '--mode', 'broadcast', '--count', '1',
                                         '--first-id', str(p['relay_id']), '--key', token or p['token'], '--centre', '0', '0', '0',
                                         '--radius', '2', '--seconds', str(seconds), '--require-connected'],
                                        stdout=out or subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            a, b = stand_in(players['ann'], 6), stand_in(players['bea'], 6)
            swarms += [a, b]
            out_a, out_b = a.communicate(timeout=30)[0], b.communicate(timeout=30)[0]
            last = lambda out: (out.strip().splitlines() or [''])[-1]
            check(a.returncode == 0 and b.returncode == 0 and 'near=1' in last(out_a) and 'near=1' in last(out_b),
                  f'two players see each other through the relay ({last(out_a)} | {last(out_b)})')
            # The group's key is no relay token under identity keys.
            c = stand_in(players['ann'], 3, token=prefix); swarms.append(c)
            out_c = c.communicate(timeout=30)[0]
            check(c.returncode != 0 and 'online=0/1' in last(out_c), f'the group key alone is not answered ({last(out_c)})')
            # coop-admin, as on the server, with this test's paths.
            admin_env = dict(os.environ, COOP_CONF=str(conf), COOP_BIN=exe, COOP_OWNER_KEY=str(owner_key))
            admin = lambda *a: subprocess.run(['sh', str(HERE / 'coop-admin.sh'), *a], capture_output=True, text=True, env=admin_env)
            listed = admin('players')
            ids = sorted(p['id'] for p in json.loads(listed.stdout)['players']) if listed.returncode == 0 else []
            check(ids == sorted(p['id'] for p in players.values()), 'coop-admin lists the two players')
            info = admin('info').stdout.strip()
            check(info == f'COOP_INVITE_INFO port={port} key={group} name=Alpha', 'coop-admin info gives the invite line')
            check(admin('ban', 'zygo_x;rm').returncode == 2, 'coop-admin refuses an argument with shell characters')
            check(admin('policy-set', 'identity=none').returncode == 2, 'coop-admin keeps identity keys')
            check(admin('member', 'zygo_x').returncode == 2 and admin('kick').returncode == 2, 'coop-admin refuses a command missing its arguments')
            # Ban Ann while she is on the relay: she is gone from it, and her launcher gets nothing more.
            a = stand_in(players['ann'], 12); b = stand_in(players['bea'], 12); swarms += [a, b]
            time.sleep(4)
            banned = admin('ban', players['ann']['id'])
            check(banned.returncode == 0, f'coop-admin bans a player ({banned.stdout.strip()} {banned.stderr.strip()})')
            out_a, out_b = a.communicate(timeout=40)[0], b.communicate(timeout=40)[0]
            check('near=0' in last(out_b), f'the banned player has left the others\' view ({last(out_b)})')
            ann = players['ann']
            check(ann['call']('POST', 'ticket', json.dumps({'player': ann['account']})).returncode == 3, 'the banned player\'s launcher gets no ticket')
            check(ann['call']('POST', 'relay-token', json.dumps({'player': ann['account']})).returncode == 3, '... and no relay token')
            check(admin('unban', players['ann']['id']).returncode == 0, 'coop-admin lifts the ban')
            check(ann['call']('POST', 'ticket', json.dumps({'player': ann['account']})).returncode == 0, 'then her launcher gets a ticket again')
            kicked = admin('kick', players['bea']['id'])
            check(kicked.returncode == 0 and json.loads(kicked.stdout).get('ended', 0) >= 1, f'coop-admin kicks a player ({kicked.stdout.strip()})')
            c = stand_in(players['bea'], 3); swarms.append(c)
            out_c = c.communicate(timeout=30)[0]
            check(c.returncode != 0, f'the kicked player\'s relay token no longer works ({last(out_c)})')
            check(admin('policy').returncode == 0 and admin('review').returncode == 0, 'coop-admin reads the rules and the review list')
        finally:
            for s in swarms:
                if s.poll() is None: s.kill()
            server.kill(); log = server.communicate()[0]
        world = (d / 'world.json').read_text()
        check('7656119' not in world and '7656119' not in log, 'no Steam id in the world file or the log')
        check('relay: player' in log, 'the server log records relay joins (on the server only)')
    if failures: print(f'{len(failures)} deployment checks failed'); sys.exit(1)
    print('deployment smoke: ok')


main()
