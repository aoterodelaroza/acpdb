#! /bin/bash

03_extract_d1e_d2e_gaussian.awk acp/*log
mv empty_d1e.dat acp_d1e.dat
mv empty_d2e.dat acp_d2e.dat
rm -f acp_e.dat
for i in acp/*log ; do
    grep -m 1 -H Done $i | sed -e 's/^.*\///' -e 's/\.log:.*=//' | awk '{print $1, $2}' >> acp_e.dat
done
