// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"
#include "OnlineSubsystemSteamModule.h"
#include "OnlineSubsystemModule.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemSteamPrivate.h"
#include "OnlineSubsystemSteam.h"
#include "HAL/PlatformProcess.h"

IMPLEMENT_MODULE(FOnlineSubsystemSteamModule, OnlineSubsystemSteam);

//HACKTASTIC (Needed to keep delete function from being stripped out and crashing when protobuffers deallocate memory)
void* HackDeleteFunctionPointer = (void*)(void(*)(void*))(::operator delete[]);

/**
 * Class responsible for creating instance(s) of the subsystem
 */
class FOnlineFactorySteam : public IOnlineFactory
{

private:

	/** Single instantiation of the STEAM interface */
	static FOnlineSubsystemSteamPtr SteamSingleton;

	virtual void DestroySubsystem()
	{
		if (SteamSingleton.IsValid())
		{
			SteamSingleton->Shutdown();
			SteamSingleton = NULL;
		}
	}

public:

	FOnlineFactorySteam() {}
	virtual ~FOnlineFactorySteam() 
	{
		DestroySubsystem();
	}

	virtual IOnlineSubsystemPtr CreateSubsystem(FName InstanceName)
	{
		if (!SteamSingleton.IsValid())
		{
			SteamSingleton = MakeShared<FOnlineSubsystemSteam, ESPMode::ThreadSafe>(InstanceName);
			if (SteamSingleton->IsEnabled())
			{
				if(!SteamSingleton->Init())
				{
					UE_LOG_ONLINE(Warning, TEXT("Steam API failed to initialize!"));
					DestroySubsystem();
				}
			}
			else
			{
				UE_CLOG_ONLINE(IsRunningDedicatedServer() || IsRunningGame(), Warning, TEXT("Steam API disabled!"));
				DestroySubsystem();
			}

			return SteamSingleton;
		}

		UE_LOG_ONLINE(Warning, TEXT("Can't create more than one instance of Steam online subsystem!"));
		return NULL;
	}
};

FOnlineSubsystemSteamPtr FOnlineFactorySteam::SteamSingleton = NULL;

bool FOnlineSubsystemSteamModule::AreSteamDllsLoaded() const
{
	bool bLoadedClientDll = true;
	bool bLoadedServerDll = true;

#if LOADING_STEAM_LIBRARIES_DYNAMICALLY
	bLoadedClientDll = (SteamDLLHandle != NULL) ? true : false;
	#if LOADING_STEAM_SERVER_LIBRARY_DYNAMICALLY
	bLoadedServerDll = IsRunningDedicatedServer() ? ((SteamServerDLLHandle != NULL || !bForceLoadSteamClientDll) ? true : false) : true;
	#endif //LOADING_STEAM_SERVER_LIBRARY_DYNAMICALLY
#endif // LOADING_STEAM_LIBRARIES_DYNAMICALLY

	return bLoadedClientDll && bLoadedServerDll;
}

static FString GetSteamModulePath()
{
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

void FOnlineSubsystemSteamModule::LoadSteamModules()
{
	UE_LOG_ONLINE(Display, TEXT("Loading Steam SDK %s"), STEAM_SDK_VER);

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

		UE_LOG_ONLINE(Log, TEXT("Attempting to force linking the steam client dlls."));
		bForceLoadSteamClientDll = true;
		SteamServerDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + SteamClientDLL));
		if(!SteamServerDLLHandle)
		{
			UE_LOG_ONLINE(Error, TEXT("Could not find the %s, %s and %s DLLs, make sure they are all located at %s! These dlls can be located in your Steam install directory."), 
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
		UE_LOG_ONLINE(Warning, TEXT("Could not find system one, loading bundled %s."), *SteamModuleFileName);
		FString RootSteamPath = GetSteamModulePath();
		SteamDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + SteamModuleFileName));
	}

	if (SteamDLLHandle)
	{
		UE_LOG_ONLINE(Display, TEXT("Loaded %s at %p"), *SteamModuleFileName, SteamDLLHandle);
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Unable to load %s, Steam functionality will not work"), *SteamModuleFileName);
	}


#elif PLATFORM_LINUX
	UE_LOG_ONLINE(Log, TEXT("libsteam_api.so is linked explicitly and should be already loaded."));
#endif // PLATFORM_WINDOWS
}

void FOnlineSubsystemSteamModule::UnloadSteamModules()
{
#if LOADING_STEAM_LIBRARIES_DYNAMICALLY
	if (SteamDLLHandle != NULL)
	{
		FPlatformProcess::FreeDllHandle(SteamDLLHandle);
		SteamDLLHandle = NULL;
	}

	if (SteamServerDLLHandle != NULL)
	{
		FPlatformProcess::FreeDllHandle(SteamServerDLLHandle);
		SteamServerDLLHandle = NULL;
	}
#endif	//LOADING_STEAM_LIBRARIES_DYNAMICALLY
}

void FOnlineSubsystemSteamModule::StartupModule()
{
	bool bSuccess = false;

	// Load the Steam modules before first call to API
	LoadSteamModules();
	if (AreSteamDllsLoaded())
	{
		// Create and register our singleton factory with the main online subsystem for easy access
		SteamFactory = new FOnlineFactorySteam();

		FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
		OSS.RegisterPlatformService(STEAM_SUBSYSTEM, SteamFactory);
		bSuccess = true;
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Steam SDK %s libraries not present at %s or failed to load!"), STEAM_SDK_VER, *GetSteamModulePath());
	}

	if (!bSuccess)
	{
		UnloadSteamModules();
	}
}

void FOnlineSubsystemSteamModule::ShutdownModule()
{
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.UnregisterPlatformService(STEAM_SUBSYSTEM);

	delete SteamFactory;
	SteamFactory = NULL;

	UnloadSteamModules();
}
