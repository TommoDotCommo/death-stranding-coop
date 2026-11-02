"""Native Windows fixture: query/read handles, identity and VirtualQueryEx layout."""
import ctypes as c
import os
import unittest
from read_actor_resources import ResourceReader, scan
from probe_puppet import read


@unittest.skipUnless(os.name == 'nt', 'native Windows fixture')
class NativeReaderTests(unittest.TestCase):
    def test_query_read_and_identity(self):
        from ctypes import wintypes as w
        api = c.WinDLL('kernel32', use_last_error=True)
        api.GetCurrentProcess.restype = w.HANDLE
        api.GetProcessTimes.argtypes = [w.HANDLE, *([c.POINTER(w.FILETIME)] * 4)]
        times = [w.FILETIME() for _ in range(4)]
        self.assertTrue(api.GetProcessTimes(api.GetCurrentProcess(), *(c.byref(t) for t in times)))
        start = times[0].dwLowDateTime | times[0].dwHighDateTime << 32
        with self.assertRaisesRegex(ValueError, 'creation time'):
            ResourceReader(os.getpid(), start + 1)
        reader = ResourceReader(os.getpid(), start)
        try:
            self.assertEqual(c.sizeof(reader.MBI), 48)
            self.assertEqual(reader.image_path().name.casefold(), 'python.exe')
            # Verify the OS-granted access rights, independently of OpenProcess's argument.
            class Basic(c.Structure):
                _fields_ = [('attributes', w.ULONG), ('access', w.ULONG),
                            ('handles', w.ULONG), ('pointers', w.ULONG), ('reserved', w.ULONG * 10)]
            nt = c.WinDLL('ntdll')
            nt.NtQueryObject.argtypes = [w.HANDLE, w.ULONG, c.c_void_p, w.ULONG, c.c_void_p]
            info = Basic()
            self.assertEqual(nt.NtQueryObject(reader.handle, 0, c.byref(info), c.sizeof(info), None), 0)
            self.assertTrue(info.access & 0x10)
            self.assertEqual(info.access & (0x2 | 0x8 | 0x20 | 0x800 | 0x1), 0)
            fixture = (c.c_uint64 * 8)(0x12345678, 1, 2, 3, 4, 5, 6, 7)
            address = c.addressof(fixture)
            regions = reader.regions(address, float('inf'))
            first, size = next(regions)
            self.assertEqual(first, address)
            self.assertGreaterEqual(size, c.sizeof(fixture))
            report = scan([(address, c.sizeof(fixture))], lambda a, n: read(reader, a, n),
                {0x12345678: dict(name='fixture', player_resource=False)}, 1024, 1)
            self.assertEqual(report['candidates'][0]['address'], hex(address))
            self.assertFalse(report['spawn_approved'])
        finally:
            reader.close()


if __name__ == '__main__':
    unittest.main()
