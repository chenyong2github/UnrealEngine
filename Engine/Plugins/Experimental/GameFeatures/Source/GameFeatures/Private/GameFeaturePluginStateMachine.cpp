// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeaturePluginStateMachine.h"

#include "AssetRegistry/AssetRegistryState.h"
#include "Components/GameFrameworkComponentManager.h"
#include "GameFeatureData.h"
#include "GameplayTagsManager.h"
#include "Engine/AssetManager.h"
#include "IPlatformFilePak.h"
#include "InstallBundleManagerInterface.h"
#include "BundlePrereqCombinedStatusHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Algo/AllOf.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/MemoryReader.h"
#include "Stats/Stats.h"
#include "EngineUtils.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeaturesProjectPolicies.h"
#include "Containers/Ticker.h"

namespace UE
{
	namespace GameFeatures
	{
		static const FString StateMachineErrorNamespace(TEXT("GameFeaturePlugin.StateMachine."));

		static int32 ShouldLogMountedFiles = 0;
		static FAutoConsoleVariableRef CVarShouldLogMountedFiles(TEXT("GameFeaturePlugin.ShouldLogMountedFiles"),
			ShouldLogMountedFiles,
			TEXT("Should the newly mounted files be logged."));

		FString ToString(const UE::GameFeatures::FResult& Result)
		{
			return Result.HasValue() ? FString(TEXT("Success")) : (FString(TEXT("Failure, ErrorCode=")) + Result.GetError());
		}

		FString ToString(EGameFeaturePluginState InType)
		{
			switch (InType)
			{
			case EGameFeaturePluginState::Uninitialized: return TEXT("Uninitialized");
			case EGameFeaturePluginState::Terminal: return TEXT("Terminal");
			case EGameFeaturePluginState::UnknownStatus: return TEXT("UnknownStatus");
			case EGameFeaturePluginState::CheckingStatus: return TEXT("CheckingStatus");
			case EGameFeaturePluginState::ErrorCheckingStatus: return TEXT("ErrorCheckingStatus");
			case EGameFeaturePluginState::ErrorUnavailable: return TEXT("ErrorUnavailable");
			case EGameFeaturePluginState::StatusKnown: return TEXT("StatusKnown");
			case EGameFeaturePluginState::ErrorInstalling: return TEXT("ErrorInstalling");
			case EGameFeaturePluginState::Uninstalling: return TEXT("Uninstalling");
			case EGameFeaturePluginState::Downloading: return TEXT("Downloading");
			case EGameFeaturePluginState::Installed: return TEXT("Installed");
			case EGameFeaturePluginState::ErrorMounting: return TEXT("ErrorMounting");
			case EGameFeaturePluginState::ErrorWaitingForDependencies: return TEXT("ErrorWaitingForDependencies");
			case EGameFeaturePluginState::ErrorRegistering: return TEXT("ErrorRegistering");
			case EGameFeaturePluginState::Unmounting: return TEXT("Unmounting");
			case EGameFeaturePluginState::Mounting: return TEXT("Mounting");
			case EGameFeaturePluginState::WaitingForDependencies: return TEXT("WaitingForDependencies");
			case EGameFeaturePluginState::Unregistering: return TEXT("Unregistering");
			case EGameFeaturePluginState::Registering: return TEXT("Registering");
			case EGameFeaturePluginState::Registered: return TEXT("Registered");
			case EGameFeaturePluginState::Unloading: return TEXT("Unloading");
			case EGameFeaturePluginState::Loading: return TEXT("Loading");
			case EGameFeaturePluginState::Loaded: return TEXT("Loaded");
			case EGameFeaturePluginState::Deactivating: return TEXT("Deactivating");
			case EGameFeaturePluginState::Activating: return TEXT("Activating");
			case EGameFeaturePluginState::Active: return TEXT("Active");
			default:
				check(0);
				return FString();
			}
		}
	}

	static_assert((int32)EGameFeaturePluginState::MAX == 26, "");
}

#define GAME_FEATURE_PLUGIN_PROTOCOL_PREFIX(inEnum, inString) case EGameFeaturePluginProtocol::inEnum: return inString;
const TCHAR* GameFeaturePluginProtocolPrefix(EGameFeaturePluginProtocol Protocol)
{
	switch (Protocol)
	{
		GAME_FEATURE_PLUGIN_PROTOCOL_LIST(GAME_FEATURE_PLUGIN_PROTOCOL_PREFIX)
	}

	check(false);
	return nullptr;
}
#undef GAME_FEATURE_PLUGIN_PROTOCOL_PREFIX

FGameFeaturePluginState::~FGameFeaturePluginState()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

void FGameFeaturePluginState::UpdateStateMachineDeferred(float Delay /*= 0.0f*/) const
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float dts) mutable
	{
		StateProperties.OnRequestUpdateStateMachine.ExecuteIfBound();
		TickHandle.Reset();
		return false;
	}), Delay);
}

void FGameFeaturePluginState::UpdateStateMachineImmediate() const
{
	StateProperties.OnRequestUpdateStateMachine.ExecuteIfBound();
}

void FGameFeaturePluginState::UpdateProgress(float Progress) const
{
	StateProperties.OnFeatureStateProgressUpdate.ExecuteIfBound(Progress);
}

/*
=========================================================
  States
=========================================================
*/

struct FDestinationGameFeaturePluginState : public FGameFeaturePluginState
{
	FDestinationGameFeaturePluginState(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual EGameFeaturePluginStateType GetStateType() const override
	{
		return EGameFeaturePluginStateType::Destination;
	}
};

struct FErrorGameFeaturePluginState : public FGameFeaturePluginState
{
	FErrorGameFeaturePluginState(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual EGameFeaturePluginStateType GetStateType() const override
	{
		return EGameFeaturePluginStateType::Error;
	}
};

struct FGameFeaturePluginState_Uninitialized : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Uninitialized(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		checkf(false, TEXT("UpdateState can not be called while uninitialized"));
	}
};

struct FGameFeaturePluginState_Terminal : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_Terminal(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	bool bEnteredTerminalState = false;

	virtual void BeginState() override
	{
		checkf(!bEnteredTerminalState, TEXT("Plugin entered terminal state more than once! %s"), *StateProperties.PluginURL);
		bEnteredTerminalState = true;

		UGameFeaturesSubsystem::Get().OnGameFeatureTerminating(StateProperties.PluginURL);
	}
};

struct FGameFeaturePluginState_UnknownStatus : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_UnknownStatus(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState < EGameFeaturePluginState::UnknownStatus)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Terminal);
		}
		else if (StateProperties.DestinationState > EGameFeaturePluginState::UnknownStatus)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::CheckingStatus);

			UGameFeaturesSubsystem::Get().OnGameFeatureCheckingStatus(StateProperties.PluginURL);
		}
	}
};

