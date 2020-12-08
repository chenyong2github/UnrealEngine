// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataRegistry.h"
#include "Engine/DataTable.h"
#include "Subsystems/EngineSubsystem.h"
#include "DataRegistrySubsystem.generated.h"


/** Enum used to indicate success or failure of EvaluateCurveTableRow. */
UENUM()
enum class EDataRegistrySubsystemGetItemResult : uint8
{
	/** Found the row successfully. */
	Found,
	/** Failed to find the row. */
	NotFound,
};

/** Singleton manager that provides access to all data registry */
UCLASS(NotBlueprintType)
class DATAREGISTRY_API UDataRegistrySubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:

	// Blueprint Interface, it is static for ease of use in custom nodes

	/** Looks up a cached item, this will return true if it is found and fill in OutItem. This needs to be hooked up to a structure of the same type as the registry */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = DataRegistry, meta = (DisplayName = "Find Data Registry Item", CustomStructureParam = "OutItem"))
	static bool GetCachedItemBP(FDataRegistryId ItemId, UPARAM(ref) FTableRowBase& OutItem) { return false; }
	DECLARE_FUNCTION(execGetCachedItemBP);

	/** Looks up a cached item using a resolved lookup, this will return true if it is found and fill in OutItem. This needs to be hooked up to a structure of the same type as the registry */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = DataRegistry, meta = (DisplayName = "Find Data Registry Item From Lookup", CustomStructureParam = "OutItem"))
	static bool GetCachedItemFromLookupBP(FDataRegistryId ItemId, const FDataRegistryLookup& ResolvedLookup, UPARAM(ref) FTableRowBase& OutItem) { return false; }
	DECLARE_FUNCTION(execGetCachedItemFromLookupBP);

	/** Starts an acquire from blueprint, will call delegate on completion */
	UFUNCTION(BlueprintCallable, Category = DataRegistry, meta = (DisplayName = "Acquire Data Registry Item") )
	static bool AcquireItemBP(FDataRegistryId ItemId, FDataRegistryItemAcquiredBPCallback AcquireCallback);

	/**
	 * Attempt to evaluate a curve stored in a DataRegistry cache using a specific input value
	 *
	 * @param ItemID		Name to lookup in cache
	 * @param InputValue	Time/level/parameter input value used to evaluate curve at certain position
	 * @param DefaultValue	Value to use if no curve found or input is outside acceptable range
	 * @param OutValue		Result will be replaced with evaluated value, or default if that fails
	 */
	UFUNCTION(BlueprintCallable, Category = DataRegistry, meta = (ExpandEnumAsExecs = "OutResult"))
	static void EvaluateDataRegistryCurve(FDataRegistryId ItemId, float InputValue, float DefaultValue, EDataRegistrySubsystemGetItemResult& OutResult, float& OutValue);

	/** Returns true if this is a non-empty type, does not verify if it is currently registered */
	UFUNCTION(BlueprintPure, Category = DataRegistry, meta = (ScriptMethod = "IsValid", ScriptOperator = "bool", BlueprintThreadSafe))
	static bool IsValidDataRegistryType(FDataRegistryType DataRegistryType);

	/** Converts a Primary Asset Type to a string. The other direction is not provided because it cannot be validated */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToString (DataRegistryType)", CompactNodeTitle = "->", ScriptMethod = "ToString", BlueprintThreadSafe), Category = DataRegistry)
	static FString Conv_DataRegistryTypeToString(FDataRegistryType DataRegistryType);

	/** Returns true if the values are equal (A == B) */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Equal (DataRegistryType)", CompactNodeTitle = "==", ScriptOperator = "==", BlueprintThreadSafe), Category = DataRegistry)
	static bool EqualEqual_DataRegistryType(FDataRegistryType A, FDataRegistryType B);

	/** Returns true if the values are not equal (A != B) */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "NotEqual (DataRegistryType)", CompactNodeTitle = "!=", ScriptOperator = "!=", BlueprintThreadSafe), Category = DataRegistry)
	static bool NotEqual_DataRegistryType(FDataRegistryType A, FDataRegistryType B);

	
	// Native interface, works using subsystem instance

	/** Returns the global subsystem instance */
	static UDataRegistrySubsystem* Get();

	/** Finds the right registry for a type name */
	UDataRegistry* GetRegistryForType(FName RegistryType) const;

	/** Gives proper display text for an id, using id format */
	FText GetDisplayTextForId(FDataRegistryId ItemId) const;

	/** Gets list of all registries, for iterating in UI mostly */
	void GetAllRegistries(TArray<UDataRegistry*>& AllRegistries, bool bSortByType = true) const;

	/** Refreshes the active registries based on what's in memory */
	void RefreshRegistryMap();

	/** Loads all registry assets and initializes them, call early in startup */
	void LoadAllRegistries();

	/** True if all registeries should be initialized */
	bool AreRegistriesInitialized() const;

	/** Returns true if the system is enabled via any config settings, will optionally warn if not enabled */
	bool IsConfigEnabled(bool bWarnIfNotEnabled = false) const;

	/** Initializes all loaded registries and prepares them for query */
	void InitializeAllRegistries(bool bResetIfInitialized = false);

	/** Deinitializes all loaded registries */
	void DeinitializeAllRegistries();

	/** Resets state for all registries, call when gameplay has concluded to destroy caches */
	void ResetRuntimeState();

	/** Handles changes to DataRegistrySettings while engine is running */
	void ReinitializeFromConfig();

	/** 
	 * Attempt to register a specified asset with all active sources that allow dynamic registration, returning true if anything changed
	 * This will fail if the registry does not exist yet
	 *
	 * @param RegistryType		Type to register with, if invalid will try all registries
	 * @param AssetData			Filled in asset data of asset to attempt to register
	 * @Param AssetPriority		Priority of asset relative to others, higher numbers will be searched first
	 */
	bool RegisterSpecificAsset(FDataRegistryType RegistryType, FAssetData& AssetData, int32 AssetPriority = 0);

	/** Removes references to a specific asset, returns bool if it was removed */
	bool UnregisterSpecificAsset(FDataRegistryType RegistryType, const FSoftObjectPath& AssetPath);

	/** Schedules registration of assets by path, this will happen immediately or queue it up if the data registries don't exist yet */
	void PreregisterSpecificAssets(const TMap<FDataRegistryType, TArray<FSoftObjectPath>>& AssetMap, int32 AssetPriority = 0);

	/** Returns the raw cached data and struct type, useful for generic C++ calls */
	FDataRegistryCacheGetResult GetCachedItemRaw(const uint8*& OutItemMemory, const UScriptStruct*& OutItemStruct, const FDataRegistryId& ItemId) const;

	/** Returns the raw cached data and struct type, useful for generic C++ calls */
	FDataRegistryCacheGetResult GetCachedItemRawFromLookup(const uint8*& OutItemMemory, const UScriptStruct*& OutItemStruct, const FDataRegistryId& ItemId, const FDataRegistryLookup& Lookup) const;

	/** Returns an evaluated curve value, as well as an actual curve if it is found. Return value specifies the cache safety for the curve */
	FDataRegistryCacheGetResult EvaluateCachedCurve(float& OutValue, const FRealCurve*& OutCurve, FDataRegistryId ItemId, float InputValue, float DefaultValue = 0.0f) const;

	/** Finds the cached item, using the request context to handle remapping. This will return null if not in local cache */
	template <class T>
	T* GetCachedItem(const FDataRegistryId& ItemId) const
	{
		const UDataRegistry* FoundRegistry = GetRegistryForType(ItemId.RegistryType);
		if (FoundRegistry)
		{
			return FoundRegistry->GetCachedItem<T>(ItemId);
		}
		return nullptr;
	}

	/** Start an async load of an item */
	bool AcquireItem(const FDataRegistryId& ItemId, FDataRegistryItemAcquiredCallback DelegateToCall) const;

