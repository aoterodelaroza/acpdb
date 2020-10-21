## project and version
cmake_minimum_required(VERSION 3.1 FATAL_ERROR)

## some project variables
set(acpdb_MAJOR_VERSION 1)
set(acpdb_MINOR_VERSION 0)
set(acpdb_URL "https://aoterodelaroza.github.io/acpdb/")
set(acpdb_EMAIL "aoterodelaroza@gmail.com")
set(acpdb_LICENSE "GNU/GPL version 3")
set(acpdb_DESCRIPTION "Database interface for the generation of ACPs")
string(TIMESTAMP DATE "%a %d %b %Y %H:%M:%S")

## project definition
project(acpdb VERSION "${acpdb_MAJOR_VERSION}.${acpdb_MINOR_VERSION}" LANGUAGES CXX)

## global flags for cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Modules)
include(GNUInstallDirs)
include(utils)

## interface options
option(USE_BTPARSE "Use the btparse bibtex file parser for bibtex support." ON)

## sqlite3
find_package(SQLite3 REQUIRED)

## btparse
if (USE_BTPARSE)
  find_package(BTPARSE)
endif()

## process subdirectories
add_subdirectory(src)

