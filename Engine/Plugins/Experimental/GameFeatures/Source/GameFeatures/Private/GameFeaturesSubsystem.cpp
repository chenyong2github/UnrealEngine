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
				if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(Args[0], /*out*/ PluginURL))
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
				if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(Args[0], /*out*/ PluginURL))
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
				if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(Args[0], /*out*/ PluginURL))
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

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnloadAndKeepRegisteredGameFeaturePlugin"),
		TEXT("Unloads a game feature plugin by URL but keeps it registered"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
			{
				if (Args.Num() > 0)
				{
					FString PluginURL;
					if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(Args[0], /*out*/ PluginURL))
					{
						PluginURL = Args[0];
					}
					UGameFeaturesSubsystem::Get().UnloadGameFeaturePlugin(PluginURL, true);
				}
				else
				{
					Ar.Logf(TEXT("Expected a game feature plugin URL as an argument"));
				}
			}),
		ECVF_Cheat);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UninstallGameFeaturePlugin"),
		TEXT("Uninstalls a game feature plugin by URL"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
			{
				if (Args.Num() > 0)
				{
					FString PluginURL;
					if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(Args[0], /*out*/ PluginURL))
					{
						PluginURL = Args[0];
					}
					UGameFeaturesSubsystem::Get().UninstallGameFeaturePlugin(PluginURL);
				}
				else
				{
					Ar.Logf(TEXT("Expected a game feature plugin URL as an argument"));
				}
			}),
		ECVF_Cheat);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("CancelGameFeaturePlugin"),
		TEXT("Cancel any state changes for a game feature plugin by URL"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
			{
				if (Args.Num() > 0)
				{
					FString PluginURL;
					if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(Args[0], /*out*/ PluginURL))
					{
						PluginURL = Args[0];
					}
					UGameFeaturesSubsystem::Get().CancelGameFeatureStateChange(PluginURL);
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

	FAssetData GameFeatureAssetData;
	{
		FARFilter ArFilter;
		ArFilter.ObjectPaths.Add(FName(*GameFeatureToLoad));
		ArFilter.ClassPaths.Add(UGameFeatureData::StaticClass()->GetClassPathName());
		ArFilter.bRecursiveClasses = true;
#if !WITH_EDITOR
		ArFilter.bIncludeOnlyOnDiskAssets = true;
#endif //if !WITH_EDITOR

		LocalAssetRegistry.EnumerateAssets(ArFilter, [&GameFeatureAssetData](const FAssetData& AssetData)
		{
			GameFeatureAssetData = AssetData;
			return false;
		});
	}

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

void UGameFeaturesSubsystem::AddGameFeatureToAssetManager(const UGameFeatureData* GameFeatureToAdd, const FString& PluginName, TArray<FName>& OutNewPrimaryAssetTypes)
{
	check(GameFeatureToAdd);
	FString PluginRootPath = TEXT("/") + PluginName + TEXT("/");
	UAssetManager& LocalAssetManager = UAssetManager::Get();
	IAssetRegistry& LocalAssetRegistry = LocalAssetManager.GetAssetRegistry();

	// @TODO: HACK - There is no guarantee that the plugin mount point was added before inte initial asset scan.
	// If not, ScanPathsForPrimaryAssets will fail to find primary assets without a syncronous scan.
	// A proper fix for this would be to handle all the primary asset discovery internally ins the asset manager 
	// instead of doing it here.
	// We just mounted the folder that contains these primary assets and the editor background scan may not
	// not be finished by the time this is called, but a rescan will happen later in OnAssetRegistryFilesLoaded 
	// as long as LocalAssetRegistry.IsLoadingAssets() is true.
	const bool bForceSynchronousScan = !LocalAssetRegistry.IsLoadingAssets();

	LocalAssetManager.PushBulkScanning();

	for (FPrimaryAssetTypeInfo TypeInfo : GameFeatureToAdd->GetPrimaryAssetTypesToScan())
	{
		// @TODO: we shouldn't be accessing private data here. Need a better way to do this
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
		LocalAssetManager.ScanPathsForPrimaryAssets(TypeInfo.PrimaryAssetType, TypeInfo.AssetScanPaths, TypeInfo.AssetBaseClassLoaded, TypeInfo.bHasBlueprintClasses, TypeInfo.bIsEditorOnly, bForceSynchronousScan);

		if (!bAlreadyExisted)
		{
			OutNewPrimaryAssetTypes.Add(TypeInfo.PrimaryAssetType);

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

void UGameFeaturesSubsystem::RemoveGameFeatureFromAssetManager(const UGameFeatureData* GameFeatureToRemove, const FString& PluginName, const TArray<FName>& AddedPrimaryAssetTypes)
{
	check(GameFeatureToRemove);
	UAssetManager& LocalAssetManager = UAssetManager::Get();

	for (FPrimaryAssetTypeInfo TypeInfo : GameFeatureToRemove->GetPrimaryAssetTypesToScan())
	{
		if (AddedPrimaryAssetTypes.Contains(TypeInfo.PrimaryAssetType))
		{
			LocalAssetManager.RemovePrimaryAssetType(TypeInfo.PrimaryAssetType);
			continue;
		}

		for (FDirectoryPath& Path : TypeInfo.Directories)
		{
			Path.Path = TEXT("/") + PluginName + TEXT("/") + Path.Path;
		}

		// This function also fills out runtime data on the copy
		if (!LocalAssetManager.ShouldScanPrimaryAssetType(TypeInfo))
		{
			continue;
		}

		LocalAssetManager.RemoveScanPathsForPrimaryAssets(TypeInfo.PrimaryAssetType, TypeInfo.AssetScanPaths, TypeInfo.AssetBaseClassLoaded, TypeInfo.bHasBlueprintClasses, TypeInfo.bIsEditorOnly);
	}
}

void UGameFeaturesSubsystem::AddObserver(UObject* Observer)
{
	//@TODO: GameFeaturePluginEnginePush: May want to warn if one is added after any game feature plugins are already initialized, or go to a CallOrRegister sort of pattern
	check(Observer);
	if (ensureAlwaysMsgf(Cast<IGameFeatureStateChangeObserver>(Observer) != nullptr, TEXT("Observers must implement the IGameFeatureStateChangeObserver interface.")))
	{
		Observers.AddUnique(Observer);
	}
}

void UGameFeaturesSubsystem::RemoveObserver(UObject* Observer)
{
	check(Observer);
	Observers.RemoveSingleSwap(Observer);
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

EGameFeaturePluginProtocol UGameFeaturesSubsystem::GetPluginURLProtocol(FStringView PluginURL)
{
	for (EGameFeaturePluginProtocol Protocol : TEnumRange<EGameFeaturePluginProtocol>())
	{
		if (PluginURL.StartsWith(UE::GameFeatures::GameFeaturePluginProtocolPrefix(Protocol)))
		{
			return Protocol;
		}
	}
	return EGameFeaturePluginProtocol::Unknown;
}

void UGameFeaturesSubsystem::OnGameFeatureTerminating(const FString& PluginName, const FString& PluginURL)
{
	CallbackObservers(EObserverCallback::Terminating, PluginURL, &PluginName);

	if (!PluginName.IsEmpty())
	{
		// Unmap plugin name to plugin URL
		GameFeaturePluginNameToPathMap.Remove(PluginName);
	}
}

void UGameFeaturesSubsystem::OnGameFeatureCheckingStatus(const FString& PluginURL)
{
	CallbackObservers(EObserverCallback::CheckingStatus, PluginURL);
}

void UGameFeaturesSubsystem::OnGameFeatureStatusKnown(const FString& PluginName, const FString& PluginURL)
{
	// Map plugin name to plugin URL
	if (ensure(!GameFeaturePluginNameToPathMap.Contains(PluginName)))
	{
		GameFeaturePluginNameToPathMap.Add(PluginName, PluginURL);
	}
}

void UGameFeaturesSubsystem::OnGameFeatureRegistering(const UGameFeatureData* GameFeatureData, const FString& PluginName, const FString& PluginURL)
{
	CallbackObservers(EObserverCallback::Registering, PluginURL, &PluginName, GameFeatureData);

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureRegistering();
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureUnregistering(const UGameFeatureData* GameFeatureData, const FString& PluginName, const FString& PluginURL)
{
	CallbackObservers(EObserverCallback::Unregistering, PluginURL, &PluginName, GameFeatureData);

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureUnregistering();
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureLoading(const UGameFeatureData* GameFeatureData, const FString& PluginURL)
{
	CallbackObservers(EObserverCallback::Loading, PluginURL, nullptr, GameFeatureData);

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureLoading();
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureActivating(const UGameFeatureData* GameFeatureData, const FString& PluginName, FGameFeatureActivatingContext& Context, const FString& PluginURL)
{
	CallbackObservers(EObserverCallback::Activating, PluginURL, &PluginName, GameFeatureData);

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureActivating(Context);
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureDeactivating(const UGameFeatureData* GameFeatureData, const FString& PluginName, FGameFeatureDeactivatingContext& Context, const FString& PluginURL)
{
	CallbackObservers(EObserverCallback::Deactivating, PluginURL, &PluginName, GameFeatureData, &Context);

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureDeactivating(Context);
		}
	}
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

bool UGameFeaturesSubsystem::IsGameFeaturePluginRegistered(const FString& PluginURL) const
{
	if (const UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		return StateMachine->GetCurrentState() >= EGameFeaturePluginState::Registered;
	}
	return false;
}

bool UGameFeaturesSubsystem::IsGameFeaturePluginLoaded(const FString& PluginURL) const
{
	if (const UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		return StateMachine->GetCurrentState() >= EGameFeaturePluginState::Loaded;
	}
	return false;
}

void UGameFeaturesSubsystem::LoadGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginLoadComplete& CompleteDelegate)
{
	const bool bIsPluginAllowed = GameSpecificPolicies->IsPluginAllowed(PluginURL);
	if (!bIsPluginAllowed)
	{
		CompleteDelegate.ExecuteIfBound(MakeError(TEXT("GameFeaturePlugin.StateMachine.Plugin_Denied_By_GameSpecificPolicy")));
		return;
	}

	UGameFeaturePluginStateMachine* StateMachine = FindOrCreateGameFeaturePluginStateMachine(PluginURL);
	
	if (!StateMachine->IsRunning() && StateMachine->GetCurrentState() == EGameFeaturePluginState::Active)
	{
		// TODO: Resolve the activated case here, this is needed because in a PIE environment the plugins
		// are not sandboxed, and we need to do simulate a successful activate call in order run GFP systems 
		// on whichever Role runs second between client and server.

		// Refire the observer for Activated and do nothing else.
		CallbackObservers(EObserverCallback::Activating, PluginURL, &StateMachine->GetPluginName(), StateMachine->GetGameFeatureDataForActivePlugin());
	}

	ChangeGameFeatureDestination(StateMachine, FGameFeaturePluginStateRange(EGameFeaturePluginState::Loaded, EGameFeaturePluginState::Active), CompleteDelegate);
}

void UGameFeaturesSubsystem::LoadAndActivateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginLoadComplete& CompleteDelegate)
{
	ChangeGameFeatureTargetState(PluginURL, EGameFeatureTargetState::Active, CompleteDelegate);
}

void UGameFeaturesSubsystem::ChangeGameFeatureTargetState(const FString& PluginURL, EGameFeatureTargetState TargetState, const FGameFeaturePluginChangeStateComplete& CompleteDelegate)
{
	EGameFeaturePluginState TargetPluginState = EGameFeaturePluginState::MAX;

	switch (TargetState)
	{
	case EGameFeatureTargetState::Installed:	TargetPluginState = EGameFeaturePluginState::Installed;		break;
	case EGameFeatureTargetState::Registered:	TargetPluginState = EGameFeaturePluginState::Registered;	break;
	case EGameFeatureTargetState::Loaded:		TargetPluginState = EGameFeaturePluginState::Loaded;		break;
	case EGameFeatureTargetState::Active:		TargetPluginState = EGameFeaturePluginState::Active;		break;
	}

	// Make sure we have coverage on all values of EGameFeatureTargetState
	static_assert(std::underlying_type<EGameFeatureTargetState>::type(EGameFeatureTargetState::Count) == 4, "");
	check(TargetPluginState != EGameFeaturePluginState::MAX);

	const bool bIsPluginAllowed = GameSpecificPolicies->IsPluginAllowed(PluginURL);

	UGameFeaturePluginStateMachine* StateMachine = nullptr;
	if (!bIsPluginAllowed)
	{
		StateMachine = FindGameFeaturePluginStateMachine(PluginURL);
		if (!StateMachine)
		{
			CompleteDelegate.ExecuteIfBound(MakeError(TEXT("GameFeaturePlugin.StateMachine.Plugin_Denied_By_GameSpecificPolicy")));
			return;
		}
	}
	else
	{
		StateMachine = FindOrCreateGameFeaturePluginStateMachine(PluginURL);
	}
	
	check(StateMachine);

	if (!bIsPluginAllowed)
	{
		if (TargetPluginState > StateMachine->GetCurrentState() || TargetPluginState > StateMachine->GetDestination())
		{
			CompleteDelegate.ExecuteIfBound(MakeError(TEXT("GameFeaturePlugin.StateMachine.Plugin_Denied_By_GameSpecificPolicy")));
			return;
		}
	}

	if (TargetState == EGameFeatureTargetState::Active &&
		!StateMachine->IsRunning() && 
		StateMachine->GetCurrentState() == TargetPluginState)
	{
		// TODO: Resolve the activated case here, this is needed because in a PIE environment the plugins
		// are not sandboxed, and we need to do simulate a successful activate call in order run GFP systems 
		// on whichever Role runs second between client and server.

		// Refire the observer for Activated and do nothing else.
		CallbackObservers(EObserverCallback::Activating, PluginURL, &StateMachine->GetPluginName(), StateMachine->GetGameFeatureDataForActivePlugin());
	}
	
	ChangeGameFeatureDestination(StateMachine, FGameFeaturePluginStateRange(TargetPluginState), CompleteDelegate);
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
			else if (StateInfo.State >= EGameFeaturePluginState::Installed)
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
	DeactivateGameFeaturePlugin(PluginURL, FGameFeaturePluginDeactivateComplete());
}

void UGameFeaturesSubsystem::DeactivateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginDeactivateComplete& CompleteDelegate)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		ChangeGameFeatureDestination(StateMachine, FGameFeaturePluginStateRange(EGameFeaturePluginState::Terminal, EGameFeaturePluginState::Loaded), CompleteDelegate);
	}
	else
	{
		CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeError(TEXT("GameFeaturePlugin.BadURL"))));
	}
}

void UGameFeaturesSubsystem::UnloadGameFeaturePlugin(const FString& PluginURL, bool bKeepRegistered /*= false*/)
{
	UnloadGameFeaturePlugin(PluginURL, FGameFeaturePluginUnloadComplete(), bKeepRegistered);
}

void UGameFeaturesSubsystem::UnloadGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUnloadComplete& CompleteDelegate, bool bKeepRegistered /*= false*/)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		EGameFeaturePluginState TargetPluginState = bKeepRegistered ? EGameFeaturePluginState::Registered : EGameFeaturePluginState::Installed;
		ChangeGameFeatureDestination(StateMachine, FGameFeaturePluginStateRange(EGameFeaturePluginState::Terminal, TargetPluginState), CompleteDelegate);
	}
	else
	{
		CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeError(TEXT("GameFeaturePlugin.BadURL"))));
	}
}

