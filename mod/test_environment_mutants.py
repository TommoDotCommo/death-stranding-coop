"""Environment protections must fail unchanged fake-memory/callback oracles."""
from pathlib import Path
import subprocess
import mutants as runner
import tempfile

ROOT = Path(__file__).resolve().parent
MUTANTS = [
    ('enable', 'MUTANT_ENV_ENABLE'), ('thread', 'MUTANT_ENV_THREAD'),
    ('gameplay', 'MUTANT_ENV_GAMEPLAY'), ('rate', 'MUTANT_ENV_RATE'),
    ('setter_signature', 'MUTANT_ENV_SIGNATURE'), ('local_read', 'MUTANT_ENV_READ'),
]
EDITS = [
    ('sampler', 'require(s.signatures.size()==6,"sampler not validated");', ''),
    ('section', 'require(s.region(s.base+p.application,8,true),"application slot outside image data");', ''),
    ('getter', 'require(code(s,p.getTime)&&code(s,p.getCycle),"time getter signature differs");', ''),
    ('range', 'require(std::isfinite(o.hour)&&o.hour>=0&&o.hour<24,"time outside [0,24)");', ''),
    ('alignment', '&&p%8==0', ''),
    ('cycle', 'require(enabled<=1&&paused<=1,"cycle flags invalid");', ''),
    ('coherent', 'require(c.unchanged(),"time observation changed");', ''),
    ('weather_coherent', 'require(c.unchanged(),"weather observation changed");', ''),
    ('grid_cap', 'width<=4096', 'true'),
    ('weather_signature', 'require(code(s,p.getRain)&&code(s,p.rainBody)&&code(s,p.mapBody),"weather getter signature differs");', ''),
    ('weather_cache', 'require(cached[0]==factor&&cached[1]==factor,"weather sampler cache uninitialized or different");', ''),
    ('weather_channel', 'require(std::isfinite(z)&&z>=0&&z<=1,"weather channel invalid");', ''),
    ('read', 'channel*4-2', 'channel'),
    ('read', 'cell*factor-.5f', 'cell*factor+.5f'),
    ('host_range', 'if(in.host->timeOfDayMs>=ds::EnvironmentDayMs)', 'if(false)'),
    ('drift', 'if(std::abs(std::remainder(double(local.hour)-hour,24.))<=.01)', 'if(false)'),
    ('midnight', 'std::min(std::nextafter(24.f,0.f),float(in.host->timeOfDayMs)/3600000.f)', 'float(in.host->timeOfDayMs)/3600000.f'),
    ('stamp', 'content=="read "+runStamp+"\\n"', 'true'),
]

def main():
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        common = ['g++', '-std=c++17', '-O1', '-pthread', '-I', str(ROOT)]
        objects = []
        for name in ('environment_test.cpp', 'config.cpp', 'discovery.cpp'):
            obj = work / (name + '.o')
            subprocess.run(common + ['-c', str(ROOT / name), '-o', str(obj)], check=True, capture_output=True)
            objects.append(str(obj))
        source = (ROOT / 'environment.cpp').read_text()
        variants = [(oracle, source, ['-D' + macro]) for oracle, macro in MUTANTS]
        for oracle, before, after in EDITS:
            assert source.count(before) == 1, (oracle, before)
            variants.append((oracle, source.replace(before, after, 1), []))
        def one(job):
            index, (oracle, changed, flags) = job
            folder = work / f'm{index}'
            folder.mkdir()
            path = folder / 'environment.cpp'
            path.write_text(changed)
            exe = folder / 'mutant'
            subprocess.run(common + flags + [str(path)] + objects + ['-o', str(exe)], check=True, capture_output=True)
            result = subprocess.run([str(exe), oracle], capture_output=True, text=True, timeout=120)
            assert result.returncode == 1 and 'environment oracle:' in result.stderr, (oracle, result.returncode, result.stderr)
            return oracle
        for oracle in runner.parallel(one, list(enumerate(variants))):
            print('KILLED environment mutant:', oracle, flush=True)
    print(f'all {len(variants)} environment mutants killed')

if __name__ == '__main__':
    main()