struct FGameFeaturePluginState_CheckingStatus : public FGameFeaturePluginState
{
	FGameFeaturePluginState_CheckingStatus(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	bool bParsedURL = false;
	bool bIsAvailable = false;

	virtual void BeginState() override
	{
		bParsedURL = false;
		bIsAvailable = false;
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (!bParsedURL)
		{
			bParsedURL = StateProperties.ParseURL();
			if (!bParsedURL)
			{
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorCheckingStatus, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Bad_PluginURL"));
				return;
			}
		}

		if (StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::File)
		{
			bIsAvailable = FPaths::FileExists(StateProperties.PluginInstalledFilename);
		}
		else if (StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::InstallBundle)
		{
			TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
			if (BundleManager == nullptr)
			{
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorCheckingStatus, UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Was_Null"));
				return;
			}

			if (BundleManager->GetInitState() == EInstallBundleManagerInitState::Failed)
			{
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorCheckingStatus, UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Failed_Init"));
				return;
			}

			if (BundleManager->GetInitState() == EInstallBundleManagerInitState::NotInitialized)
			{
				// Just wait for any pending init
				UpdateStateMachineDeferred(1.0f);
				return;
			}

			const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<FInstallBundlePluginProtocolMetaData>().InstallBundles;

			TValueOrError<FInstallBundleCombinedInstallState, EInstallBundleResult> MaybeInstallState = BundleManager->GetInstallStateSynchronous(InstallBundles, false);
			if (MaybeInstallState.HasError())
			{
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorCheckingStatus, UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Failed_GetInstallState"));
				return;
			}

			const FInstallBundleCombinedInstallState& InstallState = MaybeInstallState.GetValue();
			bIsAvailable = Algo::AllOf(InstallBundles, [&InstallState](FName BundleName) { return InstallState.IndividualBundleStates.Contains(BundleName); });
		}
		else
		{
			StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorCheckingStatus, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Unknown_Protocol"));
			return;
		}

		if (!bIsAvailable)
		{
			StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorUnavailable, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Plugin_Unavailable"));
			return;
		}

		StateStatus.SetTransition(EGameFeaturePluginState::StatusKnown);
	}
};

struct FGameFeaturePluginState_ErrorCheckingStatus : public FErrorGameFeaturePluginState
{
	FGameFeaturePluginState_ErrorCheckingStatus(FGameFeaturePluginStateMachineProperties& InStateProperties) : FErrorGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState < EGameFeaturePluginState::ErrorCheckingStatus)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Terminal);
		}
		else
		{
			StateStatus.SetTransition(EGameFeaturePluginState::CheckingStatus);
		}
	}
};

struct FGameFeaturePluginState_ErrorUnavailable : public FErrorGameFeaturePluginState
{
	FGameFeaturePluginState_ErrorUnavailable(FGameFeaturePluginStateMachineProperties& InStateProperties) : FErrorGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState < EGameFeaturePluginState::ErrorUnavailable)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Terminal);
		}
		else
		{
			StateStatus.SetTransition(EGameFeaturePluginState::CheckingStatus);
		}
	}
};

struct FGameFeaturePluginState_StatusKnown : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_StatusKnown(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState < EGameFeaturePluginState::StatusKnown)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Terminal);
		}
		else if (StateProperties.DestinationState > EGameFeaturePluginState::StatusKnown)
		{
			if (StateProperties.GetPluginProtocol() != EGameFeaturePluginProtocol::File)
			{
				StateStatus.SetTransition(EGameFeaturePluginState::Downloading);
			}
			else
			{
				StateStatus.SetTransition(EGameFeaturePluginState::Installed);
			}
		}
	}
};

struct FGameFeaturePluginState_ErrorInstalling : public FErrorGameFeaturePluginState
{
	FGameFeaturePluginState_ErrorInstalling(FGameFeaturePluginStateMachineProperties& InStateProperties) : FErrorGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState < EGameFeaturePluginState::ErrorInstalling)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Uninstalling);
		}
		else
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Downloading);
		}
	}
};

struct FGameFeaturePluginState_Uninstalling : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Uninstalling(FGameFeaturePluginStateMachineProperties& InStateProperties)
		: FGameFeaturePluginState(InStateProperties)
		, Result(MakeValue())
	{}

	UE::GameFeatures::FResult Result;
	bool bWasDeleted = false;
	TArray<FName> PendingBundles;

	void OnContentRemoved(FInstallBundleReleaseRequestResultInfo BundleResult)
	{
		if (!PendingBundles.Contains(BundleResult.BundleName))
		{
			return;
		}

		PendingBundles.Remove(BundleResult.BundleName);

		if (!Result.HasError() && BundleResult.Result != EInstallBundleReleaseResult::OK)
		{
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Uninstall_Failure_") + LexToString(BundleResult.Result));
		}

		if (PendingBundles.Num() > 0)
		{
			return;
		}

		if (Result.HasValue())
		{
			bWasDeleted = true;
		}

		UpdateStateMachineImmediate();
	}

	virtual void BeginState() override
	{
		Result = MakeValue();
		bWasDeleted = false;

		if (StateProperties.GetPluginProtocol() != EGameFeaturePluginProtocol::InstallBundle)
		{
			bWasDeleted = true;
			return;
		}

		TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();

		const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<FInstallBundlePluginProtocolMetaData>().InstallBundles;

		EInstallBundleReleaseRequestFlags ReleaseFlags = EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible;
		TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> MaybeRequestInfo = BundleManager->RequestReleaseContent(InstallBundles, ReleaseFlags);

		if (!MaybeRequestInfo.IsValid())
		{
			ensureMsgf(false, TEXT("Unable to enqueue uninstall for the PluginURL(%s) because %s"), *StateProperties.PluginURL, LexToString(MaybeRequestInfo.GetError()));
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Cannot_Uninstall"));
			return;
		}

		FInstallBundleRequestInfo RequestInfo = MaybeRequestInfo.StealValue();

		if (EnumHasAnyFlags(RequestInfo.InfoFlags, EInstallBundleRequestInfoFlags::SkippedUnknownBundles))
		{
			ensureMsgf(false, TEXT("Unable to enqueue uninstall for the PluginURL(%s) because failed to resolve install bundles!"), *StateProperties.PluginURL);
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Cannot_Resolve_InstallBundles_For_Release"));
			return;
		}

		if (RequestInfo.BundlesEnqueued.Num() == 0)
		{
			bWasDeleted = true;
		}
		else
		{
			PendingBundles = MoveTemp(RequestInfo.BundlesEnqueued);
			IInstallBundleManager::ReleasedDelegate.AddRaw(this, &FGameFeaturePluginState_Uninstalling::OnContentRemoved);
		}
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (!Result.HasValue())
		{
			StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorInstalling, Result.StealError());
			return;
		}

		if (!bWasDeleted)
		{
			return;
		}

		StateStatus.SetTransition(EGameFeaturePluginState::StatusKnown);
	}

	virtual void EndState() override
	{
		IInstallBundleManager::ReleasedDelegate.RemoveAll(this);
	}
};

