"""Build/run game-free crash fixtures; parallel mutants use the shared runner."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mod'))
from mutants import parallel

FLAGS = ['x86_64-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-D_WIN32_WINNT=0x0602',
         '-static', '-municode', 'crash_test.cpp', '../redirect/crash_record.cpp']


def build(macro):
    exe = Path('build') / (macro or 'crash-test')
    exe = exe.with_suffix('.exe')
    result = subprocess.run(FLAGS + (['-D' + macro] if macro else []) + ['-o', str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return exe.resolve()


def main():
    base = build(None)
    macros = ['MUTANT_DIAG_CRASH_OFF', 'MUTANT_DIAG_CRASH_CHAIN', 'MUTANT_DIAG_CRASH_FULL', 'MUTANT_DIAG_PLAYER_MARKER']
    built = dict(zip(macros, parallel(build, macros)))
    try:
        result = subprocess.run([str(base), '--interop-check'], capture_output=True, text=True)
    except OSError as error:
        print(f'Windows crash execution skipped: {error}')
        return
    if result.returncode and 'UtilBindVsockAnyPort' in result.stderr and 'socket failed' in result.stderr:
        print('Windows crash execution denied by WSL socket permissions; fixtures cross-built, runtime checks skipped.')
        return
    if result.returncode:
        raise RuntimeError(f'Windows fixture failed to start: {result.returncode}: {result.stderr}')
    subprocess.run([str(base)], check=True, timeout=45)

    def mutant(macro):
        result = subprocess.run([str(built[macro])], capture_output=True, text=True, timeout=45)
        assert result.returncode == 1 and 'oracle:' in result.stderr, (macro, result.returncode, result.stdout, result.stderr)
        return macro

    for macro in parallel(mutant, macros):
        print('KILLED', macro)


if __name__ == '__main__':
    main()
