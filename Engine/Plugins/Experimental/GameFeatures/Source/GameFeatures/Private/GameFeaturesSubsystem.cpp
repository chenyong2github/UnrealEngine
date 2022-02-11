// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeaturesSubsystem.h"
#include "GameFeaturesSubsystemSettings.h"
#include "GameFeaturesProjectPolicies.h"
#include "GameFeatureData.h"
#include "GameFeaturePluginStateMachine.h"
#include "GameFeatureStateChangeObserver.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Containers/Ticker.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManagerSettings.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY(LogGameFeatures);

void FGameFeatureStateChangeContext::SetRequiredWorldContextHandle(FName Handle)
{
	WorldContextHandle = Handle;
}

bool FGameFeatureStateChangeContext::ShouldApplyToWorldContext(const FWorldContext& WorldContext) const
{
	if (WorldContextHandle.IsNone())
	{
		return true;
	}
	if (WorldContext.ContextHandle == WorldContextHandle)
	{
		return true;
	}
	return false;
}

bool FGameFeatureStateChangeContext::ShouldApplyUsingOtherContext(const FGameFeatureStateChangeContext& OtherContext) const
{
	if (OtherContext == *this)
	{
		return true;
	}

	// If other context is less restrictive, apply
	if (OtherContext.WorldContextHandle.IsNone())
	{
		return true;
	}

	return false;
}

void UGameFeaturesSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UE_LOG(LogGameFeatures, Log, TEXT("Initializing game features subsystem"));

	// Create the game-specific policy manager
	check(!bInitializedPolicyManager && (GameSpecificPolicies == nullptr));

	const FSoftClassPath& PolicyClassPath = GetDefault<UGameFeaturesSubsystemSettings>()->GameFeaturesManagerClassName;

	UClass* SingletonClass = nullptr;
	if (!PolicyClassPath.IsNull())
	{
		SingletonClass = LoadClass<UGameFeaturesProjectPolicies>(nullptr, *PolicyClassPath.ToString());
	}

	if (SingletonClass == nullptr)
	{
		SingletonClass = UDefaultGameFeaturesProjectPolicies::StaticClass();
	}
		
	GameSpecificPolicies = NewObject<UGameFeaturesProjectPolicies>(this, SingletonClass);
	check(GameSpecificPolicies);

	UAssetManager::CallOrRegister_OnAssetManagerCreated(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAssetManagerCreated));

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ListGameFeaturePlugins"),
		TEXT("Prints game features plugins and their current state to log. (options: [-activeonly] [-alphasort] [-csv])"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateUObject(this, &ThisClass::ListGameFeaturePlugins),
		ECVF_Default);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("LoadGameFeaturePlugin"),
		TEXT("Loads and activates a game feature plugin by URL"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
		{
			if (Args.Num() > 0)
			{
				FString PluginURL;
				if (!UGameFeaturesSubsystem::Get().GetPluginURLForBuiltInPluginByName(Args[0], /*out*/ PluginURL))
				{
					PluginURL = Args[0];
				}
				UGameFeaturesSubsystem::Get().LoadAndActivateGameFeaturePlugin(PluginURL, FGameFeaturePluginLoadComplete());
			}
			else
			{
				Ar.Logf(TEXT("Expected a game feature plugin URL as an argument"));
			}
		}),
		ECVF_Cheat);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("DeactivateGameFeaturePlugin"),
		TEXT("Deactivates a game feature plugin by URL"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
		{
			if (Args.Num() > 0)
			{
				FString PluginURL;
				if (!UGameFeaturesSubsystem::Get().GetPluginURLForBuiltInPluginByName(Args[0], /*out*/ PluginURL))
				{
					PluginURL = Args[0];
				}
				UGameFeaturesSubsystem::Get().DeactivateGameFeaturePlugin(PluginURL, FGameFeaturePluginLoadComplete());
			}
			else
			{
				Ar.Logf(TEXT("Expected a game feature plugin URL as an argument"));
			}
		}),
		ECVF_Cheat);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnloadGameFeaturePlugin"),
		TEXT("Unloads a game feature plugin by URL"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
		{
			if (Args.Num() > 0)
			{
				FString PluginURL;
				if (!UGameFeaturesSubsystem::Get().GetPluginURLForBuiltInPluginByName(Args[0], /*out*/ PluginURL))
				{
					PluginURL = Args[0];
				}
				UGameFeaturesSubsystem::Get().UnloadGameFeaturePlugin(PluginURL);
			}
			else
			{
				Ar.Logf(TEXT("Expected a game feature plugin URL as an argument"));
			}
		}),
		ECVF_Cheat);
}

void UGameFeaturesSubsystem::Deinitialize()
{
	UE_LOG(LogGameFeatures, Log, TEXT("Shutting down game features subsystem"));

	if ((GameSpecificPolicies != nullptr) && bInitializedPolicyManager)
	{
		GameSpecificPolicies->ShutdownGameFeatureManager();
	}
	GameSpecificPolicies = nullptr;
	bInitializedPolicyManager = false;
}

void UGameFeaturesSubsystem::OnAssetManagerCreated()
{
	check(!bInitializedPolicyManager && (GameSpecificPolicies != nullptr));

	// Make sure the game has the appropriate asset manager configuration or we won't be able to load game feature data assets
	FPrimaryAssetId DummyGameFeatureDataAssetId(UGameFeatureData::StaticClass()->GetFName(), NAME_None);
	FPrimaryAssetRules GameDataRules = UAssetManager::Get().GetPrimaryAssetRules(DummyGameFeatureDataAssetId);
	if (GameDataRules.IsDefault())
	{
		UE_LOG(LogGameFeatures, Error, TEXT("Asset manager settings do not include a rule for assets of type %s, which is required for game feature plugins to function"), *UGameFeatureData::StaticClass()->GetName());
	}

	// Create the game-specific policy
	UE_LOG(LogGameFeatures, Verbose, TEXT("Initializing game features policy (type %s)"), *GameSpecificPolicies->GetClass()->GetName());
	GameSpecificPolicies->InitGameFeatureManager();
	bInitializedPolicyManager = true;
}

