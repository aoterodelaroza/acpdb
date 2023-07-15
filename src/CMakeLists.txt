## sources
set(SOURCES acp.cpp acpdb.cpp globals.cpp outputeval.cpp parseutils.cpp statement.cpp
            sqldb.cpp strtemplate.cpp structure.cpp trainset.cpp)

## C++ standards
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## executable
add_executable(acpdb ${SOURCES})

## lasso library
if (NOT "${LASSO_LIB}" STREQUAL "")
  add_library(lassolib SHARED IMPORTED)
  set_target_properties(lassolib PROPERTIES IMPORTED_LOCATION ${LASSO_LIB}/liblasso.so)
  target_include_directories(acpdb PRIVATE ${LASSO_LIB})
  target_compile_definitions(acpdb PRIVATE LASSO_LIB)
  target_link_libraries(acpdb PRIVATE lassolib)
endif()

## sqlite3
target_include_directories(acpdb PRIVATE ${SQLite3_INCLUDE_DIRS})
target_link_directories(acpdb PRIVATE ${SQLite3_LIBRARY_DIR})
target_link_libraries(acpdb PRIVATE ${SQLite3_LIBRARIES})

## btparse
if (BTPARSE_FOUND)
  target_include_directories(acpdb PRIVATE ${BTPARSE_INCLUDE_DIR})
  target_link_libraries(acpdb PRIVATE ${BTPARSE_LIBRARIES})
  target_compile_definitions(acpdb PRIVATE ${BTPARSE_DEFINITIONS})
endif()

## cereal
if (CEREAL_FOUND)
  target_include_directories(acpdb PRIVATE ${CEREAL_INCLUDE_DIR})
  target_compile_definitions(acpdb PRIVATE ${CEREAL_DEFINITIONS})
endif()

## configuration file
configure_file(config.h.in config.h)

## install the binary
install(TARGETS acpdb RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
