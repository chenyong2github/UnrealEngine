// Copyright Epic Games, Inc. All Rights Reserved.

#include "MsQuicRuntimePrivate.h"
#include "IMsQuicRuntimeModule.h"

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Misc/CoreMisc.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMsQuicRuntime);

#define LOCTEXT_NAMESPACE "FMsQuicRuntimeModule"


class FMsQuicRuntimeModule
	: public IMsQuicRuntimeModule
{

public:

	// IMsQuicRuntimeModule interface

	bool InitRuntime() override
    {
		if (MsQuicLibraryHandle)
		{
			UE_LOG(LogMsQuicRuntime, Display,
				TEXT("[MsQuicRuntimeModule] MsQuic DLL already loaded."));

			return true;
		}

		if (!LoadMsQuicDll())
		{
			UE_LOG(LogMsQuicRuntime, Error,
				TEXT("[MsQuicRuntimeModule] Could not load MsQuic DLL."));

			return false;
		}

		return true;
    }

public:

	// IModuleInterface interface

    virtual void StartupModule() override
    {
    }

    virtual void ShutdownModule() override
    {
		FreeMsQuicDll();
    }

private:

	/**
	 * Load the appropriate MsQuic DLL/So for this platform.
	 */
	bool LoadMsQuicDll()
	{
		const FString MsQuicBinariesDir = FPaths::Combine(
			*FPaths::EngineDir(), *MSQUIC_BINARIES_PATH);

		FString MsQuicLib = "";

#if PLATFORM_WINDOWS

		MsQuicLib = FPaths::Combine(*MsQuicBinariesDir,
			TEXT("win64/msquic.dll"));

#elif PLATFORM_LINUX

		MsQuicLib = FPaths::Combine(*MsQuicBinariesDir,
			TEXT("linux/libmsquic.so"));

#elif PLATFORM_MAC

		MsQuicLib = FPaths::Combine(*MsQuicBinariesDir,
			TEXT("macos/libmsquic.dylib"));

#endif

		MsQuicLibraryHandle = (MsQuicLib.IsEmpty())
			? nullptr : FPlatformProcess::GetDllHandle(*MsQuicLib);

		return MsQuicLibraryHandle != nullptr;
	}

	/**
	 * Free the MsQuic DLL/So if the LibraryHandle is valid.
	 */
	void FreeMsQuicDll()
	{
		if (MsQuicLibraryHandle)
		{
			FPlatformProcess::FreeDllHandle(MsQuicLibraryHandle);
			MsQuicLibraryHandle = nullptr;
		}
	}

private:

	/** Holds the MsQuic library dll handle. */
	void* MsQuicLibraryHandle = nullptr;

	/** Defines the MsQuic version to be used. */
	const FString MSQUIC_VERSION = "v220";

	/** Defines the MsQuic binaries path. */
	const FString MSQUIC_BINARIES_PATH = "Binaries/ThirdParty/MsQuic/" + MSQUIC_VERSION;

};


#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMsQuicRuntimeModule, MsQuicRuntime);
