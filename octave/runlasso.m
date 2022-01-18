#! /usr/bin/octave-cli -q

prefix="lasso";

## List of 1-norm constraints to use
tlist = [5];

## Use the maximum coefficients, if available?
usemaxcoef=1;

## Use the additional energy contributions, if available?
useaddcols=0;

## In the cases with additional energy contribution, use this as convergence threshold.
wrmsconv = 1e-4;

#### Do NOT touch past here ####

## the version of this lasso script
lasso_version = "1.5";

## Read the binary file written by acpdb
function [atoms,lmax,lname,explist,nrows,ncols,x,y,maxcoef,yaddnames,yadd] = readbin(filebin)

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

  ## atom names
  atomstr = char(fread(fid,2*natoms,"char"));
  atoms = cell(natoms,1);
  for i = 1:natoms
    atoms{i} = [atomstr(2*i-1) atomstr(2*i)];
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

  maxcoef = [];

  fclose(fid);
endfunction

## Read the binary
[atoms,lmax,lname,explist,nrows,ncols,x,y,maxcoef,yaddnames,yadd] = readbin("octavedump.dat");

## start the loop
nacp = 0;
printf("| #id |   t      |     norm-1   |    norm-2   |   norm-inf   |   wrms  |  nterm | iter |\n");
for it = 1:length(tlist)
  t = tlist(it);

  ## do we have maxcoef or additional columns?
  havemaxcoef = exist("maxcoef","var") && !isempty(maxcoef) && t > 0 && exist("usemaxcoef","var") && usemaxcoef;
  haveaddcols = exist("yadd","var") && !isempty(yadd) && useaddcols;

  if (havemaxcoef && haveaddcols)
    error("Don't know how to handle maxcoef and addcols just yet.")
  endif

  ## scale the columns using the maximum coefficient information
  if (haveaddcols)
    wadd = zeros(size(yadd,2),1);
    xtilde1 = x;
    xtilde2 = yadd;
    ytilde1 = y;
    wrmsold = 0;
    wrms = Inf;

    ## run lasso
    n = 0;
    while(abs(wrms-wrmsold) > wrmsconv)
      n++;
      printf("it = %d, wrms = %.5f, delta-wrms = %.5f\n",n,wrms,abs(wrms-wrmsold));
      ## run a LASSO fit for the ACP coefficients
      ytilde1 = y - xtilde2 * wadd;
      [w,iteration] = LassoActiveSet(xtilde1,ytilde1,t,'optTol',1e-9,'zeroThreshold',1e-9);

      ## run a normal LS for the additional terms
      ytilde2 = y - xtilde1 * w;
      [wadd sigma r] = ols(ytilde2,xtilde2);

      ## calculate the wrms
      wrmsold = wrms;
      wrms = sqrt(sum((y - xtilde1 * w - xtilde2 * wadd).^2));
    endwhile

  elseif (havemaxcoef)
    factor = ones(columns(x),1);
    xtilde = zeros(size(x));
    maxcoef = maxcoef(:);

    while (1)
      for i = 1:columns(x)
        xtilde(:,i) = x(:,i) * factor(i);
      endfor
      [w,iteration] = LassoActiveSet(xtilde,y,t,'optTol',1e-9,'zeroThreshold',1e-9);
      for i = 1:length(w)
        w(i) = w(i) * factor(i);
      endfor

      wrms = sqrt(sum((y - x * w).^2));
      idx = find(abs(w) > maxcoef);
      printf("norm-1 = %.4f, wrms = %.4f, maxcoef: ok = %d / violations = %d\n",sum(abs(w)),wrms,columns(x)-length(idx),length(idx));
      if (length(idx) == 0)
        break
      endif

      for i = 1:length(w)
        if (abs(w(i)) > maxcoef(i))
          ratio = min(maxcoef(i) / abs(w(i)),0.99);
          factor(i) = factor(i) * ratio^(1.1);
        endif
      endfor
    endwhile
    clear xtilde factor changed
  else
    [w,iteration] = LassoActiveSet(x,y,t,'optTol',1e-9,'zeroThreshold',1e-9);
  endif

  ## Calculate the wrms and print the line
  if (haveaddcols)
    wrms = sqrt(sum((y - x * w - yadd * wadd).^2));
  else
    wrms = sqrt(sum((y - x * w).^2));
  endif

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
    fprintf(fid,"! wrms = %.4f\n",wrms);

    ## Write the ACP itself
    for i = 1:length(atoms)
      fprintf(fid,"%s 0\n",atoms{i});
      fprintf(fid,"%s %d 0\n",atoms{i},lmax(i)-1);
      for j = 1:lmax(i)
        fprintf(fid,"%s\n",lname{j});
        fprintf(fid,"%d\n",nterms(i,j));
        for k = 1:nterms(i,j)
          fprintf(fid,"2 %.6f %.15f\n",aexp(i,j,k),acoef(i,j,k));
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

  printf("| %2.2d |  %.4f |  %.4f | %.4f | %.4f | %.4f | %d | %d |\n",it,t,...
         sum(abs(w)),sqrt(sum(w.^2)),max(abs(w)),...
         wrms,sum(sum(nterms)),iteration);
endfor
