// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeaturePluginStateMachine.h"

#include "AssetRegistry/AssetRegistryState.h"
#include "Components/GameFrameworkComponentManager.h"
#include "GameFeatureData.h"
#include "GameplayTagsManager.h"
#include "Engine/AssetManager.h"
#include "IPlatformFilePak.h"
#include "InstallBundleManager/Public/InstallBundleManagerInterface.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/ArrayReader.h"
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

		static FString OverridePluginPath = "";
		static FAutoConsoleVariableRef CVarOverridePluginPath(TEXT("GameFeaturePlugin.OverridePluginPath"),
			OverridePluginPath,
			TEXT("Override full Plugin path name for a downloaded plugin"));

		static int32 ShouldLogMountedFiles = 1;
		static FAutoConsoleVariableRef CVarShouldLogMountedFiles(TEXT("GameFeaturePlugin.ShouldLogMountedFiles"),
			ShouldLogMountedFiles,
			TEXT("Should the newly mounted files be logged."));

		FString ToString(const UE::GameFeatures::FResult& Result)
		{
			return Result.HasValue() ? FString(TEXT("Success")) : (FString(TEXT("Failure, ErrorCode=")) + Result.GetError());
		}
	}
}

FString EGameFeaturePluginState::ToString(EGameFeaturePluginState::Type InType)
{
	switch (InType)
	{
	case Uninitialized: return TEXT("Uninitialized");
	case UnknownStatus: return TEXT("UnknownStatus");
	case CheckingStatus: return TEXT("CheckingStatus");
	case StatusKnown: return TEXT("StatusKnown");
	case Uninstalling: return TEXT("Uninstalling");
	case Downloading: return TEXT("Downloading");
	case Installed: return TEXT("Installed");
	case Unmounting: return TEXT("Unmounting");
	case Mounting: return TEXT("Mounting");
	case WaitingForDependencies: return TEXT("WaitingForDependencies");
	case Unregistering: return TEXT("Unregistering");
	case Registering: return TEXT("Registering");
	case Registered: return TEXT("Registered");
	case Unloading: return TEXT("Unloading");
	case Loading: return TEXT("Loading");
	case Loaded: return TEXT("Loaded");
	case Deactivating: return TEXT("Deactivating");
	case Activating: return TEXT("Activating");
	case Active: return TEXT("Active");
	default:
		check(0);
		return FString();
	}
}

/*
=========================================================
  States
=========================================================
*/

struct FDestinationGameFeaturePluginState : public FGameFeaturePluginState
{
	FDestinationGameFeaturePluginState(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual bool CanBeDestinationState() const override
	{
		return true;
	}
};

struct FGameFeaturePluginState_Uninitialized : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Uninitialized(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		checkf(false, TEXT("UpdateState can not be called while uninitialized"));
	}
};

struct FGameFeaturePluginState_UnknownStatus : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_UnknownStatus(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		if (StateProperties.DestinationState > EGameFeaturePluginState::UnknownStatus)
		{
			StateStatus.TransitionToState = EGameFeaturePluginState::CheckingStatus;
		}
	}
};

struct FGameFeaturePluginState_CheckingStatus : public FGameFeaturePluginState
{
	FGameFeaturePluginState_CheckingStatus(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		/** Finds if the plugin is on disk initializing internal state for if the plugin is available. */
		if (StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::File)
		{
			StateProperties.PluginInstalledFilename = StateProperties.PluginURL.RightChop(StateProperties.FileProtocolPrefix().Len());
			StateProperties.bIsAvailable = FPaths::FileExists(*StateProperties.PluginInstalledFilename);
		}
		else
		{
			if (TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager())
			{
				FName BundleName = FName(StateProperties.PluginURL);
				TOptional<FInstallBundleProgress> OptionalBundleStatus = BundleManager->GetBundleProgress(BundleName);
				if (OptionalBundleStatus.IsSet())
				{
					// TODO: rely on some info that the bundle manager knows about the bundle we want to request.
				}
				// Assuming true while we discuss with BundleManager api about getting a status.
				StateProperties.bIsAvailable = true;
			}
			else
			{
				StateStatus.TransitionResult = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Was_Null"));
			}
		}
		StateStatus.TransitionToState = EGameFeaturePluginState::StatusKnown;
	}
};

