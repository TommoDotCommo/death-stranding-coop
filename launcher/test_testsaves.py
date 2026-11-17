"""Test saves end to end, game-free: tools/make_test_saves.py builds a pack from synthetic saves (saves_vectors.h) and
the launcher's reader (testsaves-test, testsaves.h) lists and uses it; the oracle also runs its own fixtures."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent/'tools'))
import make_test_saves  # noqa: E402
import test_make_test_saves as fixtures  # noqa: E402

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='ds-test-saves-') as directory:
    work = Path(directory)
    pack = work/'deathstranding-coop-test-saves-0.1.0-alpha.1.zip'
    make_test_saves.build(fixtures.source_tree(work/'src'), pack, '0.1.0-alpha.1')
    (work/'oracle').mkdir()
    result = subprocess.run([str(binary), str(work/'oracle'), str(pack)], text=True, capture_output=True)
    sys.stdout.write(result.stdout)
    if result.returncode:
        raise SystemExit(f'test saves oracle failed: {result.returncode}\n{result.stderr}')
