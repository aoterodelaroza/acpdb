#! /bin/bash

for i in *.gjf ; do
    echo $i
    g16 $i
    sed 's/D/E/g' fort.7 > ${i%gjf}d1e
    rm ${i%gjf}chk fort.7
done