struct FGameFeaturePluginState_Downloading : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Downloading(FGameFeaturePluginStateMachineProperties& InStateProperties)
		: FGameFeaturePluginState(InStateProperties)
		, Result(MakeValue())
	{}

	~FGameFeaturePluginState_Downloading()
	{
		Cleanup();
	}

	UE::GameFeatures::FResult Result;
	bool bPluginDownloaded = false;
	TArray<FName> PendingBundleDownloads;
	TUniquePtr<FInstallBundleCombinedProgressTracker> ProgressTracker;
	FTSTicker::FDelegateHandle ProgressUpdateHandle;
	FDelegateHandle GotContentStateHandle;

	void Cleanup()
	{
		if (ProgressUpdateHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(ProgressUpdateHandle);
			ProgressUpdateHandle.Reset();
		}

		if (GotContentStateHandle.IsValid())
		{
			TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
			if (BundleManager)
			{
				BundleManager->CancelAllGetContentStateRequests(GotContentStateHandle);
			}
			GotContentStateHandle.Reset();
		}

		IInstallBundleManager::InstallBundleCompleteDelegate.RemoveAll(this);

		Result = MakeValue();
		bPluginDownloaded = false;
		PendingBundleDownloads.Empty();
		ProgressTracker = nullptr;
	}

	void OnGotContentState(FInstallBundleCombinedContentState BundleContentState)
	{
		GotContentStateHandle.Reset();

		TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
		const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<FInstallBundlePluginProtocolMetaData>().InstallBundles;

		EInstallBundleRequestFlags InstallFlags = EInstallBundleRequestFlags::None;
		InstallFlags |= EInstallBundleRequestFlags::SkipMount;
		TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> MaybeRequestInfo = BundleManager->RequestUpdateContent(InstallBundles, InstallFlags);

		if (!MaybeRequestInfo.IsValid())
		{
			ensureMsgf(false, TEXT("Unable to enqueue download for the PluginURL(%s) because %s"), *StateProperties.PluginURL, LexToString(MaybeRequestInfo.GetError()));
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Cannot_Start_Download"));
			UpdateStateMachineImmediate();
			return;
		}

		FInstallBundleRequestInfo RequestInfo = MaybeRequestInfo.StealValue();

		if (EnumHasAnyFlags(RequestInfo.InfoFlags, EInstallBundleRequestInfoFlags::SkippedUnknownBundles))
		{
			ensureMsgf(false, TEXT("Unable to enqueue download for the PluginURL(%s) because failed to resolve install bundles!"), *StateProperties.PluginURL);
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Cannot_Resolve_InstallBundles_For_Download"));
			UpdateStateMachineImmediate();
			return;
		}

		if (RequestInfo.BundlesEnqueued.Num() == 0)
		{
			bPluginDownloaded = true;
			UpdateProgress(1.0f);
			UpdateStateMachineImmediate();
		}
		else
		{
			PendingBundleDownloads = MoveTemp(RequestInfo.BundlesEnqueued);
			IInstallBundleManager::InstallBundleCompleteDelegate.AddRaw(this, &FGameFeaturePluginState_Downloading::OnInstallBundleCompleted);

			ProgressTracker = MakeUnique<FInstallBundleCombinedProgressTracker>(false);
			ProgressTracker->SetBundlesToTrackFromContentState(BundleContentState, PendingBundleDownloads);

			ProgressUpdateHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateRaw(this, &FGameFeaturePluginState_Downloading::OnUpdateProgress)/*, 0.1f*/);
		}
	}

	void OnInstallBundleCompleted(FInstallBundleRequestResultInfo BundleResult)
	{
		if (!PendingBundleDownloads.Contains(BundleResult.BundleName))
		{
			return;
		}

		PendingBundleDownloads.Remove(BundleResult.BundleName);

		if (!Result.HasError() && BundleResult.Result != EInstallBundleResult::OK)
		{
			if (BundleResult.OptionalErrorCode.IsEmpty())
			{
				Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Download_Failure_") + LexToString(BundleResult.Result));
			}
			else
			{
				Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Download_Failure_") + BundleResult.OptionalErrorCode);
			}
		}

		if (PendingBundleDownloads.Num() > 0)
		{
			return;
		}

		if (Result.HasValue())
		{
			bPluginDownloaded = true;
		}

		OnUpdateProgress(0.0f);

		UpdateStateMachineImmediate();
	}

	bool OnUpdateProgress(float dts)
	{
		if (ProgressTracker)
		{
			ProgressTracker->ForceTick();

			float Progress = ProgressTracker->GetCurrentCombinedProgress().ProgressPercent;
			UpdateProgress(Progress);

			//UE_LOG(LogGameFeatures, Display, TEXT("Download Progress: %f for PluginURL(%s)"), Progress, *StateProperties.PluginURL);
		}

		return true;
	}

	virtual void BeginState() override
	{
		Cleanup();

		check(StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::InstallBundle);

		TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
		const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<FInstallBundlePluginProtocolMetaData>().InstallBundles;

		GotContentStateHandle = BundleManager->GetContentState(InstallBundles, EInstallBundleGetContentStateFlags::None, true, FInstallBundleGetContentStateDelegate::CreateRaw(this, &FGameFeaturePluginState_Downloading::OnGotContentState));
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (!Result.HasValue())
		{
			StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorInstalling, Result.StealError());
			return;
		}

		if (!bPluginDownloaded)
		{
			return;
		}

		StateStatus.SetTransition(EGameFeaturePluginState::Installed);
	}

	virtual void EndState() override
	{
		Cleanup();
	}
};

struct FGameFeaturePluginState_Installed : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_Installed(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState > EGameFeaturePluginState::Installed)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Mounting);
		}
		else if (StateProperties.DestinationState < EGameFeaturePluginState::Installed)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Uninstalling);
		}
	}
};

struct FGameFeaturePluginState_ErrorMounting : public FErrorGameFeaturePluginState
{
	FGameFeaturePluginState_ErrorMounting(FGameFeaturePluginStateMachineProperties& InStateProperties) : FErrorGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState < EGameFeaturePluginState::ErrorMounting)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Unmounting);
		}
		else
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Mounting);
		}
	}
};

struct FGameFeaturePluginState_ErrorWaitingForDependencies : public FErrorGameFeaturePluginState
{
	FGameFeaturePluginState_ErrorWaitingForDependencies(FGameFeaturePluginStateMachineProperties& InStateProperties) : FErrorGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState < EGameFeaturePluginState::ErrorWaitingForDependencies)
		{
			// There is no cleaup state equivalent to EGameFeaturePluginState::WaitingForDependencies so just go back to unmounting
			StateStatus.SetTransition(EGameFeaturePluginState::Unmounting);
		}
		else
		{
			StateStatus.SetTransition(EGameFeaturePluginState::WaitingForDependencies);
		}
	}
};

struct FGameFeaturePluginState_ErrorRegistering : public FErrorGameFeaturePluginState
{
	FGameFeaturePluginState_ErrorRegistering(FGameFeaturePluginStateMachineProperties& InStateProperties) : FErrorGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState < EGameFeaturePluginState::ErrorRegistering)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Unregistering);
		}
		else
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Registering);
		}
	}
};

