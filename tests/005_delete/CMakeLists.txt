set(TESTS
  001_delete_litref     ## delete litref
  002_delete_set        ## delete set
  003_delete_method     ## delete method
  004_delete_structure  ## delete structure
  005_delete_property   ## delete property
  006_delete_evaluation ## delete evaluation
  007_delete_term       ## delete term
  )

runtests(${TESTS})
