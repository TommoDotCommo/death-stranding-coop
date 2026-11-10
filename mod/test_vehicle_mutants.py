"""Every read-only vehicle guard has an unchanged fake-memory oracle that rejects its mutant."""
from pathlib import Path
import subprocess
import mutants as runner
import tempfile

ROOT = Path(__file__).resolve().parent
MUTANTS = [
    ('on foot treated as unknown', 'foot', 'else {o.kind=Kind::None;o.seat=Seat::None;}', 'else require(false,"no vehicle");'),
    ('non-vehicle parent called bike', 'other_parent', 'else if(table==s.base+Car.table)', 'else if(true)'),
    ('non-vehicle parent reason', 'other_parent', 'parent&&!o.riding?"parent is not a vehicle":""', '""'),
    ('unvalidated sampler', 'sampler', 'require(s.signatures.size()==6,"sampler not validated");', ''),
    ('not local human', 'human', 'require(c.value<uint16_t>(add(player,0x78))==1,"not local human");', ''),
    ('porter class', 'porter_type', 'require(c.type(o.porter,Porter),"porter type unverified");', ''),
    ('table getter identity', 'table_identity', 'if(value<uint64_t>(table)!=s.base+t.getter)return false;', ''),
    ('getter instruction identity', 'getter_identity', 'return std::memcmp(code,expected,sizeof code)==0;', 'return true;'),
    ('bike getter unchecked', 'table_identity', 'require(c.type(parent,Bike),"vehicle type unverified");', ''),
    ('truck kinds swapped', 'truck', 'o.kind=Kind::Truck;expected=2;', 'o.kind=Kind::Motorbike;expected=3;'),
    ('car unsupported', 'car', '{require(c.type(parent,Car),"vehicle type unverified");o.kind=Kind::Car;expected=1;}', '{}'),
    ('kind consistency', 'kind', 'require(c.value<uint32_t>(add(o.vehicle,0x4c4))==o.expectedKindValue(),"vehicle kind differs");', ''),
    ('child list ignored', 'not_child', 'require(child,"porter not among the vehicle\'s children");', ''),
    ('only first child checked', 'sibling', 'next=c.value<uint64_t>(add(next,NextSiblingOffset));', 'next=0;'),
    ('unbounded child walk', 'child_cycle', 'next&&i<MaxChildren', 'next'),
    ('child pointer unchecked', 'child_pointer', 'require(pointer(next),"child pointer invalid");', ''),
    ('parent pointer unchecked', 'parent_pointer', 'require(pointer(parent),"parent pointer invalid");', ''),
    ('parent alias', 'parent_alias', 'require(parent!=o.porter,"parent aliases porter");', ''),
    ('noncanonical pointer', 'parent_pointer', '&&p%8==0', ''),
    ('non-finite position', 'position_finite', 'for(double x:o.position)require(std::isfinite(x),"vehicle position non-finite");', ''),
    ('non-finite rotation', 'rotation_finite', 'for(float x:r)if(!std::isfinite(x))return false;', 'for(float x:r)if(!std::isfinite(x))return true;'),
    ('nonorthogonal rotation', 'rotation_orthogonal', 'if(std::abs(dot-(i==j?1.:0.))>.05)return false;', ''),
    ('reflected rotation', 'rotation_proper', 'return std::abs(det-1.)<=.05;', 'return true;'),
    ('partial read accepted', 'unreadable', 'require(s.read(a,out,n),"memory unreadable or torn");', 's.read(a,out,n);'),
    ('partial observation exposed', 'unreadable', 'Observation unavailable;', 'Observation unavailable=o;'),
    ('torn read accepted', 'torn', '||x!=y', ''),
    ('coherence recheck absent', 'parent_changed', 'require(c.unchanged(),"observation changed");', ''),
    ('selection recheck absent', 'selection_changed', 'require(!*selectedAgain.reason&&selectedAgain.player==player&&selectedAgain.slot==selected.slot,"player selection changed");', ''),
    ('speed wrong units', 'speed', 'double(us-previousTime_)/1000000.', 'double(us-previousTime_)/1000.'),
    ('speed height ignored', 'speed', 'i<3;++i)distance=', 'i<2;++i)distance='),
    ('speed interval ignored', 'speed_gap', '&&us-previousTime_<=MaxGapUs', ''),
    ('speed discontinuity accepted', 'speed_jump', 'speed<=MaxSpeed', 'true'),
    ('speed survives unavailable sample', 'speed_reset', 'reset();Observation unavailable;', 'Observation unavailable;'),
    ('speed crosses vehicle change', 'speed_vehicle', '&&previousVehicle_==o.vehicle', ''),
    ('stale watch accepted', 'stamp', 'if(!count)return false;', 'if(!count)count=MaxSamples;'),
    ('watch catches up', 'monitor_rate', 'sampled_=true;last_=us;', 'sampled_=true;last_+=PeriodUs;'),
    ('unbounded watch', 'monitor_cap', 'us-started_>=DurationUs', 'false'),
    ('one-shot repeats', 'stamp', '--remaining_;return row', 'return row'),
    ('unrated watch', 'monitor_rate', 'if(sampled_&&us-last_<PeriodUs)return {};', ''),
    ('unknown logged as on foot', 'log', '(o.available?(o.riding?"true":"false"):"null")', '(o.riding?"true":"false")'),
]


def main():
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        # Only implementation files are mutated. The oracle stays unchanged.
        subprocess.run(['g++', '-std=c++17', '-O1', '-pthread', '-I', str(ROOT), '-c',
                        str(ROOT / 'vehicle_test.cpp'), '-o', str(work / 'test.o')], check=True, capture_output=True)
        sources = {name: (ROOT / name).read_text() for name in ('vehicle.cpp', 'discovery.cpp')}
        for name in sources:
            subprocess.run(['g++', '-std=c++17', '-O1', '-pthread', '-I', str(ROOT), '-c',
                            str(ROOT / name), '-o', str(work / (name + '.o'))], check=True, capture_output=True)
        def one(job):
            index, (name, oracle, before, after) = job
            source = 'discovery.cpp' if name == 'torn read accepted' else 'vehicle.cpp'
            assert sources[source].count(before) == 1, (name, before)
            changed = work / f'm{index}-{source}'
            changed.write_text(sources[source].replace(before, after, 1))
            subprocess.run(['g++', '-std=c++17', '-O1', '-pthread', '-I', str(ROOT), '-c',
                            str(changed), '-o', str(work / f'mutant{index}.o')], check=True, capture_output=True)
            other = 'vehicle.cpp' if source == 'discovery.cpp' else 'discovery.cpp'
            exe = work / f'mutant{index}'
            subprocess.run(['g++', '-pthread', str(work / 'test.o'), str(work / f'mutant{index}.o'),
                            str(work / (other + '.o')), '-o', str(exe)], check=True, capture_output=True)
            result = subprocess.run([str(exe), oracle], capture_output=True, text=True, timeout=120)
            assert result.returncode == 1 and 'vehicle oracle:' in result.stderr, (name, result.returncode, result.stderr)
            return name
        for name in runner.parallel(one, list(enumerate(MUTANTS))):
            print('KILLED vehicle mutant:', name, flush=True)
    print(f'all {len(MUTANTS)} vehicle mutants killed')


if __name__ == '__main__':
    main()