struct FGameFeaturePluginState_StatusKnown : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_StatusKnown(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		if (StateProperties.DestinationState > EGameFeaturePluginState::StatusKnown)
		{
			if (StateProperties.bIsAvailable)
			{
				if (StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::File)
				{
					StateStatus.TransitionToState = EGameFeaturePluginState::Installed;

				}
				else
				{
					// TODO: The bundle manager in the checking status should be able to tell us if the plugin is already installed.
					// though the downloading step would just complete instantly if multiple requests are made and the plugin is downloaded.
					StateStatus.TransitionToState = EGameFeaturePluginState::Downloading;
				}
			}
			else
			{
				StateStatus.TransitionResult = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("Plugin_Unavailable"));
			}
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

	void OnContentRemoved(FInstallBundleReleaseRequestResultInfo BundleResult, const FName BundleName)
	{
		if (BundleResult.BundleName == BundleName)
		{
			bWasDeleted = true;
			FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float dts)
				{
					StateProperties.OnRequestUpdateStateMachine.ExecuteIfBound();
					return false;
				}));
		}
	}

	virtual void BeginState()
	{
		Result = MakeValue();
		bWasDeleted = false;

		if (TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager())
		{
			const FName BundleName = FName(StateProperties.PluginURL);
			IInstallBundleManager::ReleasedDelegate.AddRaw(this, &FGameFeaturePluginState_Uninstalling::OnContentRemoved, BundleName);

			EInstallBundleReleaseRequestFlags ReleaseFlags = EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible;
			BundleManager->RequestReleaseContent(BundleName, ReleaseFlags);
		}
		else
		{
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Was_Null"));
			return;
		}
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		// @TODO Uninstall plugin
		if (StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::Web)
		{
			if (!bWasDeleted && Result.HasValue())
			{
				return;
			}
		}

		StateStatus.TransitionToState = EGameFeaturePluginState::StatusKnown;
		//@TODO: Collapse once TValueOrError operator= works
		if (Result.HasValue())
		{
			StateStatus.TransitionResult = MakeValue();
		}
		else
		{
			StateStatus.TransitionResult = MakeError(Result.GetError());
		}
	}

	virtual void EndState()
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

	UE::GameFeatures::FResult Result;
	bool bPluginDownloaded = false;

	void SetPluginFilename(const FString& BundleNameAsString)
	{
		if (UE::GameFeatures::OverridePluginPath.IsEmpty())
		{
			StateProperties.PluginInstalledFilename = FPaths::ProjectPluginsDir() / TEXT("GameFeatures") / BundleNameAsString / BundleNameAsString + TEXT(".uplugin");
		}
		else
		{
			StateProperties.PluginInstalledFilename = UE::GameFeatures::OverridePluginPath;
		}
	}

	void OnInstallBundleCompleted(FInstallBundleRequestResultInfo BundleResult, const FName BundleName)
	{
		if (BundleName == BundleResult.BundleName)
		{
			if (BundleResult.Result == EInstallBundleResult::OK)
			{
				bPluginDownloaded = true;
			}
			else
			{
				Result = MakeError(BundleResult.OptionalErrorCode.IsEmpty() ? (UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_DownloadFailure")) : BundleResult.OptionalErrorCode);
			}

			// Delay the completion as the bundle manager needs time after it completes a broadcast for completion.
			FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float dts)
				{
					QUICK_SCOPE_CYCLE_COUNTER(STAT_FGameFeaturePluginState_Downloading_OnInstallBundleCompleted_OnRequestUpdateStateMachine_Delegate);

					StateProperties.OnRequestUpdateStateMachine.ExecuteIfBound();
					return false;
				}));
		}
	}

	virtual void BeginState()
	{
		Result = MakeValue();
		bPluginDownloaded = false;

		TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
		if (!BundleManager)
		{
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Was_Null"));
			return;
		}

		SetPluginFilename(StateProperties.PluginURL);
		// Assume the passed in PluginURL is the BundleName. The BundleManager should know about this bundle name by this time.
		const FName BundleName = FName(StateProperties.PluginURL);
		TOptional<FInstallBundleProgress> OptionalBundleStatus = BundleManager->GetBundleProgress(BundleName);
		if (OptionalBundleStatus.IsSet())
		{
			FInstallBundleProgress BundleStatus = OptionalBundleStatus.GetValue();
			if (BundleStatus.Status == EInstallBundleStatus::Ready)
			{
				// already downloaded and ready to go.
				bPluginDownloaded = true;
			}
		}
		else
		{
			// assuming it's not installed
		}

		EInstallBundleRequestFlags InstallFlags = EInstallBundleRequestFlags::None;
		InstallFlags |= EInstallBundleRequestFlags::TrackPersistentBundleStats;
		InstallFlags |= EInstallBundleRequestFlags::SendNotificationIfDownloadCompletesInBackground;
		InstallFlags |= EInstallBundleRequestFlags::SkipMount;
		TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> MaybeRequestInfo = BundleManager->RequestUpdateContent(BundleName, InstallFlags);

		if (!MaybeRequestInfo.IsValid())
		{
			ensureMsgf(false, TEXT("Unable to enqueue downloadload for the PluginURL(%s) because %s"), *StateProperties.PluginURL, LexToString(MaybeRequestInfo.GetError()));
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_CannotStartDownload"));
			return;
		}

		FInstallBundleRequestInfo RequestInfo = MaybeRequestInfo.StealValue();
		if (EnumHasAnyFlags(RequestInfo.InfoFlags, EInstallBundleRequestInfoFlags::EnqueuedBundles))
		{
			IInstallBundleManager::InstallBundleCompleteDelegate.AddRaw(this, &FGameFeaturePluginState_Downloading::OnInstallBundleCompleted, BundleName);
		}
		else if (EnumHasAnyFlags(RequestInfo.InfoFlags, EInstallBundleRequestInfoFlags::SkippedAlreadyMountedBundles | EInstallBundleRequestInfoFlags::SkippedAlreadyUpdatedBundles))
		{
			bPluginDownloaded = true;
		}
		else if (EnumHasAnyFlags(RequestInfo.InfoFlags, EInstallBundleRequestInfoFlags::SkippedUnknownBundles))
		{
			// TODO: Handle this
		}
		else
		{
			ensureMsgf(false, TEXT("Unable to enqueue downloadload for the PluginURL(%s)"), *StateProperties.PluginURL);
			Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_UnknownBundle"));
		}
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		if (!Result.HasValue())
		{
			StateStatus.TransitionResult = MakeError(Result.GetError());
			StateStatus.TransitionToState = EGameFeaturePluginState::Uninstalling;
			return;
		}

		if (bPluginDownloaded)
		{
			StateStatus.TransitionToState = EGameFeaturePluginState::Installed;
			return;
		}
	}

	virtual void EndState()
	{
		IInstallBundleManager::InstallBundleCompleteDelegate.RemoveAll(this);
	}
};