struct FGameFeaturePluginState_Unmounting : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Unmounting(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	UE::GameFeatures::FResult Result{ MakeValue() };
	TArray<FName> PendingBundles;
	bool bUnmounted = false;

	void OnContentReleased(FInstallBundleReleaseRequestResultInfo BundleResult)
	{
		if (!PendingBundles.Contains(BundleResult.BundleName))
		{
			return;
		}

		PendingBundles.Remove(BundleResult.BundleName);

		if (!Result.HasError() && BundleResult.Result != EInstallBundleReleaseResult::OK)
		{
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Unmount_Error_") + LexToString(BundleResult.Result));
		}

		if (PendingBundles.Num() > 0)
		{
			return;
		}

		if (Result.HasValue())
		{
			bUnmounted = true;
		}

		UpdateStateMachineImmediate();
	}

	virtual void BeginState() override
	{
		Result = MakeValue();
		PendingBundles.Empty();
		bUnmounted = false;

		const FString PluginName = FPaths::GetBaseFilename(StateProperties.PluginInstalledFilename);
		if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName))
		{
			IPluginManager::Get().UnmountExplicitlyLoadedPlugin(PluginName, nullptr);
		}

		if (StateProperties.GetPluginProtocol() != EGameFeaturePluginProtocol::InstallBundle)
		{
			bUnmounted = true;
			return;
		}

		TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();

		const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<FInstallBundlePluginProtocolMetaData>().InstallBundles;

		EInstallBundleReleaseRequestFlags ReleaseFlags = EInstallBundleReleaseRequestFlags::None;
		TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> MaybeRequestInfo = BundleManager->RequestReleaseContent(InstallBundles, ReleaseFlags);

		if (!MaybeRequestInfo.IsValid())
		{
			ensureMsgf(false, TEXT("Unable to enqueue unmount for the PluginURL(%s) because %s"), *StateProperties.PluginURL, LexToString(MaybeRequestInfo.GetError()));
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Cannot_Start_Unmount"));
			return;
		}

		FInstallBundleRequestInfo RequestInfo = MaybeRequestInfo.StealValue();

		if (EnumHasAnyFlags(RequestInfo.InfoFlags, EInstallBundleRequestInfoFlags::SkippedUnknownBundles))
		{
			ensureMsgf(false, TEXT("Unable to enqueue unmount for the PluginURL(%s) because failed to resolve install bundles!"), *StateProperties.PluginURL);
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Cannot_Resolve_InstallBundles_For_Unmount"));
			return;
		}

		if (RequestInfo.BundlesEnqueued.Num() == 0)
		{
			bUnmounted = true;
		}
		else
		{
			PendingBundles = MoveTemp(RequestInfo.BundlesEnqueued);
			IInstallBundleManager::ReleasedDelegate.AddRaw(this, &FGameFeaturePluginState_Unmounting::OnContentReleased);
		}
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (!Result.HasValue())
		{
			StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorMounting, Result.StealError());
			return;
		}

		if (!bUnmounted)
		{
			return;
		}

		StateStatus.SetTransition(EGameFeaturePluginState::Installed);
	}

	virtual void EndState() override
	{
		IInstallBundleManager::ReleasedDelegate.RemoveAll(this);
	}
};

struct FGameFeaturePluginState_Mounting : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Mounting(FGameFeaturePluginStateMachineProperties& InStateProperties)
		: FGameFeaturePluginState(InStateProperties)
		, Result(MakeValue())
	{}

	UE::GameFeatures::FResult Result;
	TArray<FName> PendingBundles;
	bool bMounted = false;

	void OnInstallBundleCompleted(FInstallBundleRequestResultInfo BundleResult)
	{
		if (!PendingBundles.Contains(BundleResult.BundleName))
		{
			return;
		}

		PendingBundles.Remove(BundleResult.BundleName);

		if (!Result.HasError() && BundleResult.Result != EInstallBundleResult::OK)
		{
			if (BundleResult.OptionalErrorCode.IsEmpty())
			{
				Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Mount_Error_") + LexToString(BundleResult.Result));
			}
			else
			{
				Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Mount_Error_") + BundleResult.OptionalErrorCode);
			}
		}

		if (PendingBundles.Num() > 0)
		{
			return;
		}

		if (Result.HasValue())
		{
			bMounted = true;
		}

		UpdateStateMachineImmediate();
	}

	void OnPakFileMounted(const IPakFile& PakFile)
	{
		if (FPakFile* Pak = (FPakFile*)(&PakFile))
		{
			UE_LOG(LogGameFeatures, Display, TEXT("Mounted Pak File for (%s) with following files:"), *StateProperties.PluginURL);
			TArray<FString> OutFileList;
			Pak->GetPrunedFilenames(OutFileList);
			for (const FString& FileName : OutFileList)
			{
				UE_LOG(LogGameFeatures, Display, TEXT("(%s)"), *FileName);
			}
		}
	}

	virtual void BeginState() override
	{
		Result = MakeValue();
		PendingBundles.Empty();
		bMounted = false;

		if (StateProperties.GetPluginProtocol() != EGameFeaturePluginProtocol::InstallBundle)
		{
			bMounted = true;
			return;
		}
		
		TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();

		const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<FInstallBundlePluginProtocolMetaData>().InstallBundles;
		
		// JMarcus TODO: Async Mounting?
		EInstallBundleRequestFlags InstallFlags = EInstallBundleRequestFlags::None;

		// Make bundle manager use verbose log level for most logs.
		// We are already done with downloading, so we don't care about logging too much here unless mounting fails.
		const ELogVerbosity::Type InstallBundleManagerVerbosityOverride = ELogVerbosity::Verbose;
		TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> MaybeRequestInfo = BundleManager->RequestUpdateContent(InstallBundles, InstallFlags, InstallBundleManagerVerbosityOverride);

		if (!MaybeRequestInfo.IsValid())
		{
			ensureMsgf(false, TEXT("Unable to enqueue mount for the PluginURL(%s) because %s"), *StateProperties.PluginURL, LexToString(MaybeRequestInfo.GetError()));
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Cannot_Start_Mount"));
			return;
		}

		FInstallBundleRequestInfo RequestInfo = MaybeRequestInfo.StealValue();

		if (EnumHasAnyFlags(RequestInfo.InfoFlags, EInstallBundleRequestInfoFlags::SkippedUnknownBundles))
		{
			ensureMsgf(false, TEXT("Unable to enqueue mount for the PluginURL(%s) because failed to resolve install bundles!"), *StateProperties.PluginURL);
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Cannot_Resolve_InstallBundles_For_Mount"));
			return;
		}

		if (RequestInfo.BundlesEnqueued.Num() == 0)
		{
			bMounted = true;
		}
		else
		{
			PendingBundles = MoveTemp(RequestInfo.BundlesEnqueued);
			IInstallBundleManager::InstallBundleCompleteDelegate.AddRaw(this, &FGameFeaturePluginState_Mounting::OnInstallBundleCompleted);
			if (UE::GameFeatures::ShouldLogMountedFiles)
			{
				FCoreDelegates::OnPakFileMounted2.AddRaw(this, &FGameFeaturePluginState_Mounting::OnPakFileMounted);
			}
		}
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (!Result.HasValue())
		{
			StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorMounting, Result.StealError());
			return;
		}
		if (!bMounted)
		{
			return;
		}

		checkf(!StateProperties.PluginInstalledFilename.IsEmpty(), TEXT("PluginInstalledFilename must be set by the Mounting. PluginURL: %s"), *StateProperties.PluginURL);
		checkf(FPaths::GetExtension(StateProperties.PluginInstalledFilename) == TEXT("uplugin"), TEXT("PluginInstalledFilename must have a uplugin extension. PluginURL: %s"), *StateProperties.PluginURL);

		// refresh the plugins list to let the plugin manager know about it
		const bool bAddedPlugin = IPluginManager::Get().AddToPluginsList(StateProperties.PluginInstalledFilename);
		if (!bAddedPlugin)
		{
			StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorMounting, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Failed_To_Register_Plugin"));
			return;
		}
		
		const FString PluginName = FPaths::GetBaseFilename(StateProperties.PluginInstalledFilename);
		IPluginManager::Get().MountExplicitlyLoadedPlugin(PluginName);

		// After the new plugin is mounted add the asset registry for that plugin.
		if (StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::InstallBundle)
		{
			TSharedPtr<IPlugin> NewlyMountedPlugin = IPluginManager::Get().FindPlugin(PluginName);
			if (NewlyMountedPlugin.IsValid() && NewlyMountedPlugin->CanContainContent())
			{
				TArray<uint8> SerializedAssetData;
				const FString PluginFolder = FPaths::GetPath(StateProperties.PluginInstalledFilename);
				const FString PluginAssetRegistry = PluginFolder / TEXT("AssetRegistry.bin");
				if (!ensure(IFileManager::Get().FileExists(*PluginAssetRegistry)))
				{
					StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorMounting, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Plugin_AssetRegistry_Not_Found"));
					return;
				}

				if (!FFileHelper::LoadFileToArray(SerializedAssetData, *PluginAssetRegistry))
				{
					StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorMounting, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Failed_To_Load_Plugin_AssetRegistry"));
					return;
				}

				FAssetRegistryState PluginAssetRegistryState;
				FMemoryReader Ar(SerializedAssetData);
				PluginAssetRegistryState.Load(Ar);

				IAssetRegistry& AssetRegistry = UAssetManager::Get().GetAssetRegistry();
				AssetRegistry.AppendState(PluginAssetRegistryState);
			}
		}

		StateStatus.SetTransition(EGameFeaturePluginState::WaitingForDependencies);
	}

	virtual void EndState() override
	{
		IInstallBundleManager::InstallBundleCompleteDelegate.RemoveAll(this);
		FCoreDelegates::OnPakFileMounted2.RemoveAll(this);
	}
};

