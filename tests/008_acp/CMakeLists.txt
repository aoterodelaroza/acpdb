set(TESTS
  001_acp_load  ## load an ACP
  002_acp_write ## write an ACP
  003_acp_split ## split an ACP
)

runtests(${TESTS})
