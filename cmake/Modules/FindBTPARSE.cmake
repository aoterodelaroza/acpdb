#[=======================================================================[
Findbtparse
-----------

Try to find the btparse library for parsing bibtex files. This module
defines the following variables:

::

  BTPARSE_FOUND - system has btparse
  BTPARSE_INCLUDE_DIR - the btparse include directory
  BTPARSE_LIBRARIES - The libraries needed to use btparse
  BTPARSE_DEFINITIONS - Compiler switches required for using btparse

#]=======================================================================]

include(FindPackageHandleStandardArgs)

if (DEFINED ENV{BTPARSE_DIR})
  set(QHULL_DIR "$ENV{BTPARSE_DIR}")
endif()

find_path(BTPARSE_INCLUDE_DIR
          NAMES btparse.h
          HINTS "${BTPARSE_DIR}"
	  PATH_SUFFIXES btparse)

find_library(BTPARSE_LIBRARIES 
             NAMES btparse
             HINTS "${BTPARSE_DIR}"
             PATH_SUFFIXES project build bin lib)

find_package_handle_standard_args(BTPARSE
  REQUIRED_VARS BTPARSE_LIBRARIES BTPARSE_INCLUDE_DIR)

mark_as_advanced(BTPARSE_INCLUDE_DIR BTPARSE_LIBRARIES)
if (NOT BTPARSE_FOUND)
  set(BTPARSE_DIR "${BTPARSE_DIR}" CACHE STRING "Directory containing the qhull library.")
endif()
