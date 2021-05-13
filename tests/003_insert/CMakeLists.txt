set(TESTS
  001_insert_litref        ## insert litref keyword
  002_insert_litref_bibtex ## insert litref keyword from a bib file
  003_insert_set           ## insert an empty set manually
  )

runtests(${TESTS})
