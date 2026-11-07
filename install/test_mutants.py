"""Each mutant is a deliberately broken copy of coop_install.py; the install tests must fail on every one."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).parent
MUTANTS = {
    # uninstall leaves the proxy behind
    'UNINSTALL_LEAVES_FILE': [("            p.unlink()\n            removed.append(entry['path'])",
                               "            if entry['path'] != 'avrt.dll':\n                p.unlink()\n            removed.append(entry['path'])")],
    # uninstall leaves the folders it created
    'UNINSTALL_LEAVES_DIRS': [("            Path(d['path']).rmdir()", "            pass")],
    # install overwrites a file that is already there (a shipped or foreign file)
    'INSTALL_OVERWRITES': [("    if before['state'] == CONFLICT:", "    if False:"),
                           ("if exists_any_case(p) and (root, rel) not in owned:", "if False:")],
    # install does not undo a half-finished copy
    'NO_ROLLBACK': [("        for p in created_files:\n            for q in", "        for p in []:\n            for q in")],
    # status trusts a file because it exists
    'STATUS_IGNORES_CONTENT': [("report['problems'].append(f\"missing: {entry['path']}\")\n            elif sha256_file(p) != entry['sha256']:",
                                "report['problems'].append(f\"missing: {entry['path']}\")\n            elif False:")],
    # install does not check the game build
    'BUILD_NOT_CHECKED': [("    if before['state'] == UNSUPPORTED:", "    if False:")],
    # install runs while the game is running
    'IGNORES_RUNNING_GAME': [("    if running(game_dir, exe):", "    if False:")],
    # uninstall deletes a file the player changed
    'UNINSTALL_DELETES_MODIFIED': [("        elif sha256_file(p) != entry['sha256']:\n            kept.append(entry)", "        elif False:\n            kept.append(entry)")],
}


class Mutants(unittest.TestCase):
    def test_every_mutant_fails_the_install_tests(self):
        for name, edits in MUTANTS.items():
            with self.subTest(mutant=name), tempfile.TemporaryDirectory() as tmp:
                source = (HERE/'coop_install.py').read_text()
                for old, new in edits:
                    self.assertEqual(source.count(old), 1, f'{name}: mutation point not found exactly once: {old!r}')
                    source = source.replace(old, new)
                (Path(tmp)/'coop_install.py').write_text(source)
                for other in ('make_package.py', 'test_coop_install.py'):
                    (Path(tmp)/other).write_text((HERE/other).read_text())
                r = subprocess.run([sys.executable, '-B', '-m', 'unittest', '-q', 'test_coop_install'], cwd=tmp,
                                   capture_output=True, text=True)
                self.assertNotEqual(r.returncode, 0, f'SURVIVED {name}')


if __name__ == '__main__':
    unittest.main()