struct FGameFeaturePluginState_WaitingForDependencies : public FGameFeaturePluginState
{
	FGameFeaturePluginState_WaitingForDependencies(FGameFeaturePluginStateMachineProperties& InStateProperties)
		: FGameFeaturePluginState(InStateProperties)
		, bRequestedDependencies(false)
	{}

	virtual ~FGameFeaturePluginState_WaitingForDependencies()
	{
		ClearDependencies();
	}

	virtual void BeginState() override
	{
		ClearDependencies();
	}

	virtual void EndState() override
	{
		ClearDependencies();
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		checkf(!StateProperties.PluginInstalledFilename.IsEmpty(), TEXT("PluginInstalledFilename must be set by the loading dependencies phase. PluginURL: %s"), *StateProperties.PluginURL);
		checkf(FPaths::GetExtension(StateProperties.PluginInstalledFilename) == TEXT("uplugin"), TEXT("PluginInstalledFilename must have a uplugin extension. PluginURL: %s"), *StateProperties.PluginURL);

		if (!bRequestedDependencies)
		{
			TArray<UGameFeaturePluginStateMachine*> Dependencies;
			check(StateProperties.OnRequestStateMachineDependencies.IsBound());
			if (StateProperties.OnRequestStateMachineDependencies.Execute(StateProperties.PluginInstalledFilename, Dependencies))
			{
				bRequestedDependencies = true;
				for (UGameFeaturePluginStateMachine* Dependency : Dependencies)
				{
					check(Dependency);
					if (Dependency->GetCurrentState() < EGameFeaturePluginState::Registered)
					{
						RemainingDependencies.Add(Dependency);
						Dependency->OnStateChanged().AddRaw(this, &FGameFeaturePluginState_WaitingForDependencies::OnDependencyStateChanged);

						// If we are not alreadying loading this dependency, do so now
						if (Dependency->GetDestinationState() < EGameFeaturePluginState::Registered)
						{
							Dependency->SetDestinationState(EGameFeaturePluginState::Registered, FGameFeatureStateTransitionComplete());
						}
					}
				}
			}
			else
			{
				// Failed to query dependencies
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorWaitingForDependencies, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Failed_Dependency_Query"));
				return;
			}
		}

		for (int32 DepIdx = RemainingDependencies.Num() - 1; DepIdx >= 0; --DepIdx)
		{
			UGameFeaturePluginStateMachine* RemainingDependency = RemainingDependencies[DepIdx].Get();
			if (!RemainingDependency)
			{
				// One of the dependency state machines was destroyed before finishing
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorWaitingForDependencies, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Dependency_Destroyed_Before_Finish"));
				return;
			}
			else if (RemainingDependency->GetCurrentState() >= EGameFeaturePluginState::Registered)
			{
				RemainingDependency->OnStateChanged().RemoveAll(this);
				RemainingDependencies.RemoveAt(DepIdx, 1, false);
			}
			else if (RemainingDependency->GetCurrentState() == RemainingDependency->GetDestinationState())
			{
				// The dependency is no longer transitioning and is not Registered or later, so it failed to register, thus we cannot proceed
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorWaitingForDependencies, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Failed_Dependency_Register"));
			}
		}

		if (RemainingDependencies.Num() == 0)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Registering);
		}
	}

	void OnDependencyStateChanged(UGameFeaturePluginStateMachine* Dependency)
	{
		if (RemainingDependencies.Contains(Dependency))
		{
			UpdateStateMachineImmediate();
		}
	}

	void ClearDependencies()
	{
		for (const TWeakObjectPtr<UGameFeaturePluginStateMachine>& WeakDependency : RemainingDependencies)
		{
			if (UGameFeaturePluginStateMachine* Dependency = WeakDependency.Get())
			{
				Dependency->OnStateChanged().RemoveAll(this);
			}
		}
		RemainingDependencies.Empty();
		bRequestedDependencies = false;
	}

	TArray<TWeakObjectPtr<UGameFeaturePluginStateMachine>> RemainingDependencies;
	bool bRequestedDependencies;
};

struct FGameFeaturePluginState_Unregistering : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Unregistering(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.GameFeatureData)
		{
			const FString PluginName = FPaths::GetBaseFilename(StateProperties.PluginInstalledFilename);
			UGameFeaturesSubsystem::Get().OnGameFeatureUnregistering(StateProperties.GameFeatureData, PluginName);
			UGameFeaturesSubsystem::Get().UnloadGameFeatureData(StateProperties.GameFeatureData);
		}

		StateProperties.GameFeatureData = nullptr;

		// @todo GC, then make sure all loaded content is out of memory
		GEngine->ForceGarbageCollection(true); // this is tick delayed

		StateStatus.SetTransition(EGameFeaturePluginState::Unmounting);
	}
};

