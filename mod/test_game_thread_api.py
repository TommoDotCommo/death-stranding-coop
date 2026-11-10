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

class Pad(c.Structure):
    _fields_=[('size',c.c_uint32),('buttons',c.c_uint16),('left_trigger',c.c_uint8),('right_trigger',c.c_uint8),
              ('lx',c.c_int16),('ly',c.c_int16),('rx',c.c_int16),('ry',c.c_int16),
              ('keys',c.c_uint8*8),('key_count',c.c_uint8),('mouse_buttons',c.c_uint8),('mouse_x',c.c_int16),('mouse_y',c.c_int16)]

class ApiTests(unittest.TestCase):
    def test_partner_from_config(self):
        # partner "own" in mod.json: own mode from the start, no puppet.double command; the body is made and parked.
        for partner,mode in (('own','own'),('peer','peer'),('off','none')):
            with tempfile.TemporaryDirectory() as directory:
                root=Path(directory);shutil.copyfile(ROOT/'build/fake.so',root/'mod.so')
                lib=c.CDLL(str(root/'mod.so'));lib.mod_init.argtypes=[c.c_char_p]
                lib.mod_game_thread_tick.argtypes=[c.POINTER(Context)];lib.mod_status.argtypes=[c.c_void_p,c.c_uint]
                lib.mod_last_error.restype=c.c_char_p;lib.mod_test_thread_id.restype=c.c_uint32;lib.mod_test_frame_caller.restype=c.c_uint64
                lib.mod_test_set_pose.argtypes=[c.POINTER(c.c_double),c.POINTER(c.c_float),c.c_int]
                config=root/'mod.json';config.write_text(json.dumps(dict(role='host',peer='127.0.0.1',slot=0,peer_slot=1,
                    nickname='probe',provider='game',session=45,required=True,game_thread=True,puppet_probe=True,partner=partner,
                    discovery_sha256=PIN)))
                self.assertEqual(lib.mod_init(str(config).encode()),1,lib.mod_last_error())
                def status():
                    cap=lib.mod_status(None,0)
                    for _ in range(8):
                        out=c.create_string_buffer(cap);required=lib.mod_status(out,cap)
                        if required<=cap:return json.loads(out.value)
                        cap=required*2
                    self.fail('status kept growing')
                ctx=Context(c.sizeof(Context),1,lib.mod_test_thread_id(),1,lib.mod_test_frame_caller(),1,0)
                try:
                    lib.mod_test_set_pose((c.c_double*3)(10,20,30),(c.c_float*9)(1,0,0,0,1,0,0,0,1),0)
                    for _ in range(3):self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                    self.assertFalse((root/'puppet.double').exists())
                    self.assertEqual(status()['double']['mode'],mode,partner)
                    log=next(root.glob('mod-*.log')).read_text()
                    self.assertEqual(log.count('made and parked'),1 if partner=='own' else 0,partner)
                    self.assertEqual('DOUBLE start mode' in log,partner!='off',partner)
                    if partner!='off':self.assertEqual(status()['double']['stage'],'parked')   # no partner yet: nothing borrowed
                finally:lib.mod_shutdown()
    def test_guarded_tick_and_one_shot_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);shutil.copyfile(ROOT/'build/fake.so',root/'mod.so')
            lib=c.CDLL(str(root/'mod.so'));lib.mod_init.argtypes=[c.c_char_p];lib.mod_tick.argtypes=[c.c_double]
            lib.mod_game_thread_tick.argtypes=[c.POINTER(Context)];lib.mod_status.argtypes=[c.c_void_p,c.c_uint]
            lib.mod_last_error.restype=c.c_char_p;lib.mod_test_run_stamp.restype=c.c_char_p
            lib.mod_test_thread_id.restype=c.c_uint32;lib.mod_test_frame_caller.restype=c.c_uint64
            lib.mod_test_set_pose.argtypes=[c.POINTER(c.c_double),c.POINTER(c.c_float),c.c_int]
            lib.mod_pad.argtypes=[c.POINTER(Pad)]
            config=root/'mod.json';config.write_text(json.dumps(dict(role='host',peer='127.0.0.1',slot=0,peer_slot=1,
                nickname='probe',provider='game',session=44,required=True,game_thread=True,puppet_probe=True,virtual_pad=True,
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
                # The resident double: echo the porter one second late, two metres to the right, then hand it back.
                self.assertEqual(status()['double'],dict(stage='off',mode='none',entity=0))
                twin=root/'puppet.double';twin.write_text('echo wrong-process\n')
                self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1);self.assertEqual(status()['double']['stage'],'off')
                stamp=lib.mod_test_run_stamp().decode()
                twin.write_text('echo '+stamp+'\n');self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertFalse(twin.exists());self.assertEqual(status()['double']['stage'],'parked')
                time.sleep(1.02);self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertEqual(status()['double']['stage'],'driving');self.assertEqual(status()['double']['mode'],'echo')
                # A local game event hands the double back; it comes back when the event ends.
                lib.mod_test_set_event(1);self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertEqual(status()['double']['stage'],'parked');self.assertTrue(status()['event'])
                lib.mod_test_set_event(0);time.sleep(.06);self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertEqual(status()['double']['stage'],'driving');self.assertFalse(status()['event'])
                twin.write_text('off '+stamp+'\n');self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertEqual(status()['double']['stage'],'parked')
                log=next(root.glob('mod-*.log')).read_text()
                rows=[json.loads(line[len('DOUBLE '):]) for line in log.splitlines() if line.startswith('DOUBLE {')]
                self.assertEqual([r['event'] for r in rows],['acquired','released','acquired','released'])
                self.assertEqual(rows[0]['readback'],[12,20,30]);self.assertEqual(int(rows[0]['flags'],16)&0x102,0x2)
                self.assertEqual(rows[1]['readback'],[0,0,0]);self.assertEqual(int(rows[1]['flags'],16)&0x102,0x100)
                self.assertIn('DOUBLE game event: double handed back',log);self.assertIn('DOUBLE game event over',log)
                self.assertIn('DOUBLE ignored command for another process',log)
                # Own body: the mod makes its body (dressed, parked) and leaves the resident double alone.
                twin.write_text('own '+stamp+'\n');self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertEqual(status()['double']['mode'],'own');self.assertEqual(status()['double']['stage'],'parked')
                self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                log=next(root.glob('mod-*.log')).read_text()
                self.assertEqual(log.count('made and parked'),1);self.assertIn('BODY model components built',log)
                self.assertEqual(status()['double']['stage'],'parked')   # no partner yet: nothing borrowed
                twin.write_text('off '+stamp+'\n');self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertEqual(status()['double']['mode'],'none')
                # Scripted pad: refused foreign script, then press A for 300 ms; never blocks, size-checked.
                pad=Pad(c.sizeof(Pad));self.assertEqual(c.sizeof(Pad),32)
                self.assertEqual(lib.mod_pad(c.byref(pad)),0);self.assertEqual(pad.buttons,0)
                script=root/'pad.script';script.write_text('pad other\npress A 300\n')
                self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1);self.assertFalse(script.exists())
                self.assertEqual(lib.mod_pad(c.byref(pad)),0)
                script.write_text('pad '+stamp+'\npress A 300\nstick 0 1 200\n')
                self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1);self.assertTrue(status()['pad']['running'])
                self.assertEqual(lib.mod_pad(c.byref(pad)),1);self.assertEqual((pad.buttons,pad.ly,pad.size,pad.key_count),(0x1000,0,32,0))
                bad=Pad(15);self.assertEqual(lib.mod_pad(c.byref(bad)),-1)
                time.sleep(.55);self.assertEqual(lib.mod_pad(c.byref(pad)),1);self.assertEqual((pad.buttons,pad.ly),(0,32767))
                time.sleep(.25);self.assertEqual(lib.mod_pad(c.byref(pad)),0);self.assertEqual(pad.ly,0)
                self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1);self.assertFalse(status()['pad']['running'])
                self.assertGreaterEqual(status()['pad']['active_calls'],2)
                # Scripted keyboard and mouse ride in the same state: keys held, mouse speed, a button; all clear after.
                script.write_text('pad '+stamp+'\nkey W+SHIFT 300 mouse 300 -40\nclick R 200\n')
                self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1)
                self.assertEqual(lib.mod_pad(c.byref(pad)),1)
                self.assertEqual((pad.key_count,pad.keys[0],pad.keys[1],pad.mouse_x,pad.mouse_y,pad.mouse_buttons,pad.buttons),(2,0x57,0x10,300,-40,0,0))
                time.sleep(.35);self.assertEqual(lib.mod_pad(c.byref(pad)),1);self.assertEqual((pad.key_count,pad.mouse_buttons,pad.mouse_x),(0,2,0))
                time.sleep(.45);self.assertEqual(lib.mod_pad(c.byref(pad)),0);self.assertEqual((pad.key_count,pad.mouse_buttons),(0,0))
                self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),1);self.assertFalse(status()['pad']['running'])
                log=next(root.glob('mod-*.log')).read_text()
                self.assertIn('PAD script refused: pad script line 1',log);self.assertIn('PAD script started steps=3',log)
                self.assertIn('PAD script finished',log);self.assertIn('POSE available',log)
            finally:lib.mod_shutdown()
            self.assertEqual(lib.mod_game_thread_tick(c.byref(ctx)),-1)

if __name__=='__main__':unittest.main()
