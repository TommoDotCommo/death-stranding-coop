"""The fake-world test of the partner with other players (test_others_partner.py) must fail on a build whose seam
leaks: one where another player's datagram is also handed to the partner's core, and one where `others` starts
sessions without a relay configured. Each mutant library is run against the property that guards it, side by side."""
from pathlib import Path
import subprocess
import sys
import mutants
ROOT=Path(__file__).resolve().parent
CASES=[('fake-others-to-partner.so','together'),('fake-others-no-relay.so','no_relay')]
def one(case):
    library,prop=case
    r=subprocess.run([sys.executable,'-B',str(ROOT/'test_others_partner.py'),str(ROOT/'build'/library),prop],capture_output=True,text=True,timeout=300)
    assert r.returncode==1 and 'others-partner oracle' in r.stderr,(library,r.returncode,r.stderr[-800:])
    return library
for library in mutants.parallel(one,CASES):print('KILLED',library)
