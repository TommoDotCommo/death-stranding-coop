"""Join/provider mutants must fail the unchanged fake-world oracle, never crash."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / 'mutant'
    for family, names in [('JOIN', ['ONCE', 'CONNECTION', 'ROLE', 'THREAD', 'GAMEPLAY', 'DISTANCE', 'SIDE']),
                          ('PROGRESS', ['COHERENT', 'BOUNDS', 'KEY', 'UNIQUE', 'IDENTITY', 'ZERO'])]:
        for name in names:
            macro = 'MUTANT_' + family + '_' + name
            subprocess.run(['g++', '-std=c++17', '-O1', '-pthread', '-D' + macro,
                            *(str(ROOT / s) for s in ['join_test.cpp', 'join.cpp', 'discovery.cpp', '../core/core.cpp']),
                            '-o', str(executable)], check=True, capture_output=True)
            result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=20)
            assert result.returncode == 1 and 'join oracle:' in result.stderr, (macro, result.returncode, result.stderr)
            print('KILLED', macro, flush=True)
