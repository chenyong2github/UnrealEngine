// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSSDKManager.h"

#if WITH_EOS_SDK

#include "Containers/Ticker.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/App.h"
#include "Misc/CoreMisc.h"
#include "Misc/CoreDelegates.h"
#include "Misc/NetworkVersion.h"
#include "Stats/Stats.h"

#include "CoreGlobals.h"

#include "EOSShared.h"

#include "eos_init.h"
#include "eos_logging.h"
#include "eos_sdk.h"
#include "eos_version.h"

namespace
{
	static void* EOS_MEMORY_CALL EosMalloc(size_t Bytes, size_t Alignment)
	{
		LLM_SCOPE(ELLMTag::RealTimeCommunications);
		return FMemory::Malloc(Bytes, Alignment);
	}

	static void* EOS_MEMORY_CALL EosRealloc(void* Ptr, size_t Bytes, size_t Alignment)
	{
		LLM_SCOPE(ELLMTag::RealTimeCommunications);
		return FMemory::Realloc(Ptr, Bytes, Alignment);
	}

	static void EOS_MEMORY_CALL EosFree(void* Ptr)
	{
		LLM_SCOPE(ELLMTag::RealTimeCommunications);
		FMemory::Free(Ptr);
	}

#if !NO_LOGGING
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

	EOS_ELogLevel ConvertLogLevel(ELogVerbosity::Type LogLevel)
	{
		switch (LogLevel)
		{
		case ELogVerbosity::NoLogging:		return EOS_ELogLevel::EOS_LOG_Off;
		case ELogVerbosity::Fatal:			return EOS_ELogLevel::EOS_LOG_Fatal;
		case ELogVerbosity::Error:			return EOS_ELogLevel::EOS_LOG_Error;
		case ELogVerbosity::Warning:		return EOS_ELogLevel::EOS_LOG_Warning;
		default:							// Intentional fall through
		case ELogVerbosity::Display:		// Intentional fall through
		case ELogVerbosity::Log:			return EOS_ELogLevel::EOS_LOG_Info;
		case ELogVerbosity::Verbose:		return EOS_ELogLevel::EOS_LOG_Verbose;
		case ELogVerbosity::VeryVerbose:	return EOS_ELogLevel::EOS_LOG_VeryVerbose;
		}
	}
#endif // !NO_LOGGING
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
		UE_LOG(LogEOSSDK, Log, TEXT("Initializing EOSSDK Version:%s"), UTF8_TO_TCHAR(EOS_GetVersion()));

		const FTCHARToUTF8 ProductName(*GetProductName());
		const FTCHARToUTF8 ProductVersion(*GetProductVersion());

		EOS_InitializeOptions InitializeOptions = {};
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

#if !NO_LOGGING
			FCoreDelegates::OnLogVerbosityChanged.AddRaw(this, &FEOSSDKManager::OnLogVerbosityChanged);

			EosResult = EOS_Logging_SetCallback(&EOSLogMessageReceived);
			if (EosResult != EOS_EResult::EOS_Success)
			{
				UE_LOG(LogEOSSDK, Warning, TEXT("EOS_Logging_SetCallback failed error:%s"), *LexToString(EosResult));
			}

			EosResult = EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, ConvertLogLevel(LogEOSSDK.GetVerbosity()));
			if (EosResult != EOS_EResult::EOS_Success)
			{
				UE_LOG(LogEOSSDK, Warning, TEXT("EOS_Logging_SetLogLevel failed Verbosity=%s error=[%s]"), ToString(LogEOSSDK.GetVerbosity()), *LexToString(EosResult));
			}
#endif // !NO_LOGGING
		}
		else
		{
			UE_LOG(LogEOSSDK, Warning, TEXT("EOS_Initialize failed error:%s"), *LexToString(EosResult));
		}

		return EosResult;
	}
}

IEOSPlatformHandlePtr FEOSSDKManager::CreatePlatform(const EOS_Platform_Options& PlatformOptions)
{
	IEOSPlatformHandlePtr SharedPlatform;

	if (IsInitialized())
	{
		const EOS_HPlatform PlatformHandle = EOS_Platform_Create(&PlatformOptions);
		if (PlatformHandle)
		{
			PlatformHandles.Emplace(PlatformHandle);
			SharedPlatform = MakeShared<FEOSPlatformHandle, ESPMode::ThreadSafe>(*this, PlatformHandle);

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

	return SharedPlatform;
}

bool FEOSSDKManager::Tick(float)
{
	//LLM_SCOPE(ELLMTag::EOSSDK); // TODO
	for (EOS_HPlatform PlatformHandle : PlatformHandles)
	{
		QUICK_SCOPE_CYCLE_COUNTER(FEOSSDKManager_Tick);
		EOS_Platform_Tick(PlatformHandle);
	}

	return true;
}

void FEOSSDKManager::OnLogVerbosityChanged(const FLogCategoryName& CategoryName, ELogVerbosity::Type OldVerbosity, ELogVerbosity::Type NewVerbosity)
{
#if !NO_LOGGING
	if (IsInitialized() &&
		CategoryName == LogEOSSDK.GetCategoryName())
	{
		const EOS_EResult EosResult = EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, ConvertLogLevel(NewVerbosity));
		if (EosResult != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogEOSSDK, Warning, TEXT("EOS_Logging_SetLogLevel failed Verbosity=%s error=[%s]"), ToString(NewVerbosity), *LexToString(EosResult));
		}
	}
#endif // !NO_LOGGING
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

void FEOSSDKManager::ReleasePlatform(EOS_HPlatform PlatformHandle)
{
	// Only release the platform if it was actually present in PlatformHandles, as Shutdown may have already released it.
	if (PlatformHandles.Remove(PlatformHandle))
	{
		EOS_Platform_Release(PlatformHandle);

		if (ensure(TickerHandle.IsValid()) &&
			PlatformHandles.Num() == 0)
		{
			FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}
	}
	else
	{
		UE_LOG(LogEOSSDK, Warning, TEXT("FEOSSDKManager::ReleasePlatform PlatformHandle does not exist."));
	}
}

void FEOSSDKManager::Shutdown()
{
	if (IsInitialized())
	{
		if (PlatformHandles.Num() > 0)
		{
			UE_LOG(LogEOSSDK, Warning, TEXT("FEOSSDKManager::Shutdown Releasing %d remaining platforms"), PlatformHandles.Num());
			for (EOS_HPlatform PlatformHandle : PlatformHandles)
			{
				EOS_Platform_Release(PlatformHandle);
			}
			PlatformHandles.Empty();

			if (ensure(TickerHandle.IsValid()))
			{
				FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
				TickerHandle.Reset();
			}
		}

#if !NO_LOGGING
		FCoreDelegates::OnLogVerbosityChanged.RemoveAll(this);
#endif // !NO_LOGGING

		const EOS_EResult Result = EOS_Shutdown();
		UE_LOG(LogEOSSDK, Log, TEXT("FEOSSDKManager::Shutdown EOS_Shutdown Result=[%s]"), *LexToString(Result));

		bInitialized = false;
	}
}

EOS_EResult FEOSSDKManager::EOSInitialize(EOS_InitializeOptions& Options)
{
	return EOS_Initialize(&Options);
}

FEOSPlatformHandle::~FEOSPlatformHandle()
{
	Manager.ReleasePlatform(PlatformHandle);
}

void FEOSPlatformHandle::Tick()
{
	QUICK_SCOPE_CYCLE_COUNTER(FEOSPlatformHandle_Tick);
	EOS_Platform_Tick(PlatformHandle);
}

#endif // WITH_EOS_SDK