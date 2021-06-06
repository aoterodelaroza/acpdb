set(TESTS
  001_compare           ## compare
  002_compare_set       ## compare, with set option
  003_compare_internal  ## compare to the database method
  )

runtests(${TESTS})
