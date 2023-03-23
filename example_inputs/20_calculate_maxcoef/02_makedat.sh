#! /bin/bash

grep -H Done run/*.log | sed -e 's!^.*/!!' -e 's/\.log.*=//' | awk '{print $1, $2}' > scf.dat
