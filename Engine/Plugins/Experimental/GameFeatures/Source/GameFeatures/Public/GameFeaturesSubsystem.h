// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "GameFeaturePluginOperationResult.h"
#include "Engine/Engine.h"

#include "GameFeaturesSubsystem.generated.h"

class UGameFeaturePluginStateMachine;
class UGameFeatureStateChangeObserver;
struct FStreamableHandle;
class UGameFeatureData;
class UGameFeaturesProjectPolicies;


struct FGameFeatureDeactivatingContext
{
public:
	// Call this if your observer has an asynchronous action to complete as part of shutdown, and invoke the returned delegate when you are done (on the game thread!)
	FSimpleDelegate PauseDeactivationUntilComplete()
	{
		++NumPausers;
		return CompletionDelegate;
	}

private:
	FGameFeatureDeactivatingContext(FSimpleDelegate&& InCompletionDelegate)
		: CompletionDelegate(MoveTemp(InCompletionDelegate))
	{
	}

	FSimpleDelegate CompletionDelegate;
	int32 NumPausers = 0;

	friend struct FGameFeaturePluginState_Deactivating;
};

DECLARE_LOG_CATEGORY_EXTERN(LogGameFeatures, Log, All);

/** Notification that a game feature plugin load has finished */
DECLARE_DELEGATE_OneParam(FGameFeaturePluginLoadComplete, const UE::GameFeatures::FResult& /*Result*/);

/** Notification that a game feature plugin deactivate has finished.*/
DECLARE_DELEGATE_OneParam(FGameFeaturePluginDeactivateComplete, const UE::GameFeatures::FResult& /*Result*/);

/** Notification that a game feature plugin unload has finished.*/
DECLARE_DELEGATE_OneParam(FGameFeaturePluginUnloadComplete, const UE::GameFeatures::FResult& /*Result*/);

/** Notification that a game feature plugin uninstall has finished.*/
DECLARE_DELEGATE_OneParam(FGameFeaturePluginUninstallComplete, const UE::GameFeatures::FResult& /*Result*/);

/** Notification that a game feature plugin load has finished successfully and feeds back the GameFeatureData*/
DECLARE_MULTICAST_DELEGATE_TwoParams(FGameFeaturePluginLoadCompleteDataReady, const FString& /*Name*/, const UGameFeatureData* /*Data*/);

/** Notification that a game feature plugin load has deactivated successfully and feeds back the GameFeatureData that was being used*/
DECLARE_MULTICAST_DELEGATE_TwoParams(FGameFeaturePluginDeativated, const FString& /*Name*/, const UGameFeatureData* /*Data*/);

enum class EBuiltInAutoState : uint8
{
	Invalid,
	Installed,
	Registered,
	Loaded,
	Active
};

struct FGameFeaturePluginDetails
{
	TArray<FString> PluginDependencies;
	TMap<FString, FString> AdditionalMetadata;
	bool bHotfixable;
	EBuiltInAutoState BuiltInAutoState;

	FGameFeaturePluginDetails()
		: bHotfixable(false)
		, BuiltInAutoState(EBuiltInAutoState::Installed)
	{}
};

struct FBuiltInGameFeaturePluginBehaviorOptions
{
	EBuiltInAutoState AutoStateOverride = EBuiltInAutoState::Invalid;
};



/** The manager subsystem for game features */
UCLASS()
class GAMEFEATURES_API UGameFeaturesSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	//~UEngineSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~End of UEngineSubsystem interface

	static UGameFeaturesSubsystem& Get() { return *GEngine->GetEngineSubsystem<UGameFeaturesSubsystem>(); }

public:
	/** Loads the specified game feature data and its bundles */
	static TSharedPtr<FStreamableHandle> LoadGameFeatureData(const FString& GameFeatureToLoad);

	void AddObserver(UGameFeatureStateChangeObserver* Observer);
	void RemoveObserver(UGameFeatureStateChangeObserver* Observer);

	/**
	 * Calls the compile-time lambda on each loaded game feature data of the specified type
	 * @param GameFeatureDataType       The kind of data required
	 */
	template<class GameFeatureDataType, typename Func>
	void ForEachActiveGameFeature(Func InFunc) const
	{
		for (auto StateMachineIt = GameFeaturePluginStateMachines.CreateConstIterator(); StateMachineIt; ++StateMachineIt)
		{
			if (UGameFeaturePluginStateMachine* GFSM = StateMachineIt.Value())
			{
				if (const GameFeatureDataType* GameFeatureData = Cast<const GameFeatureDataType>(GetDataForStateMachine(GFSM)))
				{
					InFunc(GameFeatureData);
				}
			}
		}
	}