struct FGameFeaturePluginState_Installed : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_Installed(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		if (StateProperties.DestinationState > EGameFeaturePluginState::Installed)
		{
			StateStatus.TransitionToState = EGameFeaturePluginState::Mounting;
		}
		else if (StateProperties.DestinationState < EGameFeaturePluginState::Installed)
		{
			StateStatus.TransitionToState = EGameFeaturePluginState::Uninstalling;
		}
	}
};

struct FGameFeaturePluginState_Unmounting : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Unmounting(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	bool bUnmounted = false;
	void OnContentReleased(FInstallBundleReleaseRequestResultInfo BundleResult, const FName BundleName)
	{
		if (BundleResult.BundleName == BundleName)
		{
			bUnmounted = true;
			FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float dts)
				{
					StateProperties.OnRequestUpdateStateMachine.ExecuteIfBound();
					return false;
				}));
		}
	}

	virtual void BeginState()
	{
		bUnmounted = false;
		if (TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager())
		{
			const FName BundleName = FName(StateProperties.PluginURL);
			IInstallBundleManager::ReleasedDelegate.AddRaw(this, &FGameFeaturePluginState_Unmounting::OnContentReleased, BundleName);

			EInstallBundleReleaseRequestFlags ReleaseFlags = EInstallBundleReleaseRequestFlags::None;
			BundleManager->RequestReleaseContent(BundleName, ReleaseFlags);
		}
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		if (StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::Web)
		{
			if (!bUnmounted)
			{
				return;
			}
		}

		StateStatus.TransitionToState = EGameFeaturePluginState::Installed;
	}

	virtual void EndState()
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
	bool bMounted = false;

	void OnInstallBundleCompleted(FInstallBundleRequestResultInfo BundleResult, const FName BundleName)
	{
		if (BundleName == BundleResult.BundleName)
		{
			if (BundleResult.Result == EInstallBundleResult::OK && BundleResult.bContentWasInstalled)
			{
				bMounted = true;
			}
			else
			{
				Result = MakeError(BundleResult.OptionalErrorCode.IsEmpty() ? (UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Install_Error")) : BundleResult.OptionalErrorCode);
			}

			StateProperties.OnRequestUpdateStateMachine.ExecuteIfBound();
		}
	}

	void OnPakFileMounted(const IPakFile& PakFile, const FName BundleName)
	{
		if (FPakFile* Pak = (FPakFile*)(&PakFile))
		{
			UE_LOG(LogGameFeatures, Display, TEXT("Mounted Pak File for (%s) with following files:"), *BundleName.ToString());
			TArray<FString> OutFileList;
			Pak->GetPrunedFilenames(OutFileList);
			for (const FString& FileName : OutFileList)
			{
				UE_LOG(LogGameFeatures, Display, TEXT("(%s)"), *FileName);
			}
		}
	}

	virtual void BeginState()
	{
		Result = MakeValue();
		bMounted = false;

		if (StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::Web)
		{
			if (TSharedPtr<IInstallBundleManager> BundleManager = IInstallBundleManager::GetPlatformInstallBundleManager())
			{
				// Assume the passed in PluginURL is the BundleName. The BundleManager should know about this bundle name by this time.
				const FName BundleName = FName(StateProperties.PluginURL);
				EInstallBundleRequestFlags InstallFlags = EInstallBundleRequestFlags::None;
				TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> MaybeRequestInfo = BundleManager->RequestUpdateContent(BundleName, InstallFlags);

				if (!MaybeRequestInfo.IsValid())
				{
					ensureMsgf(false, TEXT("Unable to enqueue mount for the PluginURL(%s) because %s"), *StateProperties.PluginURL, LexToString(MaybeRequestInfo.GetError()));
					Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_CannotStartMount"));
					return;
				}

				FInstallBundleRequestInfo RequestInfo = MaybeRequestInfo.StealValue();
				if (EnumHasAnyFlags(RequestInfo.InfoFlags, EInstallBundleRequestInfoFlags::SkippedAlreadyMountedBundles))
				{
					bMounted = true;
					return;
				}
				else if (EnumHasAnyFlags(RequestInfo.InfoFlags, EInstallBundleRequestInfoFlags::EnqueuedBundles))
				{
					// Success, bundle mounted.
					IInstallBundleManager::InstallBundleCompleteDelegate.AddRaw(this, &FGameFeaturePluginState_Mounting::OnInstallBundleCompleted, BundleName);
					if (UE::GameFeatures::ShouldLogMountedFiles)
					{
						FCoreDelegates::OnPakFileMounted2.AddRaw(this, &FGameFeaturePluginState_Mounting::OnPakFileMounted, BundleName);
					}
					return;
				}
			}
			else
			{
				Result = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("BundleManager_Was_Null"));
			}

			return;
		}
		else
		{
			bMounted = true;
		}
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		if (!Result.HasValue())
		{
			ensureMsgf(false, TEXT("Unable to Mount downloadload PluginURL(%s)"), *StateProperties.PluginURL);
			StateStatus.TransitionResult = MakeError(Result.GetError());
			StateStatus.TransitionToState = EGameFeaturePluginState::Installed;
		}
		if (!bMounted)
		{
			return;
		}

		checkf(!StateProperties.PluginInstalledFilename.IsEmpty(), TEXT("PluginInstalledFilename must be set by the Mounting. PluginURL: %s"), *StateProperties.PluginURL);
		checkf(FPaths::GetExtension(StateProperties.PluginInstalledFilename) == TEXT("uplugin"), TEXT("PluginInstalledFilename must have a uplugin extension. PluginURL: %s"), *StateProperties.PluginURL);

		// refresh the plugins list to let the plugin manager know about it
		IPluginManager::Get().AddToPluginsList(StateProperties.PluginInstalledFilename);
		const FString PluginName = FPaths::GetBaseFilename(StateProperties.PluginInstalledFilename);
		IPluginManager::Get().MountExplicitlyLoadedPlugin(PluginName);

		// After the new plugin is mounted add the asset registry for that plugin.
		if (StateProperties.GetPluginProtocol() == EGameFeaturePluginProtocol::Web)
		{
			TSharedPtr<IPlugin> NewlyMountedPlugin = IPluginManager::Get().FindPlugin(PluginName);
			if (NewlyMountedPlugin.IsValid() && NewlyMountedPlugin->CanContainContent())
			{
				TArray<uint8> SerializedAssetData;
				const FString PluginFolder = FPaths::GetPath(StateProperties.PluginInstalledFilename);
				const FString PluginAssetRegistry = PluginFolder / TEXT("AssetRegistry.bin");
				if (ensure(IFileManager::Get().FileExists(*PluginAssetRegistry) && FFileHelper::LoadFileToArray(SerializedAssetData, *PluginAssetRegistry)))
				{
					FAssetRegistryState PluginAssetRegistryState;
					FMemoryReader Ar(SerializedAssetData);
					PluginAssetRegistryState.Load(Ar);

					IAssetRegistry& AssetRegistry = UAssetManager::Get().GetAssetRegistry();
					AssetRegistry.AppendState(PluginAssetRegistryState);
				}
				else
				{
					StateStatus.TransitionResult = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("Failed_To_Mount_Plugin"));;
					StateStatus.TransitionToState = EGameFeaturePluginState::Installed;
					return;
				}
			}
		}

		StateStatus.TransitionToState = EGameFeaturePluginState::WaitingForDependencies;
	}

	virtual void EndState()
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

	virtual void EndState()
	{
		ClearDependencies();
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
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
					if (Dependency->GetCurrentState() < EGameFeaturePluginState::Loaded)
					{
						RemainingDependencies.Add(Dependency);
						Dependency->OnStateChanged().AddRaw(this, &FGameFeaturePluginState_WaitingForDependencies::OnDependencyStateChanged);

						// If we are not alreadying loading this dependency, do so now
						if (Dependency->GetDestinationState() < EGameFeaturePluginState::Loaded)
						{
							Dependency->SetDestinationState(EGameFeaturePluginState::Loaded, FGameFeatureStateTransitionComplete());
						}
					}
				}
			}
			else
			{
				// Failed to query dependencies
				StateStatus.TransitionResult = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("Failed_Dependency_Query"));
				StateStatus.TransitionToState = EGameFeaturePluginState::Installed;
				return;
			}
		}

		for (int32 DepIdx = RemainingDependencies.Num() - 1; DepIdx >= 0; --DepIdx)
		{
			UGameFeaturePluginStateMachine* RemainingDependency = RemainingDependencies[DepIdx].Get();
			if (!RemainingDependency)
			{
				// One of the dependency state machines was destroyed before finishing, go back to installed.
				StateStatus.TransitionResult = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("Dependency_Destroyed_Before_Finish"));
				StateStatus.TransitionToState = EGameFeaturePluginState::Installed;
				return;
			}
			else if (RemainingDependency->GetCurrentState() >= EGameFeaturePluginState::Loaded)
			{
				RemainingDependency->OnStateChanged().RemoveAll(this);
				RemainingDependencies.RemoveAt(DepIdx, 1, false);
			}
		}

		if (RemainingDependencies.Num() == 0)
		{
			StateStatus.TransitionToState = EGameFeaturePluginState::Registering;
		}
	}

	void OnDependencyStateChanged(UGameFeaturePluginStateMachine* Dependency)
	{
		if (RemainingDependencies.Contains(Dependency))
		{
			StateProperties.OnRequestUpdateStateMachine.ExecuteIfBound();
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

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		StateProperties.GameFeatureData = nullptr;
		// @todo Make UnloadGameFeature in asset manager, which will call UnloadPrimaryAsset on the GameFeatureData

		// @todo GC, then make sure all loaded content is out of memory
		GEngine->ForceGarbageCollection(true);

		StateStatus.TransitionToState = EGameFeaturePluginState::Unmounting;
	}
};

