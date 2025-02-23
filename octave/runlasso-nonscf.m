#! /usr/bin/octave-cli -q

prefix="hfco-all";

## List of 1-norm constraints to use
tlist = linspace(0,1,21);

## run k-fold validation with this number of folds
kfold = 5;

#### Do NOT touch past here ####

## the version of this lasso script
lasso_version = "1.7bin";

## Read the binary file written by acpdb
function [atoms,symbols,lmax,lname,explist,exprnlist,nrows,ncols,x,y,maxcoef,yaddnames,yadd] = readbin(filebin)

  if (!exist(filebin))
    error(sprintf("File not found: %s",filebin))
  endif
  fid = fopen(filebin,"r");

  ## read all the info ##
  ## integers
  natoms = fread(fid,1,"uint64");
  nexp = fread(fid,1,"uint64");
  nrows = fread(fid,1,"uint64");
  ncols = fread(fid,1,"uint64");
  naddsub = fread(fid,1,"uint64");
  nadd = fread(fid,1,"uint64");
  addmaxl = fread(fid,1,"uint64");
  nsub = naddsub - nadd;
  printf("## Reading from binary file:\n");
  printf("# %d atoms\n",natoms);
  printf("# %d exponents\n",nexp);
  printf("# %d exponent r^n\n",nexp);
  printf("# %d rows\n",nrows);
  printf("# %d columns\n",ncols);
  printf("# %d additional method evaluations\n",naddsub);

  ## atom names
  atomstr = char(fread(fid,2*natoms,"char"));
  atoms = cell(natoms,1);
  for i = 1:natoms
    atoms{i} = [atomstr(2*i-1) atomstr(2*i)];
  endfor

  ## atom symbols
  symbolstr = char(fread(fid,5*natoms,"char"));
  symbols = cell(natoms,1);
  for i = 1:natoms
    symbols{i} = [symbolstr(5*i-4) symbolstr(5*i-3) symbolstr(5*i-2) symbolstr(5*i-1) symbolstr(5*i)];
  endfor

  ## additional method names
  yaddnames = cell(nadd,1);
  for i = 1:nadd
    yaddnames{i} = char(fread(fid,addmaxl,"char"))';
  endfor

  ## small data arrays
  lname={"l","s","p","d","f","g","h"};
  lmax = fread(fid,[1 natoms],"unsigned char");
  explist = fread(fid,[1 nexp],"double");
  exprnlist = fread(fid,[1 nexp],"int");

  ## large data arrays
  w = fread(fid,[nrows 1],"double");
  x = fread(fid,[nrows ncols],"double");
  yref = fread(fid,[nrows 1],"double");
  yempty = fread(fid,[nrows 1],"double");
  if (nadd > 0)
    yadd = fread(fid,[nrows nadd],"double");
  else
    yadd = [];
  endif
  if (nsub > 0)
    ynofit = fread(fid,[nrows nsub],"double");
  else
    ynofit = [];
  endif

  ## maxcoef
  nmaxcoef = fread(fid,1,"uint64");
  if (nmaxcoef > 0)
    maxcoef = fread(fid,[nmaxcoef 1],"double");
    if (nmaxcoef != ncols)
      maxcoef = [];
    endif
  else
    maxcoef = [];
  endif
  printf("# %d maximum coefficients\n",length(maxcoef));

  ## apply the weights and transform the matrices for the fit
  wsqrt = sqrt(w);
  x = x .* (wsqrt * ones(1,ncols));
  if (!isempty(yadd))
    yadd = yadd .* wsqrt;
  endif

  y = yref - yempty;
  if (!isempty(ynofit))
    y = y - sum(ynofit,2);
  endif
  y = y .* wsqrt;

  printf("## DONE\n");
  fclose(fid);
endfunction

## Read the binary
[atoms,symbols,lmax,lname,explist,exprnlist,nrows,ncols,x,y,maxcoef,yaddnames,yadd] = readbin("octavedump.dat");

## do we have maxcoef or additional columns?
havemaxcoef = exist("maxcoef","var") && !isempty(maxcoef);
haveaddcols = exist("yadd","var") && !isempty(yadd);
if (havemaxcoef || haveaddcols)
  printf("!! Don't know how to handle maxcoef and/or addcols in non-scf mode. Ignoring. !!\n")
endif