public:
	/** Returns all the active plugins GameFeatureDatas */
	void GetGameFeatureDataForActivePlugins(TArray<const UGameFeatureData*>& OutActivePluginFeatureDatas);

	/** Returns the game feature data for the plugin specified by PluginURL */
	const UGameFeatureData* GetGameFeatureDataForActivePluginByURL(const FString& PluginURL);

	/** Loads a single game feature plugin and activates it. */
	void LoadAndActivateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginLoadComplete& CompleteDelegate);

	/** Gets the Install_Percent for single game feature plugin if it is active. */
	bool GetGameFeaturePluginInstallPercent(const FString& PluginURL, float& Install_Percent);

	/** Deactivates the specified plugin */
	void DeactivateGameFeaturePlugin(const FString& PluginURL);
	void DeactivateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginDeactivateComplete& CompleteDelegate);

	/** Unloads the specified game feature plugin. */
	void UnloadGameFeaturePlugin(const FString& PluginURL, bool bKeepRegistered = false);
	void UnloadGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUnloadComplete& CompleteDelegate, bool bKeepRegistered = false);

	/** Uninstall the specified game feature plugin. Will remove the game feature plugin from the device if it was downloaded */
	void UninstallGameFeaturePlugin(const FString& PluginURL);
	void UninstallGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUninstallComplete& CompleteDelegate);

	/** If the specified plugin is a built-in plugin, return the URL used to identify it. Returns true if the plugin exists, false if it was not found */
	bool GetPluginURLForBuiltInPluginByName(const FString& PluginName, FString& OutPluginURL);
	
	/** Get the plugin path from the plugin URL */
	FString GetPluginFilenameFromPluginURL(const FString& PluginURL);

	/** Returns the game-specific policy for managing game feature plugins */
	template <typename T = UGameFeaturesProjectPolicies>
	T& GetPolicy() const
	{
		return *CastChecked<T>(GameSpecificPolicies, ECastCheckedType::NullChecked);
	}

	typedef TFunction<bool(const FString& PluginFilename, const FGameFeaturePluginDetails& Details, FBuiltInGameFeaturePluginBehaviorOptions& OutOptions)> FBuiltInPluginAdditionalFilters;

	/** Loads built-in game feature plugins that pass the specified filters */
	void LoadBuiltInGameFeaturePlugins(FBuiltInPluginAdditionalFilters AdditionalFilter);

	/** Returns the list of plugin filenames that have progressed beyond installed. Used in cooking to determine which will be cooked. */
	//@TODO: GameFeaturePluginEnginePush: Might not be general enough for engine level, TBD
	void GetLoadedGameFeaturePluginFilenamesForCooking(TArray<FString>& OutLoadedPluginFilenames) const;
	
	/** Broadcasts when a plugin is activated and the GameFeatureData is avaialble */
	static FGameFeaturePluginLoadCompleteDataReady& OnPluginLoadCompleteDataReady() { return PluginLoadedGameFeatureDataReadyDelegate; }

	/** Broadcasts when a plugin is deactivated */
	static FGameFeaturePluginDeativated& OnPluginDeactivatedDataReady() { return PluginDeactivatedDelegate; }

private:
	void OnGameFeatureRegistering(const UGameFeatureData* GameFeatureData, const FString& PluginName);
	friend struct FGameFeaturePluginState_Registering;

	void OnGameFeatureActivating(const UGameFeatureData* GameFeatureData);
	friend struct FGameFeaturePluginState_Activating;

	void OnGameFeatureDeactivating(const UGameFeatureData* GameFeatureData, FGameFeatureDeactivatingContext& Context);
	friend struct FGameFeaturePluginState_Deactivating;

	void OnGameFeatureLoading(const UGameFeatureData* GameFeatureData);
	friend struct FGameFeaturePluginState_Loading;

	void OnAssetManagerCreated();

	/** Scans for assets specified in the game feature data */
	static void AddGameFeatureToAssetManager(const UGameFeatureData* GameFeatureToAdd);

	static void RemoveGameFeatureFromAssetManager(const UGameFeatureData* GameFeatureToRemove);

private:
	const UGameFeatureData* GetDataForStateMachine(UGameFeaturePluginStateMachine* GFSM) const;

	/** Gets relevant properties out of a uplugin file */
	bool GetGameFeaturePluginDetails(const FString& PluginDescriptorFilename, struct FGameFeaturePluginDetails& OutPluginDetails) const;

	/** Gets the state machine associated with the specified URL, creates it if it doesnt exist when bCreateIfItDoesntExist is true */
	UGameFeaturePluginStateMachine* GetGameFeaturePluginStateMachine(const FString& PluginURL, bool bCreateIfItDoesntExist);

	/** Notification that a game feature has finished loading, and whether it was successful */
	void LoadGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result);

	/** Notification that a game feature that was requested by LoadAndActivateGameFeaturePlugin has finished loading, and whether it was successful */
	void LoadExternallyRequestedGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginLoadComplete CompleteDelegate);

	/** Notification that a game feature that was requested to be deactivated has finished deactivating, and whether it was successful. */
	void DeactivateGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginDeactivateComplete CompleteDelegate);

	/** Notification that a game feature that was requested to be unloaded has finished unloading, and whether it was successful. */
	void UnloadGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginUnloadComplete CompleteDelegate);

	/** Notification that a game feature that was requested to be uninstalled has finished uninstalling, and whether it was successful. */
	void UninstallGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginUninstallComplete CompleteDelegate);

	/** Handler for when a state machine requests its dependencies. Returns false if the dependencies could not be read */
	bool HandleRequestPluginDependencyStateMachines(const FString& PluginFilename, TArray<UGameFeaturePluginStateMachine*>& OutDependencyMachines);

	/** Handle 'ListGameFeaturePlugins' console command */
	void ListGameFeaturePlugins(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& Ar);

private:
	/** The list of all game feature plugin state machine objects */
	UPROPERTY(Transient)
	TMap<FString, UGameFeaturePluginStateMachine*> GameFeaturePluginStateMachines;


	TMap<FString, FString> GameFeaturePluginNameToPathMap;

	UPROPERTY(Transient)
	TArray<UGameFeatureStateChangeObserver*> Observers;

	UPROPERTY(Transient)
	UGameFeaturesProjectPolicies* GameSpecificPolicies;

	bool bInitializedPolicyManager = false;
	
	static FGameFeaturePluginLoadCompleteDataReady PluginLoadedGameFeatureDataReadyDelegate;
	static FGameFeaturePluginDeativated PluginDeactivatedDelegate;
};
