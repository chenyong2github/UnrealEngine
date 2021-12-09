// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "GameFeaturePluginOperationResult.h"
#include "Engine/Engine.h"

#include "GameFeaturesSubsystem.generated.h"

class UGameFeaturePluginStateMachine;
class IGameFeatureStateChangeObserver;
struct FStreamableHandle;
struct FAssetIdentifier;
class UGameFeatureData;
class UGameFeaturesProjectPolicies;
enum class EGameFeaturePluginState : uint8;
class IPlugin;
class FJsonObject;
struct FWorldContext;

/** 
 * Struct that determines if game feature action state changes should be applied for cases where there are multiple worlds or contexts.
 * The default value means to apply to all possible objects. This can be safely copied and used for later querying.
 */
struct GAMEFEATURES_API FGameFeatureStateChangeContext
{
public:

	/** Sets a specific world context handle to limit changes to */
	void SetRequiredWorldContextHandle(FName Handle);

	/** Sees if the specific world context matches the application rules */
	bool ShouldApplyToWorldContext(const FWorldContext& WorldContext) const;

	/** True if events bound using this context should apply when using other context */
	bool ShouldApplyUsingOtherContext(const FGameFeatureStateChangeContext& OtherContext) const;

	/** Check if this has the exact same state change application rules */
	FORCEINLINE bool operator==(const FGameFeatureStateChangeContext& OtherContext) const
	{
		if (OtherContext.WorldContextHandle == WorldContextHandle)
		{
			return true;
		}

		return false;
	}

	/** Allow this to be used as a map key */
	FORCEINLINE friend uint32 GetTypeHash(const FGameFeatureStateChangeContext& OtherContext)
	{
		return GetTypeHash(OtherContext.WorldContextHandle);
	}

private:
	/** Specific world context to limit changes to, if none then it will apply to all */
	FName WorldContextHandle;
};

/** Context that provides extra information for activating a game feature */
struct FGameFeatureActivatingContext : public FGameFeatureStateChangeContext
{
public:
	//@TODO: Add rules specific to activation when required

private:

	friend struct FGameFeaturePluginState_Activating;
};

/** Context that provides extra information for deactivating a game feature, will use the same change context rules as the activating context */
struct FGameFeatureDeactivatingContext : public FGameFeatureStateChangeContext
{
public:
	// Call this if your observer has an asynchronous action to complete as part of shutdown, and invoke the returned delegate when you are done (on the game thread!)
	FSimpleDelegate PauseDeactivationUntilComplete()
	{
		++NumPausers;
		return CompletionDelegate;
	}

	FGameFeatureDeactivatingContext(FSimpleDelegate&& InCompletionDelegate)
		: CompletionDelegate(MoveTemp(InCompletionDelegate))
	{
	}

	int32 GetNumPausers() const { return NumPausers; }
private:
	FSimpleDelegate CompletionDelegate;
	int32 NumPausers = 0;

	friend struct FGameFeaturePluginState_Deactivating;
};

GAMEFEATURES_API DECLARE_LOG_CATEGORY_EXTERN(LogGameFeatures, Log, All);
/** Notification that a game feature plugin install/register/load/unload has finished */
DECLARE_DELEGATE_OneParam(FGameFeaturePluginChangeStateComplete, const UE::GameFeatures::FResult& /*Result*/);

using FGameFeaturePluginLoadComplete = FGameFeaturePluginChangeStateComplete;
using FGameFeaturePluginDeactivateComplete = FGameFeaturePluginChangeStateComplete;
using FGameFeaturePluginUnloadComplete = FGameFeaturePluginChangeStateComplete;
using FGameFeaturePluginUninstallComplete = FGameFeaturePluginChangeStateComplete;

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

UENUM(BlueprintType)
enum class EGameFeatureTargetState : uint8
{
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
	static void UnloadGameFeatureData(const UGameFeatureData* GameFeatureToUnload);

	void AddObserver(UObject* Observer);
	void RemoveObserver(UObject* Observer);

	/**
	 * Calls the compile-time lambda on each active game feature data of the specified type
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

	/**
	 * Calls the compile-time lambda on each registered game feature data of the specified type
	 * @param GameFeatureDataType       The kind of data required
	 */
	template<class GameFeatureDataType, typename Func>
	void ForEachRegisteredGameFeature(Func InFunc) const
	{
		for (auto StateMachineIt = GameFeaturePluginStateMachines.CreateConstIterator(); StateMachineIt; ++StateMachineIt)
		{
			if (UGameFeaturePluginStateMachine* GFSM = StateMachineIt.Value())
			{
				if (const GameFeatureDataType* GameFeatureData = Cast<const GameFeatureDataType>(GetRegisteredDataForStateMachine(GFSM)))
				{
					InFunc(GameFeatureData);
				}
			}
		}
	}

public:
	/** Construct a 'file:' Plugin URL using from the PluginDescriptorPath */
	static FString GetPluginURL_FileProtocol(const FString& PluginDescriptorPath);

