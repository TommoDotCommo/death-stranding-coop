"""Actual mod APIs + network threads over a test-only in-memory datagram boundary.

No socket permission required. Production Winsock receive/endpoint filtering and
Windows loader remain separate verifier checks, not proved by this test.
"""
import ctypes as c
import json
import math
import os
from pathlib import Path
import re
import shutil
import tempfile
import time
from probe_report import validate
root = Path(__file__).resolve().parent


def pair(tmp, variant, case):
    libs = []
    for slot in range(2):
        directory = tmp/(variant+case+str(slot))
        directory.mkdir()
        path = directory/'mod.so'
        shutil.copyfile(root/'build'/variant, path)
        lib = c.CDLL(str(path))
        lib.mod_init.argtypes = [c.c_char_p]
        lib.mod_tick.argtypes = [c.c_double]
        lib.mod_status.argtypes = [c.c_void_p, c.c_uint]
        lib.mod_last_error.restype = c.c_char_p
        config = directory/'config.json'
        config.write_text(json.dumps(dict(role='host' if slot == 0 else 'client',
            slot=slot, peer_slot=1-slot, session=123, peer='127.0.0.1',
            nickname='test', provider='synthetic', logging=True, impairment=True,
            fault=case if slot == 1 and case != 'good' else 'none')))
        if not lib.mod_init(str(config).encode()):
            raise AssertionError(lib.mod_last_error())
        libs.append(lib)
    reports = []
    try:
        begin = time.monotonic()
        packet = (c.c_ubyte*128)()
        last_presence = None
        while time.monotonic()-begin < 3:
            for i, lib in enumerate(libs):
                assert lib.mod_tick(.01), lib.mod_last_error()
                for _ in range(64):
                    if not lib.mod_test_drain(packet):
                        break
                    if i == 0 and packet[6] == 3:
                        last_presence = bytes(packet)
                    libs[1-i].mod_test_deliver(packet)
            time.sleep(.01)
        for lib in libs:
            data = c.create_string_buffer(lib.mod_status(None, 0))
            lib.mod_status(data, len(data))
            reports.append(json.loads(data.value))
        for r in reports:
            timing = r['timing']
            assert timing['frequency_hz'] > 0 and timing['wire_quantum_us'] == 1000
            for name in ('network_tick', 'provider_tick', 'presence_send', 'sample_age', 'receive_to_apply', 'delivery_lateness'):
                h = timing[name]
                assert sum(h['buckets']) == h['count']
                assert len(h['buckets']) == len(timing['bucket_upper_us'])+1
            if case == 'good':
                assert timing['ping_sent'] >= timing['pong_received'] > 0
                assert timing['receive_to_apply']['count'] > 0
        if variant == 'fake.so' and case == 'good':
            # Reproduce an earlier-finishing peer's authenticated BYE. Keep core
            # reset semantics, but do not erase metrics already measured.
            assert last_presence is not None
            bye = bytearray(last_presence)
            bye[6] = 6
            checksum = 2166136261
            for at, byte in enumerate(bye):
                checksum = ((checksum ^ (0 if 120 <= at < 124 else byte))*16777619) & 0xffffffff
            bye[120:124] = checksum.to_bytes(4, 'little')
            libs[1].mod_test_deliver((c.c_ubyte*128).from_buffer_copy(bye))
            deadline = time.monotonic()+1
            while True:
                data = c.create_string_buffer(libs[1].mod_status(None, 0))
                libs[1].mod_status(data, len(data))
                after = json.loads(data.value)
                if not after['connected']:
                    break
                assert time.monotonic() < deadline, 'BYE did not reset core'
                time.sleep(.002)
            assert after['current_rtt_ms'] == -1
            assert after['rtt_ms'] == reports[1]['rtt_ms'] >= 0
            assert after['handshake'] and after['samples'] == reports[1]['samples']
            validate([reports[0], after], case)
            print('peer BYE retains measured RTT and resets live core')
        validate(reports, case)
        return reports
    finally:
        for lib in libs:
            lib.mod_shutdown()


PINNED = re.search(r'PinnedHash="([0-9a-f]{64})"', (root/'discovery_options.h').read_text()).group(1)


