#!/usr/bin/env python3
"""Pose-exchange exit test from two presence logs (no game needed).

  analyse_presence.py SENDER.jsonl RECEIVER.jsonl [--delay-ms 120] [--max-m 0.5] [--max-deg 10]

For every receiver line whose remote pose is visible and not stale, the sender's own (local) track is linearly
interpolated at the same host time minus the interpolation delay, and the position (metres) and yaw (degrees) errors
are measured. Both processes run on one PC, so `t_us` is one clock. Lines whose sender time falls outside the sender's
coherent track (menu, loading, gaps over 1 s) are skipped and counted. Exit 0 only if every compared line is within
the limits and at least 50 lines were compared. This is the launch-10 exit test (2026-11-01), as a tool.
"""
import argparse
import bisect
import json
import math
import sys


def load(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line: rows.append(json.loads(line))
    return rows


def track(rows):
    out = [(r['t_us'], r['local']) for r in rows if r.get('local', {}).get('available')]
    out.sort(key=lambda p: p[0])
    return out


def at(tr, times, t):
    i = bisect.bisect_left(times, t)
    if i == 0 or i == len(tr): return None
    (t0, a), (t1, b) = tr[i-1], tr[i]
    if t1 - t0 > 1_000_000: return None
    w = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
    dyaw = ((b['yaw_deg'] - a['yaw_deg'] + 180) % 360) - 180
    return {k: a[k] + w*(b[k]-a[k]) for k in ('x', 'y', 'z')} | {'yaw_deg': a['yaw_deg'] + w*dyaw}


def analyse(sender, receiver, delay_ms=120, max_m=0.5, max_deg=10.0, snap_window=0):
    tr = track(sender); times = [t for t, _ in tr]
    pos, yaw, skipped, stale, snapped, near_snap = [], [], 0, 0, 0, 0
    snaps = [i for i, r in enumerate(receiver) if r.get('remote', {}).get('snapped')]
    for i, r in enumerate(receiver):
        rem = r.get('remote', {})
        if rem.get('snapped'): snapped += 1
        if not rem.get('visible'): continue
        if rem.get('stale'): stale += 1; continue
        if snap_window and any(abs(i - j) <= snap_window for j in snaps[max(0, bisect.bisect_left(snaps, i) - 1):bisect.bisect_left(snaps, i) + 1]):
            near_snap += 1; continue
        truth = at(tr, times, r['t_us'] - delay_ms*1000)
        if truth is None: skipped += 1; continue
        pos.append(math.dist((rem['x'], rem['y'], rem['z']), (truth['x'], truth['y'], truth['z'])))
        yaw.append(abs(((rem['yaw_deg'] - truth['yaw_deg'] + 180) % 360) - 180))
    within = sum(1 for p, y in zip(pos, yaw) if p <= max_m and y <= max_deg)
    med = lambda v: sorted(v)[len(v)//2] if v else None
    moved = max((math.dist((a['x'], a['y'], a['z']), (b['x'], b['y'], b['z'])) for (_, a), (_, b) in zip(tr, tr[1:])
                 if True), default=0.0)
    path = sum(math.dist((a['x'], a['y'], a['z']), (b['x'], b['y'], b['z'])) for (_, a), (_, b) in zip(tr, tr[1:]))
    return {'compared': len(pos), 'within': within, 'skipped': skipped, 'stale_visible': stale, 'snapped': snapped,
            'excluded_near_snap': near_snap, 'snap_window': snap_window,
            'pos_median_m': med(pos), 'pos_max_m': max(pos) if pos else None,
            'yaw_median_deg': med(yaw), 'yaw_max_deg': max(yaw) if yaw else None,
            'sender_path_m': round(path, 1), 'sender_max_step_m': round(moved, 2),
            'ok': len(pos) >= 50 and within == len(pos)}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('sender'); p.add_argument('receiver')
    p.add_argument('--delay-ms', type=float, default=120); p.add_argument('--max-m', type=float, default=0.5)
    p.add_argument('--max-deg', type=float, default=10.0)
    p.add_argument('--snap-window', type=int, default=0, help='exclude (and count) lines within N lines of a snapped '
                   'line: snaps are the core\'s documented height-change limitation, and fast turns around them are '
                   'beyond what the 4 Hz sender log can serve as truth')
    a = p.parse_args()
    r = analyse(load(a.sender), load(a.receiver), a.delay_ms, a.max_m, a.max_deg, a.snap_window)
    print(json.dumps(r)); return 0 if r['ok'] else 1


if __name__ == '__main__': sys.exit(main())
