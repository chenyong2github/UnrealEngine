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
#include "UObject/ReferenceChainSearch.h"

#if WITH_EDITOR
#include "PluginUtils.h"
#endif //if WITH_EDITOR

namespace UE::GameFeatures
{
	static const FString StateMachineErrorNamespace(TEXT("GameFeaturePlugin.StateMachine."));

	static UE::GameFeatures::FResult CanceledResult = MakeError(StateMachineErrorNamespace + TEXT("Canceled"));

	static int32 ShouldLogMountedFiles = 0;
	static FAutoConsoleVariableRef CVarShouldLogMountedFiles(TEXT("GameFeaturePlugin.ShouldLogMountedFiles"),
		ShouldLogMountedFiles,
		TEXT("Should the newly mounted files be logged."));

	static TAutoConsoleVariable<bool> CVarVerifyPluginUnload(TEXT("GameFeaturePlugin.VerifyUnload"), 
		true,
		TEXT("Verify plugin assets are no longer in memory when unloading."),
		ECVF_Default);

	static TAutoConsoleVariable<bool> CVarVerifyPluginUnloadDumpChains(TEXT("GameFeaturePlugin.VerifyUnloadDumpChains"),
		false,
		TEXT("Dump reference chains for any detected plugin asset leaks."),
		ECVF_Default);

	FString ToString(const UE::GameFeatures::FResult& Result)
	{
		return Result.HasValue() ? FString(TEXT("Success")) : (FString(TEXT("Failure, ErrorCode=")) + Result.GetError());
	}

	#define GAME_FEATURE_PLUGIN_STATE_TO_STRING(inEnum, inText) case EGameFeaturePluginState::inEnum: return TEXT(#inEnum);
	FString ToString(EGameFeaturePluginState InType)
	{
		switch (InType)
		{
		GAME_FEATURE_PLUGIN_STATE_LIST(GAME_FEATURE_PLUGIN_STATE_TO_STRING)
		default:
			check(0);
			return FString();
		}
	}
	#undef GAME_FEATURE_PLUGIN_STATE_TO_STRING

	// Verify that all assets from this plugin have been unloaded and GC'd	
	void VerifyAssetsUnloaded(const FString& PluginName, bool bIgnoreGameFeatureData)
	{
#if (!UE_BUILD_SHIPPING || UE_SERVER || WITH_EDITOR)
		if (!UE::GameFeatures::CVarVerifyPluginUnload.GetValueOnGameThread())
		{
			return;
		}

		FARFilter PluginArFilter;
		PluginArFilter.PackagePaths.Add(FName(TEXT("/") + PluginName));
		PluginArFilter.bRecursivePaths = true;

		auto CheckForLoadedAsset = [](const FString& PluginName, const FAssetData &AssetData)
		{
			if (AssetData.IsAssetLoaded())
			{
				UE_LOG(LogGameFeatures, Error, TEXT("GFP %s failed to unload asset %s!"), *PluginName, *AssetData.GetFullName());

				if (CVarVerifyPluginUnloadDumpChains.GetValueOnGameThread())
				{
					UObject* AssetObj = AssetData.GetAsset();
					//EInternalObjectFlags InternalFlags = AssetObj->GetInternalFlags();
					FReferenceChainSearch(AssetObj, EReferenceChainSearchMode::Shortest | EReferenceChainSearchMode::PrintResults);
				}

				ensureAlwaysMsgf(false, TEXT("GFP %s failed to unload asset %s!"), *PluginName, *AssetData.GetFullName());
			}
		};

		if (bIgnoreGameFeatureData)
		{
			FARFilter RawGameFeatureDataFilter;
			RawGameFeatureDataFilter.ClassPaths.Add(UGameFeatureData::StaticClass()->GetClassPathName());
			RawGameFeatureDataFilter.bRecursiveClasses = true;

			FARCompiledFilter GameFeatureDataFilter;
			UAssetManager::Get().GetAssetRegistry().CompileFilter(RawGameFeatureDataFilter, GameFeatureDataFilter);

			UAssetManager::Get().GetAssetRegistry().EnumerateAssets(PluginArFilter, [&PluginName, &GameFeatureDataFilter, CheckForLoadedAsset](const FAssetData& AssetData)
				{
					if (UAssetManager::Get().GetAssetRegistry().IsAssetIncludedByFilter(AssetData, GameFeatureDataFilter))
					{
						return true;
					}

					CheckForLoadedAsset(PluginName, AssetData);

					return true;
				});
		}
		else
		{
			UAssetManager::Get().GetAssetRegistry().EnumerateAssets(PluginArFilter, [&PluginName, CheckForLoadedAsset](const FAssetData& AssetData)
				{
					CheckForLoadedAsset(PluginName, AssetData);

					return true;
				});
		}

#endif // UE_BUILD_SHIPPING
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
}

FGameFeaturePluginState::~FGameFeaturePluginState()
{
	CleanupDeferredUpdateCallbacks();
}

FDestinationGameFeaturePluginState* FGameFeaturePluginState::AsDestinationState()
{
	FDestinationGameFeaturePluginState* Ret = nullptr;

	EGameFeaturePluginStateType Type = GetStateType();
	if (Type == EGameFeaturePluginStateType::Destination || Type == EGameFeaturePluginStateType::Error)
	{
		Ret = static_cast<FDestinationGameFeaturePluginState*>(this);
	}

	return Ret;
}

FErrorGameFeaturePluginState* FGameFeaturePluginState::AsErrorState()
{
	FErrorGameFeaturePluginState* Ret = nullptr;

	if (GetStateType() == EGameFeaturePluginStateType::Error)
	{
		Ret = static_cast<FErrorGameFeaturePluginState*>(this);
	}

	return Ret;
}

void FGameFeaturePluginState::UpdateStateMachineDeferred(float Delay /*= 0.0f*/) const
{
	CleanupDeferredUpdateCallbacks();

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float dts) mutable
	{
		// @note Release FGameFeaturePluginState::TickHandle first in case the termination callback triggers a GC and destroys the state machine
		TickHandle.Reset();
		StateProperties.OnRequestUpdateStateMachine.ExecuteIfBound();
		return false;
	}), Delay);
}

