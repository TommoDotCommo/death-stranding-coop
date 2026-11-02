"""Synthetic class metadata and fake memory; no game data or Windows required."""
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import find_actor_types as types
from read_actor_resources import scan, readable_private
from test_find_message_calls import fixture

BASE = 0x140000000


def image():
    data = fixture()
    # Reuse file-backed data space; imports are not needed for class inventory.
    def put(rva, raw):
        offset = rva - 0x2000 + 0x800
        data[offset:offset+len(raw)] = raw
    def meta(rva, name_rva, name, parent=None):
        header = bytearray(0x70)
        header[:6] = b'\xff\xff\xff\xff\x04\x00'
        header[6] = int(parent is not None)
        struct.pack_into('<Q', header, 0x40, BASE + name_rva)
        if parent:
            struct.pack_into('<Q', header, 0x60, BASE + 0x2320)
            put(0x2320, struct.pack('<QQ', BASE + parent, 0))
        put(rva, header)
        put(name_rva, name.encode() + b'\0')
    meta(0x2180, 0x22e0, 'HumanoidResource')
    meta(0x2200, 0x2300, 'DSPlayerEntityResource', 0x2180)
    for site, target, table in ((0x1040, 0x2180, 0x2080), (0x1060, 0x2200, 0x2100)):
        off = site - 0x1000 + 0x400
        data[off:off+8] = b'\x48\x8d\x05' + struct.pack('<i', target-site-7) + b'\xc3'
        if table == 0x2080:
            put(table, struct.pack('<Q', BASE + site))
            put(table+0x80, struct.pack('<Q', BASE + 0x1000))
    # The first table's factory slot overlaps the second table's first slot;
    # use a single root table for these fixtures, then separately test player.
    put(0x2080+0x80, struct.pack('<Q', BASE + 0x1000))
    data[0x420:0x428] = b'\x48\x8b\x05' + struct.pack('<i', 0x2340-0x1027) + b'\xc3'
    put(0x2340, struct.pack('<Q', BASE + 0x2180))
    put(0x20e8, struct.pack('<Q', BASE + 0x1020))
    return data


class CatalogTests(unittest.TestCase):
    def test_catalog_uses_getter_ancestry_and_never_approves(self):
        result = types.catalog(image())
        self.assertEqual(len(result['types']), 1)
        row = result['types'][0]
        self.assertEqual(row['name'], 'HumanoidResource')
        self.assertEqual(row['static_entity_type'], 'HumanoidResource')
        self.assertEqual(row['vtable_candidate_rva'], '0x2080')
        self.assertFalse(row['spawn_approved'])
        self.assertFalse(result['spawn_approved'])

    def test_player_resource_classification(self):
        data = image()
        struct.pack_into('<Q', data, 0x800+0x80, BASE + 0x1060)
        row = types.catalog(data)['types'][0]
        self.assertTrue(row['player_resource'])
        self.assertEqual(row['ancestry'], ['DSPlayerEntityResource', 'HumanoidResource'])
        self.assertFalse(row['spawn_approved'])

    def test_corrupt_getter_parent_factory_and_data_decoys(self):
        for off, raw in ((0x447, b'\x90'), (0x980, b'bad!!!'),
                         (0x900, struct.pack('<Q', BASE+0x2000))):
            data=image();data[off:off+len(raw)]=raw
            self.assertEqual(types.catalog(data)['types'], [])
        data=image()
        # A cyclic player's bases cannot produce an approved or selected type.
        struct.pack_into('<Q', data, 0xb20, BASE+0x2200)
        struct.pack_into('<Q', data, 0x880, BASE+0x1060)
        self.assertEqual(types.catalog(data)['types'], [])

    def test_hash_refusal_and_exact_hash_success(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'fixture.exe';p.write_bytes(image())
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                types.pinned_catalog(p)
            with patch.object(types, 'PINNED_SHA256', hashlib.sha256(image()).hexdigest()):
                self.assertEqual(len(types.pinned_catalog(p)['types']), 1)


class ScanTests(unittest.TestCase):
    def setUp(self):
        self.vt = BASE + 0x2080
        self.targets = {self.vt: {'name':'HumanoidResource','player_resource':False}}
        self.blob = bytearray(96)
        struct.pack_into('<Q', self.blob, 16, self.vt)
    def read(self, at, size):
        return bytes(self.blob[at-0x10000:at-0x10000+size])
    def run_scan(self, **kw):
        args=dict(regions=[(0x10000,len(self.blob))],read_bytes=self.read,
                  targets=self.targets,max_bytes=96,seconds=1,clock=lambda:0)
        args.update(kw)
        return scan(**args)
    def test_stable_header_and_no_spawn_approval(self):
        r=self.run_scan(max_bytes=128)
        self.assertEqual(r['candidates'][0]['address'],'0x10010')
        self.assertTrue(r['address_space_exhausted'])
        self.assertFalse(r['candidates'][0]['spawn_approved'])
    def test_torn_or_short_header_refused(self):
        n=0
        def torn(at,size):
            nonlocal n
            n+=1
            b=self.read(at,size)
            return b if n!=3 else bytes(size)
        r=self.run_scan(read_bytes=torn)
        self.assertEqual(r['candidates'],[])
        self.assertEqual(r['unstable_candidates'],1)
    def test_guard_pages_image_pages_and_caps(self):
        self.assertTrue(readable_private(0x1000,4,0x20000))
        for state,protect,kind in ((0x2000,4,0x20000),(0x1000,0x104,0x20000),
                                    (0x1000,1,0x20000),(0x1000,4,0x1000000)):
            self.assertFalse(readable_private(state,protect,kind))
        r=self.run_scan(max_bytes=8)
        self.assertEqual(r['attempted_bytes'],8)
        self.assertFalse(r['address_space_exhausted'])
        self.assertEqual(r['next_address'],'0x10008')
        ticks=iter((0,2))
        self.assertEqual(self.run_scan(clock=lambda:next(ticks))['attempted_bytes'],0)
        def expired():
            raise TimeoutError('fixture enumeration cap')
            yield
        self.assertEqual(self.run_scan(regions=expired())['stop_reason'],'enumeration_cap')
    def test_failures_alignment_and_bad_limits(self):
        def fail(at,size):raise OSError('fixture unreadable')
        self.assertEqual(self.run_scan(read_bytes=fail)['failed_chunks'],1)
        for kw in ({'max_bytes':0},{'seconds':float('nan')},{'regions':[(3,10)]}):
            with self.assertRaises(ValueError):self.run_scan(**kw)


if __name__=='__main__':unittest.main()
