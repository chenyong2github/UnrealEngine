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
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManagerSettings.h"
#include "InstallBundleManagerInterface.h"

DEFINE_LOG_CATEGORY(LogGameFeatures);

FGameFeaturePluginLoadCompleteDataReady UGameFeaturesSubsystem::PluginLoadedGameFeatureDataReadyDelegate;
FGameFeaturePluginDeativated UGameFeaturesSubsystem::PluginDeactivatedDelegate;

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
		//@TODO: GameFeaturePluginEnginePush: Re-enable this once the rule in FN is updated
		//UE_LOG(LogGameFeatures, Error, TEXT("Asset manager settings do not include a rule for assets of type %s, which is required for game feature plugins to function"), *UGameFeatureData::StaticClass()->GetName());
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

void UGameFeaturesSubsystem::AddGameFeatureToAssetManager(const UGameFeatureData* GameFeatureToAdd)
{
	check(GameFeatureToAdd);
	UAssetManager& LocalAssetManager = UAssetManager::Get();

	const FString GameFeaturePath = GameFeatureToAdd->GetOutermost()->GetName();
	FString PluginName;
	if (ensureMsgf(UAssetManager::GetContentRootPathFromPackageName(GameFeaturePath, PluginName), TEXT("Must be a valid package path with a root. GameFeaturePath: %s"), *GameFeaturePath))
	{
		LocalAssetManager.StartBulkScanning();

		for (FPrimaryAssetTypeInfo TypeInfo : GameFeatureToAdd->GetPrimaryAssetTypesToScan())
		{
			// This function also fills out runtime data on the copy
			if (!LocalAssetManager.ShouldScanPrimaryAssetType(TypeInfo))
			{
				continue;
			}

			for (FString& Path : TypeInfo.AssetScanPaths)
			{
				Path = PluginName + Path;
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

		LocalAssetManager.StopBulkScanning();
	}
}

void UGameFeaturesSubsystem::RemoveGameFeatureFromAssetManager(const UGameFeatureData* GameFeatureToRemove)
{
	/** NOT IMPLEMENTED - STUB */
}

void UGameFeaturesSubsystem::AddObserver(UGameFeatureStateChangeObserver* Observer)
{
	//@TODO: GameFeaturePluginEnginePush: May want to warn if one is added after any game feature plugins are already initialized, or go to a CallOrRegister sort of pattern
	check(Observer);
	Observers.Add(Observer);
}

void UGameFeaturesSubsystem::RemoveObserver(UGameFeatureStateChangeObserver* Observer)
{
	check(Observer);
	Observers.Remove(Observer);
}

void UGameFeaturesSubsystem::OnGameFeatureRegistering(const UGameFeatureData* GameFeatureData, const FString& PluginName)
{
	check(GameFeatureData);
	AddGameFeatureToAssetManager(GameFeatureData);

	for (UGameFeatureStateChangeObserver* Observer : Observers)
	{
		Observer->OnGameFeatureRegistering(GameFeatureData, PluginName);
	}

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureRegistering();
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureLoading(const UGameFeatureData* GameFeatureData)
{
	check(GameFeatureData);
	for (UGameFeatureStateChangeObserver* Observer : Observers)
	{
		Observer->OnGameFeatureLoading(GameFeatureData);
	}

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureLoading();
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureActivating(const UGameFeatureData* GameFeatureData)
{
	check(GameFeatureData);

	for (UGameFeatureStateChangeObserver* Observer : Observers)
	{
		Observer->OnGameFeatureActivating(GameFeatureData);
	}

	for (UGameFeatureAction* Action : GameFeatureData->GetActions())
	{
		if (Action != nullptr)
		{
			Action->OnGameFeatureActivating();
		}
	}
}

void UGameFeaturesSubsystem::OnGameFeatureDeactivating(const UGameFeatureData* GameFeatureData, FGameFeatureDeactivatingContext& Context)
{
	check(GameFeatureData);
	for (UGameFeatureStateChangeObserver* Observer : Observers)
	{
		Observer->OnGameFeatureDeactivating(GameFeatureData, Context);
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
	if (UGameFeaturePluginStateMachine* GFSM = GetGameFeaturePluginStateMachine(PluginURL, false))
	{
		return GFSM->GetGameFeatureDataForActivePlugin();
	}

	return nullptr;
}

void UGameFeaturesSubsystem::LoadAndActivateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginLoadComplete& CompleteDelegate)
{
	if (GameSpecificPolicies->IsPluginAllowed(PluginURL))
	{
		UGameFeaturePluginStateMachine* StateMachine = GetGameFeaturePluginStateMachine(PluginURL, true);
		StateMachine->SetDestinationState(EGameFeaturePluginState::Active, FGameFeatureStateTransitionComplete::CreateUObject(this, &ThisClass::LoadExternallyRequestedGameFeaturePluginComplete, CompleteDelegate));
	}
	else
	{
		//@TODO: Raise a failure to the CompleteDelegate
	}
}

bool UGameFeaturesSubsystem::GetGameFeaturePluginInstallPercent(const FString& PluginURL, float& Install_Percent)
{
	if (UGameFeaturePluginStateMachine* StateMachine = GetGameFeaturePluginStateMachine(PluginURL, false))
	{
		TSharedPtr<IInstallBundleManager> InstallBundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();

		const FName BundleName = FName(PluginURL);
		TOptional<FInstallBundleProgress> BundleProgress = InstallBundleManager ?
			InstallBundleManager->GetBundleProgress(BundleName) : TOptional<FInstallBundleProgress>();
		if (BundleProgress.IsSet())
		{
			Install_Percent = BundleProgress->Install_Percent;
			return true;
		}
	}
	return false;
}

void UGameFeaturesSubsystem::DeactivateGameFeaturePlugin(const FString& PluginURL)
{
	FGameFeaturePluginDeactivateComplete Callback = FGameFeaturePluginDeactivateComplete();
	DeactivateGameFeaturePlugin(PluginURL, Callback);
}

void UGameFeaturesSubsystem::DeactivateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginDeactivateComplete& CompleteDelegate)
{
	if (UGameFeaturePluginStateMachine* StateMachine = GetGameFeaturePluginStateMachine(PluginURL, false))
	{
		if (StateMachine->GetCurrentState() > EGameFeaturePluginState::Loaded)
		{
			StateMachine->SetDestinationState(EGameFeaturePluginState::Loaded, FGameFeatureStateTransitionComplete::CreateUObject(this, &ThisClass::DeactivateGameFeaturePluginComplete, CompleteDelegate));
		}
	}
}

void UGameFeaturesSubsystem::UnloadGameFeaturePlugin(const FString& PluginURL, bool bKeepRegistered)
{
	FGameFeaturePluginUnloadComplete Callback = FGameFeaturePluginUnloadComplete();
	UnloadGameFeaturePlugin(PluginURL, Callback, bKeepRegistered);
}

void UGameFeaturesSubsystem::UnloadGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUnloadComplete& CompleteDelegate, bool bKeepRegistered)
{
	if (UGameFeaturePluginStateMachine* StateMachine = GetGameFeaturePluginStateMachine(PluginURL, false))
	{
		EGameFeaturePluginState::Type DestinationState = bKeepRegistered ? EGameFeaturePluginState::Registered : EGameFeaturePluginState::Installed;
		if (StateMachine->GetCurrentState() > DestinationState)
		{
			StateMachine->SetDestinationState(DestinationState, FGameFeatureStateTransitionComplete::CreateUObject(this, &ThisClass::UnloadGameFeaturePluginComplete, CompleteDelegate));
		}
	}
}

void UGameFeaturesSubsystem::UninstallGameFeaturePlugin(const FString& PluginURL)
{
	FGameFeaturePluginUninstallComplete Callback = FGameFeaturePluginUninstallComplete();
	UninstallGameFeaturePlugin(PluginURL, Callback);
}

void UGameFeaturesSubsystem::UninstallGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUninstallComplete& CompleteDelegate)
{
	if (UGameFeaturePluginStateMachine* StateMachine = GetGameFeaturePluginStateMachine(PluginURL, false))
	{
		if (StateMachine->GetCurrentState() > EGameFeaturePluginState::StatusKnown)
		{
			StateMachine->SetDestinationState(EGameFeaturePluginState::StatusKnown, FGameFeatureStateTransitionComplete::CreateUObject(this, &ThisClass::UninstallGameFeaturePluginComplete, CompleteDelegate));
		}
	}
}

void UGameFeaturesSubsystem::LoadBuiltInGameFeaturePlugins(FBuiltInPluginAdditionalFilters AdditionalFilter)
{
	TArray<TSharedRef<IPlugin>> EnabledPlugins = IPluginManager::Get().GetEnabledPlugins();
	for (const TSharedRef<IPlugin>& Plugin : EnabledPlugins)
	{
		const FString& PluginDescriptorFilename = Plugin->GetDescriptorFileName();

		// Make sure you are in the game feature plugins folder. All GameFeaturePlugins are in this folder.
		//@TODO: GameFeaturePluginEnginePush: Comments elsewhere allow plugins outside of the folder as long as they explicitly opt in, either those are wrong or this check is wrong
		if (!PluginDescriptorFilename.IsEmpty() && FPaths::ConvertRelativePathToFull(PluginDescriptorFilename).StartsWith(GetDefault<UGameFeaturesSubsystemSettings>()->BuiltInGameFeaturePluginsFolder) && FPaths::FileExists(PluginDescriptorFilename))
		{
			const FString PluginURL = TEXT("file:") + PluginDescriptorFilename;
			if (GameSpecificPolicies->IsPluginAllowed(PluginURL))
			{
				FGameFeaturePluginDetails PluginDetails;
				if (GetGameFeaturePluginDetails(PluginDescriptorFilename, PluginDetails))
				{
					FBuiltInGameFeaturePluginBehaviorOptions BehaviorOptions;
					bool bShouldProcess = AdditionalFilter(PluginDescriptorFilename, PluginDetails, BehaviorOptions);

					if (bShouldProcess)
					{
						UGameFeaturePluginStateMachine* StateMachine = GetGameFeaturePluginStateMachine(PluginURL, true);

						EBuiltInAutoState InitialAutoState = BehaviorOptions.AutoStateOverride != EBuiltInAutoState::Invalid ? BehaviorOptions.AutoStateOverride : PluginDetails.BuiltInAutoState;
						
						EGameFeaturePluginState::Type DestinationState = EGameFeaturePluginState::UnknownStatus;
						switch (InitialAutoState)
						{
						case EBuiltInAutoState::Installed:
							DestinationState = EGameFeaturePluginState::Installed;
							break;
						case EBuiltInAutoState::Registered:
							DestinationState = EGameFeaturePluginState::Registered;
							break;
						case EBuiltInAutoState::Loaded:
							DestinationState = EGameFeaturePluginState::Loaded;
							break;
						case EBuiltInAutoState::Active:
							DestinationState = EGameFeaturePluginState::Active;
							break;

						default:
							ensureMsgf(false, TEXT("BuilIn Game Feature Plugin %s configured for invalid auto state %d. Setting to UnknownStatus."), (uint8)InitialAutoState);
							break;
						}

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
}

bool UGameFeaturesSubsystem::GetPluginURLForBuiltInPluginByName(const FString& PluginName, FString& OutPluginURL)
{
	if (FString* PluginURL = GameFeaturePluginNameToPathMap.Find(PluginName))
	{
		OutPluginURL = *PluginURL;
		return true;
	}

	return false;
}

FString UGameFeaturesSubsystem::GetPluginFilenameFromPluginURL(const FString& PluginURL)
{
	FString PluginFilename;
	if (UGameFeaturePluginStateMachine* GFSM = GetGameFeaturePluginStateMachine(PluginURL, false))
	{
		GFSM->GetPluginFilename(PluginFilename);
	}
	else
	{
		UE_LOG(LogGameFeatures, Error, TEXT("UGameFeaturesSubsystem could not get the plugin path form the plugin URL. URL:%s "), *PluginURL);
	}
	return PluginFilename;
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

	// Read the properties

	// Hotfixable. If it is not specified, then we assume it is
	OutPluginDetails.bHotfixable = true;
	ObjectPtr->TryGetBoolField(TEXT("Hotfixable"), OutPluginDetails.bHotfixable);

	// BuiltInAutoRegister. Default to true. If this is a built in plugin, should it be registered automatically (set to false if you intent to load late with LoadAndActivateGameFeaturePlugin)
	bool bBuiltInAutoRegister = true;
	ObjectPtr->TryGetBoolField(TEXT("BuiltInAutoRegister"), bBuiltInAutoRegister);

	// BuiltInAutoLoad. Default to true. If this is a built in plugin, should it be loaded automatically (set to false if you intent to load late with LoadAndActivateGameFeaturePlugin)
	bool bBuiltInAutoLoad = true;
	ObjectPtr->TryGetBoolField(TEXT("BuiltInAutoLoad"), bBuiltInAutoLoad);

	// The cooker will need to activate the plugin so that assets can be scanned properly
	bool bBuiltInAutoActivate = true;
	ObjectPtr->TryGetBoolField(TEXT("BuiltInAutoActivate"), bBuiltInAutoActivate);

	if (bBuiltInAutoRegister)
	{
		OutPluginDetails.BuiltInAutoState = EBuiltInAutoState::Registered;
		if (bBuiltInAutoLoad)
		{
			OutPluginDetails.BuiltInAutoState = EBuiltInAutoState::Loaded;
			if (bBuiltInAutoActivate)
			{
				OutPluginDetails.BuiltInAutoState = EBuiltInAutoState::Active;
			}
		}
	}

	// Read any additional metadata the policy might want to consume (e.g., a release version number)
	for (const FString& ExtraKey : GetDefault<UGameFeaturesSubsystemSettings>()->AdditionalPluginMetadataKeys)
	{
		FString ExtraValue;
		ObjectPtr->TryGetStringField(ExtraKey, ExtraValue);
		OutPluginDetails.AdditionalMetadata.Add(ExtraKey, ExtraValue);
	}

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
									FPaths::ConvertRelativePathToFull(PluginDependencyDescriptorFilename).StartsWith(GetDefault<UGameFeaturesSubsystemSettings>()->BuiltInGameFeaturePluginsFolder) &&
									FPaths::FileExists(PluginDependencyDescriptorFilename))
								{
									OutPluginDetails.PluginDependencies.Add(TEXT("file:") + DependencyPlugin->GetDescriptorFileName());
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

UGameFeaturePluginStateMachine* UGameFeaturesSubsystem::GetGameFeaturePluginStateMachine(const FString& PluginURL, bool bCreateIfItDoesntExist)
{
	UGameFeaturePluginStateMachine** ExistingStateMachine = GameFeaturePluginStateMachines.Find(PluginURL);
	if (ExistingStateMachine)
	{
		return *ExistingStateMachine;
	}

	if (!bCreateIfItDoesntExist)
	{
		return nullptr;
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
		UE_LOG(LogGameFeatures, Display, TEXT("Game feature '%s' loaded successfully."), *Machine->GetGameFeatureName());
	}
	else
	{
		const FString ErrorMessage = UE::GameFeatures::ToString(Result);
		UE_LOG(LogGameFeatures, Error, TEXT("Game feature '%s' load failed. Ending state: %s. Result: %s"),
			*Machine->GetGameFeatureName(),
			*EGameFeaturePluginState::ToString(Machine->GetCurrentState()),
			*ErrorMessage);
	}
}

void UGameFeaturesSubsystem::LoadExternallyRequestedGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginLoadComplete CompleteDelegate)
{
	LoadGameFeaturePluginComplete(Machine, Result);
	CompleteDelegate.ExecuteIfBound(Result);
	
	if (UGameFeatureData* GameFeatureData = Machine ? Machine->GetGameFeatureDataForActivePlugin() : nullptr)
	{
		PluginLoadedGameFeatureDataReadyDelegate.Broadcast(Machine->GetGameFeatureName(), GameFeatureData);
	}
}

void UGameFeaturesSubsystem::DeactivateGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginLoadComplete CompleteDelegate)
{
	// @todo: DO we need a DeactivateGameFeaturePluginComplete like "LoadExternallyRequestedGameFeaturePluginComplete" has?
	// Note: LoadGameFeaturePluginComplete is just used for logging.
	CompleteDelegate.ExecuteIfBound(Result);
	
	if (UGameFeatureData* GameFeatureData = Machine ? Machine->GetGameFeatureDataForActivePlugin() : nullptr)
	{
		PluginDeactivatedDelegate.Broadcast(Machine->GetGameFeatureName(), GameFeatureData);
	}
}

void UGameFeaturesSubsystem::UnloadGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginLoadComplete CompleteDelegate)
{
	// @todo: DO we need a UnloadedGameFeaturePluginComplete like "LoadExternallyRequestedGameFeaturePluginComplete" has?
	// Note: LoadGameFeaturePluginComplete is just used for logging.
	CompleteDelegate.ExecuteIfBound(Result);
}

void UGameFeaturesSubsystem::UninstallGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginUninstallComplete CompleteDelegate)
{
	CompleteDelegate.ExecuteIfBound(Result);
}


bool UGameFeaturesSubsystem::HandleRequestPluginDependencyStateMachines(const FString& PluginFilename, TArray<UGameFeaturePluginStateMachine*>& OutDependencyMachines)
{
	FGameFeaturePluginDetails Details;
	if (GetGameFeaturePluginDetails(PluginFilename, Details))
	{
		for (const FString& DependencyURL : Details.PluginDependencies)
		{
			UGameFeaturePluginStateMachine* Dependency = GetGameFeaturePluginStateMachine(DependencyURL, true);
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
			Ar.Logf(TEXT(",%s,%s"), *GFSM->GetGameFeatureName(), *EGameFeaturePluginState::ToString(GFSM->GetCurrentState()));
		}
		else
		{
			Ar.Logf(TEXT("%s (%s)"), *GFSM->GetGameFeatureName(), *EGameFeaturePluginState::ToString(GFSM->GetCurrentState()));
		}
		++PluginCount;
	}

	Ar.Logf(TEXT("Total Game Feature Plugins: %d"), PluginCount);
}