void FGameFeaturePluginState::GarbageCollectAndUpdateStateMachineDeferred() const
{
	GEngine->ForceGarbageCollection(true); // Tick Delayed

	CleanupDeferredUpdateCallbacks();
	FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &FGameFeaturePluginState::UpdateStateMachineDeferred, 0.0f);
}

void FGameFeaturePluginState::UpdateStateMachineImmediate() const
{
	StateProperties.OnRequestUpdateStateMachine.ExecuteIfBound();
}

void FGameFeaturePluginState::UpdateProgress(float Progress) const
{
	StateProperties.OnFeatureStateProgressUpdate.ExecuteIfBound(Progress);
}

void FGameFeaturePluginState::CleanupDeferredUpdateCallbacks() const
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);
}

/*
=========================================================
  States
=========================================================
*/

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

		UGameFeaturesSubsystem::Get().OnGameFeatureTerminating(StateProperties.PluginName, StateProperties.PluginURL);
	}
};

struct FGameFeaturePluginState_UnknownStatus : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_UnknownStatus(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.Destination < EGameFeaturePluginState::UnknownStatus)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Terminal);
		}
		else if (StateProperties.Destination > EGameFeaturePluginState::UnknownStatus)
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

			const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<UE::GameFeatures::FInstallBundlePluginProtocolMetaData>().InstallBundles;

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

		UGameFeaturesSubsystem::Get().OnGameFeatureStatusKnown(StateProperties.PluginName, StateProperties.PluginURL);
		StateStatus.SetTransition(EGameFeaturePluginState::StatusKnown);
	}
};

