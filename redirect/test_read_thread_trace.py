import struct
import tempfile
from pathlib import Path
import unittest
import read_thread_trace as reader


def fixture():
    data = bytearray(reader.BUFFER_SIZE)
    reader.HEADER.pack_into(data, 0, reader.MAGIC, 1, 2048, 184, 80, 12345, 0x140000000, 0x8000000,
                           42, 0, 2, 0, 1, 0)
    for index in range(2):
        reader.RECORD.pack_into(data, 80 + 184*index, 1, 101, 1, 1000+index, 0, 1+index, 1, -1 if index else 0,
                               0x141a19387, 0x141a17aec, *([0]*15), 1 if index==0 else 0, 0)
    return data


class ReaderTests(unittest.TestCase):
    def test_round_trip_negative_result_and_host_relative_stack(self):
        report = reader.decode(fixture(), fixture(), 42, 12345)
        self.assertEqual(report['records'][1]['result'], -1)
        self.assertEqual(report['records'][0]['stack'][0]['host_rva'], '0x1a17aec')
        self.assertIsNone(report['records'][0]['result'])
        self.assertEqual(report['safe_mutation_phase'], 'unknown')

    def test_pending_or_changing_record_never_fabricates_exit(self):
        first, second = fixture(), fixture()
        struct.pack_into('<I', first, 80+184, 0)
        report = reader.decode(first, second, 42, 12345)
        self.assertEqual(report['incomplete_records'], [1])
        self.assertEqual(len(report['records']), 1)
        struct.pack_into('<I', second, 80+184, 0)
        self.assertEqual(reader.decode(first, second, 42, 12345)['incomplete_records'], [1])

    def test_abi_identity_and_bounds_mutants(self):
        for offset, fmt, value in ((8,'<I',2), (12,'<I',4096), (16,'<I',192), (20,'<I',88),
            (24,'<Q',0), (48,'<I',99), (56,'<I',2049), (56,'<I',1),
            (80,'<I',2), (80+32,'<I',0), (80+176,'<I',17)):
            with self.subTest(offset=offset):
                data=fixture();struct.pack_into(fmt,data,offset,value)
                with self.assertRaises(ValueError):reader.decode(data,data,42,12345)
        with self.assertRaises(ValueError):reader.decode(fixture()[:-1],fixture(),42,12345)
        with self.assertRaises(ValueError):reader.decode(fixture(),fixture(),43,12345)

    def test_pair_identity_and_clock_mutants(self):
        for offset,fmt,value in ((80+184+4,'<I',999), (80+184+16,'<Q',999), (80+184+28,'<I',1)):
            data=fixture();struct.pack_into(fmt,data,offset,value)
            with self.assertRaises(ValueError):reader.decode(data,data,42,12345)

    def test_wrong_identity_between_reads_refused(self):
        second=fixture();struct.pack_into('<Q',second,32,0x150000000)
        with self.assertRaises(ValueError):reader.decode(fixture(),second,42,12345)

    def test_log_address_and_pid_creation_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'redirect-42-0000000000003039.log'
            line=f'THREAD_TRACE\tv1 buffer=0x12340000\tbytes={reader.BUFFER_SIZE}; observation only\r\n'
            path.write_text(line,encoding='utf-16-le')
            self.assertEqual(reader.identity_from_log(path),(42,12345,0x12340000))
            path.write_text(line+line,encoding='utf-16-le')
            with self.assertRaises(ValueError):reader.identity_from_log(path)
            path.write_text('',encoding='utf-16-le')
            with self.assertRaises(ValueError):reader.identity_from_log(path)


if __name__ == '__main__':unittest.main()
