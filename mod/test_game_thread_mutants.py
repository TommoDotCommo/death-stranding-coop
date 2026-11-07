"""Each disabled dispatch/puppet protection must fail an oracle, never crash."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parent
families=[('dispatch',['ABI','THREAD','CALLER','DEPTH','NETWORK','STACK','SIGNATURE','TARGET'],
           ['game_thread_test.cpp','game_thread.cpp','discovery.cpp']),
          ('puppet',['PERMISSION','WORLD','ALIAS','LOCAL','READBACK','LIFETIME','ONCE','ARGUMENTS','REMOVE_MODE','RESOURCE'],
           ['puppet_test.cpp','puppet.cpp','pose.cpp','discovery.cpp']),
          ('double',['ANCHOR','STILL','NETWORK','ACTION_OWNER','ACTION_MASK','TRANSIENT','EFFECTS','SMOOTH','YIELD_ANCHOR','PRIVATE_ROOM','PERMISSION','PARKED','IDENTITY','YIELD','READBACK','LOCAL','RELEASE','WORLD','FIND_PARENT','FIND_UNIQUE','HOLD','HIDDEN_YIELD'],
           ['double_test.cpp','double.cpp','puppet.cpp','pose.cpp','discovery.cpp']),
          ('ownbody',['WORLD','ALIVE','PARK','DRESS','OWN_PARSE','MODEL_OWNER','MODEL_EXACT','DRESSED_COUNT','APPLY_ALWAYS'],['double_test.cpp','double.cpp','puppet.cpp','pose.cpp','discovery.cpp']),
          ('padscript',['BOUNDS','STAMP','TIMING'],['pad_test.cpp','pad.cpp']),
          ('cargo',['ECHO','HELD','TAKEN','TOOK_ECHO','DOOM','FIND','NEAR_HELD','CARRY_FRAME','LOCAL','STAGED','MOVED','OTHER_ID','RUN'],['cargo_test.cpp','cargo.cpp','discovery.cpp']),
          ('partner',['TELEPORT','ACCEL','WIRE','WIRE_ANY','OFFSET','OFFSET_RANGE','PROGRESS_RANGE'],['partner_test.cpp','partner_logic.cpp','../core/core.cpp']),
          ('structure',['PACK','NET','REMOVED','EARLY'],['structures_test.cpp','structures.cpp','discovery.cpp']),
          ('body',['ANY_CLASS'],['body_test.cpp','body.cpp','discovery.cpp']),
          ('auto',['ONCE_IN_GAME'],['auto_continue_test.cpp']),
          ('marker',['LEFT','TWICE','RATE','FOREIGN'],['marker_test.cpp','marker.cpp','discovery.cpp'])]
with tempfile.TemporaryDirectory() as directory:
    executable=Path(directory)/'mutant'
    for family,mutants,sources in families:
        for name in mutants:
            macro='MUTANT_'+family.upper()+'_'+name
            subprocess.run(['g++','-std=c++17','-O1','-pthread','-D'+macro,
                            *(str(ROOT/s) for s in sources),'-o',str(executable)],check=True,capture_output=True)
            result=subprocess.run([str(executable)],text=True,capture_output=True,timeout=20)
            assert result.returncode==1 and 'oracle' in result.stderr,(macro,result.returncode,result.stderr)
            print('KILLED',macro,flush=True)
