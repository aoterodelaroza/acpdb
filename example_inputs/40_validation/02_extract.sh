#! /bin/bash

grep -H Done run/*log | cut -f 2 -d / | sed 's/\.log:/ /' | awk '{print $1,$6}' > run.dat
