# Configure PROJ4
#
# Set
#  PROJ4_FOUND = 1
#  PROJ4_INCLUDE_DIRS = /usr/local/include
#  PROJ4_LIBRARIES = proj
#  PROJ4_LIBRARY_DIRS = /usr/local/lib
#  PROJ4_BINARY_DIRS = /usr/local/bin
#  PROJ4_VERSION = 4.9.1 (for example)

message (STATUS "Reading ${CMAKE_CURRENT_LIST_FILE}")
# PROJ4_VERSION is set by version file
message (STATUS
  "PROJ4 configuration, version ${PROJ4_VERSION}")

find_package(unofficial-sqlite3 CONFIG REQUIRED)

# Tell the user project where to find our headers and libraries
get_filename_component (_DIR ${CMAKE_CURRENT_LIST_FILE} PATH)
get_filename_component (_ROOT "${_DIR}/../../../" ABSOLUTE)
set (PROJ4_INCLUDE_DIRS "${_ROOT}/include")
set (PROJ4_LIBRARY_DIRS "${_ROOT}/lib")
set (PROJ4_BINARY_DIRS "${_ROOT}/bin")

set (PROJ4_LIBRARIES proj)
# Read in the exported definition of the library
include ("${_DIR}/proj4-targets.cmake")

unset (_ROOT)
unset (_DIR)

# For backward compatibility with old releases of libgeotiff
set (PROJ4_INCLUDE_DIR ${PROJ4_INCLUDE_DIRS})
