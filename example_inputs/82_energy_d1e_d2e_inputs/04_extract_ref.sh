#! /bin/bash

03_extract_d1e_d2e_gaussian.awk ref/*log
mv empty_d1e.dat ref_d1e.dat
mv empty_d2e.dat ref_d2e.dat
rm -f ref_e.dat
for i in ref/*log ; do
    grep -m 1 -H Done $i | sed -e 's/^.*\///' -e 's/\.log:.*=//' | awk '{print $1, $2}' >> ref_e.dat
done
