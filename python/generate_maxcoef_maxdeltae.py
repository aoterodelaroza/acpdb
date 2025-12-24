#! /usr/bin/python

## This script generates a maxcoef file by setting a maximum change in
## the energy from any given term.

source_empty="empty.dat" ## file with the empty energies
source_terms="terms-merged.dat" ## file with the term energies
contains="eHOMO" ## only structure names that contain this string; None if not applied
calccoef=0.001 ## coefficient with which terms were calculated
ethr=0.2 ## maximum change in the value reported

## training set definition
atoms=["H","C","N","O"]
lmax=["d","f","f","f"]
exp=[0.12, 0.14, 0.16, 0.18, 0.20, 0.22, 0.24, 0.26, 0.28, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90, 1.00, 1.10, 1.20, 1.30, 1.40, 1.50, 1.60, 1.70, 1.80, 1.90, 2.00, 2.50, 3.00,
     0.12, 0.14, 0.16, 0.18, 0.20, 0.22, 0.24, 0.26, 0.28, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90, 1.00, 1.10, 1.20, 1.30, 1.40, 1.50, 1.60, 1.70, 1.80, 1.90, 2.00, 2.50, 3.00,
     0.12, 0.14, 0.16, 0.18, 0.20, 0.22, 0.24, 0.26, 0.28, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90, 1.00, 1.10, 1.20, 1.30, 1.40, 1.50, 1.60, 1.70, 1.80, 1.90, 2.00, 2.50, 3.00]
exprn=[2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

######### end of input block

import numpy as np

ltoint = {"l": 0, "s": 1, "p": 2, "d": 3, "f": 4, "g": 5, "h": 6}
inttol = dict()
for i in ltoint:
    inttol[ltoint[i]] = i

## consistency checks
if len(exp) != len(exprn):
    print(f"error: exp ({len(exp)}) and exprn ({len(exprn)}) have different lengths")
    exit()

## read the empty
empty = {}
names = []
with open(source_empty,"r") as fin:
    for line in fin:
        name, value = tuple(line.split())
        if contains is None or name.find(contains) >= 0:
            empty[name] = float(value)
            names.append(name)

## read the terms
terms_vector = {}
with open(source_terms,"r") as fin:
    for line in fin:
        name, value = tuple(line.split())
        if contains is None or name.find(contains) >= 0:
            if name not in terms_vector:
                terms_vector[name] = []
            terms_vector[name].append(float(value))

## count the number of terms necessary
nterm = 0
for iat,atom in enumerate(atoms):
    for l_ in range(ltoint[lmax[iat].lower()]+1):
        for e_ in exp:
            nterm += 1
for name in terms_vector:
    if (len(terms_vector[name]) != nterm):
        print(f"error: {name} has {len(terms_vector[name])} terms but there should be {nterm}")
        exit()

## calculate the maximum coefficients
nterm = -1
for iat,atom in enumerate(atoms):
    for l_ in range(ltoint[lmax[iat].lower()]+1):
        for iexp,e_ in enumerate(exp):
            nterm = nterm + 1

            cmax = []
            for name in names:
                slope = (terms_vector[name][nterm] - empty[name]) / calccoef
                if slope > 1e-80:
                    cmax_ = ethr / slope
                    cmax.append(cmax_)
            cmax = np.array(cmax)

            print(atom, inttol[l_], e_, exprn[iexp], np.min(cmax))
