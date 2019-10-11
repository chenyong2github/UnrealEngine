cmake_minimum_required(VERSION 3.3)
#set(CMAKE_VERBOSE_MAKEFILE ON)
include(../common/CMakeLists.txt)

STRING(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_LOWERCASE)

IF(NOT DEFINED TARGET_BUILD_PLATFORM) # Not defined, default to HTML5
	SET(TARGET_BUILD_PLATFORM "html5")
ENDIF()

SET(PLATFORM_LIST html5)

IF (NOT ${TARGET_BUILD_PLATFORM} IN_LIST PLATFORM_LIST)
	MESSAGE(FATAL_ERROR "Invalid platform:" ${TARGET_BUILD_PLATFORM})
ENDIF()

# -msse2
SET(CMAKE_CXX_FLAGS "-std=c++11 -fdiagnostics-show-option -Wall -Wextra -Werror -fno-rtti -fno-exceptions -fno-strict-aliasing -ffunction-sections -fdata-sections -Wno-unknown-pragmas -Wno-dynamic-class-memaccess -Wno-reorder -Wno-logical-not-parentheses -Wno-unused-parameter -Wno-unused-variable -Wno-unused-const-variable -Werror -Wno-unused-function -Wno-unknown-pragmas -Wno-overloaded-virtual  -Wno-non-virtual-dtor -Wno-invalid-noreturn -Wno-return-type-c-linkage -Wno-reserved-id-macro -Wno-c++98-compat-pedantic -Wno-unused-local-typedef -Wno-old-style-cast -Wno-newline-eof -Wno-unused-private-field -Wno-undefined-reinterpret-cast -Wno-invalid-offsetof -Wno-local-type-template-args -Wno-undefined-func-template -Wno-expansion-to-defined -Wno-zero-as-null-pointer-constant -Wno-unused-template -Wno-c++2a-compat -Wno-unused-private-field -Wno-tautological-type-limit-compare")

SET(CMAKE_STATIC_LIBRARY_PREFIX "")

SET(APEX_HTML5_COMPILE_DEFS _CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_DEPRECATE)
SET(APEX_HTML5_DEBUG_COMPILE_DEFS _DEBUG;PX_DEBUG;PX_CHECKED;PHYSX_PROFILE_SDK;PX_SUPPORT_VISUAL_DEBUGGER;PX_PROFILE)
SET(APEX_HTML5_CHECKED_COMPILE_DEFS NDEBUG;PX_CHECKED;PHYSX_PROFILE_SDK;PX_SUPPORT_VISUAL_DEBUGGER;PX_ENABLE_CHECKED_ASSERTS)
SET(APEX_HTML5_PROFILE_COMPILE_DEFS NDEBUG;PX_PROFILE;PHYSX_PROFILE_SDK;PX_SUPPORT_VISUAL_DEBUGGER)
SET(APEX_HTML5_RELEASE_COMPILE_DEFS NDEBUG;APEX_SHIPPING)

SET(CMAKE_DEBUG_POSTFIX $ENV{LIB_SUFFIX})
SET(CMAKE_PROFILE_POSTFIX $ENV{LIB_SUFFIX})
SET(CMAKE_CHECKED_POSTFIX $ENV{LIB_SUFFIX})
SET(CMAKE_RELEASE_POSTFIX $ENV{LIB_SUFFIX})

ADD_DEFINITIONS(-DPX_SIMD_DISABLED)

# Add PhysX as a dependency so that we can use project references
ADD_SUBDIRECTORY(${PHYSX_ROOT_DIR}/Source/compiler/cmake/html5 "${CMAKE_CURRENT_BINARY_DIR}/physx_bin")

#Include projects
INCLUDE(ApexCommon.cmake)
#INCLUDE(ApexClothing.cmake) # REQUIRES SIMD !!! -- reenable when this is finally in for WASM
INCLUDE(ApexDestructible.cmake)
INCLUDE(ApexLegacy.cmake)
INCLUDE(ApexLoader.cmake)
INCLUDE(ApexFramework.cmake)
INCLUDE(ApexShared.cmake)
INCLUDE(NvParameterized.cmake)
INCLUDE(RenderDebug.cmake)