	/** Construct a 'installbundle:' Plugin URL using from the PluginName and required install bundles */
	static FString GetPluginURL_InstallBundleProtocol(const FString& PluginName, TArrayView<const FString> BundleNames);
	static FString GetPluginURL_InstallBundleProtocol(const FString& PluginName, const FString& BundleName);
	static FString GetPluginURL_InstallBundleProtocol(const FString& PluginName, TArrayView<const FName> BundleNames);
	static FString GetPluginURL_InstallBundleProtocol(const FString& PluginName, FName BundleName);

public:
	/** Returns all the active plugins GameFeatureDatas */
	void GetGameFeatureDataForActivePlugins(TArray<const UGameFeatureData*>& OutActivePluginFeatureDatas);

	/** Returns the game feature data for an active plugin specified by PluginURL */
	const UGameFeatureData* GetGameFeatureDataForActivePluginByURL(const FString& PluginURL);

	/** Returns the game feature data for a registered plugin specified by PluginURL */
	const UGameFeatureData* GetGameFeatureDataForRegisteredPluginByURL(const FString& PluginURL);

	/** Loads a single game feature plugin. */
	void LoadGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginLoadComplete& CompleteDelegate);

	/** Loads a single game feature plugin and activates it. */
	void LoadAndActivateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginLoadComplete& CompleteDelegate);

	/** Changes the target state of a game feature plugin */
	void ChangeGameFeatureTargetState(const FString& PluginURL, EGameFeatureTargetState TargetState, const FGameFeaturePluginChangeStateComplete& CompleteDelegate);

	/** Gets the Install_Percent for single game feature plugin if it is active. */
	bool GetGameFeaturePluginInstallPercent(const FString& PluginURL, float& Install_Percent) const;

	/** Determines if a plugin is in the Active state.*/
	bool IsGameFeaturePluginActive(const FString& PluginURL, bool bCheckForActivating = false) const;

	/** Deactivates the specified plugin */
	void DeactivateGameFeaturePlugin(const FString& PluginURL);
	void DeactivateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginDeactivateComplete& CompleteDelegate);

	/** Unloads the specified game feature plugin. */
	void UnloadGameFeaturePlugin(const FString& PluginURL, bool bKeepRegistered = false);
	void UnloadGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUnloadComplete& CompleteDelegate, bool bKeepRegistered = false);

	/** Uninstall the specified game feature plugin. Will remove the game feature plugin from the device if it was downloaded */
	void UninstallGameFeaturePlugin(const FString& PluginURL);
	void UninstallGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUninstallComplete& CompleteDelegate);

	/** Same as UninstallGameFeaturePlugin, but completely removes all tracking data associated with the plugin. */
	void TerminateGameFeaturePlugin(const FString& PluginURL);
	void TerminateGameFeaturePlugin(const FString& PluginURL, const FGameFeaturePluginUninstallComplete& CompleteDelegate);

	/** If the specified plugin is a built-in plugin, return the URL used to identify it. Returns true if the plugin exists, false if it was not found */
	bool GetPluginURLForBuiltInPluginByName(const FString& PluginName, FString& OutPluginURL) const;
	
	/** Get the plugin path from the plugin name */
	FString GetPluginFilenameFromPluginName(const FString& PluginName);

	/** Get the plugin path from the plugin URL */
	FString GetPluginFilenameFromPluginURL(const FString& PluginURL) const;

	/** Fixes a package path/directory to either be relative to plugin root or not. Paths relative to different roots will not be modified */
	static void FixPluginPackagePath(FString& PathToFix, const FString& PluginRootPath, bool bMakeRelativeToPluginRoot);

	/** Returns the game-specific policy for managing game feature plugins */
	template <typename T = UGameFeaturesProjectPolicies>
	T& GetPolicy() const
	{
		return *CastChecked<T>(GameSpecificPolicies, ECastCheckedType::NullChecked);
	}

	typedef TFunctionRef<bool(const FString& PluginFilename, const FGameFeaturePluginDetails& Details, FBuiltInGameFeaturePluginBehaviorOptions& OutOptions)> FBuiltInPluginAdditionalFilters;

	/** Loads a built-in game feature plugin if it passes the specified filter */
	void LoadBuiltInGameFeaturePlugin(const TSharedRef<IPlugin>& Plugin, FBuiltInPluginAdditionalFilters AdditionalFilter);

	/** Loads all built-in game feature plugins that pass the specified filters */
	void LoadBuiltInGameFeaturePlugins(FBuiltInPluginAdditionalFilters AdditionalFilter);

	/** Returns the list of plugin filenames that have progressed beyond installed. Used in cooking to determine which will be cooked. */
	//@TODO: GameFeaturePluginEnginePush: Might not be general enough for engine level, TBD
	void GetLoadedGameFeaturePluginFilenamesForCooking(TArray<FString>& OutLoadedPluginFilenames) const;

	/** Removes assets that are in plugins we know to be inactive.  Order is not maintained. */
	void FilterInactivePluginAssets(TArray<FAssetIdentifier>& AssetsToFilter) const;

	/** Removes assets that are in plugins we know to be inactive.  Order is not maintained. */
	void FilterInactivePluginAssets(TArray<FAssetData>& AssetsToFilter) const;

	/** Returns the current state of the state machine for the specified plugin URL */
	EGameFeaturePluginState GetPluginState(const FString& PluginURL);

	/** Determine the initial feature state for a built-in plugin */
	static EBuiltInAutoState DetermineBuiltInInitialFeatureState(TSharedPtr<FJsonObject> Descriptor, const FString& ErrorContext);

	static EGameFeaturePluginState ConvertInitialFeatureStateToTargetState(EBuiltInAutoState InitialState);