struct FGameFeaturePluginState_ErrorCheckingStatus : public FErrorGameFeaturePluginState
{
	FGameFeaturePluginState_ErrorCheckingStatus(FGameFeaturePluginStateMachineProperties& InStateProperties) : FErrorGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.Destination < EGameFeaturePluginState::ErrorCheckingStatus)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Terminal);
		}
		else if (StateProperties.Destination > EGameFeaturePluginState::ErrorCheckingStatus)
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
		if (StateProperties.Destination < EGameFeaturePluginState::ErrorUnavailable)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Terminal);
		}
		else if (StateProperties.Destination > EGameFeaturePluginState::ErrorUnavailable)
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
		if (StateProperties.Destination < EGameFeaturePluginState::StatusKnown)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Terminal);
		}
		else if (StateProperties.Destination > EGameFeaturePluginState::StatusKnown)
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
		if (StateProperties.Destination < EGameFeaturePluginState::ErrorInstalling)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Uninstalling);
		}
		else if (StateProperties.Destination > EGameFeaturePluginState::ErrorInstalling)
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

		const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<UE::GameFeatures::FInstallBundlePluginProtocolMetaData>().InstallBundles;

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

		if (StateProperties.bTryCancel)
		{
			Result = UE::GameFeatures::CanceledResult;
			UpdateStateMachineImmediate();
			return;
		}

		const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<UE::GameFeatures::FInstallBundlePluginProtocolMetaData>().InstallBundles;

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

			if(BundleResult.Result != EInstallBundleResult::UserCancelledError)
			{
				TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
				BundleManager->CancelUpdateContent(PendingBundleDownloads);
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
		const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<UE::GameFeatures::FInstallBundlePluginProtocolMetaData>().InstallBundles;

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

	virtual void TryCancelState() override
	{
		if (PendingBundleDownloads.Num() > 0)
		{
			TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
			BundleManager->CancelUpdateContent(PendingBundleDownloads);
		}
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
		if (StateProperties.Destination > EGameFeaturePluginState::Installed)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Mounting);
		}
		else if (StateProperties.Destination < EGameFeaturePluginState::Installed)
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
		if (StateProperties.Destination < EGameFeaturePluginState::ErrorMounting)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Unmounting);
		}
		else if (StateProperties.Destination > EGameFeaturePluginState::ErrorMounting)
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
		if (StateProperties.Destination < EGameFeaturePluginState::ErrorWaitingForDependencies)
		{
			// There is no cleaup state equivalent to EGameFeaturePluginState::WaitingForDependencies so just go back to unmounting
			StateStatus.SetTransition(EGameFeaturePluginState::Unmounting);
		}
		else if (StateProperties.Destination > EGameFeaturePluginState::ErrorWaitingForDependencies)
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
		if (StateProperties.Destination < EGameFeaturePluginState::ErrorRegistering)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Unregistering);
		}
		else if (StateProperties.Destination > EGameFeaturePluginState::ErrorRegistering)
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

		if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(StateProperties.PluginName); 
			Plugin && Plugin->GetDescriptor().bExplicitlyLoaded)
		{
			// The asset registry listens to FPackageName::OnContentPathDismounted() and 
			// will automatically cleanup the asset registry state we added for this plugin.
			// This will also cause any assets we added to the asset manager to be removed.
			// Scan paths added to the asset manager should have already been cleaned up.
			FText FailureReason;
			if (!IPluginManager::Get().UnmountExplicitlyLoadedPlugin(StateProperties.PluginName, &FailureReason))
			{
				ensureMsgf(false, TEXT("Failed to explicitly unmount the PluginURL(%s) because %s"), *StateProperties.PluginURL, *FailureReason.ToString());
				Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("Plugin_Cannot_Explicitly_Unmount"));
				return;
			}
		}

		if (StateProperties.bAddedPluginToManager)
		{
			verify(IPluginManager::Get().RemoveFromPluginsList(StateProperties.PluginName));
			StateProperties.bAddedPluginToManager = false;
		}

		if (StateProperties.GetPluginProtocol() != EGameFeaturePluginProtocol::InstallBundle)
		{
			bUnmounted = true;
			return;
		}

		TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();

		const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<UE::GameFeatures::FInstallBundlePluginProtocolMetaData>().InstallBundles;

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

		const TArray<FName>& InstallBundles = StateProperties.ProtocolMetadata.GetSubtype<UE::GameFeatures::FInstallBundlePluginProtocolMetaData>().InstallBundles;
		
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
		TSharedPtr<IPlugin> MaybePlugin = IPluginManager::Get().FindPlugin(StateProperties.PluginName);
		const bool bNeedsPluginMount = (MaybePlugin == nullptr || MaybePlugin->GetDescriptor().bExplicitlyLoaded);

		if (MaybePlugin == nullptr)
		{
			const bool bAddedPlugin = IPluginManager::Get().AddToPluginsList(StateProperties.PluginInstalledFilename);
			if (!bAddedPlugin)
			{
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorMounting, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Failed_To_Register_Plugin"));
				return;
			}

			StateProperties.bAddedPluginToManager = true;
		}
		
		if (bNeedsPluginMount)
		{
			IPluginManager::Get().MountExplicitlyLoadedPlugin(StateProperties.PluginName);
		}

		// After the new plugin is mounted add the asset registry for that plugin.
		if (StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::InstallBundle)
		{
			TSharedPtr<IPlugin> NewlyMountedPlugin = IPluginManager::Get().FindPlugin(StateProperties.PluginName);
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
			UGameFeaturesSubsystem& GameFeaturesSubsystem = UGameFeaturesSubsystem::Get();

			TArray<UGameFeaturePluginStateMachine*> Dependencies;
			if (!GameFeaturesSubsystem.FindOrCreatePluginDependencyStateMachines(StateProperties.PluginInstalledFilename, Dependencies))
			{
				// Failed to query dependencies
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorWaitingForDependencies, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Failed_Dependency_Query"));
				return;
			}
			
			bRequestedDependencies = true;

			RemainingDependencies.Reserve(Dependencies.Num());
			for (UGameFeaturePluginStateMachine* Dependency : Dependencies)
			{	
				RemainingDependencies.Emplace(Dependency, MakeValue());
				TransitionDependency(Dependency);
			}
		}

		for (FDepResultPair& Pair : RemainingDependencies)
		{
			UGameFeaturePluginStateMachine* RemainingDependency = Pair.Key.Get();
			if (!RemainingDependency)
			{
				// One of the dependency state machines was destroyed before finishing
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorWaitingForDependencies, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Dependency_Destroyed_Before_Finish"));
				return;
			}

			if (Pair.Value.HasError())
			{
				StateStatus.SetTransitionError(EGameFeaturePluginState::ErrorWaitingForDependencies, UE::GameFeatures::StateMachineErrorNamespace + TEXT("Failed_Dependency_Register"));
				return;
			}
		}

		if (RemainingDependencies.Num() == 0)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Registering);
		}
	}

	FGameFeaturePluginStateRange GetDependencyStateRange() const
	{
		return FGameFeaturePluginStateRange(EGameFeaturePluginState::Registered, EGameFeaturePluginState::Active);
	}

	void TransitionDependency(UGameFeaturePluginStateMachine* Dependency)
	{
		const bool bSetDestination = Dependency->SetDestination(GetDependencyStateRange(),
			FGameFeatureStateTransitionComplete::CreateRaw(this, &FGameFeaturePluginState_WaitingForDependencies::OnDependencyTransitionComplete));

		if (!bSetDestination)
		{
			const bool bCancelPending = Dependency->TryCancel(
				FGameFeatureStateTransitionCanceled::CreateRaw(this, &FGameFeaturePluginState_WaitingForDependencies::OnDependencyTransitionCanceled));
			if (!ensure(bCancelPending))
			{
				OnDependencyTransitionComplete(Dependency, MakeError(TEXT("Failed_Dependency_Register")));
			}
		}
	}

	void OnDependencyTransitionCanceled(UGameFeaturePluginStateMachine* Dependency)
	{
		// Special case for terminal state since it cannot be exited, we need to make a new machine
		if (Dependency->GetCurrentState() == EGameFeaturePluginState::Terminal)
		{
			UGameFeaturePluginStateMachine* NewMachine = UGameFeaturesSubsystem::Get().FindOrCreateGameFeaturePluginStateMachine(Dependency->GetPluginURL());
			checkf(NewMachine != Dependency, TEXT("Game Feature Plugin %s should have already been removed from subsystem!"), *Dependency->GetPluginURL());

			const int32 Index = RemainingDependencies.IndexOfByPredicate([Dependency](const FDepResultPair& Pair)
				{
					return Pair.Key == Dependency;
				});

			check(Index != INDEX_NONE);
			FDepResultPair& FoundDep = RemainingDependencies[Index];
			FoundDep.Key = NewMachine;

			Dependency->RemovePendingTransitionCallback(this);
			Dependency->RemovePendingCancelCallback(this);

			Dependency = NewMachine;
		}

		// Now that the transition has been canceled, retry reaching the desired destination
		const bool bSetDestination = Dependency->SetDestination(GetDependencyStateRange(),
			FGameFeatureStateTransitionComplete::CreateRaw(this, &FGameFeaturePluginState_WaitingForDependencies::OnDependencyTransitionComplete));

		if (!ensure(bSetDestination))
		{
			OnDependencyTransitionComplete(Dependency, MakeError(TEXT("Failed_Dependency_Register")));
		}
	}

	void OnDependencyTransitionComplete(UGameFeaturePluginStateMachine* Dependency, const UE::GameFeatures::FResult& Result)
	{
		const int32 Index = RemainingDependencies.IndexOfByPredicate([Dependency](const FDepResultPair& Pair)
		{
			return Pair.Key == Dependency;
		});

		if (Index != INDEX_NONE)
		{
			FDepResultPair& FoundDep = RemainingDependencies[Index];
			
			if (Result.HasError())
			{
				FoundDep.Value = Result;
			}
			else
			{
				RemainingDependencies.RemoveAtSwap(Index, 1, false);
			}

			UpdateStateMachineImmediate();
		}
	}

	void ClearDependencies()
	{
		for (FDepResultPair& Pair : RemainingDependencies)
		{
			UGameFeaturePluginStateMachine* RemainingDependency = Pair.Key.Get();
			if (RemainingDependency)
			{
				RemainingDependency->RemovePendingTransitionCallback(this);
				RemainingDependency->RemovePendingCancelCallback(this);
			}
		}

		RemainingDependencies.Empty();
		bRequestedDependencies = false;
	}

	using FDepResultPair = TPair<TWeakObjectPtr<UGameFeaturePluginStateMachine>, UE::GameFeatures::FResult>;
	TArray<FDepResultPair> RemainingDependencies;
	bool bRequestedDependencies = false;
};

