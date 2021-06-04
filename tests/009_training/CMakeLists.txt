set(TESTS
  001_define_ts  ## define the training set
  002_describe   ## training describe
  003_saveload   ## save, load, delete, print, clear
  004_writedin   ## write din files
  005_writetrain ## write input files from the training set
)

runtests(${TESTS})
