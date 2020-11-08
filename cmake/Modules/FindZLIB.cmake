include(FindPackageHandleStandardArgs)

if (DEFINED ENV{ZLIB_DIR})
  set(QHULL_DIR "$ENV{ZLIB_DIR}")
endif()

find_path(ZLIB_INCLUDE_DIR
          NAMES zlib.h
          HINTS "${ZLIB_DIR}"
	  PATH_SUFFIXES btparse)

find_library(ZLIB_LIBRARIES 
             NAMES z
             HINTS "${ZLIB_DIR}"
             PATH_SUFFIXES lib)

find_package_handle_standard_args(ZLIB
  REQUIRED_VARS ZLIB_LIBRARIES ZLIB_INCLUDE_DIR)

mark_as_advanced(ZLIB_INCLUDE_DIR ZLIB_LIBRARIES)
if (NOT ZLIB_FOUND)
  set(ZLIB_DIR "${ZLIB_DIR}" CACHE STRING "Directory containing the zlib library.")
endif()