struct FGameFeaturePluginState_Registering : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Registering(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		const FString PluginName = FPaths::GetBaseFilename(StateProperties.PluginInstalledFilename);
		const FString PluginFolder = FPaths::GetPath(StateProperties.PluginInstalledFilename);
		UGameplayTagsManager::Get().AddTagIniSearchPath(PluginFolder / TEXT("Config") / TEXT("Tags"));

		const FString PreferredGameFeatureDataPath = FString::Printf(TEXT("/%s/%s.%s"), *PluginName, *PluginName, *PluginName);

		FString BackupGameFeatureDataPath = TEXT("/") + PluginName + TEXT("/GameFeatureData.GameFeatureData");
		// Allow game feature location to be overriden globally and from within the plugin
		FString OverrideIniPathName = PluginName + TEXT("_Override");
		FString OverridePath = GConfig->GetStr(TEXT("GameFeatureData"), *OverrideIniPathName, GGameIni);
		if (OverridePath.IsEmpty())
		{
			const FString SettingsOverride = PluginFolder / TEXT("Config") / TEXT("Settings.ini");
			if (FPaths::FileExists(SettingsOverride))
			{
				GConfig->LoadFile(SettingsOverride);
				OverridePath = GConfig->GetStr(TEXT("GameFeatureData"), TEXT("Override"), SettingsOverride);
				GConfig->UnloadFile(SettingsOverride);
			}
		}
		if (!OverridePath.IsEmpty())
		{
			BackupGameFeatureDataPath = OverridePath;
		}
		
		TSharedPtr<FStreamableHandle> GameFeatureDataHandle = UGameFeaturesSubsystem::LoadGameFeatureData(PreferredGameFeatureDataPath);
		if (!GameFeatureDataHandle.IsValid())
		{
			GameFeatureDataHandle = UGameFeaturesSubsystem::LoadGameFeatureData(BackupGameFeatureDataPath);
		}

		// @todo make this async. For now we just wait
		if (GameFeatureDataHandle.IsValid())
		{
			GameFeatureDataHandle->WaitUntilComplete(0.0f, false);
			StateProperties.GameFeatureData = Cast<UGameFeatureData>(GameFeatureDataHandle->GetLoadedAsset());
		}

		if (StateProperties.GameFeatureData)
		{
			StateProperties.PluginName = PluginName;
			StateProperties.GameFeatureData->InitializeBasePluginIniFile(StateProperties.PluginInstalledFilename);
			StateStatus.SetTransition(EGameFeaturePluginState::Registered);

			UGameFeaturesSubsystem::Get().OnGameFeatureRegistering(StateProperties.GameFeatureData, PluginName);
		}
		else
		{
			// The gamefeaturedata does not exist. The pak file may not be openable or this is a builtin plugin where the pak file does not exist.
			StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorRegistering, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Plugin_Missing_GameFeatureData"));
		}
	}
};

struct FGameFeaturePluginState_Registered : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_Registered(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState > EGameFeaturePluginState::Registered)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Loading);
		}
		else if (StateProperties.DestinationState < EGameFeaturePluginState::Registered)
		{
			StateStatus.SetTransition( EGameFeaturePluginState::Unregistering);
		}
	}
};

struct FGameFeaturePluginState_Unloading : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Unloading(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		// @todo Unload everything that was loaded in Loading, then collect garbage the next frame (since this state may be traversed mid-world tick)
		//CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		StateStatus.SetTransition(EGameFeaturePluginState::Registered);
	}
};

struct FGameFeaturePluginState_Loading : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Loading(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		check(StateProperties.GameFeatureData);

		// AssetManager
		TSharedPtr<FStreamableHandle> BundleHandle = LoadGameFeatureBundles(StateProperties.GameFeatureData);
		// @todo make this async. For now we just wait
		if (BundleHandle.IsValid())
		{
			BundleHandle->WaitUntilComplete(0.0f, false);
		}

		UGameFeaturesSubsystem::Get().OnGameFeatureLoading(StateProperties.GameFeatureData);

		StateStatus.SetTransition(EGameFeaturePluginState::Loaded);
	}

	/** Loads primary assets and bundles for the specified game feature */
	TSharedPtr<FStreamableHandle> LoadGameFeatureBundles(const UGameFeatureData* GameFeatureToLoad)
	{
		check(GameFeatureToLoad);

		TArray<FPrimaryAssetId> AssetIdsToLoad;
		FPrimaryAssetId GameFeatureAssetId = GameFeatureToLoad->GetPrimaryAssetId();
		if (GameFeatureAssetId.IsValid())
		{
			AssetIdsToLoad.Add(GameFeatureAssetId);
		}

		UGameFeaturesProjectPolicies& Policy = UGameFeaturesSubsystem::Get().GetPolicy<UGameFeaturesProjectPolicies>();
		AssetIdsToLoad.Append(Policy.GetPreloadAssetListForGameFeature(GameFeatureToLoad));

		TSharedPtr<FStreamableHandle> RetHandle;
		if (AssetIdsToLoad.Num() > 0)
		{
			RetHandle = UAssetManager::Get().LoadPrimaryAssets(AssetIdsToLoad, Policy.GetPreloadBundleStateForGameFeature());
		}

		return RetHandle;
	}
};

struct FGameFeaturePluginState_Loaded : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_Loaded(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState > EGameFeaturePluginState::Loaded)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Activating);
		}
		else if (StateProperties.DestinationState < EGameFeaturePluginState::Loaded)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Unloading);
		}
	}
};

struct FGameFeaturePluginState_Deactivating : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Deactivating(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	int32 NumObservedPausers = 0;
	int32 NumExpectedPausers = 0;
	bool bInProcessOfDeactivating = false;

	virtual void BeginState() override
	{
		NumObservedPausers = 0;
		NumExpectedPausers = 0;
		bInProcessOfDeactivating = false;
	}

	void OnPauserCompleted()
	{
		check(IsInGameThread());
		++NumObservedPausers;

		if (NumObservedPausers == NumExpectedPausers)
		{
			UpdateStateMachineImmediate();
		}
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (!bInProcessOfDeactivating)
		{
			// Make sure we won't complete the transition prematurely if someone registers as a pauser but fires immediately
			bInProcessOfDeactivating = true;
			NumExpectedPausers = INDEX_NONE;
			NumObservedPausers = 0;

			// Deactivate
			FGameFeatureDeactivatingContext Context(FSimpleDelegate::CreateRaw(this, &FGameFeaturePluginState_Deactivating::OnPauserCompleted));
			const FString PluginName = FPaths::GetBaseFilename(StateProperties.PluginInstalledFilename);
			UGameFeaturesSubsystem::Get().OnGameFeatureDeactivating(StateProperties.GameFeatureData, PluginName, Context);
			NumExpectedPausers = Context.NumPausers;
		}

		if (NumExpectedPausers == NumObservedPausers)
		{
			GEngine->ForceGarbageCollection(true);
			StateStatus.SetTransition(EGameFeaturePluginState::Loaded);
			bInProcessOfDeactivating = false;
		}
		else
		{
			UE_LOG(LogGameFeatures, Log, TEXT("Game feature %s deactivation paused until %d observer tasks complete their deactivation"), *GetPathNameSafe(StateProperties.GameFeatureData), NumExpectedPausers - NumObservedPausers);
		}
	}
};

