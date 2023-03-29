#! /bin/bash

./03_extract_d1e_d2e_gaussian.awk terms/*log
grep -m 1 -H Done terms/*.log | grep -v UHF | sed -e 's/^.*\///' -e 's/\.log:.*=//' | awk '{print $1, $2}' > empty_e.dat
rm -f terms_e.dat
for i in terms/*.log ; do
    grep -H Done $i | grep -v UHF | tail -n+3 | sed -e 's/^.*\///' -e 's/\.log:.*=//' | awk '{print $1, $2}' >> terms_e.dat
done
