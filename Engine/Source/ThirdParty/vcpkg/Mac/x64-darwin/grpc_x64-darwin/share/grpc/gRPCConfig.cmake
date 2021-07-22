# Module path
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/modules)

# Depend packages
if(NOT ZLIB_FOUND)
  find_package(ZLIB)
endif()
if(NOT Protobuf_FOUND AND NOT PROTOBUF_FOUND)
  find_package(Protobuf CONFIG)
endif()
if(NOT OPENSSL_FOUND)
  find_package(OpenSSL)
endif()
if(NOT c-ares_FOUND)
  find_package(c-ares)
endif()
if(NOT absl_FOUND)
  find_package(absl CONFIG)
endif()
if(NOT re2_FOUND)
  find_package(re2)
endif()
if(NOT upb_FOUND)
  find_package(upb CONFIG REQUIRED)
endif()

# Targets
include(${CMAKE_CURRENT_LIST_DIR}/gRPCTargets.cmake)
