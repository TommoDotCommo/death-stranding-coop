"""Disabled reader/catalog guards must fail the unchanged synthetic test suite."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
MUTANTS = [
    ('hash', 'find_actor_types.py', 'if hashlib.sha256(data).hexdigest() != PINNED_SHA256:', 'if False:'),
    ('getter', 'find_actor_types.py', r"rb'\x48\x8d\x05[\s\S]{4}\xc3'", r"rb'\x48\x8d\x05[\s\S]{5}'"),
    ('metadata', 'find_actor_types.py', "if image.read(rva, 6) != b'\\xff\\xff\\xff\\xff\\x04\\x00':", 'if False:'),
    ('factory', 'find_actor_types.py', 'if not image.executable(factory):', 'if False:'),
    ('player-class', 'find_actor_types.py', "player_resource='DSPlayerEntityResource' in ancestry", 'player_resource=False'),
    ('spawn-approval', 'find_actor_types.py', 'spawn_approved=False', 'spawn_approved=True'),
    ('page-guard', 'read_actor_resources.py', 'and not protect & 0x100', ''),
    ('double-read', 'read_actor_resources.py', 'or first != second', ''),
    ('byte-cap', 'read_actor_resources.py', 'size - offset, max_bytes - attempted', 'size - offset'),
]
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    names = ['find_actor_types.py', 'read_actor_resources.py', 'probe_puppet.py',
             'find_message_calls.py', 'find_signatures.py', 'test_actor_resources.py',
             'test_find_message_calls.py']
    for label, name, before, after in MUTANTS:
        for source in names:
            (directory/source).write_text((ROOT/source).read_text())
        path = directory/name
        text = path.read_text()
        assert before in text, label
        path.write_text(text.replace(before, after))
        env = dict(os.environ, PYTHONPATH=str(ROOT.parent/'redirect'))
        result = subprocess.run(['python3', '-B', '-m', 'unittest', '-q', 'test_actor_resources'],
            cwd=directory, env=env, text=True, capture_output=True, timeout=20)
        assert result.returncode == 1 and 'FAIL:' in result.stderr, (label, result.returncode, result.stderr)
        print('KILLED actor resource guard', label)
