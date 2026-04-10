# This script shows how to generate a merged ACP from two ACP files.

import sys
sys.path.append('/home/alberto/git/acpdb/python')

from ACP import *
from glob import glob

acpbase = ACP.from_file("nonlocal2-cycle0-07.acp")

for acp in glob("*cycle1*acp"):
    acpnew = acpbase + ACP.from_file(acp)
    with open("ALL-" + acp,"w") as fout:
        print(acpnew,file=fout)

