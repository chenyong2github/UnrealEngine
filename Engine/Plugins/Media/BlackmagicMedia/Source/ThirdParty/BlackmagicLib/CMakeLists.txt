cmake_minimum_required(VERSION 3.0.0)
project(UE4BlackmagicDesign VERSION 0.1.0)

include(CTest)
enable_testing()

add_library(
    BlackmagicLib SHARED
    ../../../../../../../Plugins/Media/BlackmagicMedia/Source/ThirdParty/Build/Include/BlackmagicLib.h
    ../../../../../../../Plugins/Media/BlackmagicMedia/Source/ThirdParty/Build/Include/BlackmagicReferencePtr.h
    Platform/GenericPlatform.h
    Platform/Linux/LinuxPlatform.h
    Platform/Linux/LinuxPlatform.cpp
    Source/BlackmagicDeviceScanner.h
    Source/BlackmagicDeviceScanner.cpp
    Source/BlackmagicHelper.h
    Source/BlackmagicHelper.cpp
    Source/BlackmagicInputChannel.h
    Source/BlackmagicInputChannel.cpp
    Source/BlackmagicDevice.h
    Source/BlackmagicDevice.cpp
    Source/BlackmagicLib.cpp
    Source/BlackmagicOutputChannel.h
    Source/BlackmagicOutputChannel.cpp
    Source/BlackmagicVideoFormats.h
    Source/BlackmagicVideoFormats.cpp
	Source/TextureDMA.cpp
    Source/SSE2MemCpy.h
    Source/SSE2MemCpy.cpp
    Source/stdafx.h
    Source/stdafx.cpp
    Source/BlackmagicLog.h
    Source/BlackmagicLog.cpp
    Include/Linux/DeckLinkAPIDispatch.cpp
)

# target_precompile_headers(BlackmagicLib PRIVATE Source/stdafx.h)
target_include_directories(
    BlackmagicLib PRIVATE
    ../../../../../../../Plugins/Media/BlackmagicMedia/Source/ThirdParty/Build/Include/
	../../../../../../../Restricted/NotForLicensees/Plugins/Media/GPUTextureTransfer/Source/ThirdParty/Include
    ../../../../../../../Source/ThirdParty/Unix/LibCxx/include
    ../../../../../../../Source/ThirdParty/Unix/LibCxx/include/c++/v1
    .
    ../Build/Include
    Include
)

target_link_directories(
    BlackmagicLib PRIVATE
    ../../../../../../../Source/ThirdParty/Unix/LibCxx/lib/Linux/x86_64-unknown-linux-gnu
)

set_target_properties(
    BlackmagicLib PROPERTIES
    CXX_STANDARD 14
    DEFINE_SYMBOL UEBLACKMAGICDESIGN_EXPORTS
    ARCHIVE_OUTPUT_DIRECTORY "../../../../../../../../Plugins/Media/BlackmagicMedia/Binaries/ThirdParty/Linux"
    LIBRARY_OUTPUT_DIRECTORY "../../../../../../../../Plugins/Media/BlackmagicMedia/Binaries/ThirdParty/Linux"
    RUNTIME_OUTPUT_DIRECTORY "../../../../../../../../Plugins/Media/BlackmagicMedia/Binaries/ThirdParty/Linux"
)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
    
include(CPack)
