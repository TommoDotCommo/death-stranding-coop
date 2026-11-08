"""Each broken protection must fail its own host oracle, never crash."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parent
source = (ROOT/'discovery.cpp').read_text()
mutants = [
 ('hash', 'expected==PinnedHash&&actual==expected', 'true'),
 ('unmapped', 'if(!a||!m.read(a,b.data(),n))return "null";', 'if(!a)return "null";m.read(a,b.data(),n);'),
 ('torn', '||x!=y', ''),
 ('short', '!read_(a,x.data(),n)||!read_(a,y.data(),n)||x!=y', 'false'),
 ('short', 'unsigned char x[8];if(!read_(a,x,n))return false;', 'unsigned char x[8]{};read_(a,x,n);'),
 ('null', 'p&&p<0x0000800000000000ULL', 'true'),
 ('duplicate', 'require(matches==1,"duplicate signature")', 'require(true,"duplicate signature")'),
 ('zero', 'require(matches>0,"zero signature matches")', 'require(true,"zero signature matches")'),
 ('rip', 'require(region(target,8,true),', 'require(true,'),
 ('rip_unmapped', 'require(read(target,&probe,sizeof probe),"RIP target unreadable")', 'require(true,"RIP target unreadable")'),
 ('root_section', '&&region(base+i->second,8,true)', ''),
 ('section', 'size&&rva<imageSize&&size<=imageSize-rva', 'true'),
 ('overflow', '!a||!n||n>128*1024*1024||n>UINT64_MAX-a', '!a||!n||n>128*1024*1024'),
 ('save_count', 'n>=0&&n<=64', 'true'),
 ('log_cap', 'if(n>limit-bytes)return false;', 'if(false)return false;'),
 ('rate_cap', 'if(started&&(us<last||us-last<50000))return false;', 'if(false)return false;'),
 ('kill', 'require(!error,"cannot inspect discovery stop file");return exists;', 'require(!error,"cannot inspect discovery stop file");return false;'),
 ('chain', 'coherent&=pointer(add(player,0x70))==entity', 'coherent&=true'),
 ('selection', 'mode==1&&kind==2&&other==0&&index>=0&&index<64', 'index>=0&&index<64'),
 ('selection', '0x48+8*uint64_t(index)', '0x48'),
]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    for name, before, after in mutants:
        assert before in source, name
        cpp = tmp/'mutant.cpp'
        cpp.write_text(source.replace(before, after, 1))
        exe = tmp/'mutant'
        subprocess.run(['g++', '-std=c++17', '-O1', '-pthread', '-I', str(ROOT),
                        str(ROOT/'discovery_test.cpp'), str(cpp), '-o', str(exe)],
                       check=True, capture_output=True)
        result = subprocess.run([str(exe), name], text=True, capture_output=True, timeout=10)
        assert result.returncode == 1 and 'oracle' in result.stderr, (name, result.returncode, result.stderr)
        print('KILLED discovery guard', name, result.stderr.strip())