TSharedPtr<FStreamableHandle> UGameFeaturesSubsystem::LoadGameFeatureData(const FString& GameFeatureToLoad)
{
	UAssetManager& LocalAssetManager = UAssetManager::Get();
	IAssetRegistry& LocalAssetRegistry = LocalAssetManager.GetAssetRegistry();

#if WITH_EDITOR
	const FString GameFeaturePackageName = FPackageName::ObjectPathToPackageName(GameFeatureToLoad);
	LocalAssetRegistry.ScanFilesSynchronous({ FPackageName::LongPackageNameToFilename(GameFeaturePackageName, FPackageName::GetAssetPackageExtension()) });
#endif // WITH_EDITOR

	FAssetData GameFeatureAssetData = LocalAssetRegistry.GetAssetByObjectPath(FName(*GameFeatureToLoad));
	if (GameFeatureAssetData.IsValid())
	{
		FPrimaryAssetId AssetId = GameFeatureAssetData.GetPrimaryAssetId();

#if WITH_EDITOR
		// Support for pre-primary data asset game feature data, or game feature data copied from another plugin without being resaved.
		FString PluginRoot;
		FString ExpectedPluginRoot = FString::Printf(TEXT("/%s/"), *AssetId.PrimaryAssetName.ToString());
		if (!AssetId.IsValid() || (UAssetManager::GetContentRootPathFromPackageName(GameFeaturePackageName, PluginRoot) && (PluginRoot != ExpectedPluginRoot)))
		{
			if (UObject* LoadedObject = GameFeatureAssetData.GetAsset())
			{
				AssetId = LoadedObject->GetPrimaryAssetId();
				GameFeatureAssetData = FAssetData(LoadedObject);
			}
		}
#endif // WITH_EDITOR

		// Add the GameFeatureData itself to the primary asset list
		LocalAssetManager.RegisterSpecificPrimaryAsset(AssetId, GameFeatureAssetData);

		// LoadPrimaryAsset will return a null handle if the AssetID is already loaded. Check if there is an existing handle first.
		TSharedPtr<FStreamableHandle> ReturnHandle = LocalAssetManager.GetPrimaryAssetHandle(AssetId);
		if (ReturnHandle.IsValid())
		{
			return ReturnHandle;
		}
		else
		{
			return LocalAssetManager.LoadPrimaryAsset(AssetId);
		}
	}

	return nullptr;
}

void UGameFeaturesSubsystem::UnloadGameFeatureData(const UGameFeatureData* GameFeatureToUnload)
{
	UAssetManager& LocalAssetManager = UAssetManager::Get();
	LocalAssetManager.UnloadPrimaryAsset(GameFeatureToUnload->GetPrimaryAssetId());
}

void UGameFeaturesSubsystem::AddGameFeatureToAssetManager(const UGameFeatureData* GameFeatureToAdd, const FString& PluginName)
{
	check(GameFeatureToAdd);
	FString PluginRootPath = TEXT("/") + PluginName + TEXT("/");
	UAssetManager& LocalAssetManager = UAssetManager::Get();

	LocalAssetManager.PushBulkScanning();

	for (FPrimaryAssetTypeInfo TypeInfo : GameFeatureToAdd->GetPrimaryAssetTypesToScan())
	{
		for (FDirectoryPath& Path : TypeInfo.Directories)
		{
			// Convert plugin-relative paths to full package paths
			FixPluginPackagePath(Path.Path, PluginRootPath, false);
		}

		// This function also fills out runtime data on the copy
		if (!LocalAssetManager.ShouldScanPrimaryAssetType(TypeInfo))
		{
			continue;
		}

		FPrimaryAssetTypeInfo ExistingAssetTypeInfo;
		const bool bAlreadyExisted = LocalAssetManager.GetPrimaryAssetTypeInfo(FPrimaryAssetType(TypeInfo.PrimaryAssetType), /*out*/ ExistingAssetTypeInfo);
		const bool bForceSynchronousScan = false; // We just mounted the folder that contains these primary assets and the editor background scan is not going to be finished by the time this is called, but a rescan will happen later in OnAssetRegistryFilesLoaded
		LocalAssetManager.ScanPathsForPrimaryAssets(TypeInfo.PrimaryAssetType, TypeInfo.AssetScanPaths, TypeInfo.AssetBaseClassLoaded, TypeInfo.bHasBlueprintClasses, TypeInfo.bIsEditorOnly, bForceSynchronousScan);

		if (!bAlreadyExisted)
		{
			// If we did not previously scan anything for a primary asset type that is in our config, try to reuse the cook rules from the config instead of the one in the gamefeaturedata, which should not be modifying cook rules
			const FPrimaryAssetTypeInfo* ConfigTypeInfo = LocalAssetManager.GetSettings().PrimaryAssetTypesToScan.FindByPredicate([&TypeInfo](const FPrimaryAssetTypeInfo& PATI) -> bool { return PATI.PrimaryAssetType == TypeInfo.PrimaryAssetType; });
			if (ConfigTypeInfo)
			{
				LocalAssetManager.SetPrimaryAssetTypeRules(TypeInfo.PrimaryAssetType, ConfigTypeInfo->Rules);
			}
			else
			{
				LocalAssetManager.SetPrimaryAssetTypeRules(TypeInfo.PrimaryAssetType, TypeInfo.Rules);
			}
		}
	}

	LocalAssetManager.PopBulkScanning();
}

void UGameFeaturesSubsystem::RemoveGameFeatureFromAssetManager(const UGameFeatureData* GameFeatureToRemove)
{
	/** NOT IMPLEMENTED - STUB */
}

void UGameFeaturesSubsystem::AddObserver(UObject* Observer)
{
	//@TODO: GameFeaturePluginEnginePush: May want to warn if one is added after any game feature plugins are already initialized, or go to a CallOrRegister sort of pattern
	check(Observer);
	if (ensureAlwaysMsgf(Cast<IGameFeatureStateChangeObserver>(Observer) != nullptr, TEXT("Observers must implement the IGameFeatureStateChangeObserver interface.")))
	{
		Observers.Add(Observer);
	}
}

