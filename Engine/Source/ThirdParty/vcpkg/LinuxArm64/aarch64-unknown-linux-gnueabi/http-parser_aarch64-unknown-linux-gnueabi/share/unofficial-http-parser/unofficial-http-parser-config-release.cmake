#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "unofficial::http_parser::http_parser" for configuration "Release"
set_property(TARGET unofficial::http_parser::http_parser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(unofficial::http_parser::http_parser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libhttp_parser.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS unofficial::http_parser::http_parser )
list(APPEND _IMPORT_CHECK_FILES_FOR_unofficial::http_parser::http_parser "${_IMPORT_PREFIX}/lib/libhttp_parser.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