void UGameFeaturesSubsystem::UninstallGameFeaturePlugin(const FString& PluginURL)
{
	UninstallGameFeaturePlugin(PluginURL, FGameFeaturePluginUninstallComplete());
}

void UGameFeaturesSubsystem::UninstallGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUninstallComplete& CompleteDelegate)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		ChangeGameFeatureDestination(StateMachine, FGameFeaturePluginStateRange(EGameFeaturePluginState::Terminal, EGameFeaturePluginState::StatusKnown), CompleteDelegate);
	}
	else
	{
		CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeError(TEXT("GameFeaturePlugin.BadURL"))));
	}
}

void UGameFeaturesSubsystem::TerminateGameFeaturePlugin(const FString& PluginURL)
{
	TerminateGameFeaturePlugin(PluginURL, FGameFeaturePluginUninstallComplete());
}

void UGameFeaturesSubsystem::TerminateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUninstallComplete& CompleteDelegate)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		ChangeGameFeatureDestination(StateMachine, FGameFeaturePluginStateRange(EGameFeaturePluginState::Terminal), CompleteDelegate);
	}
	else
	{
		CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeError(TEXT("GameFeaturePlugin.BadURL"))));
	}
}

void UGameFeaturesSubsystem::CancelGameFeatureStateChange(const FString& PluginURL)
{
	CancelGameFeatureStateChange(PluginURL, FGameFeaturePluginChangeStateComplete());
}