## start the loop
nacp = 0;
printf("| #id |   t      |     norm-1   |    norm-2   |   norm-inf   |   wrms(train)  |  wrms(val) | nterm |\n");
for it = 1:length(tlist)
  t = tlist(it);

  ## calculate the permutation
  idx = randperm(rows(x));

  ## run the k-fold cross-validation
  wrmsval = 0;
  idx = 1:nrows;
  nsplit = round(nrows/kfold);
  for k = 1:kfold
    idxtrain = [idx(1:(k-1)*nsplit), idx(k*nsplit+1:end)];
    idxval = idx((k-1)*nsplit+1:min(k*nsplit,nrows));

    xtrain = x(idxtrain,:);
    ytrain = y(idxtrain,:);

    xval = x(idxval,:);
    yval = y(idxval,:);

    ## run LASSO
    [w,iteration] = LassoActiveSet(xtrain,ytrain,t,'optTol',1e-9,'zeroThreshold',1e-9,'mode',1);
    wrmsval += sum((yval - xval * w).^2);
  endfor
  wrmsval = sqrt(wrmsval);

  ## run the fit with the whole training set
  [w,iteration] = LassoActiveSet(x,y,t,'optTol',1e-9,'zeroThreshold',1e-9,'mode',1);
  wrmstrain = sqrt(sum((y - x * w).^2));

  ## unpack the ACP coefficients from the row vector
  nterms = zeros(length(atoms),length(lname));
  n = 0;
  for iat = 1:length(atoms)
    for il = 1:lmax(iat)
      for iexp = 1:length(explist)
        n++;
        coef = w(n);
        if (abs(coef) > 1e-20)
          nterms(iat,il)++;
          aexp(iat,il,nterms(iat,il)) = explist(iexp);
          aexprn(iat,il,nterms(iat,il)) = exprnlist(iexp);
          acoef(iat,il,nterms(iat,il)) = coef;
        endif
      endfor
    endfor
  endfor

  ## Write the ACP in Gaussian format
  if (exist("prefix","var") && !isempty(prefix))
    nacp++;
    fid = fopen(sprintf("%s-%2.2d.acp",prefix,nacp),"w");

    ## Write the ACP header
    fprintf(fid,"! This ACP was generated with runlasso.m, version %s.\n",lasso_version);
    fprintf(fid,"! Atoms: ");
    for i = 1:length(atoms)
      fprintf(fid,"%2.2s ",atoms{i});
    endfor
    fprintf(fid,"\n");
    fprintf(fid,"! Atomic symbols: ");
    for i = 1:length(atoms)
      fprintf(fid,"%5.5s ",symbols{i});
    endfor
    fprintf(fid,"\n");
    fprintf(fid,"! Lmax:  ");
    for i = 1:length(lmax)
      fprintf(fid,"%2.2s ",lname{lmax(i)});
    endfor
    fprintf(fid,"\n");
    fprintf(fid,"! Exponents: ");
    for i = 1:length(explist)
      fprintf(fid,"%.2f ",explist(i));
    endfor
    fprintf(fid,"\n");
    fprintf(fid,"! Exponent r^n: ");
    for i = 1:length(exprnlist)
      fprintf(fid,"%d ",exprnlist(i));
    endfor
    fprintf(fid,"\n");
    fprintf(fid,"! ACP terms in training set: %d\n",ncols);
    fprintf(fid,"! Data points in training set: %d\n",nrows);
    if (haveaddcols)
      fprintf(fid,"! Additional energy contributions:\n");
      for i = 1:length(yaddnames)
        fprintf(fid,"!  %s: coeff = %.10f\n",yaddnames{i},wadd(i));
      endfor
    endif
    if (havemaxcoef)
      fprintf(fid,"! Maximum coefficients applied\n");
    endif
    fprintf(fid,"! norm-1 = %.4f\n",sum(abs(w)));
    fprintf(fid,"! norm-inf = %.4f\n",max(abs(w)));
    fprintf(fid,"! wrmstrain = %.4f\n",wrmstrain);
    fprintf(fid,"! wrmsval = %.4f\n",wrmsval);

    ## Write the ACP itself
    for i = 1:length(atoms)
      fprintf(fid,"%s 0\n",atoms{i});
      fprintf(fid,"%s %d 0\n",symbols{i},lmax(i)-1);
      for j = 1:lmax(i)
        fprintf(fid,"%s\n",lname{j});
        fprintf(fid,"%d\n",nterms(i,j));
        for k = 1:nterms(i,j)
          fprintf(fid,"%d %.6f %.15f\n",aexprn(i,j,k),aexp(i,j,k),acoef(i,j,k));
        endfor
      endfor
    endfor
    fclose(fid);

    fid = fopen(sprintf("%s-%2.2d.w",prefix,nacp),"w");
    for i = 1:ncols
      fprintf(fid,"%.15e\n",w(i));
    endfor
    fclose(fid);
  endif

  printf("| %2.2d |  %.4f |  %.4f | %.4f | %.4f | %.4f | %.4f | %d |\n",it,t,...
         sum(abs(w)),sqrt(sum(w.^2)),max(abs(w)),...
         wrmstrain,wrmsval,sum(sum(nterms)));
endfor