void UGameFeaturesSubsystem::RemoveObserver(UObject* Observer)
{
	check(Observer);
	Observers.Remove(Observer);
}

FString UGameFeaturesSubsystem::GetPluginURL_FileProtocol(const FString& PluginDescriptorPath)
{
	return TEXT("file:") + PluginDescriptorPath;
}

FString UGameFeaturesSubsystem::GetPluginURL_InstallBundleProtocol(const FString& PluginName, TArrayView<const FString> BundleNames)
{
	ensure(BundleNames.Num() > 0);

	FString Path;
	Path += TEXT("installbundle:");
	Path += PluginName;
	Path += TEXT("?");
	Path += FString::Join(BundleNames, TEXT(","));

	return Path;
}

FString UGameFeaturesSubsystem::GetPluginURL_InstallBundleProtocol(const FString& PluginName, const FString& BundleName)
{
	return GetPluginURL_InstallBundleProtocol(PluginName, MakeArrayView(&BundleName, 1));
}

FString UGameFeaturesSubsystem::GetPluginURL_InstallBundleProtocol(const FString& PluginName, const TArrayView<const FName> BundleNames)
{
	ensure(BundleNames.Num() > 0);

	FString Path;
	Path += TEXT("installbundle:");
	Path += PluginName;
	Path += TEXT("?");
	Path += FString::JoinBy(BundleNames, TEXT(","), UE_PROJECTION_MEMBER(FName, ToString));

	return Path;
}

FString UGameFeaturesSubsystem::GetPluginURL_InstallBundleProtocol(const FString& PluginName, FName BundleName)
{
	return GetPluginURL_InstallBundleProtocol(PluginName, MakeArrayView(&BundleName, 1));
}

void UGameFeaturesSubsystem::OnGameFeatureTerminating(const FString& PluginURL)
{
	for (UObject* Observer : Observers)
	{
		CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureTerminating(PluginURL);
	}
}

void UGameFeaturesSubsystem::OnGameFeatureCheckingStatus(const FString& PluginURL)
{
	for (UObject* Observer : Observers)
	{
		CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureCheckingStatus(PluginURL);
	}
}

void UGameFeaturesSubsystem::OnGameFeatureRegistering(const UGameFeatureData* GameFeatureData, const FString& PluginName)
{
	check(GameFeatureData);
	AddGameFeatureToAssetManager(GameFeatureData, PluginName);

	for (UObject* Observer : Observers)
	{
		CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureRegistering(GameFeatureData, PluginName);
	}

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureRegistering();
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureUnregistering(const UGameFeatureData* GameFeatureData, const FString& PluginName)
{
	check(GameFeatureData);

	for (UObject* Observer : Observers)
	{
		CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureUnregistering(GameFeatureData, PluginName);
	}

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureUnregistering();
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureLoading(const UGameFeatureData* GameFeatureData)
{
	check(GameFeatureData);
	for (UObject* Observer : Observers)
	{
		CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureLoading(GameFeatureData);
	}

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureLoading();
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureActivating(const UGameFeatureData* GameFeatureData, const FString& PluginName, FGameFeatureActivatingContext& Context)
{
	check(GameFeatureData);

	for (UObject* Observer : Observers)
	{
		CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureActivating(GameFeatureData);
	}
	
	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureActivating(Context);
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureDeactivating(const UGameFeatureData* GameFeatureData, const FString& PluginName, FGameFeatureDeactivatingContext& Context)
{
	check(GameFeatureData);

	for (UObject* Observer : Observers)
	{
		CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureDeactivating(GameFeatureData, Context);
	}

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureDeactivating(Context);
		}
	}

	RemoveGameFeatureFromAssetManager(GameFeatureData);
}

const UGameFeatureData* UGameFeaturesSubsystem::GetDataForStateMachine(UGameFeaturePluginStateMachine* GFSM) const
{
	return GFSM->GetGameFeatureDataForActivePlugin();
}

const UGameFeatureData* UGameFeaturesSubsystem::GetRegisteredDataForStateMachine(UGameFeaturePluginStateMachine* GFSM) const
{
	return GFSM->GetGameFeatureDataForRegisteredPlugin();
}

void UGameFeaturesSubsystem::GetGameFeatureDataForActivePlugins(TArray<const UGameFeatureData*>& OutActivePluginFeatureDatas)
{
	for (auto StateMachineIt = GameFeaturePluginStateMachines.CreateConstIterator(); StateMachineIt; ++StateMachineIt)
	{
		if (UGameFeaturePluginStateMachine* GFSM = StateMachineIt.Value())
		{
			if (const UGameFeatureData* GameFeatureData = GFSM->GetGameFeatureDataForActivePlugin())
			{
				OutActivePluginFeatureDatas.Add(GameFeatureData);
			}
		}
	}
}

const UGameFeatureData* UGameFeaturesSubsystem::GetGameFeatureDataForActivePluginByURL(const FString& PluginURL)
{
	if (UGameFeaturePluginStateMachine* GFSM = FindGameFeaturePluginStateMachine(PluginURL))
	{
		return GFSM->GetGameFeatureDataForActivePlugin();
	}

	return nullptr;
}

const UGameFeatureData* UGameFeaturesSubsystem::GetGameFeatureDataForRegisteredPluginByURL(const FString& PluginURL)
{
	if (UGameFeaturePluginStateMachine* GFSM = FindGameFeaturePluginStateMachine(PluginURL))
	{
		return GFSM->GetGameFeatureDataForRegisteredPlugin();
	}

	return nullptr;
}

void UGameFeaturesSubsystem::LoadGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginLoadComplete& CompleteDelegate)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		if (StateMachine->GetCurrentState() >= EGameFeaturePluginState::Loaded)
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, CompleteDelegate](float dts)
				{
					CompleteDelegate.ExecuteIfBound(MakeValue());
					return false;
				}));

			return; // Early out, we are already loaded.
		}
	}

	ChangeGameFeatureTargetState(PluginURL, EGameFeatureTargetState::Loaded, CompleteDelegate);
}

