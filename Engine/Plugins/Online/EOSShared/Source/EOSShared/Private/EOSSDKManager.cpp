// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSSDKManager.h"

#if WITH_EOS_SDK

#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/NetworkVersion.h"

#include "CoreGlobals.h"
#include "EOSSharedUtils.h"

#include "eos_init.h"
#include "eos_logging.h"
#include "eos_sdk.h"
#include "eos_version.h"

DEFINE_LOG_CATEGORY_STATIC(LogEOSSDK, Log, All);

namespace
{
	static void* EosMalloc(size_t Bytes, size_t Alignment)
	{
		LLM_SCOPE(ELLMTag::RealTimeCommunications);
		return FMemory::Malloc(Bytes, Alignment);
	}

	static void* EosRealloc(void* Ptr, size_t Bytes, size_t Alignment)
	{
		LLM_SCOPE(ELLMTag::RealTimeCommunications);
		return FMemory::Realloc(Ptr, Bytes, Alignment);
	}

	static void EosFree(void* Ptr)
	{
		LLM_SCOPE(ELLMTag::RealTimeCommunications);
		FMemory::Free(Ptr);
	}

	void EOS_CALL EOSLogMessageReceived(const EOS_LogMessage* Message)
	{
#define EOSLOG(Level) UE_LOG(LogEOSSDK, Level, TEXT("%s: %s"), ANSI_TO_TCHAR(Message->Category), *MessageStr)

		FString MessageStr(ANSI_TO_TCHAR(Message->Message));
		MessageStr.TrimStartAndEndInline();

		switch (Message->Level)
		{
		case EOS_ELogLevel::EOS_LOG_Fatal:			EOSLOG(Fatal); break;
		case EOS_ELogLevel::EOS_LOG_Error:			EOSLOG(Error); break;
		case EOS_ELogLevel::EOS_LOG_Warning:		EOSLOG(Warning); break;
		case EOS_ELogLevel::EOS_LOG_Info:			EOSLOG(Log); break;
		case EOS_ELogLevel::EOS_LOG_Verbose:		EOSLOG(Verbose); break;
		case EOS_ELogLevel::EOS_LOG_VeryVerbose:	EOSLOG(VeryVerbose); break;
		case EOS_ELogLevel::EOS_LOG_Off:
		default:
			// do nothing
			break;
		}
#undef EOSLOG
	}
}


// if the platform wants to preload the EOS runtime library, then use the auto registration system to kick off
// the GetDllHandle call at a specified time. it is assumed that calling GetDllHandle here will not conflict
// with the call in FEOSSDKManager::FEOSSDKManager, and will only speed it up, even if it's not complete before
// the later one is called. if that's not the case, we will need an FEvent or similar to block the constructor
// until this is complete
#if EOSSDK_RUNTIME_LOAD_REQUIRED && defined(EOS_DLL_PRELOAD_PHASE)

#include "Misc/DelayedAutoRegister.h"
#include "Async/Async.h"

static FDelayedAutoRegisterHelper GKickoffDll(EDelayedRegisterRunPhase::EOS_DLL_PRELOAD_PHASE, []
{
	Async(EAsyncExecution::Thread, []
	{
		SCOPED_BOOT_TIMING("Preloading EOS module");
		FPlatformProcess::GetDllHandle(TEXT(EOSSDK_RUNTIME_LIBRARY_NAME));
	});
});

#endif

FEOSSDKManager::FEOSSDKManager()
{
#if EOSSDK_RUNTIME_LOAD_REQUIRED
	SDKHandle = FPlatformProcess::GetDllHandle(TEXT(EOSSDK_RUNTIME_LIBRARY_NAME));

	if (SDKHandle == nullptr)
	{
		UE_LOG(LogEOSSDK, Warning, TEXT("Unable to load EOSSDK dynamic library"));
		return;
	}
#endif
}

FEOSSDKManager::~FEOSSDKManager()
{
#if EOSSDK_RUNTIME_LOAD_REQUIRED
	if (SDKHandle != nullptr)
	{
		FPlatformProcess::FreeDllHandle(SDKHandle);
	}
#endif
}

