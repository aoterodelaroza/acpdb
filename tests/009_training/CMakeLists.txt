set(TESTS
  001_define_ts        ## define the training set
  002_describe         ## training describe
  003_saveload         ## save, load, delete, print, clear
  004_writedin         ## write din files
  005_writetrain       ## write input files from the training set
  006_comparetrain     ## compare results to the training set
  007_eval             ## training eval empty/acp
  008_write_one_term   ## write term, only one
##  009_write_many_terms ## write term, many
)

runtests(${TESTS})
