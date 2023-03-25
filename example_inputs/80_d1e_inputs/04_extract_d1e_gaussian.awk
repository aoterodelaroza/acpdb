#! /usr/bin/awk -f

## Run as: extract_d1e_gaussian.awk terms/*.log
## Writes: empty.dat and terms.dat

FNR==1{
    nblock = 0
}
/^ *AtFile/,/Initial command/{
    if ($0 ~ /^ *AtFile/) nblock++
    if ($0 ~ /^ *AtFile/ || $0 ~ /Initial command/) next
    numbers = $0
    gsub("D","E",numbers)
    prefix = FILENAME
    gsub(/^.*\//,"",prefix)
    gsub(/\.log.*$/,"",prefix)
    if (nblock == 1)
    	print prefix, numbers > "empty.dat"
    else
    	print prefix, numbers > "terms.dat"
}
