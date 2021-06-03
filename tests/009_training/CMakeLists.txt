set(TESTS
  001_define_ts ## define the training set
  002_describe  ## training describe
  003_saveload  ## save, load, delete, print, clear
)

runtests(${TESTS})