void UGameFeaturesSubsystem::CancelGameFeatureStateChange(const FString& PluginURL, const FGameFeaturePluginChangeStateComplete& CompleteDelegate)
{
	if (UGameFeaturePluginStateMachine* StateMachine = FindGameFeaturePluginStateMachine(PluginURL))
	{
		const bool bCancelPending = StateMachine->TryCancel(FGameFeatureStateTransitionCanceled::CreateWeakLambda(this, [CompleteDelegate](UGameFeaturePluginStateMachine* Machine)
		{
			CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeValue()));
		}));

		if (!bCancelPending)
		{
			CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeValue()));
		}
	}
	else
	{
		CompleteDelegate.ExecuteIfBound(UE::GameFeatures::FResult(MakeError(TEXT("GameFeaturePlugin.BadURL"))));
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

					// If we're already at the destination or beyond, don't transition back
					FGameFeaturePluginStateRange Destination(DestinationState, EGameFeaturePluginState::Active);
					ChangeGameFeatureDestination(StateMachine, Destination, 
						FGameFeaturePluginChangeStateComplete::CreateUObject(this, &ThisClass::LoadBuiltInGameFeaturePluginComplete, StateMachine, Destination));
				}
			}
		}
	}

	UAssetManager::Get().PopBulkScanning();
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

