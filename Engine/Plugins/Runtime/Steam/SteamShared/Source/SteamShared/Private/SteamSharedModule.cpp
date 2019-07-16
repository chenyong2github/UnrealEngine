// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SteamSharedModule.h"
#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"

#ifndef STEAM_SDK_INSTALLED
#error Steam SDK not located! Expected to be found in Engine/Source/ThirdParty/Steamworks/{SteamVersion}
#endif // STEAM_SDK_INSTALLED

// Steam API for Initialization
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogSteamShared, Log, All);

IMPLEMENT_MODULE(FSteamSharedModule, SteamShared);

FString FSteamSharedModule::GetSteamModulePath() const
{
	const FString STEAM_SDK_ROOT_PATH(TEXT("Binaries/ThirdParty/Steamworks"));
#if PLATFORM_WINDOWS

	#if PLATFORM_64BITS
		return FPaths::EngineDir() / STEAM_SDK_ROOT_PATH / STEAM_SDK_VER_PATH / TEXT("Win64/");
	#else
		return FPaths::EngineDir() / STEAM_SDK_ROOT_PATH / STEAM_SDK_VER_PATH / TEXT("Win32/");
	#endif	//PLATFORM_64BITS

#elif PLATFORM_LINUX

	#if PLATFORM_64BITS
		return FPaths::EngineDir() / STEAM_SDK_ROOT_PATH / STEAM_SDK_VER_PATH / TEXT("x86_64-unknown-linux-gnu/");
	#else
		return FPaths::EngineDir() / STEAM_SDK_ROOT_PATH / STEAM_SDK_VER_PATH / TEXT("i686-unknown-linux-gnu/");
	#endif	//PLATFORM_64BITS
	
#elif PLATFORM_MAC
	return FPaths::EngineDir() / STEAM_SDK_ROOT_PATH / STEAM_SDK_VER_PATH / TEXT("Mac/");
#else

	return FString();

#endif	//PLATFORM_WINDOWS
}

void FSteamSharedModule::StartupModule()
{
	// On startup load the modules. Anyone who uses this shared library can guarantee
	// that Steamworks will be started and be ready to go for them.
	LoadSteamModules();
}

void FSteamSharedModule::ShutdownModule()
{
	// Check to see if anyone has a DLL handle still.
	if (AreSteamDllsLoaded())
	{
		// Make sure everyone cleaned up their instances properly.
		if (InstanceHandlerObserver.IsValid()) // If our weakptr is still valid, that means there are cases that didn't clean up somehow.
		{
			// If they have not, warn to them they need to clean up in the future and force the deletion.
			uint32 NumSharedReferences = InstanceHandlerObserver.Pin().GetSharedReferenceCount();
			// Make sure to subtract 1 here as we just created a new sharedptr in order to get the reference count
			// (this sharedptr is out of scope so it does not matter)
			UE_LOG(LogSteamShared, Warning, TEXT("There are still %d additional Steam instances in use. These must be shutdown before unloading the module!"), NumSharedReferences - 1);
		}
		InstanceHandlerObserver.Reset(); // Force the clearing of our weakptr.
	}

	// Here we are no longer loaded, so we need to override any DLL handles still open and unlink the DLLs.
	UnloadSteamModules();
}

TSharedPtr<class FSteamInstanceHandler> FSteamSharedModule::ObtainSteamInstanceHandle()
{
	if (InstanceHandlerObserver.IsValid())
	{
		return InstanceHandlerObserver.Pin();
	}
	else
	{
		// Create the original base object, and store our weakptrs.
		TSharedPtr<FSteamInstanceHandler> BaseInstance = MakeShareable(new FSteamInstanceHandler(this));
		InstanceHandlerObserver = BaseInstance;
		if (BaseInstance->bInitialized) // Make sure the SteamAPI was initialized properly.
		{
			// This is safe because we'll end up incrementing the BaseInstance refcounter before we go out of scope.
			// Thus the InstanceHandler will not be deleted before we return.
			return InstanceHandlerObserver.Pin();
		}
		else
		{
			// We don't want to hold any references to failed instances.
			InstanceHandlerObserver = nullptr;
		}
	}

	return nullptr;
}

bool FSteamSharedModule::AreSteamDllsLoaded() const
{
	bool bLoadedClientDll = true;
	bool bLoadedServerDll = true;

#if LOADING_STEAM_LIBRARIES_DYNAMICALLY
	bLoadedClientDll = (SteamDLLHandle != nullptr) ? true : false;
#endif // LOADING_STEAM_LIBRARIES_DYNAMICALLY
#if LOADING_STEAM_SERVER_LIBRARY_DYNAMICALLY
	bLoadedServerDll = IsRunningDedicatedServer() ? ((SteamServerDLLHandle != nullptr || !bForceLoadSteamClientDll) ? true : false) : true;
#endif // LOADING_STEAM_SERVER_LIBRARY_DYNAMICALLY

	return bLoadedClientDll && bLoadedServerDll;
}

