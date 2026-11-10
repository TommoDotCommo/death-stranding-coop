"""Every mutant must reach an assertion in the unchanged game-free oracle."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent

sys.path.insert(0, str(ROOT.parent/'mod'))
import mutants  # the shared side-by-side runner

def run(args, **kwargs):
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, **kwargs)

with tempfile.TemporaryDirectory(prefix='ds-launcher-mutants-') as directory:
    work = Path(directory)
    def one(name):
        exe = work/name
        result = run(['g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-DMUTANT_'+name,
                      'core_test.cpp', 'core.cpp', '../mod/config.cpp', '../redirect/path_map.cpp', '-o', str(exe)])
        if result.returncode:
            raise SystemExit(result.stderr)
        with tempfile.TemporaryDirectory(dir=work) as fixture:
            result = run([str(exe), fixture])
        if result.returncode != 1 or 'launcher oracle:' not in result.stderr:
            raise SystemExit(f'SURVIVED or crashed {name}: {result.returncode}\n{result.stdout}\n{result.stderr}')
        return name
    for name in mutants.parallel(one, ['CONFIG_ROLE', 'CONFIG_PLAY', 'LOBBY_SECRET', 'BACKUP_LINKS', 'OUTPUT_LINKS',
                                      'MISSING_CONFIG_CLOSES', 'TICKET_TARGET', 'TICKET_EXPIRY', 'TICKET_NETWORK',
                                      'BACKUP_RUNNING', 'NESTED_LAB', 'NESTED_RELEASE',
                                      'DIAG_SECRET', 'DIAG_ADDRESS', 'DIAG_USER', 'DIAG_BOUND']):
        print('killed', name)
    changes = [
        ('invite-crc', 'coop_invite.h', ' || ci_crc8(b, n - 1) != b[n - 1]', '', 'coop_invite_test.c'),
        ('invite-prefix', 'coop_invite.h', 'if (which != mine)', 'if (0 && which != mine)', 'coop_invite_test.c'),
        ('discovery-nonce', 'coop_discover.h', 'cd_le32(b + 4) != nonce || ', '0 * nonce || ', 'coop_discover_test.c'),
        ('discovery-game', 'coop_discover.h', 'memcmp(b + 8, game_id, 4)', '0 * strlen(game_id)', 'coop_discover_test.c'),
        ('discovery-broadcast', 'coop_discover.h', 'a[i].ip | ~mask', '0xffffffffu', 'coop_discover_test.c'),
    ]
    for name, header, before, after, test in changes:
        mutant = work/name
        shutil.copytree(ROOT/'vendor', mutant)
        path = mutant/header
        source = path.read_text()
        if source.count(before) != 1:
            raise SystemExit(f'mutant drift {name}: {source.count(before)}')
        path.write_text(source.replace(before, after))
        exe = mutant/'test'
        result = run(['cc', '-std=c99', '-O1', '-I'+str(mutant), str(mutant/'tests'/test), '-o', str(exe)])
        if result.returncode:
            raise SystemExit(result.stderr)
        result = run([str(exe), '1000'])
        if result.returncode != 1 or 'FAIL' not in result.stdout:
            raise SystemExit(f'SURVIVED or crashed {name}: {result.returncode}\n{result.stdout}\n{result.stderr}')
        print('killed', name)
