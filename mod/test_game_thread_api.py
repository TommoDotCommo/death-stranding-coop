"""Real mod C ABI and threads over fake memory; production has no fake exports."""
import ctypes as c
import json
from pathlib import Path
import shutil
import tempfile
import threading
import time
import unittest

ROOT=Path(__file__).resolve().parent
PIN='9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b'
class Context(c.Structure):
    _fields_=[('size',c.c_uint32),('version',c.c_uint32),('thread_id',c.c_uint32),('depth',c.c_uint32),
              ('caller_rva',c.c_uint64),('network_enforced',c.c_uint32),('reserved',c.c_uint32)]

class ApiTests(unittest.TestCase):
    def test_guarded_tick_and_one_shot_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);shutil.copyfile(ROOT/'build/fake.so',root/'mod.so')
            lib=c.CDLL(str(root/'mod.so'));lib.mod_init.argtypes=[c.c_char_p];lib.mod_tick.argtypes=[c.c_double]
            lib.mod_game_thread_tick.argtypes=[c.POINTER(Context)];lib.mod_status.argtypes=[c.c_void_p,c.c_uint]
            lib.mod_last_error.restype=c.c_char_p;lib.mod_test_run_stamp.restype=c.c_char_p
            lib.mod_test_thread_id.restype=c.c_uint32;lib.mod_test_frame_caller.restype=c.c_uint64
            lib.mod_test_set_pose.argtypes=[c.POINTER(c.c_double),c.POINTER(c.c_float),c.c_int]
            config=root/'mod.json';config.write_text(json.dumps(dict(role='host',peer='127.0.0.1',slot=0,peer_slot=1,
                nickname='probe',provider='game',session=44,required=True,game_thread=True,puppet_probe=True,
                discovery_sha256=PIN)))
            self.assertEqual(lib.mod_init(str(config).encode()),1,lib.mod_last_error())
            def status():
                # Network counters can grow between the size query and copy.
                cap=lib.mod_status(None,0)
                for _ in range(8):
                    out=c.create_string_buffer(cap)
                    required=lib.mod_status(out,cap)
                    if required<=cap:return json.loads(out.value)
                    cap=required*2
                self.fail('status kept growing beyond retry budget')
            ctx=Context(c.sizeof(Context),1,lib.mod_test_thread_id(),1,lib.mod_test_frame_caller(),1,0)
            try:
                self.assertEqual(lib.mod_tick(.05),1);self.assertEqual(status()['self_tick']['ticks'],0)
                for field,value in [('version',2),('thread_id',ctx.thread_id+1),('depth',2),('caller_rva',ctx.caller_rva+1),('network_enforced',0),('reserved',1)]:
                    bad=Context.from_buffer_copy(ctx);setattr(bad,field,value)
                    self.assertEqual(lib.mod_game_thread_tick(c.byref(bad)),0,field)
                self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                result=[]
                worker=threading.Thread(target=lambda:result.append(lib.mod_game_thread_tick(c.byref(ctx))))
                worker.start();worker.join();self.assertEqual(result,[0])
                self.assertEqual(status()['game_thread']['thread_id'],ctx.thread_id)
                position=(c.c_double*3)(10,20,30);rotation=(c.c_float*9)(1,0,0,0,1,0,0,0,1)
                lib.mod_test_set_pose(position,rotation,0)
                command=root/'puppet.spawn';command.write_text('spawn wrong-process\n')
                self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1);self.assertEqual(status()['puppet']['stage'],'disarmed')
                command.write_text('spawn '+lib.mod_test_run_stamp().decode()+'\n')
                self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertFalse(command.exists());self.assertEqual(status()['puppet']['stage'],'created')
                self.assertTrue(status()['pose']['available'])
                time.sleep(1.02);self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertEqual(status()['puppet']['stage'],'moved')
                time.sleep(1.02);self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertEqual(status()['puppet']['stage'],'remove_requested')
                log=next(root.glob('mod-*.log')).read_text()
                events=[json.loads(line[len('PUPPET '):]) for line in log.splitlines() if line.startswith('PUPPET {')]
                self.assertEqual([e['event'] for e in events],['resource_requested','create_requested','created','moved','remove_requested'])
                for event in events[2:]:
                    self.assertNotEqual(event['actor'],event['local_entity']);self.assertTrue(event['readback_available'])
                    self.assertEqual(event['local_after_entity'],event['local_entity']);self.assertTrue(event['local_after_available'])
                    self.assertEqual(event['requested_position'],event['readback_position'])
            finally:lib.mod_shutdown()
            self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),-1)

if __name__=='__main__':unittest.main()