struct FGameFeaturePluginState_Activating : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Activating(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		check(GEngine);
		check(StateProperties.GameFeatureData);

		FGameFeatureActivatingContext Context;

		StateProperties.GameFeatureData->InitializeHierarchicalPluginIniFiles(StateProperties.PluginInstalledFilename);

		const FString PluginName = FPaths::GetBaseFilename(StateProperties.PluginInstalledFilename);
		UGameFeaturesSubsystem::Get().OnGameFeatureActivating(StateProperties.GameFeatureData, PluginName, Context);

		StateStatus.SetTransition(EGameFeaturePluginState::Active);
	}
};

struct FGameFeaturePluginState_Active : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_Active(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.DestinationState < EGameFeaturePluginState::Active)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Deactivating);
		}
	}
};

/*
=========================================================
  State Machine
=========================================================
*/

UGameFeaturePluginStateMachine::UGameFeaturePluginStateMachine(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CurrentStateInfo(EGameFeaturePluginState::Uninitialized)
	, bInUpdateStateMachine(false)
{

}

void UGameFeaturePluginStateMachine::InitStateMachine(const FString& InPluginURL, const FGameFeaturePluginRequestStateMachineDependencies& OnRequestStateMachineDependencies)
{
	check(GetCurrentState() == EGameFeaturePluginState::Uninitialized);
	CurrentStateInfo.State = EGameFeaturePluginState::UnknownStatus;
	StateProperties = FGameFeaturePluginStateMachineProperties(
		InPluginURL,
		CurrentStateInfo.State,
		OnRequestStateMachineDependencies,
		FGameFeaturePluginRequestUpdateStateMachine::CreateUObject(this, &ThisClass::UpdateStateMachine),
		FGameFeatureStateProgressUpdate::CreateUObject(this, &ThisClass::UpdateCurrentStateProgress));

	AllStates[(int32)EGameFeaturePluginState::Uninitialized] = MakeUnique<FGameFeaturePluginState_Uninitialized>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Terminal] = MakeUnique<FGameFeaturePluginState_Terminal>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::UnknownStatus] = MakeUnique<FGameFeaturePluginState_UnknownStatus>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::CheckingStatus] = MakeUnique<FGameFeaturePluginState_CheckingStatus>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::ErrorCheckingStatus] = MakeUnique<FGameFeaturePluginState_ErrorCheckingStatus>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::ErrorUnavailable] = MakeUnique<FGameFeaturePluginState_ErrorUnavailable>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::StatusKnown] = MakeUnique<FGameFeaturePluginState_StatusKnown>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::ErrorInstalling] = MakeUnique<FGameFeaturePluginState_ErrorInstalling>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Uninstalling] = MakeUnique<FGameFeaturePluginState_Uninstalling>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Downloading] = MakeUnique<FGameFeaturePluginState_Downloading>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Installed] = MakeUnique<FGameFeaturePluginState_Installed>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::ErrorMounting] = MakeUnique<FGameFeaturePluginState_ErrorMounting>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::ErrorWaitingForDependencies] = MakeUnique<FGameFeaturePluginState_ErrorWaitingForDependencies>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::ErrorRegistering] = MakeUnique<FGameFeaturePluginState_ErrorRegistering>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Unmounting] = MakeUnique<FGameFeaturePluginState_Unmounting>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Mounting] = MakeUnique<FGameFeaturePluginState_Mounting>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::WaitingForDependencies] = MakeUnique<FGameFeaturePluginState_WaitingForDependencies>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Unregistering] = MakeUnique<FGameFeaturePluginState_Unregistering>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Registering] = MakeUnique<FGameFeaturePluginState_Registering>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Registered] = MakeUnique<FGameFeaturePluginState_Registered>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Unloading] = MakeUnique<FGameFeaturePluginState_Unloading>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Loading] = MakeUnique<FGameFeaturePluginState_Loading>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Loaded] = MakeUnique<FGameFeaturePluginState_Loaded>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Deactivating] = MakeUnique<FGameFeaturePluginState_Deactivating>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Activating] = MakeUnique<FGameFeaturePluginState_Activating>(StateProperties);
	AllStates[(int32)EGameFeaturePluginState::Active] = MakeUnique<FGameFeaturePluginState_Active>(StateProperties);

	static_assert((int32)EGameFeaturePluginState::MAX == 26, "");

	AllStates[(int32)CurrentStateInfo.State]->BeginState();
}

void UGameFeaturePluginStateMachine::SetDestinationState(EGameFeaturePluginState InDestinationState, FGameFeatureStateTransitionComplete OnFeatureStateTransitionComplete)
{
	check(IsValidDestinationState(InDestinationState));

	// JMarcus TODO: If we aren't in a destination state and our new destination is in the opposite direction of 
	// our current destination, cancel the current state transition (if possible)
	// The completion delegate may be stomped in these cases.  Should probably callback with a cancelled error

	StateProperties.DestinationState = InDestinationState;
	StateProperties.OnFeatureStateTransitionComplete = OnFeatureStateTransitionComplete;

	UpdateStateMachine();
}

FString UGameFeaturePluginStateMachine::GetGameFeatureName() const
{
	FString PluginFilename;
	if (GetPluginFilename(PluginFilename))
	{
		return FPaths::GetBaseFilename(PluginFilename);
	}
	else
	{
		return StateProperties.PluginURL;
	}
}

FString UGameFeaturePluginStateMachine::GetPluginURL() const
{
	return StateProperties.PluginURL;
}

FString UGameFeaturePluginStateMachine::GetPluginName() const
{
	return StateProperties.PluginName;
}

bool UGameFeaturePluginStateMachine::GetPluginFilename(FString& OutPluginFilename) const
{
	OutPluginFilename = StateProperties.PluginInstalledFilename;
	return !OutPluginFilename.IsEmpty();
}

EGameFeaturePluginState UGameFeaturePluginStateMachine::GetCurrentState() const
{
	return GetCurrentStateInfo().State;
}

EGameFeaturePluginState UGameFeaturePluginStateMachine::GetDestinationState() const
{
	return StateProperties.DestinationState;
}

const FGameFeaturePluginStateInfo& UGameFeaturePluginStateMachine::GetCurrentStateInfo() const
{
	return CurrentStateInfo;
}

bool UGameFeaturePluginStateMachine::IsStatusKnown() const
{
	return GetCurrentState() == EGameFeaturePluginState::ErrorUnavailable || GetCurrentState() >= EGameFeaturePluginState::StatusKnown;
}

bool UGameFeaturePluginStateMachine::IsAvailable() const
{
	ensure(IsStatusKnown());
	return GetCurrentState() >= EGameFeaturePluginState::StatusKnown;
}

UGameFeatureData* UGameFeaturePluginStateMachine::GetGameFeatureDataForActivePlugin()
{
	if (GetCurrentState() == EGameFeaturePluginState::Active)
	{
		return StateProperties.GameFeatureData;
	}

	return nullptr;
}

UGameFeatureData* UGameFeaturePluginStateMachine::GetGameFeatureDataForRegisteredPlugin()
{
	if (GetCurrentState() >= EGameFeaturePluginState::Registered)
	{
		return StateProperties.GameFeatureData;
	}

	return nullptr;
}

bool UGameFeaturePluginStateMachine::IsValidTransitionState(EGameFeaturePluginState InState) const
{
	check(InState != EGameFeaturePluginState::MAX);
	return AllStates[(int32)InState]->GetStateType() == EGameFeaturePluginStateType::Transition;
}

