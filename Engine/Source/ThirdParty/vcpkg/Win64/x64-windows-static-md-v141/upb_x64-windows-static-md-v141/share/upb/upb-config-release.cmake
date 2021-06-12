#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "upb::upb" for configuration "Release"
set_property(TARGET upb::upb APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::upb PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/upb.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::upb )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::upb "${_IMPORT_PREFIX}/lib/upb.lib" )

# Import target "upb::fastdecode" for configuration "Release"
set_property(TARGET upb::fastdecode APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::fastdecode PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/upb_fastdecode.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::fastdecode )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::fastdecode "${_IMPORT_PREFIX}/lib/upb_fastdecode.lib" )

# Import target "upb::upb_json" for configuration "Release"
set_property(TARGET upb::upb_json APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::upb_json PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/upb_json.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::upb_json )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::upb_json "${_IMPORT_PREFIX}/lib/upb_json.lib" )

# Import target "upb::upb_pb" for configuration "Release"
set_property(TARGET upb::upb_pb APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::upb_pb PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/upb_pb.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::upb_pb )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::upb_pb "${_IMPORT_PREFIX}/lib/upb_pb.lib" )

# Import target "upb::handlers" for configuration "Release"
set_property(TARGET upb::handlers APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::handlers PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/upb_handlers.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::handlers )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::handlers "${_IMPORT_PREFIX}/lib/upb_handlers.lib" )

# Import target "upb::reflection" for configuration "Release"
set_property(TARGET upb::reflection APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::reflection PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/upb_reflection.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::reflection )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::reflection "${_IMPORT_PREFIX}/lib/upb_reflection.lib" )

# Import target "upb::textformat" for configuration "Release"
set_property(TARGET upb::textformat APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::textformat PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/upb_textformat.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::textformat )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::textformat "${_IMPORT_PREFIX}/lib/upb_textformat.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