struct FGameFeaturePluginState_Registering : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Registering(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
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
			StateStatus.TransitionToState = EGameFeaturePluginState::Registered;

			UGameFeaturesSubsystem::Get().OnGameFeatureRegistering(StateProperties.GameFeatureData, PluginName);
		}
		else
		{
			// The gamefeaturedata does not exist. The pak file may not be openable or this is a builtin plugin where the pak file does not exist.
			StateStatus.TransitionResult = MakeError(UE::GameFeatures::StateMachineErrorNamespace + TEXT("Plugin_Missing_GameFeatureData"));
			StateStatus.TransitionToState = EGameFeaturePluginState::Installed;
		}
	}
};

struct FGameFeaturePluginState_Registered : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_Registered(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		if (StateProperties.DestinationState > EGameFeaturePluginState::Registered)
		{
			StateStatus.TransitionToState = EGameFeaturePluginState::Loading;
		}
		else if (StateProperties.DestinationState < EGameFeaturePluginState::Registered)
		{
			StateStatus.TransitionToState = EGameFeaturePluginState::Unregistering;
		}
	}
};

struct FGameFeaturePluginState_Unloading : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Unloading(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		GEngine->ForceGarbageCollection(true);
		StateStatus.TransitionToState = EGameFeaturePluginState::Registered;
	}
};