bool UGameFeaturesSubsystem::GetPluginURLByName(const FString& PluginName, FString& OutPluginURL) const
{
	if (const FString* PluginURL = GameFeaturePluginNameToPathMap.Find(PluginName))
	{
		OutPluginURL = *PluginURL;
		return true;
	}

	return false;
}

bool UGameFeaturesSubsystem::GetPluginURLForBuiltInPluginByName(const FString& PluginName, FString& OutPluginURL) const
{
	return GetPluginURLByName(PluginName, OutPluginURL);
}

FString UGameFeaturesSubsystem::GetPluginFilenameFromPluginURL(const FString& PluginURL) const
{
	FString PluginFilename;
	const UGameFeaturePluginStateMachine* GFSM = FindGameFeaturePluginStateMachine(PluginURL);
	if (GFSM == nullptr || !GFSM->GetPluginFilename(PluginFilename))
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
	// @TODO: We load the descriptor 2-3 per plugin because FPluginReferenceDescriptor doesn't cache any of this info.
	// GFPs are implemented with a plugin so FPluginReferenceDescriptor doesn't know anything about them.
	// Need a better way of storing GFP specific plugin data...

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
						FString DependencyName;
						ElementObject->TryGetStringField(NameField, DependencyName);
						if (!DependencyName.IsEmpty())
						{
							TSharedPtr<IPlugin> DependencyPlugin = IPluginManager::Get().FindPlugin(DependencyName);
							if (DependencyPlugin.IsValid())
							{
								FString DependencyURL;
								if (!GetPluginURLByName(DependencyPlugin->GetName(), DependencyURL))
								{
									if (!DependencyPlugin->GetDescriptorFileName().IsEmpty() &&
										GetDefault<UGameFeaturesSubsystemSettings>()->IsValidGameFeaturePlugin(FPaths::ConvertRelativePathToFull(DependencyPlugin->GetDescriptorFileName())) &&
										FPaths::FileExists(DependencyPlugin->GetDescriptorFileName()))
									{
										DependencyURL = GetPluginURL_FileProtocol(DependencyPlugin->GetDescriptorFileName());
									}
								}

								if (!DependencyURL.IsEmpty())
								{
									OutPluginDetails.PluginDependencies.Add(DependencyURL);
								}
							}
							else
							{
								UE_LOG(LogGameFeatures, Display, TEXT("Game feature plugin '%s' has unknown dependency '%s'."), *PluginDescriptorFilename, *DependencyName);
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
	NewStateMachine->InitStateMachine(PluginURL);

	return NewStateMachine;
}

void UGameFeaturesSubsystem::LoadBuiltInGameFeaturePluginComplete(const UE::GameFeatures::FResult& Result, UGameFeaturePluginStateMachine* Machine, FGameFeaturePluginStateRange RequestedDestination)
{
	check(Machine);
	if (Result.HasValue())
	{
		//@note It's possible for the machine to still be tranitioning at this point as long as it's withing the requested destination range
		UE_LOG(LogGameFeatures, Display, TEXT("Game feature '%s' loaded successfully. Ending state: %s, [%s, %s]"), 
			*Machine->GetGameFeatureName(), 
			*UE::GameFeatures::ToString(Machine->GetCurrentState()),
			*UE::GameFeatures::ToString(Machine->GetDestination().MinState),
			*UE::GameFeatures::ToString(Machine->GetDestination().MaxState));

		checkf(RequestedDestination.Contains(Machine->GetCurrentState()), TEXT("Game feature '%s': Ending state %s is not in expected range [%s, %s]"), 
			*Machine->GetGameFeatureName(), 
			*UE::GameFeatures::ToString(Machine->GetCurrentState()), 
			*UE::GameFeatures::ToString(RequestedDestination.MinState), 
			*UE::GameFeatures::ToString(RequestedDestination.MaxState));
	}
	else
	{
		const FString ErrorMessage = UE::GameFeatures::ToString(Result);
		UE_LOG(LogGameFeatures, Error, TEXT("Game feature '%s' load failed. Ending state: %s, [%s, %s]. Result: %s"),
			*Machine->GetGameFeatureName(),
			*UE::GameFeatures::ToString(Machine->GetCurrentState()),
			*UE::GameFeatures::ToString(Machine->GetDestination().MinState),
			*UE::GameFeatures::ToString(Machine->GetDestination().MaxState),
			*ErrorMessage);
	}
}

void UGameFeaturesSubsystem::ChangeGameFeatureDestination(UGameFeaturePluginStateMachine* Machine, const FGameFeaturePluginStateRange& StateRange, FGameFeaturePluginChangeStateComplete CompleteDelegate)
{
	const bool bSetDestination = Machine->SetDestination(StateRange,
		FGameFeatureStateTransitionComplete::CreateUObject(this, &ThisClass::ChangeGameFeatureTargetStateComplete, MoveTemp(CompleteDelegate)));

	if (bSetDestination)
	{
		UE_LOG(LogGameFeatures, Verbose, TEXT("ChangeGameFeatureDestination: Set Game Feature %s Destination State to [%s, %s]"), *Machine->GetGameFeatureName(), *UE::GameFeatures::ToString(StateRange.MinState), *UE::GameFeatures::ToString(StateRange.MaxState));
	}
	else
	{
		// Try canceling any current transition, then retry
		auto OnCanceled = [this, StateRange, CompleteDelegate=MoveTemp(CompleteDelegate)](UGameFeaturePluginStateMachine* Machine) mutable
		{
			// Special case for terminal state since it cannot be exited, we need to make a new machine
			if (Machine->GetCurrentState() == EGameFeaturePluginState::Terminal)
			{
				UGameFeaturePluginStateMachine* NewMachine = FindOrCreateGameFeaturePluginStateMachine(Machine->GetPluginURL());
				checkf(NewMachine != Machine, TEXT("Game Feature Plugin %s should have already been removed from subsystem!"), *Machine->GetPluginURL());
				Machine = NewMachine;
			}

			// Now that the transition has been canceled, retry reaching the desired destination
			const bool bSetDestination = Machine->SetDestination(StateRange,
				FGameFeatureStateTransitionComplete::CreateUObject(this, &ThisClass::ChangeGameFeatureTargetStateComplete, MoveTemp(CompleteDelegate)));

			if (!ensure(bSetDestination))
			{
				UE_LOG(LogGameFeatures, Verbose, TEXT("ChangeGameFeatureDestination: Failed to set Game Feature %s Destination State to [%s, %s]"), *Machine->GetGameFeatureName(), *UE::GameFeatures::ToString(StateRange.MinState), *UE::GameFeatures::ToString(StateRange.MaxState));

				CompleteDelegate.ExecuteIfBound(MakeError(TEXT("GameFeaturePlugin.StateMachine.State_Currently_Unreachable")));
			}
			else
			{
				UE_LOG(LogGameFeatures, Verbose, TEXT("ChangeGameFeatureDestination: OnCanceled, set Game Feature %s Destination State to [%s, %s]"), *Machine->GetGameFeatureName(), *UE::GameFeatures::ToString(StateRange.MinState), *UE::GameFeatures::ToString(StateRange.MaxState));
			}
		};

		const bool bCancelPending = Machine->TryCancel(FGameFeatureStateTransitionCanceled::CreateWeakLambda(this, MoveTemp(OnCanceled)));
		if (!ensure(bCancelPending))
		{
			UE_LOG(LogGameFeatures, Verbose, TEXT("ChangeGameFeatureDestination: Failed to cancel Game Feature %s"), *Machine->GetGameFeatureName());

			CompleteDelegate.ExecuteIfBound(MakeError(TEXT("GameFeaturePlugin.StateMachine.State_Currently_Unreachable_Cancel_Failed")));
		}
	}
}

void UGameFeaturesSubsystem::ChangeGameFeatureTargetStateComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginChangeStateComplete CompleteDelegate)
{
	CompleteDelegate.ExecuteIfBound(Result);
}

void UGameFeaturesSubsystem::BeginTermination(UGameFeaturePluginStateMachine* Machine)
{
	check(IsValid(Machine));
	check(Machine->GetCurrentState() == EGameFeaturePluginState::Terminal);
	GameFeaturePluginStateMachines.Remove(Machine->GetPluginURL());
	TerminalGameFeaturePluginStateMachines.Add(Machine);
}

void UGameFeaturesSubsystem::FinishTermination(UGameFeaturePluginStateMachine* Machine)
{
	TerminalGameFeaturePluginStateMachines.RemoveSwap(Machine);
}

bool UGameFeaturesSubsystem::FindOrCreatePluginDependencyStateMachines(const FString& PluginFilename, TArray<UGameFeaturePluginStateMachine*>& OutDependencyMachines)
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

void UGameFeaturesSubsystem::CallbackObservers(EObserverCallback CallbackType, const FString& PluginURL, 
	const FString* PluginName /*= nullptr*/, 
	const UGameFeatureData* GameFeatureData /*= nullptr*/, 
	FGameFeatureDeactivatingContext* DeactivatingContext /*= nullptr*/)
{
	// Protect against modifying the observer list during iteration
	TArray<UObject*> LocalObservers(Observers);

	switch (CallbackType)
	{
	case EObserverCallback::CheckingStatus:
		for (UObject* Observer : LocalObservers)
		{
			CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureCheckingStatus(PluginURL);
		}
		break;

	case EObserverCallback::Terminating:
		for (UObject* Observer : LocalObservers)
		{
			CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureTerminating(PluginURL);
		}
		break;

	case EObserverCallback::Registering:
		check(PluginName);
		check(GameFeatureData);
		for (UObject* Observer : LocalObservers)
		{
			CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureRegistering(GameFeatureData, *PluginName, PluginURL);
		}
		break;

	case EObserverCallback::Unregistering:
		check(PluginName);
		check(GameFeatureData);
		for (UObject* Observer : LocalObservers)
		{
			CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureUnregistering(GameFeatureData, *PluginName, PluginURL);
		}
		break;

	case EObserverCallback::Loading:
		check(GameFeatureData);
		for (UObject* Observer : LocalObservers)
		{
			CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureLoading(GameFeatureData, PluginURL);
		}
		break;

	case EObserverCallback::Activating:
		check(GameFeatureData);
		for (UObject* Observer : LocalObservers)
		{
			CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureActivating(GameFeatureData, PluginURL);
		}
		break;

	case EObserverCallback::Deactivating:
		check(GameFeatureData);
		check(DeactivatingContext);
		for (UObject* Observer : LocalObservers)
		{
			CastChecked<IGameFeatureStateChangeObserver>(Observer)->OnGameFeatureDeactivating(GameFeatureData, *DeactivatingContext, PluginURL);
		}
		break;

	default:
		UE_LOG(LogGameFeatures, Fatal, TEXT("Unkown EObserverCallback!"));
	}
}

TSet<FString> UGameFeaturesSubsystem::GetActivePluginNames() const
{
	TSet<FString> ActivePluginNames;

	for (const TPair<FString, UGameFeaturePluginStateMachine*>& Pair : GameFeaturePluginStateMachines)
	{
		UGameFeaturePluginStateMachine* StateMachine = Pair.Value;
		if (StateMachine->GetCurrentState() == EGameFeaturePluginState::Active &&
			StateMachine->GetDestination().Contains(EGameFeaturePluginState::Active))
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
