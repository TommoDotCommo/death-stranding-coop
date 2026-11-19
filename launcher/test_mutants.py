"""Every mutant must reach an assertion in the unchanged game-free oracle."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
KEYS = ['../world/identity.cpp', '../world/collection.cpp', '../vendor/monocypher/monocypher.c', '../vendor/monocypher/monocypher-ed25519.c']

sys.path.insert(0, str(ROOT.parent/'mod'))
import mutants  # the shared side-by-side runner

def run(args, **kwargs):
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, **kwargs)

with tempfile.TemporaryDirectory(prefix='ds-launcher-mutants-') as directory:
    work = Path(directory)
    def one(name):
        exe = work/name
        result = run(['g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-DMUTANT_'+name,
                      'core_test.cpp', 'core.cpp', 'strand.cpp', 'owner.cpp', '../mod/config.cpp', '../redirect/path_map.cpp', *KEYS, '../world/signatures.cpp', '-o', str(exe)])
        if result.returncode:
            raise SystemExit(result.stderr)
        with tempfile.TemporaryDirectory(dir=work) as fixture:
            result = run([str(exe), fixture])
        if result.returncode != 1 or 'launcher oracle:' not in result.stderr:
            raise SystemExit(f'SURVIVED or crashed {name}: {result.returncode}\n{result.stdout}\n{result.stderr}')
        return name
    for name in mutants.parallel(one, ['CONFIG_ROLE', 'CONFIG_PLAY', 'WORLD_GUEST_ADDRESS', 'WORLD_ONE_ACCOUNT', 'WORLD_BIND', 'WORLD_SOLO', 'WORLD_GUEST_DECIDES', 'LOBBY_WORLD_BIT', 'LOBBY_SECRET', 'GROUP_PEER', 'GROUP_PEERS', 'GROUP_NO_RELAY', 'GROUP_KEY', 'GROUP_ID', 'GROUP_ROUTE', 'GROUP_INVITE_KIND', 'GROUP_SAME_PC', 'DIAG_GROUP_KEY',
                                      'GROUP_POLICY', 'STRAND_ACCOUNT', 'STRAND_PARSE', 'STRAND_WRONG_KEY', 'STRAND_OLD_SERVER', 'STRAND_SEND_FAIL', 'STRAND_FRIENDS_READ', 'STRAND_DEFAULTS', 'STRAND_PER_SERVER', 'STRAND_SETTINGS_KEEP', 'BACKUP_LINKS', 'OUTPUT_LINKS',
                                      'MISSING_CONFIG_CLOSES', 'TICKET_TARGET', 'TICKET_EXPIRY', 'TICKET_NETWORK',
                                      'KEYS_KEEP', 'STRAND_SERVER_WORLD', 'GROUP_NO_SERVER_WORLD', 'STRAND_SIGN', 'STRAND_NO_PASSES', 'GROUP_TICKET', 'GROUP_RELAY_TOKEN', 'DIAG_TICKET', 'GROUP_NOTICE_TOKEN', 'GROUP_NO_MAP', 'GROUP_NO_CARGO', 'GROUP_PAIRS', 'GROUP_NO_GEAR', 'GROUP_NO_HELP', 'GROUP_NO_CHAT', 'GROUP_BUILD_RECEIVED', 'GROUP_BUILD_SETTING', 'GROUP_OFFER_STRAND', 'GROUP_TEST_OWN', 'STRAND_TEST_MODE', 'STRAND_NOTICE_FATAL', 'DIAG_NOTICE_TOKEN', 'STRAND_FREE_BUILD',
                                      'OWNER_UNSIGNED', 'OWNER_CLOCK', 'OWNER_REJECT_ALL', 'OWNER_REFUSAL', 'OWNER_REMOVE_TEXT', 'OWNER_HOST_ONLY', 'OWNER_PARSE',
                                      'BACKUP_RUNNING', 'NESTED_LAB', 'NESTED_RELEASE',
                                      'DIAG_SECRET', 'DIAG_STEAM_ACCOUNT', 'DIAG_ADDRESS', 'DIAG_USER', 'DIAG_BOUND', 'FIREWALL_BLOCK_WINS', 'FIREWALL_ANY_PROGRAM',
                                      'LAUNCHER_COLLECTION_UNCHECKED']):
        print('killed', name)
    # The socket test (test_group.py) must fail too when the games' key is not the server's, the server has no relay,
    # a joiner on the host's PC signs in or acts as the host's account, the host's rules do not reach a new world, or
    # a join goes on (or stops) when it should not. One at a time: each socket test takes the first free server port.
    def group(name):
        result = run([sys.executable, '-B', 'test_group.py', str(work/name), '../world/build/coop-world', '../mod/build/coop-swarm'])
        if result.returncode != 1 or 'group server oracle:' not in result.stderr:
            raise SystemExit(f'SURVIVED or crashed {name} (socket test): {result.returncode}\n{result.stdout}\n{result.stderr}')
        return name
    for name in map(group, ['GROUP_KEY', 'GROUP_NO_RELAY', 'GROUP_SAME_PC', 'GROUP_POLICY', 'STRAND_ACCOUNT', 'STRAND_WRONG_KEY', 'STRAND_OLD_SERVER', 'STRAND_SEND_FAIL']):
        print('killed', name, '(socket test)')
    # Identity keys on the real server (test_identity.py): the game's ticket and relay token, the signed calls, the kept key.
    def keyed(name):
        result = run([sys.executable, '-B', 'test_identity.py', str(work/name), '../world/build/coop-world'])
        if result.returncode != 1 or 'identity oracle:' not in result.stderr:
            raise SystemExit(f'SURVIVED or crashed {name} (identity socket test): {result.returncode}\n{result.stdout}\n{result.stderr}')
        return name
    for name in map(keyed, ['GROUP_TICKET', 'GROUP_RELAY_TOKEN', 'STRAND_SIGN', 'KEYS_KEEP', 'GROUP_BUILD_RECEIVED', 'GROUP_OFFER_STRAND']):
        print('killed', name, '(identity socket test)')
    # The owner's calls on the real server (test_owner.py): signed with the owner key, one player's objects only, refusals.
    def owned(name):
        result = run([sys.executable, '-B', 'test_owner.py', str(work/name), '../world/build/coop-world'])
        if result.returncode != 1 or 'owner oracle:' not in result.stderr:
            raise SystemExit(f'SURVIVED or crashed {name} (owner socket test): {result.returncode}\n{result.stdout}\n{result.stderr}')
        return name
    for name in map(owned, ['OWNER_UNSIGNED', 'OWNER_REJECT_ALL', 'OWNER_REFUSAL']):
        print('killed', name, '(owner socket test)')
    # Collections on the real server (test_collections.py): a changed string never leaves the launcher.
    def collected(name):
        result = run([sys.executable, '-B', 'test_collections.py', str(work/name), '../world/build/coop-world'])
        if result.returncode != 1 or 'launcher collections oracle:' not in result.stderr:
            raise SystemExit(f'SURVIVED or crashed {name} (collections socket test): {result.returncode}\n{result.stdout}\n{result.stderr}')
        return name
    for name in map(collected, ['LAUNCHER_COLLECTION_UNCHECKED']):
        print('killed', name, '(collections socket test)')
    # The install core (install.h, shared with Setup): every protection of install_test.cpp has its mutant.
    def installer(name):
        exe = work/('install-'+name)
        result = run(['g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-DMUTANT_INSTALL_'+name, 'install_test.cpp', 'install.cpp', '-o', str(exe)])
        if result.returncode:
            raise SystemExit(result.stderr)
        with tempfile.TemporaryDirectory(dir=work) as fixture:
            result = run([str(exe), fixture])
        if result.returncode != 1 or 'install oracle:' not in result.stderr:
            raise SystemExit(f'SURVIVED or crashed install {name}: {result.returncode}\n{result.stdout}\n{result.stderr}')
        return name
    for name in mutants.parallel(installer, ['OVERWRITE_FOREIGN', 'TRUSTS_EXISTENCE', 'RECEIPT_IGNORES_PENDING', 'NO_BUILD_CHECK', 'IGNORES_RUNNING',
                                             'NO_PACKAGE_CHECK', 'STALE_KEPT', 'NO_ROLLBACK', 'UNINSTALL_DELETES_CHANGED', 'UNINSTALL_DELETES_FOREIGN',
                                             'MANIFEST_ESCAPE', 'FOLLOWS_LINKS', 'VDF_ESCAPES', 'VDF_FIRST_ONLY', 'ACF_IGNORED', 'GAME_DUPLICATES',
                                             'UNINSTALL_IGNORES_FAILURE']):
        print('killed install', name)
    def save(name):
        exe = work/name
        result = run(['g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-DMUTANT_'+name,
                      'saves_test.cpp', 'saves.cpp', 'core.cpp', 'strand.cpp', '../mod/config.cpp', '../redirect/path_map.cpp', *KEYS, '-o', str(exe)])
        if result.returncode:
            raise SystemExit(result.stderr)
        with tempfile.TemporaryDirectory(dir=work) as fixture:
            result = run([str(exe), fixture])
        if result.returncode != 1 or 'launcher oracle:' not in result.stderr:
            raise SystemExit(f'SURVIVED or crashed {name}: {result.returncode}\n{result.stdout}\n{result.stderr}')
        return name
    for name in mutants.parallel(save, ['SAVE_LINKS', 'SAVE_SECRET', 'SAVE_RESIGN', 'SAVE_KEEP', 'SAVE_CHECKSUM', 'SAVE_NAMED_OTHER', 'SAVE_PADDING']):
        print('killed', name)
    # Test saves (testsaves.h): the pack against its manifest, checkpoints only, re-signed for the player, the co-op
    # saves moved aside first, never the ordinary saves, never while the game runs.
    def testsave(name):
        exe = work/name
        result = run(['g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-DMUTANT_'+name,
                      'testsaves_test.cpp', 'testsaves.cpp', 'saves.cpp', 'core.cpp', 'strand.cpp', '../mod/config.cpp', '../redirect/path_map.cpp', *KEYS, '-o', str(exe)])
        if result.returncode:
            raise SystemExit(result.stderr)
        with tempfile.TemporaryDirectory(dir=work) as fixture:
            result = run([str(exe), fixture])
        if result.returncode != 1 or 'launcher oracle:' not in result.stderr:
            raise SystemExit(f'SURVIVED or crashed {name}: {result.returncode}\n{result.stdout}\n{result.stderr}')
        return name
    for name in mutants.parallel(testsave, ['TESTSAVES_HASH', 'TESTSAVES_PATH', 'TESTSAVES_EXTRA', 'TESTSAVES_RESIGN', 'TESTSAVES_BACKUP', 'TESTSAVES_ORDINARY', 'TESTSAVES_RUNNING']):
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