def load(directory, slot, **extra):
    """One fake-memory game-provider instance (test build: process memory is a fake PE in the live layout)."""
    directory.mkdir()
    path = directory/'mod.so'
    shutil.copyfile(root/'build'/'fake.so', path)
    lib = c.CDLL(str(path))
    lib.mod_init.argtypes = [c.c_char_p]
    lib.mod_tick.argtypes = [c.c_double]
    lib.mod_status.argtypes = [c.c_void_p, c.c_uint]
    lib.mod_last_error.restype = c.c_char_p
    lib.mod_test_set_pose.argtypes = [c.POINTER(c.c_double), c.POINTER(c.c_float), c.c_int]
    config = dict(role='host' if slot == 0 else 'client', slot=slot, peer_slot=1-slot, session=777, peer='127.0.0.1',
                  nickname='pose', provider='game', logging=True, self_tick=True, discovery_sha256=PINNED,
                  presence_log_max_mib=1)
    config.update(extra)
    (directory/'config.json').write_text(json.dumps(config))
    return lib, directory/'config.json'


def status(lib):
    # Counters can grow between the size query and the copy (the mod ticks itself): retry with headroom.
    cap = lib.mod_status(None, 0)
    for _ in range(8):
        data = c.create_string_buffer(cap)
        need = lib.mod_status(data, len(data))
        if need <= cap:
            return json.loads(data.value)
        cap = need * 2
    raise AssertionError('status kept growing beyond retry budget')


def set_pose(lib, x, y, z, heading_deg, menu=False):
    """Forward = row m10..m12 = (cos h, sin h, 0), h = map heading atan2(dy, dx); the right vector is row 0."""
    h = math.radians(heading_deg)
    rot = (c.c_float*9)(math.sin(h), -math.cos(h), 0, math.cos(h), math.sin(h), 0, 0, 0, 1)
    lib.mod_test_set_pose((c.c_double*3)(x, y, z), rot, 1 if menu else 0)