struct FGameFeaturePluginState_Unregistering : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Unregistering(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	bool bRequestedGC = false;

	virtual void BeginState()
	{
		bRequestedGC = false;
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (bRequestedGC)
		{
			UE::GameFeatures::VerifyAssetsUnloaded(StateProperties.PluginName, false);

			StateStatus.SetTransition(EGameFeaturePluginState::Unmounting);
			return;
		}

		if (StateProperties.GameFeatureData)
		{
			UGameFeaturesSubsystem::Get().OnGameFeatureUnregistering(StateProperties.GameFeatureData, StateProperties.PluginName, StateProperties.PluginURL);

			UGameFeaturesSubsystem::Get().RemoveGameFeatureFromAssetManager(StateProperties.GameFeatureData, StateProperties.PluginName, StateProperties.AddedPrimaryAssetTypes);
			StateProperties.AddedPrimaryAssetTypes.Empty();

			UGameFeaturesSubsystem::Get().UnloadGameFeatureData(StateProperties.GameFeatureData);
		}

		StateProperties.GameFeatureData = nullptr;

#if WITH_EDITOR
		// This will properly unload any plugin asset that could be opened in the editor
		// and ensure standalone packages get unloaded as well
		verify(FPluginUtils::UnloadPluginAssets(StateProperties.PluginName));
#endif //if WITH_EDITOR

		bRequestedGC = true;
		GarbageCollectAndUpdateStateMachineDeferred();
	}
};

struct FGameFeaturePluginState_Registering : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Registering(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		const FString PluginFolder = FPaths::GetPath(StateProperties.PluginInstalledFilename);
		UGameplayTagsManager::Get().AddTagIniSearchPath(PluginFolder / TEXT("Config") / TEXT("Tags"));

		const FString PreferredGameFeatureDataPath = FString::Printf(TEXT("/%s/%s.%s"), *StateProperties.PluginName, *StateProperties.PluginName, *StateProperties.PluginName);

		FString BackupGameFeatureDataPath = TEXT("/") + StateProperties.PluginName + TEXT("/GameFeatureData.GameFeatureData");
		// Allow game feature location to be overriden globally and from within the plugin
		FString OverrideIniPathName = StateProperties.PluginName + TEXT("_Override");
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
			StateProperties.GameFeatureData->InitializeBasePluginIniFile(StateProperties.PluginInstalledFilename);
			StateStatus.SetTransition(EGameFeaturePluginState::Registered);

			check(StateProperties.AddedPrimaryAssetTypes.Num() == 0);
			UGameFeaturesSubsystem::Get().AddGameFeatureToAssetManager(StateProperties.GameFeatureData, StateProperties.PluginName, StateProperties.AddedPrimaryAssetTypes);

			UGameFeaturesSubsystem::Get().OnGameFeatureRegistering(StateProperties.GameFeatureData, StateProperties.PluginName, StateProperties.PluginURL);
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
		if (StateProperties.Destination > EGameFeaturePluginState::Registered)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Loading);
		}
		else if (StateProperties.Destination < EGameFeaturePluginState::Registered)
		{
			StateStatus.SetTransition( EGameFeaturePluginState::Unregistering);
		}
	}
};

