include(FindPackageHandleStandardArgs)

if (DEFINED ENV{CEREAL_DIR})
  set(QHULL_DIR "$ENV{CEREAL_DIR}")
endif()

find_path(CEREAL_INCLUDE_DIR
          NAMES cereal/cereal.hpp
          HINTS "${CEREAL_DIR}"
	  PATH_SUFFIXES cereal)

find_package_handle_standard_args(CEREAL
  REQUIRED_VARS CEREAL_INCLUDE_DIR)

mark_as_advanced(CEREAL_INCLUDE_DIR)
if (NOT CEREAL_FOUND)
  set(CEREAL_DIR "${CEREAL_DIR}" CACHE STRING "Directory containing the cereal library.")
endif()
