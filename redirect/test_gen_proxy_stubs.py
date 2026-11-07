import os
from pathlib import Path
import unittest

import gen_proxy_stubs as g

HERE = Path(__file__).parent
SYSTEM = Path(os.environ.get('SystemRoot', r'C:\Windows'))/'System32'
REAL = next((p for p in (SYSTEM/'avrt.dll', Path('/mnt/c/Windows/System32/avrt.dll')) if p.exists()), None)
FAKE = [{'name': 'Alpha', 'ordinal': 1}, {'name': 'Beta', 'ordinal': 3}]


class Generator(unittest.TestCase):
    def test_def_keeps_names_and_ordinals(self):
        self.assertEqual(g.render_def('x', FAKE), 'LIBRARY x\nEXPORTS\n  Alpha=coop_fwd_0 @1\n  Beta=coop_fwd_1 @3\n')

    def test_every_export_has_stub_slot_and_name(self):
        inc = g.render_inc('x', FAKE)
        for i in range(2):
            self.assertIn(f'coop_fwd_{i}: jmp qword ptr [rip+coop_fwd_table+{8 * i}]', inc)
            self.assertIn(f'.quad coop_slow_{i}', inc)
        self.assertIn('"Alpha",', inc)
        self.assertIn('#define COOP_PROXY_EXPORTS 2', inc)

    @unittest.skipUnless(REAL, 'no System32 avrt.dll (not Windows/WSL)')
    def test_committed_files_match_the_real_export_table(self):
        exports = g.exports_of(REAL)
        self.assertEqual((HERE/'avrt.def').read_text(), g.render_def('avrt', exports))
        self.assertEqual((HERE/'avrt_stubs.inc').read_text(), g.render_inc('avrt', exports))
        # a dropped export (the forwarding surface shrinks) must be detected
        self.assertNotEqual((HERE/'avrt.def').read_text(), g.render_def('avrt', exports[:-1]))


if __name__ == '__main__':
    unittest.main()
