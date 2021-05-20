set(TESTS
  001_insert_litref           ## insert litref keyword
  002_insert_litref_bibtex    ## insert litref keyword from a bib file
  003_insert_set              ## insert an empty set manually
  004_insert_set_xyz          ## insert a set with xyz files
  005_insert_set_xyz_regexp   ## insert a set with xyz files, use regexp
  006_insert_din              ## insert a set with a din file
  007_insert_method           ## insert a method manually
  008_insert_structure_xyz    ## insert a molecular structure from an xyz file
  009_insert_structure_poscar ## insert a crystal structure from a POSCAR file
  )

runtests(${TESTS})
