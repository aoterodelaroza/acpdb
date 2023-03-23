#! /bin/bash

grep -H -m 1 Done run/*.log | sed -e 's!^.*/!!' -e 's/\.log.*=//' | awk '{print $1, $2}' > empty.dat
awk 'FNR==1{x=0}/Done/ && !x{x=1;next}/Done/{print FILENAME, $5}' run/*.log | sed -e 's!^.*/!!' -e 's/\.log//' > terms.dat
