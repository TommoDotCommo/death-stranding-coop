"""Run optional environment protocol fuzz and require each guard's named mutant to fail."""
import subprocess
import sys

exe = sys.argv[1]
subprocess.run([exe], check=True)
for name in ('environmentRange', 'environmentRole', 'environmentOrder', 'environmentFresh', 'environmentRate'):
    result = subprocess.run([exe, name], capture_output=True, text=True)
    assert result.returncode == 1 and f'FAIL {name}\n' in result.stderr, (name, result.stdout, result.stderr)
    print('KILLED core mutant:', name)
