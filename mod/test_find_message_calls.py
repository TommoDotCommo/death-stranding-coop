"""Synthetic AMD64 images; no game files or Windows required."""
import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import find_message_calls as scan


def fixture():
    data = bytearray(0x1000)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x8664, 3)
    struct.pack_into('<H', data, 0x94, 0xf0)
    struct.pack_into('<H', data, 0x98, 0x20b)
    struct.pack_into('<Q', data, 0xb0, 0x140000000)
    struct.pack_into('<I', data, 0x104, 16)
    struct.pack_into('<II', data, 0x110, 0x2000, 40)
    struct.pack_into('<II', data, 0x120, 0x3000, 24)
    for index, (name, rva, raw, flags) in enumerate((
        (b'.text', 0x1000, 0x400, 0x60000020),
        (b'.rdata', 0x2000, 0x800, 0x40000040),
        (b'.pdata', 0x3000, 0xc00, 0x40000040))):
        off = 0x188 + index * 40
        data[off:off+8] = name.ljust(8, b'\0')
        struct.pack_into('<IIII', data, off+8, 0x400, rva, 0x400, raw)
        struct.pack_into('<I', data, off+36, flags)
    struct.pack_into('<IIIII', data, 0x800, 0x2080, 0, 0, 0x2060, 0x20b0)
    data[0x860:0x86b] = b'USER32.dll\0'
    struct.pack_into('<QQQ', data, 0x880, 0x20e0, 0x2100, 0)
    for off, name in ((0x8e0, b'PeekMessageA\0'), (0x900, b'GetMessageA\0')):
        data[off+2:off+2+len(name)] = name
    struct.pack_into('<IIIIII', data, 0xc00, 0x1000, 0x1080, 0x3100, 0x1080, 0x1100, 0x3100)
    data[0x410:0x416] = b'\xff\x15'+struct.pack('<i', 0x20b0-0x1016)
    data[0x420:0x426] = b'\xff\x15'+struct.pack('<i', 0x20b8-0x1026)
    data[0x490:0x497] = b'\x48\x8d\x05'+struct.pack('<i', 0x1000-0x1097)
    return data


class MessageCallTests(unittest.TestCase):
    def test_imports_signed_rip_and_function_bounds(self):
        image = scan.Inventory(fixture())
        imports = image.imports()
        self.assertEqual([(i['name'], i['iat_rva']) for i in imports],
                         [('PeekMessageA', 0x20b0), ('GetMessageA', 0x20b8)])
        refs = image.references(imports)
        self.assertEqual(refs['PeekMessageA'][0]['site_rva'], '0x1010')
        self.assertEqual(refs['GetMessageA'][0]['function'], {'begin_rva': '0x1000', 'end_rva': '0x1080'})
        self.assertFalse(refs['PeekMessageA'][0]['decoded'])
        self.assertEqual(image.code_address_references(0x1000)[0]['site_rva'], '0x1090')

    def test_import_thunk_negative_rel32_and_data_decoy(self):
        data = fixture()
        data[0x410:0x416] = b'\xff\x25'+struct.pack('<i', 0x20b0-0x1016)
        data[0x4a0:0x4a5] = b'\xe8'+struct.pack('<i', 0x1010-0x10a5)
        data[0x980:0x986] = b'\xff\x15'+struct.pack('<i', 0x20b0-0x2186)
        image = scan.Inventory(data)
        refs = image.references(image.imports())['PeekMessageA']
        self.assertEqual([r['kind'] for r in refs], ['indirect_jump', 'call_thunk'])
        self.assertEqual(refs[1]['thunk_rva'], '0x1010')
        self.assertEqual(refs[1]['function']['begin_rva'], '0x1080')

    def test_function_end_is_exclusive(self):
        image = scan.Inventory(fixture())
        self.assertEqual(image.containing(0x1080)['begin_rva'], '0x1080')
        self.assertIsNone(image.containing(0x107e, 6))
        self.assertIsNone(image.containing(0x1100))

    def test_ordinal_import_is_not_a_name_pointer(self):
        data = fixture()
        struct.pack_into('<Q', data, 0x880, (1 << 63) | 123)
        self.assertEqual([i['name'] for i in scan.Inventory(data).imports()], ['GetMessageA'])

    def test_malformed_pe_mutants(self):
        for offset, fmt, value in ((0x84, '<H', 0x14c), (0x94, '<H', 112),
            (0x120, '<I', 0), (0x124, '<I', 23), (0xc0c, '<I', 0x1070),
            (0xc04, '<I', 0x1000), (0xc08, '<I', 0x5000)):
            with self.subTest(offset=offset):
                data = fixture()
                struct.pack_into(fmt, data, offset, value)
                with self.assertRaises(scan.SignatureError):
                    scan.Inventory(data)

    def test_invalid_import_rva_mutants(self):
        for offset, fmt, value in ((0x800, '<I', 0), (0x810, '<I', 0),
            (0x800, '<I', 0x5000), (0x80c, '<I', 0x5000), (0x880, '<Q', 0x5000)):
            with self.subTest(offset=offset, value=value):
                data = fixture()
                struct.pack_into(fmt, data, offset, value)
                with self.assertRaises(scan.SignatureError):
                    scan.Inventory(data).imports()

    def test_unterminated_descriptors_and_strings_refused(self):
        data = fixture()
        struct.pack_into('<I', data, 0x114, 20)
        with self.assertRaisesRegex(scan.SignatureError, 'descriptor'):
            scan.Inventory(data).imports()
        data = fixture()
        data[0x860:0xa60] = b'x'*512
        with self.assertRaisesRegex(scan.SignatureError, 'unterminated import string'):
            scan.Inventory(data).imports()

    def test_wrong_dll_refused(self):
        data = fixture()
        data[0x860:0x86b] = b'OTHERX.dll\0'
        with self.assertRaisesRegex(scan.SignatureError, 'unexpected DLL'):
            scan.Inventory(data).imports()

    def test_unbacked_and_ambiguous_rva_refused(self):
        image = scan.Inventory(fixture())
        for rva, size in ((0x2400, 1), (0x23ff, 2), (-1, 1), (0x2000, 0)):
            with self.subTest(rva=rva), self.assertRaises(scan.SignatureError):
                image.read(rva, size)
        image.sections.append(dict(image.sections[1]))
        with self.assertRaises(scan.SignatureError):
            image.read(0x2000, 1)

    def test_hash_gate_cli_and_missing_import(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'fixture.exe'
            data = fixture()
            path.write_bytes(data)
            with self.assertRaisesRegex(scan.SignatureError, 'SHA-256 mismatch'):
                scan.analyze(path)
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(scan.main([str(path)]), 1)
            self.assertEqual(path.read_bytes(), data)
            with patch.object(scan, 'PINNED_SHA256', scan.hashlib.sha256(data).hexdigest()):
                result = scan.analyze(path)
                self.assertEqual(result['safe_game_thread'], 'unknown')
                self.assertEqual(result['pump_address_references']['0x1000'][0]['site_rva'], '0x1090')
            struct.pack_into('<Q', data, 0x880, (1 << 63) | 123)
            path.write_bytes(data)
            with patch.object(scan, 'PINNED_SHA256', scan.hashlib.sha256(data).hexdigest()):
                with self.assertRaisesRegex(scan.SignatureError, 'exactly one'):
                    scan.analyze(path)


if __name__ == '__main__':
    unittest.main()