private:
	TSet<FString> GetActivePluginNames() const;

	void OnGameFeatureTerminating(const FString& PluginURL);
	friend struct FGameFeaturePluginState_Terminal;

	void OnGameFeatureCheckingStatus(const FString& PluginURL);
	friend struct FGameFeaturePluginState_UnknownStatus;

	void OnGameFeatureRegistering(const UGameFeatureData* GameFeatureData, const FString& PluginName);
	friend struct FGameFeaturePluginState_Registering;

	void OnGameFeatureUnregistering(const UGameFeatureData* GameFeatureData, const FString& PluginName);
	friend struct FGameFeaturePluginState_Unregistering;

	void OnGameFeatureActivating(const UGameFeatureData* GameFeatureData, const FString& PluginName, FGameFeatureActivatingContext& Context);
	friend struct FGameFeaturePluginState_Activating;

	void OnGameFeatureDeactivating(const UGameFeatureData* GameFeatureData, const FString& PluginName, FGameFeatureDeactivatingContext& Context);
	friend struct FGameFeaturePluginState_Deactivating;

	void OnGameFeatureLoading(const UGameFeatureData* GameFeatureData);
	friend struct FGameFeaturePluginState_Loading;

	void OnAssetManagerCreated();

	/** Scans for assets specified in the game feature data */
	static void AddGameFeatureToAssetManager(const UGameFeatureData* GameFeatureToAdd, const FString& PluginName);

	static void RemoveGameFeatureFromAssetManager(const UGameFeatureData* GameFeatureToRemove);

private:
	const UGameFeatureData* GetDataForStateMachine(UGameFeaturePluginStateMachine* GFSM) const;
	const UGameFeatureData* GetRegisteredDataForStateMachine(UGameFeaturePluginStateMachine* GFSM) const;

	/** Gets relevant properties out of a uplugin file */
	bool GetGameFeaturePluginDetails(const FString& PluginDescriptorFilename, struct FGameFeaturePluginDetails& OutPluginDetails) const;

	/** Gets the state machine associated with the specified plugin name */
	UGameFeaturePluginStateMachine* FindGameFeaturePluginStateMachineByPluginName(const FString& PluginName) const;

	/** Gets the state machine associated with the specified URL */
	UGameFeaturePluginStateMachine* FindGameFeaturePluginStateMachine(const FString& PluginURL) const;

	/** Gets the state machine associated with the specified URL, creates it if it doesnt exist */
	UGameFeaturePluginStateMachine* FindOrCreateGameFeaturePluginStateMachine(const FString& PluginURL);

	/** Notification that a game feature has finished loading, and whether it was successful */
	void LoadGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result);

	/** Notification that a game feature that was requested to be terminate has finished terminating, and whether it was successful. */
	void TerminateGameFeaturePluginComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginUninstallComplete CompleteDelegate);

	/** Generic notification that calls the Complete delegate without broadcasting anything else.*/
	void ChangeGameFeatureTargetStateComplete(UGameFeaturePluginStateMachine* Machine, const UE::GameFeatures::FResult& Result, FGameFeaturePluginUninstallComplete CompleteDelegate);

	/** Handler for when a state machine requests its dependencies. Returns false if the dependencies could not be read */
	bool HandleRequestPluginDependencyStateMachines(const FString& PluginFilename, TArray<UGameFeaturePluginStateMachine*>& OutDependencyMachines);

	/** Handle 'ListGameFeaturePlugins' console command */
	void ListGameFeaturePlugins(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& Ar);

private:
	/** The list of all game feature plugin state machine objects */
	UPROPERTY(Transient)
	TMap<FString, UGameFeaturePluginStateMachine*> GameFeaturePluginStateMachines;

	TMap<FString, FString> GameFeaturePluginNameToPathMap;

	UPROPERTY()
	TArray<UObject*> Observers;

	UPROPERTY(Transient)
	UGameFeaturesProjectPolicies* GameSpecificPolicies;

	bool bInitializedPolicyManager = false;
};
