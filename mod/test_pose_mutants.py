"""Each broken pose-exchange protection must fail its own host oracle (exit 1, 'oracle' on stderr), never crash."""
from pathlib import Path
import subprocess
import mutants as runner
import tempfile
ROOT = Path(__file__).resolve().parent
SOURCES = ['pose.cpp', 'providers.cpp', 'discovery.cpp', '../core/core.cpp']
P = 'pose.cpp'
V = 'providers.cpp'
D = 'discovery.cpp'
mutants = [
 # (mutant name, test to run, [(file, before, after)])
 ('axes not swapped', 'convert_axes', [(V, 's.position={p.position[0],p.position[2],p.position[1]};', 's.position={p.position[0],p.position[1],p.position[2]};')]),
 ('yaw from the right-vector row', 'yaw_live', [(V, 'yawFromForward(p.rotation[3],p.rotation[4],previousYaw)', 'yawFromForward(p.rotation[0],p.rotation[1],previousYaw)')]),
 ('yaw sign flipped', 'yaw_live', [(V, 'return std::atan2(fx,fy);', 'return std::atan2(-fx,fy);')]),
 ('yaw sign flipped (axes)', 'yaw_axes', [(V, 'return std::atan2(fx,fy);', 'return std::atan2(-fx,fy);')]),
 ('yaw as map heading', 'yaw_matches_velocity', [(V, 'return std::atan2(fx,fy);', 'return std::atan2(fy,fx);')]),
 ('yaw degenerate keeps nothing', 'yaw_axes', [(V, 'if(!(std::hypot(fx,fy)>1e-3)) return previous;', '')]),
 ('gate always false', 'gate', [('providers.h', 'return !available_;', 'return false;')]),
 ('velocity not clamped', 'velocity_clamp', [(V, 'double scale=speed>MaxSpeed?MaxSpeed/speed:1.;', 'double scale=1.;')]),
 ('velocity gap ignored', 'velocity_gap', [(V, 'us-t_<=MaxGapUs', 'true')]),
 ('velocity jump ignored', 'velocity_jump', [(V, 'if(d>JumpMetres) m.teleport=true;', '')]),
 ('velocity from the first sample', 'velocity_basic', [(V, 'if(!have_ || us>t_) {have_=true;t_=us;p_=position;}', 'if(!have_) {have_=true;t_=us;p_=position;}')]),
 ('teleport flag dropped', 'provider_flow', [(V, 'if(motion.teleport) s.flags|=ds::Teleport;', '')]),
 ('self_tick catches up', 'self_tick_no_catch_up', [('timing.h', 'deadline=nextDeadline(deadline,now,period);return true;', 'deadline+=period;return true;')]),
 ('self_tick wrong period', 'self_tick_rate', [('timing.h', 'explicit FixedRate(uint64_t us):period(us) {}', 'explicit FixedRate(uint64_t us):period(us/2) {}')]),
 ('presence not rate limited', 'presence_rate', [(V, 'if(started_ && (us<last_ || us-last_<PeriodUs)) return false;', '')]),
 ('presence not size bounded', 'presence_cap', [(V, 'if(!budget_.append(text.size()))', 'if(false)')]),
 ('presence keeps writing after failure', 'presence_write_failure', [(V, 'if(!write_(text)) {stopped_=true;', 'if(!write_(text)) {stopped_=false;')]),
 ('manager null accepted', 'pose_manager_null', [(P, 'if(!manager||manager>=0x0000800000000000ULL) return fail("manager null");', ''), (D, 'if(!manager){r.reason="manager null";return r;}', '')]),
 ('slow selection path followed', 'pose_slow_path', [(D, 'mode==1&&kind==2&&other==0&&index>=0&&index<64) {\n            auto slot', 'index>=0&&index<64) {\n            auto slot')]),
 ('bad index accepted', 'pose_bad_index', [(D, 'mode==1&&kind==2&&other==0&&index>=0&&index<64) {\n            auto slot', 'mode==1&&kind==2&&other==0&&index>=0) {\n            auto slot')]),
 ('index ignored', 'pose_index_selects_slot', [(D, 'add(manager,0x48+8*uint64_t(index))', 'add(manager,0x48+16)')]),
 ('player null accepted', 'pose_player_null', [(D, 'if(!p||p>=0x0000800000000000ULL){r.reason="player null";return r;}', '')]),
 ('entity null accepted', 'pose_entity_null', [(P, 'if(!entity||entity>=0x0000800000000000ULL) return fail("entity null");', '')]),
 ('non-finite position accepted', 'pose_nonfinite_position', [(P, 'for(double v:p.position) if(!std::isfinite(v)) return fail("non-finite pose");', '')]),
 ('non-finite rotation accepted', 'pose_nonfinite_rotation', [(P, 'for(float v:p.rotation) if(!std::isfinite(v)) return fail("non-finite pose");', '')]),
 ('local-human flags unchecked', 'pose_not_human', [(P, 'if(human[0]!=1||human[1]!=0) return fail("not local human");', '')]),
 ('only the first human byte checked', 'pose_not_human', [(P, 'human[0]!=1||human[1]!=0', 'human[0]!=1')]),
 ('unreadable pose accepted', 'pose_unreadable', [(P, 'if(!s.read(entity+0xe0,p.rotation.data(),36)) return fail("pose unreadable");', 's.read(entity+0xe0,p.rotation.data(),36);')]),
 ('coherence recheck removed', 'pose_incoherent_entity', [(P, 'if(!same) return fail("incoherent chain");', '')]),
 ('entity recheck removed', 'pose_incoherent_entity', [(P, '&&entity2==entity', '')]),
 ('player recheck removed', 'pose_incoherent_player', [(P, '&&player2==player', '')]),
 ('manager recheck removed', 'pose_incoherent_manager', [(P, '&&manager2==manager', '')]),
 ('torn copies accepted', 'pose_torn', [(D, '||x!=y', '')]),
]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    # Sources are copied so a mutated header is seen by every translation unit.
    def one(job):
        index, (name, test, edits) = job
        work = tmp/f'w{index}'
        (work/'core').mkdir(parents=True)
        (work/'mod').mkdir()
        for f in list(ROOT.glob('*.h'))+list(ROOT.glob('*.cpp')):
            (work/'mod'/f.name).write_text(f.read_text())
        for f in (ROOT.parent/'core').glob('*'):
            if f.suffix in ('.h', '.cpp'):
                (work/'core'/f.name).write_text(f.read_text())
        for file, before, after in edits:
            path = work/'mod'/file if not file.startswith('..') else work/'core'/Path(file).name
            text = path.read_text()
            assert before in text, (name, file, before)
            path.write_text(text.replace(before, after, 1))
        exe = work/'mutant'
        subprocess.run(['g++', '-std=c++17', '-O1', '-pthread', '-DDS_TESTING', str(work/'mod'/'pose_test.cpp'), str(work/'mod'/'pose.cpp'),
                        str(work/'mod'/'providers.cpp'), str(work/'mod'/'discovery.cpp'), str(work/'core'/'core.cpp'), '-o', str(exe)],
                       check=True, capture_output=True)
        result = subprocess.run([str(exe), test], text=True, capture_output=True, timeout=120)
        assert result.returncode == 1 and 'oracle' in result.stderr, (name, test, result.returncode, result.stderr)
        return name, result.stderr.strip()
    for name, oracle in runner.parallel(one, list(enumerate(mutants))):
        print('KILLED pose mutant', name, '->', oracle)
    print('all', len(mutants), 'pose mutants killed')
