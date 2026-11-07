"""Every read-only vehicle guard has an unchanged fake-memory oracle that rejects its mutant."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
MUTANTS = [
    ('unvalidated sampler', 'sampler', 'require(s.signatures.size()==6,"sampler not validated");', ''),
    ('not local human', 'human', 'require(c.value<uint16_t>(add(player,0x78))==1,"not local human");', ''),
    ('porter class', 'porter_type', 'require(c.type(o.porter,Porter),"porter type unverified");', ''),
    ('table getter identity', 'table_identity', 'if(value<uint64_t>(table)!=s.base+t.getter)return false;', ''),
    ('getter instruction identity', 'getter_identity', 'return std::memcmp(code,expected,sizeof code)==0;', 'return true;'),
    ('unbounded components', 'count', 'require(count&&count<=MaxComponents,"component count invalid");', ''),
    ('null component list', 'list', 'require(pointer(list),"component list invalid");', ''),
    ('ambiguous components', 'duplicate_component', 'require(!o.component,"ambiguous ride component");', ''),
    ('foreign component owner', 'owner', 'require(c.value<uint64_t>(add(component,0x48))==o.porter,"ride owner differs");', ''),
    ('activation ignored', 'foot', 'o.riding=(active&1)!=0;', 'o.riding=true;'),
    ('primary mover ignored', 'activation', 'require(o.riding==(mover==o.component),"ride activation changing");', ''),
    ('null reference', 'reference', 'require(pointer(reference)&&reference>=0x10020,"vehicle reference invalid");', ''),
    ('noncanonical pointer', 'canonical', 'p<0x0000800000000000ULL', 'true'),
    ('unaligned pointer', 'alignment', '&&p%8==0', ''),
    ('reference base adjustment', 'bike', 'o.vehicle=reference-0x20;', 'o.vehicle=reference;'),
    ('unknown vehicle called bike', 'vehicle_type', 'else require(false,"vehicle type unsupported");', 'else o.kind=Kind::Motorbike;'),
    ('kind consistency', 'kind', 'require(kind==(o.kind==Kind::Motorbike?3u:2u),"vehicle kind differs");', ''),
    ('seat vehicle link', 'seat_vehicle', 'require(c.value<uint64_t>(add(seatState,0xa68))==reference,"seat vehicle differs");', ''),
    ('null seat state', 'seat_null', 'require(pointer(seatState),"seat state unavailable");', ''),
    ('seat range ignored', 'seat_unknown', 'if(seat<=4)', 'if(true)'),
    ('driver passenger reversed', 'truck_passengers', 'seat==0?Seat::Driver:Seat::Passenger', 'seat==0?Seat::Passenger:Seat::Driver'),
    ('non-finite position', 'position_finite', 'for(double x:o.position)require(std::isfinite(x),"vehicle position non-finite");', ''),
    ('non-finite rotation', 'rotation_finite', 'for(float x:r)if(!std::isfinite(x))return false;', 'for(float x:r)if(!std::isfinite(x))return true;'),
    ('nonorthogonal rotation', 'rotation_orthogonal', 'if(std::abs(dot-(i==j?1.:0.))>.05)return false;', ''),
    ('reflected rotation', 'rotation_proper', 'return std::abs(det-1.)<=.05;', 'return true;'),
    ('partial read accepted', 'unreadable', 'require(s.read(a,out,n),"memory unreadable or torn");', 's.read(a,out,n);'),
    ('partial observation exposed', 'unreadable', 'Observation unavailable;', 'Observation unavailable=o;'),
    ('torn read accepted', 'torn', '||x!=y', ''),
    ('coherence recheck absent', 'vehicle_changed', 'require(c.unchanged(),"observation changed");', ''),
    ('seat coherence absent', 'seat_changed', 'require(c.unchanged(),"observation changed");', ''),
    ('selection recheck absent', 'selection_changed', 'require(!*selectedAgain.reason&&selectedAgain.player==player&&selectedAgain.slot==selected.slot,"player selection changed");', ''),
    ('speed wrong units', 'speed', 'double(us-previousTime_)/1000000.', 'double(us-previousTime_)/1000.'),
    ('speed height ignored', 'speed', 'i<3;++i)distance=', 'i<2;++i)distance='),
    ('speed interval ignored', 'speed_gap', '&&us-previousTime_<=MaxGapUs', ''),
    ('speed discontinuity accepted', 'speed_jump', 'speed<=MaxSpeed', 'true'),
    ('speed survives unavailable sample', 'speed_reset', 'reset();Observation unavailable;', 'Observation unavailable;'),
    ('speed crosses vehicle change', 'speed_vehicle', '&&previousVehicle_==o.vehicle', ''),
    ('speed crosses seat change', 'speed_seat', '&&previousSeatIndex_==o.seatIndex', ''),
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
        for name, oracle, before, after in MUTANTS:
            source = 'discovery.cpp' if name == 'torn read accepted' else 'vehicle.cpp'
            assert sources[source].count(before) == 1, (name, before)
            changed = work / source
            changed.write_text(sources[source].replace(before, after, 1))
            subprocess.run(['g++', '-std=c++17', '-O1', '-pthread', '-I', str(ROOT), '-c',
                            str(changed), '-o', str(work / 'mutant.o')], check=True, capture_output=True)
            other = 'vehicle.cpp' if source == 'discovery.cpp' else 'discovery.cpp'
            exe = work / 'mutant'
            subprocess.run(['g++', '-pthread', str(work / 'test.o'), str(work / 'mutant.o'),
                            str(work / (other + '.o')), '-o', str(exe)], check=True, capture_output=True)
            result = subprocess.run([str(exe), oracle], capture_output=True, text=True, timeout=10)
            assert result.returncode == 1 and 'vehicle oracle:' in result.stderr, (name, result.returncode, result.stderr)
            print('KILLED vehicle mutant:', name, flush=True)
    print(f'all {len(MUTANTS)} vehicle mutants killed')


if __name__ == '__main__':
    main()
