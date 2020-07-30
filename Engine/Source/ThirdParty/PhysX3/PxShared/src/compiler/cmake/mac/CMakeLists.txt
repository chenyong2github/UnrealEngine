cmake_minimum_required(VERSION 3.3)
PROJECT(PxShared CXX)
include(../common/CMakeLists.txt)


IF(NOT DEFINED TARGET_BUILD_PLATFORM) # Not defined, default to mac
	SET(TARGET_BUILD_PLATFORM "mac")
ENDIF()

SET(PLATFORM_LIST mac)

IF (NOT ${TARGET_BUILD_PLATFORM} IN_LIST PLATFORM_LIST)
	MESSAGE(FATAL_ERROR "Invalid platform:" ${TARGET_BUILD_PLATFORM})
ENDIF()

if(${CMAKE_OSX_ARCHITECTURES} STREQUAL "arm64")
	MESSAGE(STATUS "[Epic] Building PxShared for MacOS arm64")
	# Copied from iOS
	SET(CMAKE_CXX_FLAGS "-std=c++11 -fno-rtti -fno-exceptions -ffast-math -Werror -ferror-limit=0 -Wall -Wextra -fstrict-aliasing -Wstrict-aliasing=2 -Weverything -Wno-documentation-deprecated-sync -Wno-documentation-unknown-command -Wno-float-equal -Wno-padded -Wno-weak-vtables -Wno-cast-align -Wno-conversion -Wno-missing-noreturn -Wno-missing-variable-declarations -Wno-shift-sign-overflow -Wno-covered-switch-default -Wno-exit-time-destructors -Wno-global-constructors -Wno-missing-prototypes -Wno-unreachable-code -Wno-unused-macros -Wno-unused-member-function -Wno-used-but-marked-unused -Wno-weak-template-vtables -Wno-deprecated -Wno-non-virtual-dtor -Wno-invalid-noreturn -Wno-return-type-c-linkage -Wno-reserved-id-macro -Wno-c++98-compat-pedantic -Wno-unused-local-typedef -Wno-old-style-cast -Wno-newline-eof -Wno-unused-private-field -Wno-undefined-reinterpret-cast -Wno-invalid-offsetof -Wno-zero-as-null-pointer-constant -Wno-shadow-field -Wno-unused-template -Wno-c++2a-compat -Wno-unknown-warning-option -Wno-atomic-implicit-seq-cst -Wno-alloca -Wno-anon-enum-enum-conversion -gdwarf-2 -fembed-bitcode")
	SET(CMAKE_CXX_FLAGS "-arch arm64 ${CMAKE_CXX_FLAGS}")
	#SET(CMAKE_CXX_FLAGS "-arch arm64 ${CMAKE_CXX_FLAGS} -DPX_SIMD_DISABLED")
else(${CMAKE_OSX_ARCHITECTURES} STREQUAL "x86_64")
	MESSAGE(STATUS "[Epic] Building PxShared for MacOS x86_64")
	# TODO: Get real flags
	SET(CMAKE_CXX_FLAGS "-msse2 -std=c++11 -fno-rtti -fno-exceptions -ffunction-sections -fdata-sections -Werror -ferror-limit=0 -Wall -Wextra -fstrict-aliasing -Wstrict-aliasing=2 -Weverything -Wno-documentation-deprecated-sync -Wno-documentation-unknown-command -Wno-float-equal -Wno-padded -Wno-weak-vtables -Wno-cast-align -Wno-conversion -Wno-missing-noreturn -Wno-missing-variable-declarations -Wno-shift-sign-overflow -Wno-covered-switch-default -Wno-exit-time-destructors -Wno-global-constructors -Wno-missing-prototypes -Wno-unreachable-code -Wno-unused-macros -Wno-unused-member-function -Wno-used-but-marked-unused -Wno-weak-template-vtables -Wno-deprecated -Wno-non-virtual-dtor -Wno-invalid-noreturn -Wno-return-type-c-linkage -Wno-reserved-id-macro -Wno-c++98-compat-pedantic -Wno-unused-local-typedef -Wno-old-style-cast -Wno-newline-eof -Wno-unused-private-field -Wno-undefined-reinterpret-cast -Wno-invalid-offsetof -Wno-zero-as-null-pointer-constant -Wno-shadow-field -Wno-unused-template -Wno-c++2a-compat -Wno-unknown-warning-option -Wno-atomic-implicit-seq-cst -Wno-alloca -Wno-anon-enum-enum-conversion -gdwarf-2")
	SET(CMAKE_CXX_FLAGS "-arch x86_64 ${CMAKE_CXX_FLAGS}")
else()
	MESSAGE( FATAL_ERROR "Unrecognized architecture ${CMAKE_OSX_ARCHITECTURES}" )
endif()

SET(CMAKE_SHARED_LINKER_FLAGS "")

SET(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
SET(CMAKE_CXX_FLAGS_CHECKED "-O3 -g")
SET(CMAKE_CXX_FLAGS_PROFILE "-O3 -g")
SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -g")

# Build libs compatible with OS X 10.9
SET(CMAKE_OSX_DEPLOYMENT_TARGET "10.9")

#set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")

SET(PXSHARED_MAC_COMPILE_DEFS _LIB;DISABLE_CUDA_PHYSX;DISABLE_COMPUTE_PHYSX)
SET(PXSHARED_MAC_DEBUG_COMPILE_DEFS _DEBUG;PX_DEBUG=1;PX_CHECKED=1)
SET(PXSHARED_MAC_CHECKED_COMPILE_DEFS NDEBUG;PX_CHECKED=1)
SET(PXSHARED_MAC_PROFILE_COMPILE_DEFS NDEBUG;PX_PROFILE=1)
SET(PXSHARED_MAC_RELEASE_COMPILE_DEFS NDEBUG)

# NOTE: PxCudaContextManager excluded on this platform

# Include project cmake files here
INCLUDE(PxFoundation.cmake)
INCLUDE(PsFastXml.cmake)
INCLUDE(PxPvdSDK.cmake)
INCLUDE(PxTask.cmake)
