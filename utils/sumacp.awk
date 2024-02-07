#! /usr/bin/awk -f

## Sums all ACPs in the input files. Usage:
##
## sumacp.awk file1.acp file2.acp ...

BEGIN{
    channames["l"] = 1
    channames["s"] = 1
    channames["p"] = 1
    channames["d"] = 1
    channames["f"] = 1
    channames["g"] = 1
    channames["h"] = 1
    chseq[0] = "l"
    chseq[1] = "s"
    chseq[2] = "p"
    chseq[3] = "d"
    chseq[4] = "f"
    chseq[5] = "g"
    chseq[6] = "h"
}

## skip blank lines and comments
/^ *!/{next}
/^ *$/{next}

$2 == 0 && NF == 2 && $0 !~ /^ *!/{
    atom = $1
    getline
    blockname[atom] = $1
    nl = $2+0
    for (i = 0; i <= nl ; i++){
	getline
	cname = $1
	if (!channames[cname]){
	    printf("error: unknown ang. mom. channel symbol: %s\n",$1)
	    skipend = 1
	    exit
	}
	usedl[atom][cname] = 1
	getline
	nterm = $1 + 0
	for (j = 0; j < nterm; j++){
	    getline
	    coef[atom][cname][$2+0] += $3
	}
    }
}

END{
    if (skipend)
	exit
    for (atom in blockname){
	printf("%s 0\n",atom)
	nl = 0
	for (il in usedl[atom])
	    nl++
	printf("%s %d 0\n",blockname[atom],nl-1);
	for (il = 0 ; il < nl; il++){
	    cname = chseq[il]
	    print(cname)

	    ne = asorti(coef[atom][cname], alpha)
	    print(ne)

	    for (ie = 1; ie <= ne; ie++)
		printf("2 %.10f %.14f\n",alpha[ie],coef[atom][cname][alpha[ie]]);
	}
    }
}

