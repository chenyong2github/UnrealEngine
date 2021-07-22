#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "upb::upb" for configuration "Release"
set_property(TARGET upb::upb APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::upb PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libupb.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::upb )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::upb "${_IMPORT_PREFIX}/lib/libupb.a" )

# Import target "upb::fastdecode" for configuration "Release"
set_property(TARGET upb::fastdecode APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::fastdecode PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libupb_fastdecode.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::fastdecode )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::fastdecode "${_IMPORT_PREFIX}/lib/libupb_fastdecode.a" )

# Import target "upb::upb_json" for configuration "Release"
set_property(TARGET upb::upb_json APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::upb_json PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libupb_json.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::upb_json )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::upb_json "${_IMPORT_PREFIX}/lib/libupb_json.a" )

# Import target "upb::upb_pb" for configuration "Release"
set_property(TARGET upb::upb_pb APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::upb_pb PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libupb_pb.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::upb_pb )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::upb_pb "${_IMPORT_PREFIX}/lib/libupb_pb.a" )

# Import target "upb::handlers" for configuration "Release"
set_property(TARGET upb::handlers APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::handlers PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libupb_handlers.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::handlers )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::handlers "${_IMPORT_PREFIX}/lib/libupb_handlers.a" )

# Import target "upb::reflection" for configuration "Release"
set_property(TARGET upb::reflection APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::reflection PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libupb_reflection.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::reflection )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::reflection "${_IMPORT_PREFIX}/lib/libupb_reflection.a" )

# Import target "upb::textformat" for configuration "Release"
set_property(TARGET upb::textformat APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(upb::textformat PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libupb_textformat.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS upb::textformat )
list(APPEND _IMPORT_CHECK_FILES_FOR_upb::textformat "${_IMPORT_PREFIX}/lib/libupb_textformat.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
