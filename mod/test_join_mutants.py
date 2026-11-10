"""Join/provider/native-move mutants must fail the unchanged fake-world oracle, never crash."""
from pathlib import Path
import subprocess
import tempfile
import mutants
ROOT = Path(__file__).resolve().parent
SOURCES = [ROOT / s for s in ['join_test.cpp', 'join.cpp', 'discovery.cpp', '../core/core.cpp']]
FAMILIES = [('JOIN', ['ONCE', 'CONNECTION', 'ROLE', 'THREAD', 'GAMEPLAY', 'DISTANCE', 'SIDE']),
            ('PROGRESS', ['COHERENT', 'BOUNDS', 'KEY', 'UNIQUE', 'IDENTITY', 'ZERO']),
            ('MOVE', ['ONCE', 'EVENT', 'VEHICLE', 'BOUNDS', 'READBACK', 'SIGNATURE', 'IDENTITY'])]
with tempfile.TemporaryDirectory() as directory:
    builds = mutants.MacroBuilds(directory, [ROOT, ROOT.parent / 'core'])
    builds.prepare(SOURCES)
    def one(macro):
        executable = builds.build(macro, SOURCES, Path(directory) / macro)
        result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=120)
        assert result.returncode == 1 and 'join oracle:' in result.stderr, (macro, result.returncode, result.stderr)
        return macro
    for macro in mutants.parallel(one, ['MUTANT_' + family + '_' + name for family, names in FAMILIES for name in names]):
        print('KILLED', macro, flush=True)
