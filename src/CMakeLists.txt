## sources
set(SOURCES acpdb.cpp sqldb.cpp)

## C++ standards
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## executable
add_executable(acpdb ${SOURCES})

## sqlite3
target_include_directories(acpdb PRIVATE ${SQLite3_INCLUDE_DIRS})
target_link_directories(acpdb PRIVATE ${SQLite3_LIBRARY_DIR})
target_link_libraries(acpdb ${SQLite3_LIBRARIES})

## install the binary
install(TARGETS acpdb RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
