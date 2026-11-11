"""Every protection of the world server has a mutant that its test must fail."""
from pathlib import Path
import subprocess,sys,tempfile
from concurrent.futures import ThreadPoolExecutor
ROOT=Path(__file__).resolve().parent
families=[('world_test.cpp',['world.cpp'],['WORLD_NO_TOKEN','WORLD_OWNER','WORLD_OWN','WORLD_OBJECT_CAP','WORLD_PLAYER_CAP','WORLD_LIKE_RANGE','WORLD_ID_LETTER','WORLD_LIKES_MINE','WORLD_LIKES_SINCE']),
          ('front_test.cpp',['front.cpp','world.cpp'],['FRONT_PREFIX','FRONT_BEARER'])]
with tempfile.TemporaryDirectory() as d:
    def one(job):
        test,sources,name=job;exe=Path(d)/name
        subprocess.run(['g++','-std=c++17','-O1','-pthread','-DMUTANT_'+name,str(ROOT/test),*[str(ROOT/s) for s in sources],'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],text=True,capture_output=True,timeout=120)
        assert r.returncode==1 and 'oracle' in r.stderr,('SURVIVED',name,r.returncode,r.stderr[-300:])
        return name
    with ThreadPoolExecutor(8) as pool:
        for name in pool.map(one,[(t,s,n) for t,s,names in families for n in names]):print('KILLED MUTANT_'+name,flush=True)