void UGameFeaturesSubsystem::LoadAndActivateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginLoadComplete& CompleteDelegate)
{
	ChangeGameFeatureTargetState(PluginURL, EGameFeatureTargetState::Active, CompleteDelegate);
}

void UGameFeaturesSubsystem::ChangeGameFeatureTargetState(const FString& PluginURL, EGameFeatureTargetState TargetState, const FGameFeaturePluginChangeStateComplete& CompleteDelegate)
{
	EGameFeaturePluginState TargetPluginState = EGameFeaturePluginState::MAX;
	if (TargetState == EGameFeatureTargetState::Active)
	{
		TargetPluginState = EGameFeaturePluginState::Active;
	}
	else if (TargetState == EGameFeatureTargetState::Loaded)
	{
		TargetPluginState = EGameFeaturePluginState::Loaded;
	}
	else if (TargetState == EGameFeatureTargetState::Registered)
	{
		TargetPluginState = EGameFeaturePluginState::Registered;
	}
	else if (TargetState == EGameFeatureTargetState::Installed)
	{
		TargetPluginState = EGameFeaturePluginState::Installed;
	}

	if (TargetPluginState != EGameFeaturePluginState::MAX)
	{
		if (UGameFeaturePluginStateMachine* StateMachine = FindOrCreateGameFeaturePluginStateMachine(PluginURL))
		{
			if (TargetState == EGameFeatureTargetState::Active
				&& StateMachine->GetCurrentState() == TargetPluginState)
			{
				// TODO: Resolve the activated case here, this is needed because in a PIE environment the plugins
				// are not sandboxed, and we need to do simulate a successful activate call in order run GFP systems 
				// on whichever Role runs second between client and server.

				// Refire the observer for Activated and do nothing else.
				for (UObject* Observer : Observers)
				{
					CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureActivating(StateMachine->GetGameFeatureDataForActivePlugin());
				}

				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [CompleteDelegate](float)
					{
						CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeValue()));
						return false;
					}));
			}
			else if (TargetPluginState > StateMachine->GetCurrentState()
				&& !GameSpecificPolicies->IsPluginAllowed(PluginURL))
			{
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [CompleteDelegate](float)
					{
						CompleteDelegate.ExecuteIfBound(MakeError(TEXT("GameFeaturePlugin.StateMachine.Plugin_Denied_By_GameSpecificPolicy")));
						return false;
					}));
			}
			else
			{
				StateMachine->SetDestinationState(TargetPluginState, FGameFeatureStateTransitionComplete::CreateUObject(this, &ThisClass::ChangeGameFeatureTargetStateComplete, CompleteDelegate));
			}
		}
	}
}

bool UGameFeaturesSubsystem::GetGameFeaturePluginInstallPercent(const FString& PluginURL, float& Install_Percent) const
{
	if (const UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		if (StateMachine->IsStatusKnown() && StateMachine->IsAvailable())
		{
			const FGameFeaturePluginStateInfo& StateInfo = StateMachine->GetCurrentStateInfo();
			if (StateInfo.State == EGameFeaturePluginState::Downloading)
			{
				Install_Percent = StateInfo.Progress;
			}
			else if (StateMachine->GetDestinationState() >= EGameFeaturePluginState::Installed && StateInfo.State >= EGameFeaturePluginState::Installed)
			{
				Install_Percent = 1.0f;
			}
			else
			{
				Install_Percent = 0.0f;
			}
			return true;
		}
	}
	return false;
}

bool UGameFeaturesSubsystem::IsGameFeaturePluginActive(const FString& PluginURL, bool bCheckForActivating /*= false*/) const
{
	if (const UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		const EGameFeaturePluginState CurrentState = StateMachine->GetCurrentState();

		return CurrentState == EGameFeaturePluginState::Active || (bCheckForActivating && CurrentState == EGameFeaturePluginState::Activating);
	}

	return false;
}

void UGameFeaturesSubsystem::DeactivateGameFeaturePlugin(const FString& PluginURL)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		if (StateMachine->GetDestinationState() > EGameFeaturePluginState::Loaded)
		{
			FGameFeaturePluginDeactivateComplete Callback = FGameFeaturePluginDeactivateComplete();
			DeactivateGameFeaturePlugin(PluginURL, Callback);
		}
	}
}

void UGameFeaturesSubsystem::DeactivateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginDeactivateComplete& CompleteDelegate)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		if (StateMachine->GetDestinationState() <= EGameFeaturePluginState::Loaded)
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [CompleteDelegate](float)
				{
					CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeValue()));
					return false;
				}));
		}
		else
		{
			ChangeGameFeatureTargetState(PluginURL, EGameFeatureTargetState::Loaded, CompleteDelegate);
		}
	}
	else
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [CompleteDelegate](float)
			{
				CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeError(TEXT("GameFeaturePlugin.BadURL"))));
				return false;
			}));
	}
}

void UGameFeaturesSubsystem::UnloadGameFeaturePlugin(const FString& PluginURL, bool bKeepRegistered)
{
	ChangeGameFeatureTargetState(PluginURL,
		bKeepRegistered ? EGameFeatureTargetState::Registered : EGameFeatureTargetState::Installed,
		FGameFeaturePluginUnloadComplete());
}

void UGameFeaturesSubsystem::UnloadGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUnloadComplete& CompleteDelegate, bool bKeepRegistered)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		if (StateMachine->GetDestinationState() <= EGameFeaturePluginState::Loaded)
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [CompleteDelegate](float)
				{
					CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeValue()));
					return false;
				}));
		}
		else
		{
			ChangeGameFeatureTargetState(PluginURL,
				bKeepRegistered ? EGameFeatureTargetState::Registered : EGameFeatureTargetState::Installed,
				CompleteDelegate);
		}
	}
	else
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [CompleteDelegate](float)
			{
				CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeError(TEXT("GameFeaturePlugin.BadURL"))));
				return false;
			}));
	}
}

