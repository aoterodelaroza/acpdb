set(TESTS
  001_insert_litref         ## insert litref keyword
  002_insert_litref_bibtex  ## insert litref keyword from a bib file
  003_insert_set            ## insert an empty set manually
  004_insert_set_xyz        ## insert a set with xyz files
  005_insert_set_xyz_regexp ## insert a set with xyz files, use regexp
  )

runtests(${TESTS})