struct FGameFeaturePluginState_Loading : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Loading(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		check(StateProperties.GameFeatureData);

		StateStatus.TransitionToState = EGameFeaturePluginState::Loaded;

		StateProperties.GameFeatureData->InitializeBasePluginIniFile(StateProperties.PluginInstalledFilename);

		// AssetManager
		TSharedPtr<FStreamableHandle> BundleHandle = LoadGameFeatureBundles(StateProperties.GameFeatureData);
		// @todo make this async. For now we just wait
		if (BundleHandle.IsValid())
		{
			BundleHandle->WaitUntilComplete(0.0f, false);
		}

		UGameFeaturesSubsystem::Get().OnGameFeatureLoading(StateProperties.GameFeatureData);
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

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		if (StateProperties.DestinationState > EGameFeaturePluginState::Loaded)
		{
			StateStatus.TransitionToState = EGameFeaturePluginState::Activating;
		}
		else if (StateProperties.DestinationState < EGameFeaturePluginState::Loaded)
		{
			StateStatus.TransitionToState = EGameFeaturePluginState::Unloading;
		}
	}
};

struct FGameFeaturePluginState_Deactivating : public FGameFeaturePluginState
{
	FGameFeaturePluginState_Deactivating(FGameFeaturePluginStateMachineProperties& InStateProperties) : FGameFeaturePluginState(InStateProperties) {}