void UGameFeaturesSubsystem::UninstallGameFeaturePlugin(const FString& PluginURL)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		if (StateMachine->GetDestinationState() > EGameFeaturePluginState::StatusKnown)
		{
			FGameFeaturePluginUninstallComplete Callback = FGameFeaturePluginUninstallComplete();
			UninstallGameFeaturePlugin(PluginURL, Callback);
		}
	}
}

void UGameFeaturesSubsystem::UninstallGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUninstallComplete& CompleteDelegate)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		ensureAlwaysMsgf(StateMachine->GetCurrentState() == StateMachine->GetDestinationState(), TEXT("Setting a new destination state while state machine is running!"));

		if (StateMachine->GetCurrentState() > EGameFeaturePluginState::StatusKnown)
		{
			StateMachine->SetDestinationState(EGameFeaturePluginState::StatusKnown, FGameFeatureStateTransitionComplete::CreateUObject(this, &ThisClass::ChangeGameFeatureTargetStateComplete, CompleteDelegate));
		}
		else if (StateMachine->GetCurrentState() <= EGameFeaturePluginState::StatusKnown)
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, CompleteDelegate](float dts)
			{
				CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeValue()));
				return false;
			}));
		}
	}
	else
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, CompleteDelegate](float dts)
		{
			CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeError(TEXT("GameFeaturePlugin.BadURL"))));
			return false;
		}));
	}
}

void UGameFeaturesSubsystem::TerminateGameFeaturePlugin(const FString& PluginURL)
{
	FGameFeaturePluginUninstallComplete Callback = FGameFeaturePluginUninstallComplete();
	TerminateGameFeaturePlugin(PluginURL, Callback);
}

void UGameFeaturesSubsystem::TerminateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUninstallComplete& CompleteDelegate)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		ensureAlwaysMsgf(StateMachine->GetCurrentState() == StateMachine->GetDestinationState(), TEXT("Setting a new destination state while state machine is running!"));

		if (StateMachine->GetCurrentState() > EGameFeaturePluginState::Terminal)
		{
			StateMachine->SetDestinationState(EGameFeaturePluginState::Terminal, FGameFeatureStateTransitionComplete::CreateUObject(this, &ThisClass::TerminateGameFeaturePluginComplete, CompleteDelegate));
		}
		else if (StateMachine->GetCurrentState() <= EGameFeaturePluginState::Terminal)
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, CompleteDelegate](float dts)
			{
				CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeValue()));
				return false;
			}));
		}
	}
	else
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, CompleteDelegate](float dts)
		{
			CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeError(TEXT("GameFeaturePlugin.BadURL"))));
			return false;
		}));
	}
}

void UGameFeaturesSubsystem::LoadBuiltInGameFeaturePlugin(const TSharedRef<IPlugin>& Plugin, FBuiltInPluginAdditionalFilters AdditionalFilter)
{
	UAssetManager::Get().PushBulkScanning();

	const FString& PluginDescriptorFilename = Plugin->GetDescriptorFileName();

	// Make sure you are in a game feature plugins folder. All GameFeaturePlugins are rooted in a GameFeatures folder.
	if (!PluginDescriptorFilename.IsEmpty() && GetDefault<UGameFeaturesSubsystemSettings>()->IsValidGameFeaturePlugin(FPaths::ConvertRelativePathToFull(PluginDescriptorFilename)) && FPaths::FileExists(PluginDescriptorFilename))
	{
		const FString PluginURL = GetPluginURL_FileProtocol(PluginDescriptorFilename);
		if (GameSpecificPolicies->IsPluginAllowed(PluginURL))
		{
			FGameFeaturePluginDetails PluginDetails;
			if (GetGameFeaturePluginDetails(PluginDescriptorFilename, PluginDetails))
			{
				FBuiltInGameFeaturePluginBehaviorOptions BehaviorOptions;
				bool bShouldProcess = AdditionalFilter(PluginDescriptorFilename, PluginDetails, BehaviorOptions);
				if (bShouldProcess)
				{
					UGameFeaturePluginStateMachine* StateMachine = FindOrCreateGameFeaturePluginStateMachine(PluginURL);

					const EBuiltInAutoState InitialAutoState = (BehaviorOptions.AutoStateOverride != EBuiltInAutoState::Invalid) ? BehaviorOptions.AutoStateOverride : PluginDetails.BuiltInAutoState;
						
					const EGameFeaturePluginState DestinationState = ConvertInitialFeatureStateToTargetState(InitialAutoState);

					if (StateMachine->GetCurrentState() >= DestinationState)
					{
						// If we're already at the destination or beyond, don't transition back
						LoadGameFeaturePluginComplete(StateMachine, MakeValue());
					}
					else
					{
						StateMachine->SetDestinationState(DestinationState, FGameFeatureStateTransitionComplete::CreateUObject(this, &ThisClass::LoadGameFeaturePluginComplete));
					}

					if (!GameFeaturePluginNameToPathMap.Contains(Plugin->GetName()))
					{
						GameFeaturePluginNameToPathMap.Add(Plugin->GetName(), PluginURL);
					}
				}
			}
		}
	}
}

void UGameFeaturesSubsystem::LoadBuiltInGameFeaturePlugins(FBuiltInPluginAdditionalFilters AdditionalFilter)
{
	UAssetManager::Get().PushBulkScanning();

	TArray<TSharedRef<IPlugin>> EnabledPlugins = IPluginManager::Get().GetEnabledPlugins();
	for (const TSharedRef<IPlugin>& Plugin : EnabledPlugins)
	{
		LoadBuiltInGameFeaturePlugin(Plugin, AdditionalFilter);
	}

	UAssetManager::Get().PopBulkScanning();
}

bool UGameFeaturesSubsystem::GetPluginURLForBuiltInPluginByName(const FString& PluginName, FString& OutPluginURL) const
{
	if (const FString* PluginURL = GameFeaturePluginNameToPathMap.Find(PluginName))
	{
		OutPluginURL = *PluginURL;
		return true;
	}

	return false;
}

