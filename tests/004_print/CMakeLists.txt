set(TESTS
  001_print            ## print summary of the database
  002_print_litref     ## literature references
  003_print_set        ## print set
  004_print_method     ## print method
  005_print_structure  ## print structure
  006_print_property   ## print property
  007_print_evaluation ## print evaluation
  008_print_term       ## print term
  )

runtests(${TESTS})
