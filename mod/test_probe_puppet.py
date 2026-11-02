import struct
import unittest
from probe_puppet import pose_blob,validate_event,command_bytes
class ProofTests(unittest.TestCase):
    def test_command_is_binary_lf_on_every_platform(self):
        self.assertEqual(command_bytes(42,12345),b'spawn 42-0000000000003039\n')
        self.assertNotIn(b'\r',command_bytes(42,12345))
    def test_scalar_layout_and_external_state_proof(self):
        actual=pose_blob(struct.pack('<3d9f',10,23,30,1,0,0,0,1,0,0,0,1))
        local=pose_blob(struct.pack('<3d9f',10,20,30,1,0,0,0,1,0,0,0,1))
        event=dict(actor='0x2000',local_entity='0x1000',requested_position=[10,23,30],
                   local_after_available=True,local_after_entity='0x1000',local_after_position=[10,20,30])
        validate_event(event,actual,local)
        for change in ({'actor':'0x1000'},{'actor':'0x0'},{'local_after_entity':'0x3000'},
                       {'requested_position':[100,200,300]},{'local_after_position':[100,200,300]},
                       {'local_after_available':False}):
            with self.subTest(change=change),self.assertRaises(ValueError):validate_event(dict(event,**change),actual,local)
        with self.assertRaises(ValueError):validate_event(event,local,local)
        with self.assertRaises(ValueError):pose_blob(b'bad')
        with self.assertRaises(ValueError):pose_blob(struct.pack('<3d9f',float('nan'),0,0,*([0]*9)))
if __name__=='__main__':unittest.main()
