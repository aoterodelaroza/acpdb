set(TESTS
  001_acp_load       ## load an ACP
  002_acp_write      ## write an ACP
  003_acp_split      ## split an ACP
  004_write_with_acp ## write with ACP keyword
)

runtests(${TESTS})