struct FGameFeaturePluginState_Unloading : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Unloading(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	bool bRequestedGC = false;

	virtual void BeginState() 
	{
		bRequestedGC = false;
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (bRequestedGC)
		{
#if !WITH_EDITOR // Disabled in editor since it's likely to report unloaded assets because of standalone packages
			UE::GameFeatures::VerifyAssetsUnloaded(StateProperties.PluginName, true);
#endif //if !WITH_EDITOR

			StateStatus.SetTransition(EGameFeaturePluginState::Registered);
			return;
		}

		UnloadGameFeatureBundles(StateProperties.GameFeatureData);

		if (StateProperties.Destination.MaxState == EGameFeaturePluginState::Registered)
		{
			// If we aren't going farther than Registered, GC now
			// otherwise we will defer until closer to our destination state
			bRequestedGC = true;
			GarbageCollectAndUpdateStateMachineDeferred();
			return;
		}

		StateStatus.SetTransition(EGameFeaturePluginState::Registered);
	}

	void UnloadGameFeatureBundles(const UGameFeatureData* GameFeatureToLoad)
	{
		if (GameFeatureToLoad == nullptr)
		{
			return;
		}

		const UGameFeaturesProjectPolicies& Policy = UGameFeaturesSubsystem::Get().GetPolicy();

		// Remove all bundles from feature data and completely unload everything else
		FPrimaryAssetId GameFeatureAssetId = GameFeatureToLoad->GetPrimaryAssetId();
		TSharedPtr<FStreamableHandle> Handle = UAssetManager::Get().ChangeBundleStateForPrimaryAssets({ GameFeatureAssetId }, {}, {}, /*bRemoveAllBundles=*/ true);
		ensureAlways(Handle == nullptr || Handle->HasLoadCompleted()); // Should be no handle since nothing is being loaded

		TArray<FPrimaryAssetId> AssetIds = Policy.GetPreloadAssetListForGameFeature(GameFeatureToLoad, /*bIncludeLoadedAssets=*/true);

		// Don't unload game feature data asset yet, that will happen in FGameFeaturePluginState_Unregistering
		ensureAlways(AssetIds.RemoveSwap(GameFeatureAssetId, false) == 0);

		if (AssetIds.Num() > 0)
		{
			UAssetManager::Get().UnloadPrimaryAssets(AssetIds);
		}
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

		UGameFeaturesSubsystem::Get().OnGameFeatureLoading(StateProperties.GameFeatureData, StateProperties.PluginURL);

		StateStatus.SetTransition(EGameFeaturePluginState::Loaded);
	}

	/** Loads primary assets and bundles for the specified game feature */
	TSharedPtr<FStreamableHandle> LoadGameFeatureBundles(const UGameFeatureData* GameFeatureToLoad)
	{
		check(GameFeatureToLoad);

		const UGameFeaturesProjectPolicies& Policy = UGameFeaturesSubsystem::Get().GetPolicy<UGameFeaturesProjectPolicies>();

		TArray<FPrimaryAssetId> AssetIdsToLoad = Policy.GetPreloadAssetListForGameFeature(GameFeatureToLoad);

		FPrimaryAssetId GameFeatureAssetId = GameFeatureToLoad->GetPrimaryAssetId();
		if (GameFeatureAssetId.IsValid())
		{
			AssetIdsToLoad.Add(GameFeatureAssetId);
		}

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
		if (StateProperties.Destination > EGameFeaturePluginState::Loaded)
		{
			StateStatus.SetTransition(EGameFeaturePluginState::Activating);
		}
		else if (StateProperties.Destination < EGameFeaturePluginState::Loaded)
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
	bool bRequestedGC = false;

	virtual void BeginState() override
	{
		NumObservedPausers = 0;
		NumExpectedPausers = 0;
		bInProcessOfDeactivating = false;
		bRequestedGC = false;
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
		if (bRequestedGC)
		{
			check(NumExpectedPausers == NumObservedPausers);
			StateStatus.SetTransition(EGameFeaturePluginState::Loaded);
			return;
		}

		if (!bInProcessOfDeactivating)
		{
			// Make sure we won't complete the transition prematurely if someone registers as a pauser but fires immediately
			bInProcessOfDeactivating = true;
			NumExpectedPausers = INDEX_NONE;
			NumObservedPausers = 0;

			// Deactivate
			FGameFeatureDeactivatingContext Context(FSimpleDelegate::CreateRaw(this, &FGameFeaturePluginState_Deactivating::OnPauserCompleted));
			UGameFeaturesSubsystem::Get().OnGameFeatureDeactivating(StateProperties.GameFeatureData, StateProperties.PluginName, Context, StateProperties.PluginURL);
			NumExpectedPausers = Context.NumPausers;
		}

		if (NumExpectedPausers == NumObservedPausers)
		{
			if(!bRequestedGC && StateProperties.Destination.MaxState == EGameFeaturePluginState::Loaded)
			{
				// If we aren't going farther than Loaded, GC now
				// otherwise we will defer until closer to our destination state
				bRequestedGC = true;
				GarbageCollectAndUpdateStateMachineDeferred();
			}
			else
			{
				StateStatus.SetTransition(EGameFeaturePluginState::Loaded);
			}
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

		UGameFeaturesSubsystem::Get().OnGameFeatureActivating(StateProperties.GameFeatureData, StateProperties.PluginName, Context, StateProperties.PluginURL);

		StateStatus.SetTransition(EGameFeaturePluginState::Active);
	}
};

struct FGameFeaturePluginState_Active : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_Active(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus) override
	{
		if (StateProperties.Destination < EGameFeaturePluginState::Active)
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

void UGameFeaturePluginStateMachine::InitStateMachine(const FString& InPluginURL)
{
	check(GetCurrentState() == EGameFeaturePluginState::Uninitialized);
	CurrentStateInfo.State = EGameFeaturePluginState::UnknownStatus;
	StateProperties = FGameFeaturePluginStateMachineProperties(
		InPluginURL,
		FGameFeaturePluginStateRange(CurrentStateInfo.State),
		FGameFeaturePluginRequestUpdateStateMachine::CreateUObject(this, &ThisClass::UpdateStateMachine),
		FGameFeatureStateProgressUpdate::CreateUObject(this, &ThisClass::UpdateCurrentStateProgress));

#define GAME_FEATURE_PLUGIN_STATE_MAKE_STATE(inEnum, inText) AllStates[EGameFeaturePluginState::inEnum] = MakeUnique<FGameFeaturePluginState_##inEnum>(StateProperties);
	GAME_FEATURE_PLUGIN_STATE_LIST(GAME_FEATURE_PLUGIN_STATE_MAKE_STATE)
#undef GAME_FEATURE_PLUGIN_STATE_MAKE_STATE

	AllStates[CurrentStateInfo.State]->BeginState();
}

bool UGameFeaturePluginStateMachine::SetDestination(FGameFeaturePluginStateRange InDestination, FGameFeatureStateTransitionComplete OnFeatureStateTransitionComplete, FDelegateHandle* OutCallbackHandle /*= nullptr*/)
{
	check(IsValidDestinationState(InDestination.MinState));
	check(IsValidDestinationState(InDestination.MaxState));

	if (!InDestination.IsValid())
	{
		// Invalid range
		return false;
	}

	if (CurrentStateInfo.State == EGameFeaturePluginState::Terminal && !InDestination.Contains(EGameFeaturePluginState::Terminal))
	{
		// Can't tranistion away from terminal state
		return false;
	}

	if (!IsRunning())
	{
		// Not running so any new range is acceptable

		if (OutCallbackHandle)
		{
			OutCallbackHandle->Reset();
		}

		FDestinationGameFeaturePluginState* CurrState = AllStates[CurrentStateInfo.State]->AsDestinationState();

		if (InDestination.Contains(CurrentStateInfo.State))
		{
			OnFeatureStateTransitionComplete.ExecuteIfBound(this, MakeValue());
			return true;
		}
		
		if (CurrentStateInfo.State < InDestination)
		{
			FDestinationGameFeaturePluginState* MinDestState = AllStates[InDestination.MinState]->AsDestinationState();
			FDelegateHandle CallbackHandle = MinDestState->OnDestinationStateReached.Add(MoveTemp(OnFeatureStateTransitionComplete));
			if (OutCallbackHandle)
			{
				*OutCallbackHandle = CallbackHandle;
			}
		}
		else if (CurrentStateInfo.State > InDestination)
		{
			FDestinationGameFeaturePluginState* MaxDestState = AllStates[InDestination.MaxState]->AsDestinationState();
			FDelegateHandle CallbackHandle = MaxDestState->OnDestinationStateReached.Add(MoveTemp(OnFeatureStateTransitionComplete));
			if (OutCallbackHandle)
			{
				*OutCallbackHandle = CallbackHandle;
			}
		}

		StateProperties.Destination = InDestination;
		UpdateStateMachine();

		return true;
	}

	if (TOptional<FGameFeaturePluginStateRange> NewDestination = StateProperties.Destination.Intersect(InDestination))
	{
		// The machine is already running so we can only transition to this range if it overlaps with our current range.
		// We can satisfy both ranges in this case.

		if (OutCallbackHandle)
		{
			OutCallbackHandle->Reset();
		}

		if (CurrentStateInfo.State < StateProperties.Destination)
		{
			StateProperties.Destination = *NewDestination;

			if (InDestination.Contains(CurrentStateInfo.State))
			{
				OnFeatureStateTransitionComplete.ExecuteIfBound(this, MakeValue());
				return true;
			}

			FDestinationGameFeaturePluginState* MinDestState = AllStates[InDestination.MinState]->AsDestinationState();
			FDelegateHandle CallbackHandle = MinDestState->OnDestinationStateReached.Add(MoveTemp(OnFeatureStateTransitionComplete));
			if (OutCallbackHandle)
			{
				*OutCallbackHandle = CallbackHandle;
			}
		}
		else if(CurrentStateInfo.State > StateProperties.Destination)
		{
			StateProperties.Destination = *NewDestination;

			if (InDestination.Contains(CurrentStateInfo.State))
			{
				OnFeatureStateTransitionComplete.ExecuteIfBound(this, MakeValue());
				return true;
			}

			FDestinationGameFeaturePluginState* MaxDestState = AllStates[InDestination.MaxState]->AsDestinationState();
			FDelegateHandle CallbackHandle = MaxDestState->OnDestinationStateReached.Add(MoveTemp(OnFeatureStateTransitionComplete));
			if (OutCallbackHandle)
			{
				*OutCallbackHandle = CallbackHandle;
			}
		}
		else
		{
			checkf(false, TEXT("IsRunning() returned true but state machine has reached destination!"));
		}

		return true;
	}

	// The requested state range is completely outside the the current state range so reject the request
	return false;
}

bool UGameFeaturePluginStateMachine::TryCancel(FGameFeatureStateTransitionCanceled OnFeatureStateTransitionCanceled, FDelegateHandle* OutCallbackHandle /*= nullptr*/)
{
	if (!IsRunning())
	{
		return false;
	}

	StateProperties.bTryCancel = true;
	FDelegateHandle CallbackHandle = StateProperties.OnTransitionCanceled.Add(MoveTemp(OnFeatureStateTransitionCanceled));
	if(OutCallbackHandle)
	{
		*OutCallbackHandle = CallbackHandle;
	}

	const EGameFeaturePluginState CurrentState = GetCurrentState();
	AllStates[CurrentState]->TryCancelState();

	return true;
}

void UGameFeaturePluginStateMachine::RemovePendingTransitionCallback(FDelegateHandle InHandle)
{
	for (std::underlying_type<EGameFeaturePluginState>::type iState = 0;
		iState < EGameFeaturePluginState::MAX;
		++iState)
	{
		if (FDestinationGameFeaturePluginState* DestState = AllStates[iState]->AsDestinationState())
		{
			if (DestState->OnDestinationStateReached.Remove(InHandle))
			{
				break;
			}
		}
	}
}

void UGameFeaturePluginStateMachine::RemovePendingTransitionCallback(void* DelegateObject)
{
	for (std::underlying_type<EGameFeaturePluginState>::type iState = 0;
		iState < EGameFeaturePluginState::MAX;
		++iState)
	{
		if (FDestinationGameFeaturePluginState* DestState = AllStates[iState]->AsDestinationState())
		{
			if (DestState->OnDestinationStateReached.RemoveAll(DelegateObject))
			{
				break;
			}
		}
	}
}

void UGameFeaturePluginStateMachine::RemovePendingCancelCallback(FDelegateHandle InHandle)
{
	StateProperties.OnTransitionCanceled.Remove(InHandle);
}

void UGameFeaturePluginStateMachine::RemovePendingCancelCallback(void* DelegateObject)
{
	StateProperties.OnTransitionCanceled.RemoveAll(DelegateObject);
}

const FString& UGameFeaturePluginStateMachine::GetGameFeatureName() const
{
	FString PluginFilename;
	if (!StateProperties.PluginName.IsEmpty())
	{
		return StateProperties.PluginName;
	}
	else
	{
		return StateProperties.PluginURL;
	}
}

const FString& UGameFeaturePluginStateMachine::GetPluginURL() const
{
	return StateProperties.PluginURL;
}

const FString& UGameFeaturePluginStateMachine::GetPluginName() const
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

FGameFeaturePluginStateRange UGameFeaturePluginStateMachine::GetDestination() const
{
	return StateProperties.Destination;
}

const FGameFeaturePluginStateInfo& UGameFeaturePluginStateMachine::GetCurrentStateInfo() const
{
	return CurrentStateInfo;
}

bool UGameFeaturePluginStateMachine::IsRunning() const
{
	return !StateProperties.Destination.Contains(CurrentStateInfo.State);
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
	return AllStates[InState]->GetStateType() == EGameFeaturePluginStateType::Transition;
}

bool UGameFeaturePluginStateMachine::IsValidDestinationState(EGameFeaturePluginState InDestinationState) const
{
	check(InDestinationState != EGameFeaturePluginState::MAX);
	return AllStates[InDestinationState]->GetStateType() == EGameFeaturePluginStateType::Destination;
}

bool UGameFeaturePluginStateMachine::IsValidErrorState(EGameFeaturePluginState InDestinationState) const
{
	check(InDestinationState != EGameFeaturePluginState::MAX);
	return AllStates[InDestinationState]->GetStateType() == EGameFeaturePluginStateType::Error;
}

void UGameFeaturePluginStateMachine::UpdateStateMachine()
{
	EGameFeaturePluginState CurrentState = GetCurrentState();
	if (bInUpdateStateMachine)
	{
		UE_LOG(LogGameFeatures, Verbose, TEXT("Game feature state machine skipping update for %s in ::UpdateStateMachine. Current State: %s"), *GetGameFeatureName(), *UE::GameFeatures::ToString(CurrentState));
		return;
	}

	TOptional<TGuardValue<bool>> ScopeGuard(InPlace, bInUpdateStateMachine, true);
	
	using StateIt = std::underlying_type<EGameFeaturePluginState>::type;

	auto DoCallbacks = [this](const UE::GameFeatures::FResult& Result, StateIt Begin, StateIt End)
	{
		for (StateIt iState = Begin; iState < End; ++iState)
		{
			if (FDestinationGameFeaturePluginState* DestState = AllStates[iState]->AsDestinationState())
			{
				// Use a local callback on the stack. If SetDestination() is called from the callback then we don't want to stomp the callback
				// for the new state transition request.
				// Callback from terminal state could also trigger a GC that would destroy the state machine
				FDestinationGameFeaturePluginState::FOnDestinationStateReached LocalOnDestinationStateReached(MoveTemp(DestState->OnDestinationStateReached));
				DestState->OnDestinationStateReached.Clear();

				LocalOnDestinationStateReached.Broadcast(this, Result);
			}
		}
	};

	auto DoCallback = [&DoCallbacks](const UE::GameFeatures::FResult& Result, StateIt InState)
	{
		DoCallbacks(Result, InState, InState + 1);
	};

	bool bKeepProcessing = false;
	int32 NumTransitions = 0;
	const int32 MaxTransitions = 10000;
	do
	{
		bKeepProcessing = false;

		FGameFeaturePluginStateStatus StateStatus;
		AllStates[CurrentState]->UpdateState(StateStatus);

		if (StateStatus.TransitionToState == CurrentState)
		{
			UE_LOG(LogGameFeatures, Fatal, TEXT("Game feature state %s transitioning to itself. GameFeature: %s"), *UE::GameFeatures::ToString(CurrentState), *GetGameFeatureName());
		}

		if (StateStatus.TransitionToState != EGameFeaturePluginState::Uninitialized)
		{
			UE_LOG(LogGameFeatures, Verbose, TEXT("Game feature '%s' transitioning state (%s -> %s)"), *GetGameFeatureName(), *UE::GameFeatures::ToString(CurrentState), *UE::GameFeatures::ToString(StateStatus.TransitionToState));
			AllStates[CurrentState]->EndState();
			CurrentStateInfo = FGameFeaturePluginStateInfo(StateStatus.TransitionToState);
			CurrentState = StateStatus.TransitionToState;
			check(CurrentState != EGameFeaturePluginState::MAX);
			AllStates[CurrentState]->BeginState();

			if (CurrentState == EGameFeaturePluginState::Terminal)
			{
				// Remove from gamefeature subsystem before calling back in case this GFP is reloaded on callback,
				// but make sure we don't get destroyed from a GC during a callback
				UGameFeaturesSubsystem::Get().BeginTermination(this);
			}

			if (StateProperties.bTryCancel && AllStates[CurrentState]->GetStateType() != EGameFeaturePluginStateType::Transition)
			{
				StateProperties.Destination = FGameFeaturePluginStateRange(CurrentState);

				StateProperties.bTryCancel = false;
				bKeepProcessing = false;

				// Make sure bInUpdateStateMachine is not set while processing callbacks if we are at our destination
				ScopeGuard.Reset();

				// For all callbacks, return the CanceledResult
				DoCallbacks(UE::GameFeatures::CanceledResult, 0, EGameFeaturePluginState::MAX);

				// Must be called after transtition callbacks, UGameFeaturesSubsystem::ChangeGameFeatureTargetStateComplete may remove the this machine from the subsystem
				FGameFeaturePluginStateMachineProperties::FOnTransitionCanceled LocalOnTransitionCanceled(MoveTemp(StateProperties.OnTransitionCanceled));
				StateProperties.OnTransitionCanceled.Clear();
				LocalOnTransitionCanceled.Broadcast(this);
			}
			else if (const bool bError = !StateStatus.TransitionResult.HasValue(); bError)
			{
				check(IsValidErrorState(CurrentState));
				StateProperties.Destination = FGameFeaturePluginStateRange(CurrentState);

				bKeepProcessing = false;
				
				// Make sure bInUpdateStateMachine is not set while processing callbacks if we are at our destination
				ScopeGuard.Reset();

				// In case of an error, callback all possible callbacks
				DoCallbacks(StateStatus.TransitionResult, 0, EGameFeaturePluginState::MAX);
			}
			else
			{
				bKeepProcessing = AllStates[CurrentState]->GetStateType() == EGameFeaturePluginStateType::Transition || !StateProperties.Destination.Contains(CurrentState);
				if (!bKeepProcessing)
				{
					// Make sure bInUpdateStateMachine is not set while processing callbacks if we are at our destination
					ScopeGuard.Reset();
				}

				DoCallback(StateStatus.TransitionResult, CurrentState);
			}

			if (CurrentState == EGameFeaturePluginState::Terminal)
			{
				check(bKeepProcessing == false);
				// Now that callbacks are done this machine can be cleaned up
				UGameFeaturesSubsystem::Get().FinishTermination(this);
				MarkAsGarbage();
			}
		}

		if (NumTransitions++ > MaxTransitions)
		{
			UE_LOG(LogGameFeatures, Fatal, TEXT("Infinite loop in game feature state machine transitions. Current state %s. GameFeature: %s"), *UE::GameFeatures::ToString(CurrentState), *GetGameFeatureName());
		}
	} while (bKeepProcessing);
}

void UGameFeaturePluginStateMachine::UpdateCurrentStateProgress(float Progress)
{
	CurrentStateInfo.Progress = Progress;
}

FGameFeaturePluginStateMachineProperties::FGameFeaturePluginStateMachineProperties(
	const FString& InPluginURL,
	const FGameFeaturePluginStateRange& DesiredDestination,
	const FGameFeaturePluginRequestUpdateStateMachine& RequestUpdateStateMachineDelegate,
	const FGameFeatureStateProgressUpdate& FeatureStateProgressUpdateDelegate)
	: PluginURL(InPluginURL)
	, Destination(DesiredDestination)
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
		const TCHAR* Prefix = UE::GameFeatures::GameFeaturePluginProtocolPrefix(Proto);
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
			FCString::Strlen(UE::GameFeatures::GameFeaturePluginProtocolPrefix(EGameFeaturePluginProtocol::File)));
	}
	else if (GetPluginProtocol() == EGameFeaturePluginProtocol::InstallBundle)
	{
		int32 CursorIdx = FCString::Strlen(UE::GameFeatures::GameFeaturePluginProtocolPrefix(EGameFeaturePluginProtocol::InstallBundle));
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

		UE::GameFeatures::FInstallBundlePluginProtocolMetaData& MetaData = *ProtocolMetadata.SetSubtype<UE::GameFeatures::FInstallBundlePluginProtocolMetaData>();
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

	PluginName = FPaths::GetBaseFilename(PluginInstalledFilename);

	if (PluginInstalledFilename.IsEmpty() || !PluginInstalledFilename.EndsWith(TEXT(".uplugin")))
	{
		ensureMsgf(false, TEXT("PluginInstalledFilename must have a uplugin extension. PluginURL: %s"), *PluginURL);
		return false;
	}

	return true;
}
