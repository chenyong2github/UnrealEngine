#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "PROJ::proj" for configuration "Debug"
set_property(TARGET PROJ::proj APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(PROJ::proj PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C;CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/lib/proj_d.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS PROJ::proj )
list(APPEND _IMPORT_CHECK_FILES_FOR_PROJ::proj "${_IMPORT_PREFIX}/debug/lib/proj_d.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
