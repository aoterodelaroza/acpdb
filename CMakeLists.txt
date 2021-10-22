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

## policies
cmake_policy(SET CMP0110 NEW) ## allow colons in test names

## default build type, and type options
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)

## project definition
project(acpdb VERSION "${acpdb_MAJOR_VERSION}.${acpdb_MINOR_VERSION}" LANGUAGES CXX)

## global flags for cmake
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Modules)
include(GNUInstallDirs)
include(utils)

## interface options
option(USE_BTPARSE "Use the btparse bibtex file parser for bibtex support." ON)
option(USE_CEREAL "Use the cereal library for training set serialization." ON)

## testing
option(BUILD_TESTING "Enable the regression tests for the acpdb build." OFF)
include(CTest)

## numdiff
find_package(NUMDIFF)

## sqlite3
find_package(SQLite3 REQUIRED)

## btparse
if (USE_BTPARSE)
  find_package(BTPARSE)
endif()

## cereal
if (USE_CEREAL)
  find_package(CEREAL)
endif()

## process subdirectories
add_subdirectory(src)
if (BUILD_TESTING AND NUMDIFF_FOUND)
  add_subdirectory(tests)
endif()

