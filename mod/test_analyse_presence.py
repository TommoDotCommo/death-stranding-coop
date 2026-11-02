import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyse_presence as ap  # noqa: E402


def logs(offset=0.0, snaps=(), stale=(), n=200, delay_us=120_000):
    """Sender walks along x at 4 m/s, logged at 4 Hz; the receiver shows it `delay_us` later (plus an offset)."""
    sender, receiver = [], []
    for i in range(n):
        t = 1_000_000 + i*250_000
        sender.append({'t_us': t, 'local': {'available': True, 'x': 4.0*i*0.25, 'y': 0.0, 'z': 0.0, 'yaw_deg': 90.0}})
        x = 4.0*((t - delay_us - 1_000_000)/1e6) + offset
        receiver.append({'t_us': t, 'local': {'available': False},
                         'remote': {'visible': True, 'stale': i in stale, 'snapped': i in snaps,
                                    'x': x, 'y': 0.0, 'z': 0.0, 'yaw_deg': 90.0}})
    return sender, receiver


class PresenceAnalysisTests(unittest.TestCase):
    def test_matching_track_passes(self):
        r = ap.analyse(*logs())
        self.assertTrue(r['ok']); self.assertLess(r['pos_max_m'], 1e-6); self.assertGreaterEqual(r['compared'], 190)

    def test_mutants_fail(self):
        self.assertFalse(ap.analyse(*logs(offset=1.0))['ok'])                 # receiver 1 m off
        sender, receiver = logs()
        self.assertFalse(ap.analyse(sender, receiver[:40])['ok'])               # fewer than 50 compared lines
        wrong = [dict(r, remote=dict(r['remote'], x=r['remote']['x'] + 3.0)) for r in receiver]
        self.assertFalse(ap.analyse(sender, wrong)['ok'])                     # wrong pairing / other player
        self.assertFalse(ap.analyse(*logs(delay_us=600_000))['ok'])           # interpolation delay not the expected one

    def test_stale_and_snap_lines_are_excluded_and_counted(self):
        sender, receiver = logs(snaps={50}, stale={10, 11})
        receiver[50]['remote']['x'] += 5.0                                    # a snap far off
        self.assertFalse(ap.analyse(sender, receiver)['ok'])
        r = ap.analyse(sender, receiver, snap_window=2)
        self.assertTrue(r['ok']); self.assertEqual(r['excluded_near_snap'], 5); self.assertEqual(r['stale_visible'], 2)


if __name__ == '__main__':
    unittest.main()