bool UGameFeaturePluginStateMachine::IsValidDestinationState(EGameFeaturePluginState InDestinationState) const
{
	check(InDestinationState != EGameFeaturePluginState::MAX);
	return AllStates[(int32)InDestinationState]->GetStateType() == EGameFeaturePluginStateType::Destination;
}

bool UGameFeaturePluginStateMachine::IsValidErrorState(EGameFeaturePluginState InDestinationState) const
{
	check(InDestinationState != EGameFeaturePluginState::MAX);
	return AllStates[(int32)InDestinationState]->GetStateType() == EGameFeaturePluginStateType::Error;
}

void UGameFeaturePluginStateMachine::UpdateStateMachine()
{
	EGameFeaturePluginState CurrentState = GetCurrentState();
	if (bInUpdateStateMachine)
	{
		UE_LOG(LogGameFeatures, Verbose, TEXT("Game feature state machine skipping update for %s in ::UpdateStateMachine. Current State: %s"), *GetGameFeatureName(), *UE::GameFeatures::ToString(CurrentState));
		return;
	}

	TGuardValue<bool> ScopeGuard(bInUpdateStateMachine, true);

	UE::GameFeatures::FResult TransitionResult(MakeValue());
	bool bKeepProcessing = false;
	int32 NumTransitions = 0;
	const int32 MaxTransitions = 10000;
	do
	{
		bKeepProcessing = false;

		FGameFeaturePluginStateStatus StateStatus;
		AllStates[(int32)CurrentState]->UpdateState(StateStatus);

		TransitionResult = StateStatus.TransitionResult;

		if (StateStatus.TransitionToState == CurrentState)
		{
			UE_LOG(LogGameFeatures, Fatal, TEXT("Game feature state %s transitioning to itself. GameFeature: %s"), *UE::GameFeatures::ToString(CurrentState), *GetGameFeatureName());
		}

		if (StateStatus.TransitionToState != EGameFeaturePluginState::Uninitialized)
		{
			UE_LOG(LogGameFeatures, Verbose, TEXT("Game feature '%s' transitioning state (%s -> %s)"), *GetGameFeatureName(), *UE::GameFeatures::ToString(CurrentState), *UE::GameFeatures::ToString(StateStatus.TransitionToState));
			AllStates[(int32)CurrentState]->EndState();
			CurrentStateInfo = FGameFeaturePluginStateInfo(StateStatus.TransitionToState);
			CurrentState = StateStatus.TransitionToState;
			check(CurrentState != EGameFeaturePluginState::MAX);
			AllStates[(int32)CurrentState]->BeginState();
			OnStateChangedEvent.Broadcast(this);
			bKeepProcessing = true;
		}

		if (!TransitionResult.HasValue())
		{
			check(IsValidErrorState(CurrentState));
			StateProperties.DestinationState = CurrentState;
			break;
		}

		if (NumTransitions++ > MaxTransitions)
		{
			UE_LOG(LogGameFeatures, Fatal, TEXT("Infinite loop in game feature state machine transitions. Current state %s. GameFeature: %s"), *UE::GameFeatures::ToString(CurrentState), *GetGameFeatureName());
		}
	} while (bKeepProcessing);

	if (CurrentState == StateProperties.DestinationState)
	{
		check(IsValidTransitionState(CurrentState) == false);
		StateProperties.OnFeatureStateTransitionComplete.ExecuteIfBound(this, TransitionResult);
		StateProperties.OnFeatureStateTransitionComplete.Unbind();
	}
}

void UGameFeaturePluginStateMachine::UpdateCurrentStateProgress(float Progress)
{
	CurrentStateInfo.Progress = Progress;
}

FGameFeaturePluginStateMachineProperties::FGameFeaturePluginStateMachineProperties(
	const FString& InPluginURL,
	EGameFeaturePluginState DesiredDestination,
	const FGameFeaturePluginRequestStateMachineDependencies& RequestStateMachineDependenciesDelegate,
	const FGameFeaturePluginRequestUpdateStateMachine& RequestUpdateStateMachineDelegate,
	const FGameFeatureStateProgressUpdate& FeatureStateProgressUpdateDelegate)
	: PluginURL(InPluginURL)
	, DestinationState(DesiredDestination)
	, OnRequestStateMachineDependencies(RequestStateMachineDependenciesDelegate)
	, OnRequestUpdateStateMachine(RequestUpdateStateMachineDelegate)
	, OnFeatureStateProgressUpdate(FeatureStateProgressUpdateDelegate)
{
}

EGameFeaturePluginProtocol FGameFeaturePluginStateMachineProperties::GetPluginProtocol() const
{
	if (CachedPluginProtocol != EGameFeaturePluginProtocol::Unknown)
	{
		return CachedPluginProtocol;
	}

	for (EGameFeaturePluginProtocol Proto : TEnumRange<EGameFeaturePluginProtocol>())
	{
		const TCHAR* Prefix = GameFeaturePluginProtocolPrefix(Proto);
		if (Prefix && *Prefix && PluginURL.StartsWith(Prefix))
		{
			CachedPluginProtocol = Proto;
			break;
		}
	}

	return CachedPluginProtocol;
}

bool FGameFeaturePluginStateMachineProperties::ParseURL()
{
	if (GetPluginProtocol() == EGameFeaturePluginProtocol::File)
	{
		PluginInstalledFilename = PluginURL.RightChop(
			FCString::Strlen(GameFeaturePluginProtocolPrefix(EGameFeaturePluginProtocol::File)));
	}
	else if (GetPluginProtocol() == EGameFeaturePluginProtocol::InstallBundle)
	{
		int32 CursorIdx = FCString::Strlen(GameFeaturePluginProtocolPrefix(EGameFeaturePluginProtocol::InstallBundle));
		int32 QueryIdx = PluginURL.Find(TEXT("?"), ESearchCase::IgnoreCase, ESearchDir::FromStart, CursorIdx);
		if (QueryIdx == INDEX_NONE)
		{
			return false;
		}
		
		PluginInstalledFilename = PluginURL.Mid(CursorIdx, QueryIdx - CursorIdx);
		CursorIdx = QueryIdx + 1;
		
		FString BundleNamesString = PluginURL.Mid(CursorIdx);
		TArray<FString> BundleNames;
		BundleNamesString.ParseIntoArray(BundleNames, TEXT(","));
		if (BundleNames.Num() == 0)
		{
			return false;
		}

		FInstallBundlePluginProtocolMetaData& MetaData = *ProtocolMetadata.SetSubtype<FInstallBundlePluginProtocolMetaData>();
		MetaData.InstallBundles.Reserve(BundleNames.Num());
		for (FString& BundleNameString : BundleNames)
		{
			MetaData.InstallBundles.Add(*BundleNameString);
		}
	}
	else
	{
		ensureMsgf(false, TEXT("Unknown protocol for PluginURL: %s"), *PluginURL);
		return false;
	}

	if (PluginInstalledFilename.IsEmpty() || !PluginInstalledFilename.EndsWith(TEXT(".uplugin")))
	{
		ensureMsgf(false, TEXT("PluginInstalledFilename must have a uplugin extension. PluginURL: %s"), *PluginURL);
		return false;
	}

	return true;
}