	virtual void BeginState()
	{
	}

	int32 NumObservedPausers = 0;
	int32 NumExpectedPausers = 0;
	bool bInProcessOfDeactivating = false;

	void OnPauserCompleted()
	{
		check(IsInGameThread());
		++NumObservedPausers;

		if (NumObservedPausers == NumExpectedPausers)
		{
			StateProperties.OnRequestUpdateStateMachine.ExecuteIfBound();
		}
	}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		if (!bInProcessOfDeactivating)
		{
			// Make sure we won't complete the transition prematurely if someone registers as a pauser but fires immediately
			bInProcessOfDeactivating = true;
			NumExpectedPausers = INDEX_NONE;
			NumObservedPausers = 0;

			// Deactivate
			FGameFeatureDeactivatingContext Context(FSimpleDelegate::CreateRaw(this, &FGameFeaturePluginState_Deactivating::OnPauserCompleted));
			UGameFeaturesSubsystem::Get().OnGameFeatureDeactivating(StateProperties.GameFeatureData, Context);
			NumExpectedPausers = Context.NumPausers;
		}

		if (NumExpectedPausers == NumObservedPausers)
		{
			GEngine->ForceGarbageCollection(true);
			StateStatus.TransitionToState = EGameFeaturePluginState::Loaded;
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

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		check(GEngine);
		check(StateProperties.GameFeatureData);

		StateProperties.GameFeatureData->InitializeHierarchicalPluginIniFiles(StateProperties.PluginInstalledFilename);

		UGameFeaturesSubsystem::Get().OnGameFeatureActivating(StateProperties.GameFeatureData);

		StateStatus.TransitionToState = EGameFeaturePluginState::Active;
	}
};

struct FGameFeaturePluginState_Active : public FDestinationGameFeaturePluginState
{
	FGameFeaturePluginState_Active(FGameFeaturePluginStateMachineProperties& InStateProperties) : FDestinationGameFeaturePluginState(InStateProperties) {}

