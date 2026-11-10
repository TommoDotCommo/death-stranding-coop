"""Each disabled dispatch/puppet protection must fail an oracle, never crash."""
from pathlib import Path
import subprocess
import tempfile
import mutants
ROOT=Path(__file__).resolve().parent
families=[('dispatch',['ABI','THREAD','CALLER','DEPTH','NETWORK','STACK','SIGNATURE','TARGET'],
           ['game_thread_test.cpp','game_thread.cpp','discovery.cpp']),
          ('puppet',['PERMISSION','WORLD','ALIAS','LOCAL','READBACK','LIFETIME','ONCE','ARGUMENTS','REMOVE_MODE','RESOURCE'],
           ['puppet_test.cpp','puppet.cpp','pose.cpp','discovery.cpp']),
          ('double',['ANCHOR','STILL','NETWORK','ACTION_OWNER','ACTION_MASK','TRANSIENT','EFFECTS','SMOOTH','YIELD_ANCHOR','PRIVATE_ROOM','PERMISSION','PARKED','IDENTITY','YIELD','READBACK','LOCAL','RELEASE','WORLD','FIND_PARENT','FIND_UNIQUE','HOLD','HIDDEN_YIELD'],
           ['double_test.cpp','double.cpp','puppet.cpp','pose.cpp','discovery.cpp']),
          ('ownbody',['WORLD','ALIVE','PARK','DRESS','OWN_PARSE','MODEL_OWNER','MODEL_EXACT','DRESSED_COUNT','APPLY_ALWAYS','INPLACE_SINGLE','INPLACE_PORTER','READY','INPLACE_RESOURCE','INPLACE_SAME','STREAMING_REF'],['double_test.cpp','double.cpp','puppet.cpp','pose.cpp','discovery.cpp']),
          ('padscript',['BOUNDS','STAMP','TIMING'],['pad_test.cpp','pad.cpp']),
          ('cargo',['ECHO','HELD','TAKEN','TOOK_ECHO','DOOM','FIND','NEAR_HELD','CARRY_FRAME','LOCAL','STAGED','MOVED','OTHER_ID','RUN','FIRST511'],['cargo_test.cpp','cargo.cpp','discovery.cpp']),
          ('carry',['SLOT','HOLDER','CAP','DROP','DUPLICATE','REAL','LOST','VARIANT','ITEM'],['carry_visual_test.cpp','carry_visual.cpp','discovery.cpp','../core/core.cpp']),
          ('partner',['TELEPORT','ACCEL','WIRE','WIRE_ANY','OFFSET','OFFSET_RANGE','PROGRESS_RANGE'],['partner_test.cpp','partner_logic.cpp','../core/core.cpp']),
          ('structure',['PACK','NET','REMOVED','EARLY'],['structures_test.cpp','structures.cpp','discovery.cpp']),
          ('body',['ANY_CLASS'],['body_test.cpp','body.cpp','discovery.cpp']),
          ('auto',['ONCE_IN_GAME'],['auto_continue_test.cpp']),
          ('marker',['LEFT','TWICE','RATE','FOREIGN','NEAR'],['marker_test.cpp','marker.cpp','discovery.cpp']),
          ('gear',['CARGO','HIDDEN','NESTED','STALE_WORLD','VISIBILITY','SLOTS','FLICKER','ORDINAL','MIXED','RESEND','FIELDS'],['gear_test.cpp','gear.cpp','puppet.cpp','pose.cpp','discovery.cpp']),
          ('nametag',['MARKUP','STILL','ORDER'],['nametag_test.cpp','nametag.cpp','discovery.cpp','../core/core.cpp']),
          ('likes',['GAP','CAP'],['likes_test.cpp','likes.cpp','discovery.cpp','../core/core.cpp']),
          ('stack',['EXTRA','LEAK','COLUMN'],['cargo_stack_test.cpp','cargo_stack.cpp','puppet.cpp','pose.cpp','discovery.cpp']),
          ('outfit',['COUNT','SKIP','RESEND','ELEMENTS'],['outfit_test.cpp','outfit.cpp','discovery.cpp']),
          ('weather',['KEEP','MARGIN','RENEW'],['weather_test.cpp','weather.cpp','discovery.cpp']),
          ('ride',['LEAK','IDENTITY','PAINT'],['ride_test.cpp','ride.cpp','providers.cpp','pose.cpp','discovery.cpp','../core/core.cpp'])]
with tempfile.TemporaryDirectory() as directory:
    builds=mutants.MacroBuilds(directory,[ROOT,ROOT.parent/'core'])
    builds.prepare(ROOT/s for _,_,sources in families for s in sources)
    def one(job):
        family,name,sources=job;macro='MUTANT_'+family.upper()+'_'+name
        executable=builds.build(macro,[ROOT/s for s in sources],Path(directory)/macro)
        result=subprocess.run([str(executable)],text=True,capture_output=True,timeout=120)
        assert result.returncode==1 and 'oracle' in result.stderr,(macro,result.returncode,result.stderr)
        return macro
    for macro in mutants.parallel(one,[(family,name,sources) for family,names,sources in families for name in names]):
        print('KILLED',macro,flush=True)