protected:
	TMap<FName, TWeakObjectPtr<UDataRegistry>> RegistryMap;

	// Initialization order, need to wait for other early-load systems to initialize
	virtual void PostEngineInit();
	virtual void PostGameplayTags();
	virtual void PostAssetManager();
	virtual void ApplyPreregisterMap(UDataRegistry* Registry);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Paths that will be scanned for registries
	TArray<FString> AssetScanPaths;

	// List of assets to attempt to register when data registries come online
	typedef TPair<FSoftObjectPath, int32> FPreregisterAsset;
	TMap<FDataRegistryType, TArray<FPreregisterAsset>> PreregisterAssetMap;

	// True if initialization has finished and registries were scanned, will be false if not config enabled
	bool bFullyInitialized = false;

	// True if initialization is ready to start, will be true even if config disabled
	bool bReadyForInitialization = false;

	// Handle used to keep registries in memory, only set in non-editor builds
	TSharedPtr<FStreamableHandle> RegistryLoadHandle;

#if WITH_EDITOR
	virtual void PreBeginPIE(bool bStartSimulate);
	virtual void EndPIE(bool bStartSimulate);
#endif
};

/* Test actor, move later
UCLASS(Blueprintable)
class ADataRegistryTestActor : public AActor
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category=DataRegistry)
	bool TestSyncRead(FDataRegistryId RegistryId);

	UFUNCTION(BlueprintCallable, Category = DataRegistry)
	bool TestAsyncRead(FDataRegistryId RegistryId);

	void AsyncReadComplete(const FDataRegistryAcquireResult& Result);
};
*/