	virtual void UpdateState(FGameFeaturePluginStateStatus& StateStatus)
	{
		if (StateProperties.DestinationState < EGameFeaturePluginState::Active)
		{
			StateStatus.TransitionToState = EGameFeaturePluginState::Deactivating;
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
		FGameFeaturePluginRequestUpdateStateMachine::CreateUObject(this, &ThisClass::UpdateStateMachine));

	AllStates[EGameFeaturePluginState::Uninitialized] = MakeUnique<FGameFeaturePluginState_Uninitialized>(StateProperties);
	AllStates[EGameFeaturePluginState::UnknownStatus] = MakeUnique<FGameFeaturePluginState_UnknownStatus>(StateProperties);
	AllStates[EGameFeaturePluginState::CheckingStatus] = MakeUnique<FGameFeaturePluginState_CheckingStatus>(StateProperties);
	AllStates[EGameFeaturePluginState::StatusKnown] = MakeUnique<FGameFeaturePluginState_StatusKnown>(StateProperties);
	AllStates[EGameFeaturePluginState::Uninstalling] = MakeUnique<FGameFeaturePluginState_Uninstalling>(StateProperties);
	AllStates[EGameFeaturePluginState::Downloading] = MakeUnique<FGameFeaturePluginState_Downloading>(StateProperties);
	AllStates[EGameFeaturePluginState::Installed] = MakeUnique<FGameFeaturePluginState_Installed>(StateProperties);
	AllStates[EGameFeaturePluginState::Unmounting] = MakeUnique<FGameFeaturePluginState_Unmounting>(StateProperties);
	AllStates[EGameFeaturePluginState::Mounting] = MakeUnique<FGameFeaturePluginState_Mounting>(StateProperties);
	AllStates[EGameFeaturePluginState::WaitingForDependencies] = MakeUnique<FGameFeaturePluginState_WaitingForDependencies>(StateProperties);
	AllStates[EGameFeaturePluginState::Unregistering] = MakeUnique<FGameFeaturePluginState_Unregistering>(StateProperties);
	AllStates[EGameFeaturePluginState::Registering] = MakeUnique<FGameFeaturePluginState_Registering>(StateProperties);
	AllStates[EGameFeaturePluginState::Registered] = MakeUnique<FGameFeaturePluginState_Registered>(StateProperties);
	AllStates[EGameFeaturePluginState::Unloading] = MakeUnique<FGameFeaturePluginState_Unloading>(StateProperties);
	AllStates[EGameFeaturePluginState::Loading] = MakeUnique<FGameFeaturePluginState_Loading>(StateProperties);
	AllStates[EGameFeaturePluginState::Loaded] = MakeUnique<FGameFeaturePluginState_Loaded>(StateProperties);
	AllStates[EGameFeaturePluginState::Deactivating] = MakeUnique<FGameFeaturePluginState_Deactivating>(StateProperties);
	AllStates[EGameFeaturePluginState::Activating] = MakeUnique<FGameFeaturePluginState_Activating>(StateProperties);
	AllStates[EGameFeaturePluginState::Active] = MakeUnique<FGameFeaturePluginState_Active>(StateProperties);

	AllStates[CurrentStateInfo.State]->BeginState();
}

void UGameFeaturePluginStateMachine::SetDestinationState(EGameFeaturePluginState::Type InDestinationState, FGameFeatureStateTransitionComplete OnFeatureStateTransitionComplete)
{
	check(IsValidDestinationState(InDestinationState));
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

bool UGameFeaturePluginStateMachine::GetPluginFilename(FString& OutPluginFilename) const
{
	OutPluginFilename = StateProperties.PluginInstalledFilename;
	return !OutPluginFilename.IsEmpty();
}

EGameFeaturePluginState::Type UGameFeaturePluginStateMachine::GetCurrentState() const
{
	return GetCurrentStateInfo().State;
}

EGameFeaturePluginState::Type UGameFeaturePluginStateMachine::GetDestinationState() const
{
	return StateProperties.DestinationState;
}

const FGameFeaturePluginStateInfo& UGameFeaturePluginStateMachine::GetCurrentStateInfo() const
{
	return CurrentStateInfo;
}

bool UGameFeaturePluginStateMachine::IsStatusKnown() const
{
	return GetCurrentState() >= EGameFeaturePluginState::StatusKnown;
}

bool UGameFeaturePluginStateMachine::IsAvailable() const
{
	ensure(IsStatusKnown());
	return StateProperties.bIsAvailable;
}

UGameFeatureData* UGameFeaturePluginStateMachine::GetGameFeatureDataForActivePlugin()
{
	if (GetCurrentState() == EGameFeaturePluginState::Active)
	{
		return StateProperties.GameFeatureData;
	}

	return nullptr;
}

bool UGameFeaturePluginStateMachine::IsValidDestinationState(EGameFeaturePluginState::Type InDestinationState) const
{
	check(InDestinationState >= 0 && InDestinationState < EGameFeaturePluginState::MAX);
	return AllStates[InDestinationState]->CanBeDestinationState();
}

void UGameFeaturePluginStateMachine::UpdateStateMachine()
{
	if (bInUpdateStateMachine)
	{
		return;
	}

	TGuardValue<bool> ScopeGuard(bInUpdateStateMachine, true);

	UE::GameFeatures::FResult TransitionResult(MakeValue());
	bool bKeepProcessing = false;
	int32 NumTransitions = 0;
	const int32 MaxTransitions = 10000;
	EGameFeaturePluginState::Type CurrentState = GetCurrentState();
	check(CurrentState >= 0 && CurrentState < EGameFeaturePluginState::MAX);
	do
	{
		bKeepProcessing = false;

		FGameFeaturePluginStateStatus StateStatus;
		AllStates[CurrentState]->UpdateState(StateStatus);

		//@TODO: Collapse once TValueOrError operator= works
		if (StateStatus.TransitionResult.HasValue())
		{
			TransitionResult = MakeValue();
		}
		else
		{
			TransitionResult = MakeError(StateStatus.TransitionResult.GetError());
		}

		if (StateStatus.TransitionToState == CurrentState)
		{
			UE_LOG(LogGameFeatures, Fatal, TEXT("Game feature state %s transitioning to itself. GameFeature: %s"), *EGameFeaturePluginState::ToString(CurrentState), *GetGameFeatureName());
		}

		if (StateStatus.TransitionToState != EGameFeaturePluginState::Uninitialized)
		{
			UE_LOG(LogGameFeatures, Display, TEXT("Game feature '%s' transitioning state (%s -> %s)"), *GetGameFeatureName(), *EGameFeaturePluginState::ToString(CurrentState), *EGameFeaturePluginState::ToString(StateStatus.TransitionToState));
			AllStates[CurrentState]->EndState();
			CurrentStateInfo = FGameFeaturePluginStateInfo(StateStatus.TransitionToState);
			CurrentState = StateStatus.TransitionToState;
			check(CurrentState >= 0 && CurrentState < EGameFeaturePluginState::MAX);
			AllStates[CurrentState]->BeginState();
			OnStateChangedEvent.Broadcast(this);
			bKeepProcessing = true;
		}

		if (!TransitionResult.HasValue())
		{
			StateProperties.DestinationState = CurrentState;
			break;
		}

		if (NumTransitions++ > MaxTransitions)
		{
			UE_LOG(LogGameFeatures, Fatal, TEXT("Infinite loop in game feature state machine transitions. Current state %s. GameFeature: %s"), *EGameFeaturePluginState::ToString(CurrentState), *GetGameFeatureName());
		}
	} while (bKeepProcessing);

	if (CurrentState == StateProperties.DestinationState)
	{
		StateProperties.OnFeatureStateTransitionComplete.ExecuteIfBound(this, TransitionResult);
		StateProperties.OnFeatureStateTransitionComplete.Unbind();
	}
}

FGameFeaturePluginStateMachineProperties::FGameFeaturePluginStateMachineProperties(
	const FString& InPluginURL,
	EGameFeaturePluginState::Type DesiredDestination,
	const FGameFeaturePluginRequestStateMachineDependencies& RequestStateMachineDependenciesDelegate,
	const TDelegate<void()>& RequestUpdateStateMachineDelegate)
	: FGameFeaturePluginStateMachineProperties()
{
	PluginURL = InPluginURL;
	DestinationState = DesiredDestination;
	OnRequestStateMachineDependencies = RequestStateMachineDependenciesDelegate;
	OnRequestUpdateStateMachine = RequestUpdateStateMachineDelegate;
}

EGameFeaturePluginProtocol FGameFeaturePluginStateMachineProperties::GetPluginProtocol() const
{
	if (PluginURL.StartsWith(FileProtocolPrefix()))
	{
		return EGameFeaturePluginProtocol::File;
	}
	else
	{
		return EGameFeaturePluginProtocol::Web;
	}
}

FString FGameFeaturePluginStateMachineProperties::FileProtocolPrefix()
{
	const static FString ProtocolPrefixString("file:");
	return ProtocolPrefixString;
}

FString FGameFeaturePluginStateMachineProperties::WebProtocolPrefix()
{
	const static FString ProtocolPrefixString("web:");
	return ProtocolPrefixString;
}