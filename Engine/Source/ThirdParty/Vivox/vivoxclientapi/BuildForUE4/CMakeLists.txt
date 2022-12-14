cmake_minimum_required(VERSION 3.3)
project(vivoxclientapi)

set (CMAKE_CXX_STANDARD 11)

# Put our build output one level up so we can easily delete the temporary files and only check-in the final libs
if (NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../")
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../")
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../")
	set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../")
	set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../")
endif()

option(USE_LOGIN_SESSION_AUDIO_SETTINGS "Use per-login session audio settings. Requires sdk support" OFF)
if (USE_LOGIN_SESSION_AUDIO_SETTINGS)
	add_definitions(-DUSE_LOGIN_SESSION_AUDIO_SETTINGS=1)
endif (USE_LOGIN_SESSION_AUDIO_SETTINGS)

option(VALIDATE_AUDIO_DEVICE_SELECTION "Validate that audio devices exist before setting them" ON)
if (VALIDATE_AUDIO_DEVICE_SELECTION)
	add_definitions(-DVALIDATE_AUDIO_DEVICE_SELECTION=1)
endif (VALIDATE_AUDIO_DEVICE_SELECTION)

set(VIVOXSDK_PATH "" CACHE STRING "Path to Vivox sdk headers")

add_definitions(-DUSE_ACCESS_TOKENS)
include_directories(../vivoxclientapi/include ${VIVOXSDK_PATH})

set(SOURCES ../vivoxclientapi/src/accountname.cpp
			../vivoxclientapi/src/allocator_utils.cpp
			../vivoxclientapi/src/audiodeviceid.cpp
			../vivoxclientapi/src/clientconnection.cpp
			../vivoxclientapi/src/debugclientapieventhandler.cpp
			../vivoxclientapi/src/uri.cpp
			../vivoxclientapi/src/util.cpp
			../vivoxclientapi/src/vivoxclientsdk.cpp)

add_library(vivoxclientapi STATIC ${SOURCES})
