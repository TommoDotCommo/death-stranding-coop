"""Run an optional-packet protocol test (environment, animation) and require each guard's named mutant to fail."""
import subprocess
import sys

exe = sys.argv[1]
subprocess.run([exe], check=True)
names = sys.argv[2:] or ['environmentRange', 'environmentRole', 'environmentOrder', 'environmentFresh', 'environmentRate']
for name in names:
    result = subprocess.run([exe, name], capture_output=True, text=True)
    assert result.returncode == 1 and f'FAIL {name}\n' in result.stderr, (name, result.stdout, result.stderr)
    print('KILLED core mutant:', name)