FString UGameFeaturesSubsystem::GetPluginFilenameFromPluginURL(const FString& PluginURL) const
{
	FString PluginFilename;
	if (const UGameFeaturePluginStateMachine* GFSM = FindGameFeaturePluginStateMachine(PluginURL))
	{
		GFSM->GetPluginFilename(PluginFilename);
	}
	else
	{
		UE_LOG(LogGameFeatures, Error, TEXT("UGameFeaturesSubsystem could not get the plugin path from the plugin URL. URL:%s "), *PluginURL);
	}
	return PluginFilename;
}

void UGameFeaturesSubsystem::FixPluginPackagePath(FString& PathToFix, const FString& PluginRootPath, bool bMakeRelativeToPluginRoot)
{
	if (bMakeRelativeToPluginRoot)
	{
		// This only modifies paths starting with the root
		PathToFix.RemoveFromStart(PluginRootPath);
	}
	else
	{
		if (!FPackageName::IsValidLongPackageName(PathToFix))
		{
			PathToFix = PluginRootPath / PathToFix;
		}
	}
}

void UGameFeaturesSubsystem::GetLoadedGameFeaturePluginFilenamesForCooking(TArray<FString>& OutLoadedPluginFilenames) const
{
	for (auto StateMachineIt = GameFeaturePluginStateMachines.CreateConstIterator(); StateMachineIt; ++StateMachineIt)
	{
		UGameFeaturePluginStateMachine* GFSM = StateMachineIt.Value();
		if (GFSM && GFSM->GetCurrentState() > EGameFeaturePluginState::Installed)
		{
			FString PluginFilename;
			if (GFSM->GetPluginFilename(PluginFilename))
			{
				OutLoadedPluginFilenames.Add(PluginFilename);
			}
		}
	}
}

EGameFeaturePluginState UGameFeaturesSubsystem::GetPluginState(const FString& PluginURL)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		return StateMachine->GetCurrentState();
	}
	else
	{
		return EGameFeaturePluginState::UnknownStatus;
	}
}

bool UGameFeaturesSubsystem::GetGameFeaturePluginDetails(const FString& PluginDescriptorFilename, FGameFeaturePluginDetails& OutPluginDetails) const
{
	// Read the file to a string
	FString FileContents;
	if (!FFileHelper::LoadFileToString(FileContents, *PluginDescriptorFilename))
	{
		UE_LOG(LogGameFeatures, Error, TEXT("UGameFeaturesSubsystem could not determine if feature was hotfixable. Failed to read file. File:%s Error:%d"), *PluginDescriptorFilename, FPlatformMisc::GetLastError());
		return false;
	}

	// Deserialize a JSON object from the string
	TSharedPtr< FJsonObject > ObjectPtr;
	TSharedRef< TJsonReader<> > Reader = TJsonReaderFactory<>::Create(FileContents);
	if (!FJsonSerializer::Deserialize(Reader, ObjectPtr) || !ObjectPtr.IsValid())
	{
		UE_LOG(LogGameFeatures, Error, TEXT("UGameFeaturesSubsystem could not determine if feature was hotfixable. Json invalid. File:%s. Error:%s"), *PluginDescriptorFilename, *Reader->GetErrorMessage());
		return false;
	}

	//@TODO: When we properly support downloaded plugins, will need to determine this
	const bool bIsBuiltInPlugin = true;

	// Read the properties

	// Hotfixable. If it is not specified, then we assume it is
	OutPluginDetails.bHotfixable = true;
	ObjectPtr->TryGetBoolField(TEXT("Hotfixable"), OutPluginDetails.bHotfixable);

	// Determine the initial plugin state
	OutPluginDetails.BuiltInAutoState = bIsBuiltInPlugin ? DetermineBuiltInInitialFeatureState(ObjectPtr, PluginDescriptorFilename) : EBuiltInAutoState::Installed;

	// Read any additional metadata the policy might want to consume (e.g., a release version number)
	for (const FString& ExtraKey : GetDefault<UGameFeaturesSubsystemSettings>()->AdditionalPluginMetadataKeys)
	{
		FString ExtraValue;
		ObjectPtr->TryGetStringField(ExtraKey, ExtraValue);
		OutPluginDetails.AdditionalMetadata.Add(ExtraKey, ExtraValue);
	}

	// Parse plugin dependencies
	const TArray<TSharedPtr<FJsonValue>>* PluginsArray = nullptr;
	ObjectPtr->TryGetArrayField(TEXT("Plugins"), PluginsArray);
	if (PluginsArray)
	{
		FString NameField = TEXT("Name");
		FString EnabledField = TEXT("Enabled");
		for (const TSharedPtr<FJsonValue>& PluginElement : *PluginsArray)
		{
			if (PluginElement.IsValid())
			{
				const TSharedPtr<FJsonObject>* ElementObjectPtr = nullptr;
				PluginElement->TryGetObject(ElementObjectPtr);
				if (ElementObjectPtr && ElementObjectPtr->IsValid())
				{
					const TSharedPtr<FJsonObject>& ElementObject = *ElementObjectPtr;
					bool bElementEnabled = false;
					ElementObject->TryGetBoolField(EnabledField, bElementEnabled);

					if (bElementEnabled)
					{
						FString ElementName;
						ElementObject->TryGetStringField(NameField, ElementName);
						if (!ElementName.IsEmpty())
						{
							TSharedPtr<IPlugin> DependencyPlugin = IPluginManager::Get().FindPlugin(ElementName);
							if (DependencyPlugin.IsValid())
							{
								const FString& PluginDependencyDescriptorFilename = DependencyPlugin->GetDescriptorFileName();

								if (!PluginDependencyDescriptorFilename.IsEmpty() &&
									GetDefault<UGameFeaturesSubsystemSettings>()->IsValidGameFeaturePlugin(FPaths::ConvertRelativePathToFull(PluginDependencyDescriptorFilename)) &&
									FPaths::FileExists(PluginDependencyDescriptorFilename))
								{
									OutPluginDetails.PluginDependencies.Add(GetPluginURL_FileProtocol(DependencyPlugin->GetDescriptorFileName()));
								}
							}
							else
							{
								UE_LOG(LogGameFeatures, Display, TEXT("Game feature plugin '%s' has unknown dependency '%s'."), *PluginDescriptorFilename, *ElementName);
							}
						}
					}
				}
			}
		}
	}

	return true;
}

