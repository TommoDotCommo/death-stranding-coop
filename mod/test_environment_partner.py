"""Game-free stand-ins prove the fixed-time CLI speaks the optional wire message."""
from pathlib import Path
import argparse
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent
EXE = ROOT / 'build/coop-partner'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli-only', action='store_true', help='validate refused CLI fixtures without opening sockets')
    options = parser.parse_args()
    for args in (['--role', 'guest', '--environment-time', '12'],
                 ['--role', 'host', '--environment-time', '24'],
                 ['--role', 'host', '--environment-time', 'nan']):
        result = subprocess.run([str(EXE), '--session', '9723401', '--seconds', '0'] + args, capture_output=True, timeout=5)
        assert result.returncode == 2, args
    print('environment stand-in CLI: invalid range/nonfinite and non-host fixtures refused')
    if options.cli_only:
        return
    # One immediate sample can precede clock estimation. The next sample is due at 30 s.
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        with (root / 'host').open('w+') as hostlog, (root / 'guest').open('w+') as guestlog:
            guest = subprocess.Popen([str(EXE), '--slot', '15', '--peer-slot', '14', '--role', 'guest', '--session', '9723401', '--seconds', '33'], stdout=guestlog, stderr=subprocess.STDOUT)
            host = subprocess.Popen([str(EXE), '--slot', '14', '--peer-slot', '15', '--role', 'host', '--session', '9723401', '--seconds', '33', '--environment-time', '21.25'], stdout=hostlog, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 35
                while (host.poll() is None or guest.poll() is None) and time.monotonic() < deadline:
                    time.sleep(.1)
                hostlog.seek(0); guestlog.seek(0)
                sent, received = hostlog.read(), guestlog.read()
                assert host.poll() == guest.poll() == 0, f'stand-in exit/timeout: host={host.poll()}, guest={guest.poll()}\n{sent}\n{received}'
                assert sent.count('ENVIRONMENT sent fixed hour=21.250000') == 2, sent
                assert 'ENVIRONMENT received hour=21.250000 weather=unknown' in received, received
                print('environment stand-in CLI: fixed time crossed the wire, guest received, 30 s send limit passed')
            finally:
                for process in (host, guest):
                    if process.poll() is None:
                        process.terminate()
                    process.wait(timeout=5)

if __name__ == '__main__':
    main()
