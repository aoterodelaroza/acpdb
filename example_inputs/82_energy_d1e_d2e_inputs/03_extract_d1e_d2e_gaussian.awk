#! /usr/bin/awk -f

## Run as: extract_d1e_gaussian.awk terms/*.log
## Writes: empty.dat and terms.dat

FNR==1{
    nblock = 0
    nat = 0
}
/^ *NAtoms=/ && !nat{
    nat=$2
}
/^ *AtFile/,/Initial command/{
    if ($0 ~ /^ *AtFile/) {
	nblock++
	nline = 0
    }
    if ($0 ~ /^ *AtFile/ || $0 ~ /Initial command/) next
    nline++
    numbers = $0
    gsub("D","E",numbers)
    prefix = FILENAME
    gsub(/^.*\//,"",prefix)
    gsub(/\.log.*$/,"",prefix)
    if (nblock == 1){
	if (nline <= nat)
	    print prefix, numbers > "empty_d1e.dat"
	else
	    print prefix, numbers > "empty_d2e.dat"
    } else {
	if (nline <= nat)
	    print prefix, numbers > "terms_d1e.dat"
	else
	    print prefix, numbers > "terms_d2e.dat"
    }
}
