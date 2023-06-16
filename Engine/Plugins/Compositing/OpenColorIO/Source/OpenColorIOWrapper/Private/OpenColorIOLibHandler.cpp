// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOLibHandler.h"

#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
#include "OpenColorIOWrapperModule.h"


#if PLATFORM_CPU_ARM_FAMILY
	#if PLATFORM_WINDOWS
		#define TARGET_ARCH TEXT("ARM64")
	#elif PLATFORM_LINUX
		#define TARGET_ARCH TEXT("aarch64-unknown-linux-gnueabi")
	#endif
#else
	#if PLATFORM_WINDOWS
		#define TARGET_ARCH TEXT("x64")
	#elif PLATFORM_LINUX
		#define TARGET_ARCH TEXT("x86_64-unknown-linux-gnu")
	#endif
#endif


 //~ Static initialization
 //--------------------------------------------------------------------
void* FOpenColorIOLibHandler::LibHandle = nullptr;


//~ Initialization functions implementation
//--------------------------------------------------------------------
bool FOpenColorIOLibHandler::Initialize()
{
#if WITH_OCIO && defined(OCIO_DLL_NAME)
	check(LibHandle == nullptr);

#if PLATFORM_WINDOWS || PLATFORM_LINUX
	const FString OCIOBinPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/OpenColorIO"), FPlatformProcess::GetBinariesSubdirectory(), TARGET_ARCH);
#else
	const FString OCIOBinPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/OpenColorIO"), FPlatformProcess::GetBinariesSubdirectory());
#endif
	const FString DLLPath = FPaths::Combine(OCIOBinPath, TEXT(PREPROCESSOR_TO_STRING(OCIO_DLL_NAME)));

	FPlatformProcess::PushDllDirectory(*OCIOBinPath);

	if (!FPaths::FileExists(DLLPath))
	{
		UE_LOG(LogOpenColorIOWrapper, Error, TEXT("Failed to find the OpenColorIO dll. Plug-in will not be functional."));
		return false;
	}
	
	LibHandle = FPlatformProcess::GetDllHandle(*DLLPath);
	FPlatformProcess::PopDllDirectory(*OCIOBinPath);

	if (LibHandle == nullptr)
	{
		UE_LOG(LogOpenColorIOWrapper, Error, TEXT("Failed to load required library %s. Plug-in will not be functional."), *DLLPath);
		return false;
	}

#else
	return false;
#endif // WITH_EDITOR
	return true;
}

bool FOpenColorIOLibHandler::IsInitialized()
{
	return (LibHandle != nullptr);
}

void FOpenColorIOLibHandler::Shutdown()
{
#if WITH_OCIO && defined(OCIO_DLL_NAME)
	if (LibHandle != nullptr)
	{
		FPlatformProcess::FreeDllHandle(LibHandle);
		LibHandle = nullptr;
	}
#endif // WITH_EDITOR
}



