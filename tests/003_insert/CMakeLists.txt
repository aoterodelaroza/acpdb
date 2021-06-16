set(TESTS
  001_insert_litref            ## insert litref keyword
  002_insert_litref_bibtex     ## insert litref keyword from a bib file
  003_insert_set               ## insert an empty set manually
  004_insert_set_xyz           ## insert a set with xyz files
  005_insert_set_xyz_regexp    ## insert a set with xyz files, use regexp
  006_insert_din               ## insert a set with a din file
  007_insert_method            ## insert a method manually
  008_insert_structure_xyz     ## insert a molecular structure from an xyz file
  009_insert_structure_poscar  ## insert a crystal structure from a POSCAR file
  010_insert_structure_file    ## insert a crystal structure from a file, detect format
  011_insert_property          ## insert a property
  012_insert_evaluation        ## insert an evaluation
  013_insert_term              ## insert a term
  014_insert_term_maxcoef      ## update the maxcoef in a term
  015_insert_calc              ## insert a file with calculation results
  016_insert_total_energy      ## insert total energies
  017_insert_dipole            ## insert dipoles
  018_insert_property_fullset  ## insert property, for all structures in a set
  019_insert_set_poscar        ## insert a set with poscar files
  020_insert_set_poscar_regexp ## insert a set with poscar files, regexp
  021_insert_term_calcslope    ## insert a term, calcslope
  022_insert_maxcoef           ## insert maxcoef in bulk
  )

runtests(${TESTS})
