#! /bin/bash

grep -H '.' run/*d1e | sed -e 's/^.*\///' -e 's/\.d1e://' > calc.d1e
awk 'FNR>2{print FILENAME," 0.0 0.0 0.0"}' xyz/*.xyz | sed 's/^.*\//S22./' | sed 's/\.xyz//' > zero.d1e

