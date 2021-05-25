set(TESTS
  001_write_db          ## write database contents
  002_write_opts        ## write, check some options
  003_write_moltemplate ## write, molecular template
  )

runtests(${TESTS})
