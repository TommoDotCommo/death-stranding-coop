"""Run the native game-free fixture in a scoped, fake data directory."""
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='ds-launcher-test-') as directory:
    subprocess.run(['build/core-test', directory], check=True)