void FSteamSharedModule::LoadSteamModules()
{
	if (AreSteamDllsLoaded())
	{
		return;
	}
		
	UE_LOG(LogSteamShared, Display, TEXT("Loading Steam SDK %s"), STEAM_SDK_VER);

#if PLATFORM_WINDOWS

#if PLATFORM_64BITS
	FString Suffix("64");
#else
	FString Suffix;
#endif // PLATFORM_64BITS

	FString RootSteamPath = GetSteamModulePath();
	FPlatformProcess::PushDllDirectory(*RootSteamPath);
	SteamDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + "steam_api" + Suffix + ".dll"));
	if (IsRunningDedicatedServer() && FCommandLine::IsInitialized() && FParse::Param(FCommandLine::Get(), TEXT("force_steamclient_link")))
	{
		FString SteamClientDLL("steamclient" + Suffix + ".dll"),
			SteamTierDLL("tier0_s" + Suffix + ".dll"),
			SteamVSTDDLL("vstdlib_s" + Suffix + ".dll");

		UE_LOG(LogSteamShared, Log, TEXT("Attempting to force linking the steam client dlls."));
		bForceLoadSteamClientDll = true;
		SteamServerDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + SteamClientDLL));
		if(!SteamServerDLLHandle)
		{
			UE_LOG(LogSteamShared, Error, TEXT("Could not find the %s, %s and %s DLLs, make sure they are all located at %s! These dlls can be located in your Steam install directory."),
				*SteamClientDLL, *SteamTierDLL, *SteamVSTDDLL, *RootSteamPath);
		}
	}
	FPlatformProcess::PopDllDirectory(*RootSteamPath);
#elif PLATFORM_MAC || (PLATFORM_LINUX && LOADING_STEAM_LIBRARIES_DYNAMICALLY)

#if PLATFORM_MAC
	FString SteamModuleFileName("libsteam_api.dylib");
#else
	FString SteamModuleFileName("libsteam_api.so");
#endif // PLATFORM_MAC

	SteamDLLHandle = FPlatformProcess::GetDllHandle(*SteamModuleFileName);
	if (SteamDLLHandle == nullptr)
	{
		// try bundled one
		UE_LOG(LogSteamShared, Warning, TEXT("Could not find system one, loading bundled %s."), *SteamModuleFileName);
		FString RootSteamPath = GetSteamModulePath();
		SteamDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + SteamModuleFileName));
	}

	if (SteamDLLHandle)
	{
		UE_LOG(LogSteamShared, Display, TEXT("Loaded %s at %p"), *SteamModuleFileName, SteamDLLHandle);
	}
	else
	{
		UE_LOG(LogSteamShared, Warning, TEXT("Unable to load %s, Steam functionality will not work"), *SteamModuleFileName);
		return;
	}


#elif PLATFORM_LINUX
	UE_LOG(LogSteamShared, Log, TEXT("libsteam_api.so is linked explicitly and should be already loaded."));
	return;
#endif // PLATFORM_WINDOWS
	UE_LOG(LogSteamShared, Log, TEXT("Steam SDK Loaded!"));
}

void FSteamSharedModule::UnloadSteamModules()
{
	// Only free the handles if no one is using them anymore.
	// There's no need to check AreSteamDllsLoaded as this is done individually below.
	if (!InstanceHandlerObserver.IsValid())
	{
#if LOADING_STEAM_LIBRARIES_DYNAMICALLY
		UE_LOG(LogSteamShared, Log, TEXT("Freeing the Steam Loaded Modules..."));

		if (SteamDLLHandle != nullptr)
		{
			FPlatformProcess::FreeDllHandle(SteamDLLHandle);
			SteamDLLHandle = nullptr;
		}

		if (SteamServerDLLHandle != nullptr)
		{
			FPlatformProcess::FreeDllHandle(SteamServerDLLHandle);
			SteamServerDLLHandle = nullptr;
		}
#endif	//LOADING_STEAM_LIBRARIES_DYNAMICALLY
	}
}

FSteamInstanceHandler::FSteamInstanceHandler(FSteamSharedModule* SteamInitializer) :
	bInitialized(false)
{
	// A module must be loaded in order for us to initialize the Steam API.
	if (SteamInitializer != nullptr && SteamInitializer->AreSteamDllsLoaded())
	{
		if (SteamAPI_Init())
		{
			UE_LOG(LogSteamShared, Verbose, TEXT("SteamAPI initialized"));
			bInitialized = true;
			return;
		}

		// The conditions mentioned in this print can be found at https://partner.steamgames.com/doc/sdk/api#initialization_and_shutdown
		UE_LOG(LogSteamShared, Warning, TEXT("SteamAPI failed to initialize, conditions not met."));
		bInitialized = false;
		return;
	}
	UE_LOG(LogSteamShared, Warning, TEXT("SteamAPI failed to initialize as the Dlls are not loaded."));
}

FSteamInstanceHandler::~FSteamInstanceHandler()
{
	// So long as a module is loaded, we can call shutdown.
	if (bInitialized && FSteamSharedModule::IsAvailable() && FSteamSharedModule::Get().AreSteamDllsLoaded())
	{
		// By getting here, we must be the last instance of the object, thus we should be deleted.
		// If no one is using the API, we can shut it down.
		UE_LOG(LogSteamShared, Log, TEXT("Unloading the Steam API..."));
		SteamAPI_Shutdown();
	}
}