EOS_EResult FEOSSDKManager::Initialize()
{
#if EOSSDK_RUNTIME_LOAD_REQUIRED
	if (SDKHandle == nullptr)
	{
		UE_LOG(LogEOSSDK, Log, TEXT("FEOSSDKManager::Initialize failed, SDKHandle=nullptr"));
		return EOS_EResult::EOS_InvalidState;
	}
#endif

	if (IsInitialized())
	{
		return EOS_EResult::EOS_Success;
	}
	else
	{
		UpdateConfiguration();

		UE_LOG(LogEOSSDK, Log, TEXT("Initializing EOSSDK Version:%s"), UTF8_TO_TCHAR(EOS_GetVersion()));

		const FTCHARToUTF8 ProductName(*GetProductName());
		const FTCHARToUTF8 ProductVersion(*GetProductVersion());

		EOS_InitializeOptions InitializeOptions;
		memset(&InitializeOptions, 0, sizeof(InitializeOptions));
		InitializeOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;
		static_assert(EOS_INITIALIZE_API_LATEST == 4, "EOS_InitializeOptions updated, check new fields");
		InitializeOptions.AllocateMemoryFunction = &EosMalloc;
		InitializeOptions.ReallocateMemoryFunction = &EosRealloc;
		InitializeOptions.ReleaseMemoryFunction = &EosFree;
		InitializeOptions.ProductName = ProductName.Get();
		InitializeOptions.ProductVersion = ProductVersion.Length() > 0 ? ProductVersion.Get() : nullptr;
		InitializeOptions.Reserved = nullptr;
		InitializeOptions.SystemInitializeOptions = nullptr;
		InitializeOptions.OverrideThreadAffinity = nullptr;

		EOS_EResult EosResult = EOSInitialize(InitializeOptions);

		if (EosResult == EOS_EResult::EOS_Success)
		{
			bInitialized = true;

			// Enable logging
			EosResult = EOS_Logging_SetCallback(&EOSLogMessageReceived);
			if (EosResult != EOS_EResult::EOS_Success)
			{
				UE_LOG(LogEOSSDK, Warning, TEXT("EOS_Logging_SetCallback failed error:%s"), *LexToString(EosResult));
			}

			EOS_ELogLevel LogLevel = EOS_ELogLevel::EOS_LOG_Info;
			if (Config.LogLevel == TEXT("Off"))
			{
				LogLevel = EOS_ELogLevel::EOS_LOG_Off;
			}
			else if (Config.LogLevel == TEXT("Fatal"))
			{
				LogLevel = EOS_ELogLevel::EOS_LOG_Fatal;
			}
			else if (Config.LogLevel == TEXT("Error"))
			{
				LogLevel = EOS_ELogLevel::EOS_LOG_Error;
			}
			else if (Config.LogLevel == TEXT("Warning"))
			{
				LogLevel = EOS_ELogLevel::EOS_LOG_Warning;
			}
			else if (Config.LogLevel == TEXT("Info"))
			{
				LogLevel = EOS_ELogLevel::EOS_LOG_Info;
			}
			else if (Config.LogLevel == TEXT("Verbose"))
			{
				LogLevel = EOS_ELogLevel::EOS_LOG_Verbose;
			}
			else if (Config.LogLevel == TEXT("VeryVerbose"))
			{
				LogLevel = EOS_ELogLevel::EOS_LOG_VeryVerbose;
			}
			EosResult = EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, LogLevel);
			if (EosResult != EOS_EResult::EOS_Success)
			{
				UE_LOG(LogEOSSDK, Warning, TEXT("EOS_Logging_SetLogLevel failed error:%s"), *LexToString(EosResult));
			}
		}
		else
		{
			UE_LOG(LogEOSSDK, Warning, TEXT("EOS_Initialize failed error:%s"), *LexToString(EosResult));
		}

		return EosResult;
	}
}

EOS_HPlatform FEOSSDKManager::CreatePlatform(const EOS_Platform_Options& PlatformOptions)
{
	EOS_HPlatform EosPlatformHandle = nullptr;

	if (IsInitialized())
	{
		EosPlatformHandle = EOS_Platform_Create(&PlatformOptions);
		if (EosPlatformHandle)
		{
			EosPlatformHandles.Emplace(EosPlatformHandle);

			if (!TickerHandle.IsValid())
			{
				TickerHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FEOSSDKManager::Tick), 0.0f);
			}
		}
		else
		{
			UE_LOG(LogEOSSDK, Warning, TEXT("FEOSSDKManager::CreatePlatform failed. EosPlatformHandle=nullptr"));
		}
	}
	else
	{
		UE_LOG(LogEOSSDK, Warning, TEXT("FEOSSDKManager::CreatePlatform failed. SDK not initialized"));
	}

	return EosPlatformHandle;
}

void FEOSSDKManager::ReleasePlatform(EOS_HPlatform EosPlatformHandle)
{
	if (EosPlatformHandle)
	{
		EosPlatformHandles.Remove(EosPlatformHandle);
		EOS_Platform_Release(EosPlatformHandle);

		if (EosPlatformHandles.Num() == 0 && TickerHandle.IsValid())
		{
			FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}
	}
}

bool FEOSSDKManager::Tick(float)
{
	//LLM_SCOPE(ELLMTag::EOSSDK); // TODO
	for (EOS_HPlatform EosPlatformHandle : EosPlatformHandles)
	{
		QUICK_SCOPE_CYCLE_COUNTER(FEOSSDKManager_Tick);
		EOS_Platform_Tick(EosPlatformHandle);
	}

	return true;
}

FString FEOSSDKManager::GetProductName() const
{
	return FApp::GetProjectName();
}

FString FEOSSDKManager::GetProductVersion() const
{
	static const FString ProductVersion = FNetworkVersion::GetProjectVersion().IsEmpty() ? TEXT("Unknown") : FNetworkVersion::GetProjectVersion();
	return ProductVersion;
}

void FEOSSDKManager::Shutdown()
{
	if (IsInitialized())
	{
		if (EosPlatformHandles.Num() > 0)
		{
			UE_LOG(LogEOSSDK, Warning, TEXT("FEOSSDKManager::Shutdown Releasing %d remaining platforms"), EosPlatformHandles.Num());
			for (EOS_HPlatform EosPlatformHandle : EosPlatformHandles)
			{
				ReleasePlatform(EosPlatformHandle);
			}
		}

		const EOS_EResult Result = EOS_Shutdown();
		UE_LOG(LogEOSSDK, Log, TEXT("FEOSSDKManager::Shutdown EOS_Shutdown Result=[%s]"), *LexToString(Result));

		bInitialized = false;
	}
}

EOS_EResult FEOSSDKManager::EOSInitialize(EOS_InitializeOptions& Options)
{
	return EOS_Initialize(&Options);
}

void FEOSSDKManager::UpdateConfiguration()
{
	const TCHAR* SectionName = TEXT("EOSSDK");
	const FString& ConfigFile = GEngineIni;

	GConfig->GetString(SectionName, TEXT("LogLevel"), Config.LogLevel, ConfigFile);
}

#endif // WITH_EOS_SDK