def game_provider_fake(tmp):
    """Two game-provider mods over fake process memory: menu startup, gating, pose exchange both ways, back to the menu."""
    # The presence log is created exclusively: a pre-existing file for this pid must refuse startup.
    blocked = tmp/'gp-blocked'
    lib, config = load(blocked, 0)
    lib.mod_test_run_stamp.restype = c.c_char_p
    (blocked/('presence-%s.jsonl' % lib.mod_test_run_stamp().decode())).write_text('')
    # The stem is pid AND process start: the bare pid name an earlier process could leave behind no longer collides.
    assert lib.mod_test_run_stamp().decode().startswith('%d-' % os.getpid())
    assert not lib.mod_init(str(config).encode()) and b'presence log' in lib.mod_last_error(), 'presence log must be exclusive'
    # Provider game without self_tick is refused by the strict schema; the fake build is not a back door.
    lib, config = load(tmp/'gp-noself', 0, self_tick=False)
    assert not lib.mod_init(str(config).encode()) and b'self_tick' in lib.mod_last_error()
    libs = [load(tmp/'gp0', 0), load(tmp/'gp1', 1)]
    confs = [x[1] for x in libs]
    libs = [x[0] for x in libs]
    for lib, config in zip(libs, confs):
        # The main menu (no pose) is NORMAL at startup: the mod must not refuse.
        assert lib.mod_init(str(config).encode()), lib.mod_last_error()
    packet = (c.c_ubyte*128)()
    sent_presence = [0, 0]

    def pump(seconds, update=None):
        end = time.monotonic()+seconds
        while time.monotonic() < end:
            if update:
                update()
            for i, lib in enumerate(libs):
                for _ in range(64):
                    if not lib.mod_test_drain(packet):
                        break
                    if packet[6] == 3:
                        sent_presence[i] += 1
                    libs[1-i].mod_test_deliver(packet)
            time.sleep(.005)
    begin = time.monotonic()
    try:
        # Phase 1: menu. Ticking, but pairing waits for a loaded save observation and no presence is sent.
        pump(1.2)
        for s in map(status, libs):
            assert not s['failed'] and s['pose'] == {'available': False, 'reason': 'manager null'}, s
            assert not s['connected'] and s['self_tick']['enabled'] and 15 <= s['self_tick']['ticks'] <= 26, s
        assert sent_presence == [0, 0], 'presence must stay gated while no pose is available'
        ticks = status(libs[0])['self_tick']['ticks']
        assert libs[0].mod_tick(.01) == 1 and status(libs[0])['self_tick']['ticks'] <= ticks+2, 'external ticks must not add ticks'
        # Phase 2: both players walk (slot 0 heading -97.3 at 4 m/s, slot 1 heading 30 at 3 m/s), game axes, z up.
        t0 = time.monotonic_ns()//1000
        walk = [(100.0, 50.0, 20.0, -97.3, 4.0), (-200.0, 300.0, 5.0, 30.0, 3.0)]

        def where(slot, us):
            x, y, z, h, v = walk[slot]
            d = v*(us-t0)/1e6
            return x+d*math.cos(math.radians(h)), y+d*math.sin(math.radians(h)), z, h
        def move():
            now = time.monotonic_ns()//1000
            for slot, lib in enumerate(libs):
                x, y, z, h = where(slot, now)
                set_pose(lib, x, y, z, h)
        pump(4.0, move)
        for s in map(status, libs):
            assert s['pose']['available'] and s['connected'] and s['accepted'] > 20, s
        assert min(sent_presence) > 20, sent_presence
        elapsed = time.monotonic()-begin
        for s in map(status, libs):
            assert s['self_tick']['ticks'] <= 20*elapsed+3, ('self_tick must never catch up', s['self_tick'], elapsed)
        # What the verifier checks live: each peer's presence log shows the other's track, 120 ms behind, within 0.5 m / 10 deg.
        for viewer, owner in ((1, 0), (0, 1)):
            lines = [json.loads(l) for l in next((tmp/('gp%d' % viewer)).glob('presence-*.jsonl')).read_text().splitlines()]
            compared = 0
            for line in lines:
                r = line['remote']
                if not r['visible'] or r['stale'] or line['t_us'] < t0+1500000:
                    continue
                x, y, z, h = where(owner, line['t_us']-120000)
                assert not r['snapped'], line
                error = math.dist((r['x'], r['y'], r['z']), (x, z, y))   # adapter axes: (game x, game z up, game y)
                yaw = (90-h+180) % 360-180
                yaw_error = abs((r['yaw_deg']-yaw+180) % 360-180)
                assert error < 0.5 and yaw_error < 10, ('track', viewer, line, error, yaw_error)
                compared += 1
            assert compared >= 8, ('too few compared presence lines', viewer, compared, len(lines))
            local = [l['local'] for l in lines if l['local']['available']]
            assert local and abs(local[-1]['yaw_deg']-((90-walk[viewer][3]+180) % 360-180)) < 1, local[-1:]
            print('presence track', 'slot %d sees slot %d' % (viewer, owner), 'compared', compared, 'lines')
        # Phase 3: slot 0 returns to the menu: unavailable, gated, its presence stops, slot 1's view goes stale then invisible.
        set_pose(libs[0], 0, 0, 0, 0, menu=True)
        pump(.3)
        before = sent_presence[0]
        pump(2.2, lambda: set_pose(libs[1], *where(1, time.monotonic_ns()//1000)[:3], walk[1][3]))
        s0 = status(libs[0])
        assert s0['pose'] == {'available': False, 'reason': 'manager null'} and sent_presence[0] == before, (s0, before, sent_presence)
        last = json.loads(next((tmp/'gp1').glob('presence-*.jsonl')).read_text().splitlines()[-1])
        assert not last['remote']['visible'] and last['local']['available'], last
        # A non-finite pose reads as unavailable with a reason, not as a crash or a fabricated pose.
        libs[1].mod_test_set_pose((c.c_double*3)(float('nan'), 0, 0), (c.c_float*9)(1, 0, 0, 0, 1, 0, 0, 0, 1), 0)
        pump(.3)
        assert status(libs[1])['pose'] == {'available': False, 'reason': 'non-finite pose'}, status(libs[1])
        print('game-provider-fake: menu startup, gating, 20 Hz self tick, bidirectional pose track, menu return')
    finally:
        for lib in libs:
            lib.mod_shutdown()


with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    for case in ('good', 'build', 'version', 'session'):
        reports = pair(tmp, 'fake.so', case)
        print('fake datagrams', case, json.dumps(reports))
    # Build mutant must pass normal motion, then fail the SAME refusal oracle.
    pair(tmp, 'fake-no-build.so', 'good')
    for variant, case, oracle in (('fake-no-build.so', 'build', 'refusal assertion failed'),
                                  ('fake-no-interpolation.so', 'good', 'handshake/error bounds failed')):
        try:
            pair(tmp, variant, case)
        except AssertionError as e:
            if oracle not in str(e):
                raise
            print('KILLED', variant, str(e))
        else:
            raise AssertionError('SURVIVED '+variant)
with tempfile.TemporaryDirectory() as tmp:
    game_provider_fake(Path(tmp))
print('mod API/thread/fake transport checks passed')