UGameFeaturePluginStateMachine* UGameFeaturesSubsystem::FindGameFeaturePluginStateMachineByPluginName(const FString& PluginName) const
{
	for (auto StateMachineIt = GameFeaturePluginStateMachines.CreateConstIterator(); StateMachineIt; ++StateMachineIt)
	{
		if (UGameFeaturePluginStateMachine* GFSM = StateMachineIt.Value())
		{
			if (GFSM->GetGameFeatureName() == PluginName)
			{
				return GFSM;
			}
		}
	}

	return nullptr;
}

UGameFeaturePluginStateMachine* UGameFeaturesSubsystem::FindGameFeaturePluginStateMachine(const FString& PluginURL) const
{
	UGameFeaturePluginStateMachine* const* ExistingStateMachine = GameFeaturePluginStateMachines.Find(PluginURL);
	if (ExistingStateMachine)
	{
		return *ExistingStateMachine;
	}

	return nullptr;
}

UGameFeaturePluginStateMachine* UGameFeaturesSubsystem::FindOrCreateGameFeaturePluginStateMachine(const FString& PluginURL)
{
	if (UGameFeaturePluginStateMachine* ExistingStateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		return ExistingStateMachine;
	}

	UGameFeaturePluginStateMachine* NewStateMachine = NewObject<UGameFeaturePluginStateMachine>(this);
	GameFeaturePluginStateMachines.Add(PluginURL, NewStateMachine);
	NewStateMachine->InitStateMachine(PluginURL, FGameFeaturePluginRequestStateMachineDependencies::CreateUObject(this, &ThisClass::HandleRequestPluginDependencyStateMachines));

	return NewStateMachine;
}

void UGameFeaturesSubsystem::LoadGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result)
{
	check(Machine);
	if (Result.HasValue())
	{
		UE_LOG(LogGameFeatures, Display, TEXT("Game feature '%s' loaded successfully. Ending state: %s"), *Machine->GetGameFeatureName(), *UE::GameFeatures::ToString(Machine->GetCurrentState()));
	}
	else
	{
		const FString ErrorMessage = UE::GameFeatures::ToString(Result);
		UE_LOG(LogGameFeatures, Error, TEXT("Game feature '%s' load failed. Ending state: %s. Result: %s"),
			*Machine->GetGameFeatureName(),
			*UE::GameFeatures::ToString(Machine->GetCurrentState()),
			*ErrorMessage);
	}
}

void UGameFeaturesSubsystem::ChangeGameFeatureTargetStateComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginUninstallComplete CompleteDelegate)
{
	CompleteDelegate.ExecuteIfBound(Result);
}

void UGameFeaturesSubsystem::TerminateGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginUninstallComplete CompleteDelegate)
{
	if (Result.HasValue())
	{
		GameFeaturePluginStateMachines.Remove(Machine->GetPluginURL());
		Machine->MarkAsGarbage();
	}

	CompleteDelegate.ExecuteIfBound(Result);
}

bool UGameFeaturesSubsystem::HandleRequestPluginDependencyStateMachines(const FString& PluginFilename, TArray<UGameFeaturePluginStateMachine*>& OutDependencyMachines)
{
	FGameFeaturePluginDetails Details;
	if (GetGameFeaturePluginDetails(PluginFilename, Details))
	{
		for (const FString& DependencyURL : Details.PluginDependencies)
		{
			UGameFeaturePluginStateMachine* Dependency = FindOrCreateGameFeaturePluginStateMachine(DependencyURL);
			check(Dependency);
			OutDependencyMachines.Add(Dependency);
		}

		return true;
	}

	return false;
}

void UGameFeaturesSubsystem::ListGameFeaturePlugins(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& Ar)
{
	const bool bAlphaSort = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Compare(TEXT("-ALPHASORT"), ESearchCase::IgnoreCase) == 0; });
	const bool bActiveOnly = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Compare(TEXT("-ACTIVEONLY"), ESearchCase::IgnoreCase) == 0; });
	const bool bCsv = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Compare(TEXT("-CSV"), ESearchCase::IgnoreCase) == 0; });

	FString PlatformName = FPlatformMisc::GetCPUBrand().TrimStartAndEnd();
	Ar.Logf(TEXT("Listing Game Feature Plugins...(%s)"), *PlatformName);
	if (bCsv)
	{
		Ar.Logf(TEXT(",Plugin,State"));
	}

	// create a copy for sorting
	TArray<UGameFeaturePluginStateMachine*> StateMachines;
	GameFeaturePluginStateMachines.GenerateValueArray(StateMachines);

	if (bAlphaSort)
	{
		StateMachines.Sort([](const UGameFeaturePluginStateMachine& A, const UGameFeaturePluginStateMachine& B) { return A.GetGameFeatureName().Compare(B.GetGameFeatureName()) < 0; });
	}

	int32 PluginCount = 0;
	for (UGameFeaturePluginStateMachine* GFSM : StateMachines)
	{
		if (!GFSM)
		{
			continue;
		}

		if (bActiveOnly && GFSM->GetCurrentState() != EGameFeaturePluginState::Active)
		{
			continue;
		}

		if (bCsv)
		{
			Ar.Logf(TEXT(",%s,%s"), *GFSM->GetGameFeatureName(), *UE::GameFeatures::ToString(GFSM->GetCurrentState()));
		}
		else
		{
			Ar.Logf(TEXT("%s (%s)"), *GFSM->GetGameFeatureName(), *UE::GameFeatures::ToString(GFSM->GetCurrentState()));
		}
		++PluginCount;
	}

	Ar.Logf(TEXT("Total Game Feature Plugins: %d"), PluginCount);
}

TSet<FString> UGameFeaturesSubsystem::GetActivePluginNames() const
{
	TSet<FString> ActivePluginNames;

	for (const TPair<FString, UGameFeaturePluginStateMachine*>& Pair : GameFeaturePluginStateMachines)
	{
		UGameFeaturePluginStateMachine* StateMachine = Pair.Value;
		if (StateMachine->GetCurrentState() == EGameFeaturePluginState::Active &&
			StateMachine->GetDestinationState() == EGameFeaturePluginState::Active)
		{
			ActivePluginNames.Add(StateMachine->GetPluginName());
		}
	}

	return ActivePluginNames;
}

namespace GameFeaturesSubsystem
{ 
	bool IsContentWithinActivePlugin(const FString& InObjectOrPackagePath, const TSet<FString>& ActivePluginNames)
	{
		// Look for the first slash beyond the first one we start with.
		const int32 RootEndIndex = InObjectOrPackagePath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 1);

		const FString ObjectPathRootName = InObjectOrPackagePath.Mid(1, RootEndIndex - 1);

		if (ActivePluginNames.Contains(ObjectPathRootName))
		{
			return true;
		}

		return false;
	}
}

void UGameFeaturesSubsystem::FilterInactivePluginAssets(TArray<FAssetIdentifier>& AssetsToFilter) const
{
	AssetsToFilter.RemoveAllSwap([ActivePluginNames = GetActivePluginNames()](const FAssetIdentifier& Asset) 
	{
		return !GameFeaturesSubsystem::IsContentWithinActivePlugin(Asset.PackageName.ToString(), ActivePluginNames);
	});
}

void UGameFeaturesSubsystem::FilterInactivePluginAssets(TArray<FAssetData>& AssetsToFilter) const
{
	AssetsToFilter.RemoveAllSwap([ActivePluginNames = GetActivePluginNames()](const FAssetData& Asset) 
	{
		return !GameFeaturesSubsystem::IsContentWithinActivePlugin(Asset.ObjectPath.ToString(), ActivePluginNames);
	});
}
EBuiltInAutoState UGameFeaturesSubsystem::DetermineBuiltInInitialFeatureState(TSharedPtr<FJsonObject> Descriptor, const FString& ErrorContext)
{
	EBuiltInAutoState InitialState = EBuiltInAutoState::Invalid;

	FString InitialFeatureStateStr;
	if (Descriptor->TryGetStringField(TEXT("BuiltInInitialFeatureState"), InitialFeatureStateStr))
	{
		if (InitialFeatureStateStr == TEXT("Installed"))
		{
			InitialState = EBuiltInAutoState::Installed;
		}
		else if (InitialFeatureStateStr == TEXT("Registered"))
		{
			InitialState = EBuiltInAutoState::Registered;
		}
		else if (InitialFeatureStateStr == TEXT("Loaded"))
		{
			InitialState = EBuiltInAutoState::Loaded;
		}
		else if (InitialFeatureStateStr == TEXT("Active"))
		{
			InitialState = EBuiltInAutoState::Active;
		}
		else
		{
			if (!ErrorContext.IsEmpty())
			{
				UE_LOG(LogGameFeatures, Error, TEXT("Game feature '%s' has an unknown value '%s' for BuiltInInitialFeatureState (expected Installed, Registered, Loaded, or Active); defaulting to Active."), *ErrorContext, *InitialFeatureStateStr);
			}
			InitialState = EBuiltInAutoState::Active;
		}
	}
	else
	{
		// BuiltInAutoRegister. Default to true. If this is a built in plugin, should it be registered automatically (set to false if you intent to load late with LoadAndActivateGameFeaturePlugin)
		bool bBuiltInAutoRegister = true;
		Descriptor->TryGetBoolField(TEXT("BuiltInAutoRegister"), bBuiltInAutoRegister);

		// BuiltInAutoLoad. Default to true. If this is a built in plugin, should it be loaded automatically (set to false if you intent to load late with LoadAndActivateGameFeaturePlugin)
		bool bBuiltInAutoLoad = true;
		Descriptor->TryGetBoolField(TEXT("BuiltInAutoLoad"), bBuiltInAutoLoad);

		// The cooker will need to activate the plugin so that assets can be scanned properly
		bool bBuiltInAutoActivate = true;
		Descriptor->TryGetBoolField(TEXT("BuiltInAutoActivate"), bBuiltInAutoActivate);

		InitialState = EBuiltInAutoState::Installed;
		if (bBuiltInAutoRegister)
		{
			InitialState = EBuiltInAutoState::Registered;
			if (bBuiltInAutoLoad)
			{
				InitialState = EBuiltInAutoState::Loaded;
				if (bBuiltInAutoActivate)
				{
					InitialState = EBuiltInAutoState::Active;
				}
			}
		}

		if (!ErrorContext.IsEmpty())
		{
			//@TODO: Increase severity to a warning after changing existing features
			UE_LOG(LogGameFeatures, Log, TEXT("Game feature '%s' has no BuiltInInitialFeatureState key, using legacy BuiltInAutoRegister(%d)/BuiltInAutoLoad(%d)/BuiltInAutoActivate(%d) values to arrive at initial state."),
				*ErrorContext,
				bBuiltInAutoRegister ? 1 : 0,
				bBuiltInAutoLoad ? 1 : 0,
				bBuiltInAutoActivate ? 1 : 0);
		}
	}

	return InitialState;
}

EGameFeaturePluginState UGameFeaturesSubsystem::ConvertInitialFeatureStateToTargetState(EBuiltInAutoState AutoState)
{
	EGameFeaturePluginState InitialState;
	switch (AutoState)
	{
	default:
	case EBuiltInAutoState::Invalid:
		InitialState = EGameFeaturePluginState::UnknownStatus;
		break;
	case EBuiltInAutoState::Installed:
		InitialState = EGameFeaturePluginState::Installed;
		break;
	case EBuiltInAutoState::Registered:
		InitialState = EGameFeaturePluginState::Registered;
		break;
	case EBuiltInAutoState::Loaded:
		InitialState = EGameFeaturePluginState::Loaded;
		break;
	case EBuiltInAutoState::Active:
		InitialState = EGameFeaturePluginState::Active;
		break;
	}
	return InitialState;
}
