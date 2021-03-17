// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/AssetRegistryInterface.h"

class FDependsNode;
struct FARCompiledFilter;

#ifndef ASSET_REGISTRY_STATE_DUMPING_ENABLED
	#define ASSET_REGISTRY_STATE_DUMPING_ENABLED !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

/** Load/Save options used to modify how the cache is serialized. These are read out of the AssetRegistry section of Engine.ini and can be changed per platform. */
struct FAssetRegistrySerializationOptions
{
	/** True rather to load/save registry at all */
	bool bSerializeAssetRegistry = false;

	/** True rather to load/save dependency info. If true this will handle hard and soft package references */
	bool bSerializeDependencies = false;

	/** True rather to load/save dependency info for Name references,  */
	bool bSerializeSearchableNameDependencies = false;

	/** True rather to load/save dependency info for Manage references,  */
	bool bSerializeManageDependencies = false;

	/** If true will read/write FAssetPackageData */
	bool bSerializePackageData = false;

	/** True if CookFilterlistTagsByClass is a whitelist. False if it is a blacklist. */
	bool bUseAssetRegistryTagsWhitelistInsteadOfBlacklist = false;

	/** True if we want to only write out asset data if it has valid tags. This saves memory by not saving data for things like textures */
	bool bFilterAssetDataWithNoTags = false;

	/** True if we also want to filter out dependency data for assets that have no tags. Only filters if bFilterAssetDataWithNoTags is also true */
	bool bFilterDependenciesWithNoTags = false;

	/** Filter out searchable names from dependency data */
	bool bFilterSearchableNames = false;

	/** The map of classname to tag set of tags that are allowed in cooked builds. This is either a whitelist or blacklist depending on bUseAssetRegistryTagsWhitelistInsteadOfBlacklist */
	TMap<FName, TSet<FName>> CookFilterlistTagsByClass;

	/** Tag keys whose values should be stored as FName in cooked builds */
	TSet<FName> CookTagsAsName;

	/** Tag keys whose values should be stored as FRegistryExportPath in cooked builds */
	TSet<FName> CookTagsAsPath;

	/** Options used to read/write the DevelopmentAssetRegistry, which includes all data */
	void ModifyForDevelopment()
	{
		bSerializeAssetRegistry = bSerializeDependencies = bSerializeSearchableNameDependencies = bSerializeManageDependencies = bSerializePackageData = true;
		DisableFilters();
	}

	/** Disable all filters */
	void DisableFilters()
	{
		bFilterAssetDataWithNoTags = false;
		bFilterDependenciesWithNoTags = false;
		bFilterSearchableNames = false;
	}
};

struct FAssetRegistryLoadOptions
{
	FAssetRegistryLoadOptions() = default;
	explicit FAssetRegistryLoadOptions(const FAssetRegistrySerializationOptions& Options)
		: bLoadDependencies(Options.bSerializeDependencies)
		, bLoadPackageData(Options.bSerializePackageData)
	{}

	bool bLoadDependencies = true;
	bool bLoadPackageData = true;
	int32 ParallelWorkers = 0;
};

/** The state of an asset registry, this is used internally by IAssetRegistry to represent the disk cache, and is also accessed directly to save/load cooked caches */
class ASSETREGISTRY_API FAssetRegistryState
{
public:
	FAssetRegistryState() = default;
	FAssetRegistryState(const FAssetRegistryState&) = delete;
	FAssetRegistryState(FAssetRegistryState&& Rhs) { *this = MoveTemp(Rhs); }
	~FAssetRegistryState();

	FAssetRegistryState& operator=(const FAssetRegistryState&) = delete;
	FAssetRegistryState& operator=(FAssetRegistryState&& O);

	/**
	 * Enum controlling how we initialize this state
	 */
	enum class EInitializationMode
	{
		Rebuild,
		OnlyUpdateExisting,
		Append
	};

	/**
	 * Does the given path contain assets?
	 * @param bARFiltering Whether to apply filtering from UE::AssetRegistry::FFiltering (false by default)
	 * @note This function doesn't recurse into sub-paths.
	 */
	bool HasAssets(const FName PackagePath, bool bARFiltering=false) const;

	/**
	 * Gets asset data for all assets that match the filter.
	 * Assets returned must satisfy every filter component if there is at least one element in the component's array.
	 * Assets will satisfy a component if they match any of the elements in it.
	 *
	 * @param Filter filter to apply to the assets in the AssetRegistry
	 * @param PackageNamesToSkip explicit list of packages to skip, because they were already added
	 * @param OutAssetData the list of assets in this path
	 * @param bARFiltering Whether to apply filtering from UE::AssetRegistry::FFiltering (false by default)
	 */
	bool GetAssets(const FARCompiledFilter& Filter, const TSet<FName>& PackageNamesToSkip, TArray<FAssetData>& OutAssetData, bool bARFiltering = false) const;

	/**
	 * Enumerate asset data for all assets that match the filter.
	 * Assets returned must satisfy every filter component if there is at least one element in the component's array.
	 * Assets will satisfy a component if they match any of the elements in it.
	 *
	 * @param Filter filter to apply to the assets in the AssetRegistry
	 * @param PackageNamesToSkip explicit list of packages to skip, because they were already added
	 * @param Callback function to call for each asset data enumerated
	 * @param bARFiltering Whether to apply filtering from UE::AssetRegistry::FFiltering (false by default)
	 */
	bool EnumerateAssets(const FARCompiledFilter& Filter, const TSet<FName>& PackageNamesToSkip, TFunctionRef<bool(const FAssetData&)> Callback, bool bARFiltering = false) const;

	/**
	 * Gets asset data for all assets in the registry state.
	 *
	 * @param PackageNamesToSkip explicit list of packages to skip, because they were already added
	 * @param OutAssetData the list of assets
	 * @param bARFiltering Whether to apply filtering from UE::AssetRegistry::FFiltering (false by default)
	 */
	bool GetAllAssets(const TSet<FName>& PackageNamesToSkip, TArray<FAssetData>& OutAssetData, bool bARFiltering = false) const;

	/**
	 * Enumerates asset data for all assets in the registry state.
	 *
	 * @param PackageNamesToSkip explicit list of packages to skip, because they were already added
	 * @param Callback function to call for each asset data enumerated
	 * @param bARFiltering Whether to apply filtering from UE::AssetRegistry::FFiltering (false by default)
	 */
	bool EnumerateAllAssets(const TSet<FName>& PackageNamesToSkip, TFunctionRef<bool(const FAssetData&)> Callback, bool bARFiltering = false) const;

	UE_DEPRECATED(4.26, "Use GetDependencies that takes a UE::AssetRegistry::EDependencyCategory instead")
	bool GetDependencies(const FAssetIdentifier& AssetIdentifier, TArray<FAssetIdentifier>& OutDependencies, EAssetRegistryDependencyType::Type InDependencyType) const;
	/**
	 * Gets a list of packages and searchable names that are referenced by the supplied package or name. (On disk references ONLY)
	 *
	 * @param AssetIdentifier	the name of the package/name for which to gather dependencies
	 * @param OutDependencies	a list of things that are referenced by AssetIdentifier
	 * @param Category	which category(ies) of dependencies to include in the output list. Dependencies matching ANY of the OR'd categories will be returned.
	 * @param Flags	which flags are required present or not present on the dependencies. Dependencies matching ALL required and NONE excluded bits will be returned. For each potentially returned dependency, flags not applicable to their category are ignored.
	 */
	bool GetDependencies(const FAssetIdentifier& AssetIdentifier, TArray<FAssetIdentifier>& OutDependencies, UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::All, const UE::AssetRegistry::FDependencyQuery& Flags = UE::AssetRegistry::FDependencyQuery()) const;
	bool GetDependencies(const FAssetIdentifier& AssetIdentifier, TArray<FAssetDependency>& OutDependencies, UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::All, const UE::AssetRegistry::FDependencyQuery& Flags = UE::AssetRegistry::FDependencyQuery()) const;

	UE_DEPRECATED(4.26, "Use GetReferencers that takes a UE::AssetRegistry::EDependencyCategory instead")
	bool GetReferencers(const FAssetIdentifier& AssetIdentifier, TArray<FAssetIdentifier>& OutReferencers, EAssetRegistryDependencyType::Type InReferenceType = EAssetRegistryDependencyType::All) const;
	/**
	 * Gets a list of packages and searchable names that reference the supplied package or name. (On disk references ONLY)
	 *
	 * @param AssetIdentifier	the name of the package/name for which to gather dependencies
	 * @param OutReferencers	a list of things that reference AssetIdentifier
	 * @param Category	which category(ies) of dependencies to include in the output list. Dependencies matching ANY of the OR'd categories will be returned.
	 * @param Flags	which flags are required present or not present on the dependencies. Dependencies matching ALL required and NONE excluded bits will be returned. For each potentially returned dependency, flags not applicable to their category are ignored.
	 */
	bool GetReferencers(const FAssetIdentifier& AssetIdentifier, TArray<FAssetIdentifier>& OutReferencers, UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::All, const UE::AssetRegistry::FDependencyQuery& Flags = UE::AssetRegistry::FDependencyQuery()) const;
	bool GetReferencers(const FAssetIdentifier& AssetIdentifier, TArray<FAssetDependency>& OutReferencers, UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::All, const UE::AssetRegistry::FDependencyQuery& Flags = UE::AssetRegistry::FDependencyQuery()) const;

	/**
	 * Gets the asset data for the specified object path
	 *
	 * @param ObjectPath the path of the object to be looked up
	 * @return the assets data, null if not found
	 */
	const FAssetData* GetAssetByObjectPath(const FName ObjectPath) const
	{
		FAssetData* const* FoundAsset = CachedAssetsByObjectPath.Find(ObjectPath);
		if (FoundAsset)
		{
			return *FoundAsset;
		}

		return nullptr;
	}

	/**
	 * Gets the asset data for the specified package name
	 *
	 * @param PackageName the path of the package to be looked up
	 * @return an array of AssetData*, empty if nothing found
	 */
	TArrayView<FAssetData const* const> GetAssetsByPackageName(const FName PackageName) const
	{
		if (const TArray<FAssetData*, TInlineAllocator<1>>* FoundAssetArray = CachedAssetsByPackageName.Find(PackageName))
		{
			return MakeArrayView(*FoundAssetArray);
		}

		return TArrayView<FAssetData* const>();
	}

	/**
	 * Gets the asset data for the specified asset class
	 *
	 * @param ClassName the class name of the assets to look for
	 * @return An array of AssetData*, empty if nothing found
	 */
	const TArray<const FAssetData*>& GetAssetsByClassName(const FName ClassName) const
	{
		static TArray<const FAssetData*> InvalidArray;
		const TArray<FAssetData*>* FoundAssetArray = CachedAssetsByClass.Find(ClassName);
		if (FoundAssetArray)
		{
			return reinterpret_cast<const TArray<const FAssetData*>&>(*FoundAssetArray);
		}

		return InvalidArray;
	}

	/**
	 * Gets the asset data for the specified asset tag
	 *
	 * @param TagName the tag name to search for
	 * @return An array of AssetData*, empty if nothing found
	 */
	const TArray<const FAssetData*>& GetAssetsByTagName(const FName TagName) const
	{
		static TArray<const FAssetData*> InvalidArray;
		const TArray<FAssetData*>* FoundAssetArray = CachedAssetsByTag.Find(TagName);
		if (FoundAssetArray)
		{
			return reinterpret_cast<const TArray<const FAssetData*>&>(*FoundAssetArray);
		}

		return InvalidArray;
	}

	/** Returns const version of internal ObjectPath->AssetData map for fast iteration */
	const TMap<FName, const FAssetData*>& GetObjectPathToAssetDataMap() const
	{
		return reinterpret_cast<const TMap<FName, const FAssetData*>&>(CachedAssetsByObjectPath);
	}

	/** Returns const version of internal Tag->AssetDatas map for fast iteration */
	const TMap<FName, const TArray<const FAssetData*>> GetTagToAssetDatasMap() const
	{
		return reinterpret_cast<const TMap<FName, const TArray<const FAssetData*>>&>(CachedAssetsByTag);
	}

	/** Returns const version of internal PackageName->PackageData map for fast iteration */
	const TMap<FName, const FAssetPackageData*>& GetAssetPackageDataMap() const
	{
		return reinterpret_cast<const TMap<FName, const FAssetPackageData*>&>(CachedPackageData);
	}

	/** Get the set of primary assets contained in this state */
	void GetPrimaryAssetsIds(TSet<FPrimaryAssetId>& OutPrimaryAssets) const;

	/** Returns non-editable pointer to the asset package data */
	const FAssetPackageData* GetAssetPackageData(FName PackageName) const;

	/** Returns all package names */
	void GetPackageNames(TArray<FName>& OutPackageNames) const
	{
		OutPackageNames.Reserve(CachedAssetsByPackageName.Num());
		for (auto It = CachedAssetsByPackageName.CreateConstIterator(); It; ++It)
		{
			OutPackageNames.Add(It.Key());
		}
	}

	/** Finds an existing package data, or creates a new one to modify */
	FAssetPackageData* CreateOrGetAssetPackageData(FName PackageName);

	/** Removes existing package data */
	bool RemovePackageData(FName PackageName);

	/** Adds the asset data to the lookup maps */
	void AddAssetData(FAssetData* AssetData);

	/** Finds an existing asset data based on object path and updates it with the new value and updates lookup maps */
	void UpdateAssetData(const FAssetData& NewAssetData);

	/** Updates an existing asset data with the new value and updates lookup maps */
	void UpdateAssetData(FAssetData* AssetData, const FAssetData& NewAssetData);

	/** Removes the asset data from the lookup maps */
	void RemoveAssetData(FAssetData* AssetData, bool bRemoveDependencyData, bool& bOutRemovedAssetData, bool& bOutRemovedPackageData);

	/** Resets to default state */
	void Reset();

	/** Initializes cache from existing set of asset data and depends nodes */
	void InitializeFromExisting(const TMap<FName, FAssetData*>& AssetDataMap, const TMap<FAssetIdentifier, FDependsNode*>& DependsNodeMap, const TMap<FName, FAssetPackageData*>& AssetPackageDataMap, const FAssetRegistrySerializationOptions& Options, EInitializationMode InitializationMode = EInitializationMode::Rebuild);
	void InitializeFromExisting(const FAssetRegistryState& Existing, const FAssetRegistrySerializationOptions& Options, EInitializationMode InitializationMode = EInitializationMode::Rebuild)
	{
		InitializeFromExisting(Existing.CachedAssetsByObjectPath, Existing.CachedDependsNodes, Existing.CachedPackageData, Options, InitializationMode);
	}

	/** 
	 * Prunes an asset cache, this removes asset data, nodes, and package data that isn't needed. 
	 * @param RequiredPackages If set, only these packages will be maintained. If empty it will keep all unless filtered by other parameters
	 * @param RemovePackages These packages will be removed from the current set
	 * @param ChunksToKeep The list of chunks that are allowed to remain. Any assets in other chunks are pruned. If empty, all assets are kept regardless of chunk
	 * @param Options Serialization options to read filter info from
	 */
	void PruneAssetData(const TSet<FName>& RequiredPackages, const TSet<FName>& RemovePackages, const TSet<int32> ChunksToKeep, const FAssetRegistrySerializationOptions& Options);
	void PruneAssetData(const TSet<FName>& RequiredPackages, const TSet<FName>& RemovePackages, const FAssetRegistrySerializationOptions& Options);

	
	/**
	 * Initializes a cache from an existing using a set of filters. This is more efficient than calling InitalizeFromExisting and then PruneAssetData.
	 * @param ExistingState State to use initialize from
	 * @param RequiredPackages If set, only these packages will be maintained. If empty it will keep all unless filtered by other parameters
	 * @param RemovePackages These packages will be removed from the current set
	 * @param ChunksToKeep The list of chunks that are allowed to remain. Any assets in other chunks are pruned. If empty, all assets are kept regardless of chunk
	 * @param Options Serialization options to read filter info from
	 */
	void InitializeFromExistingAndPrune(const FAssetRegistryState& ExistingState, const TSet<FName>& RequiredPackages, const TSet<FName>& RemovePackages, const TSet<int32> ChunksToKeep, const FAssetRegistrySerializationOptions& Options);


	/** Serialize the registry to/from a file, skipping editor only data */
	bool Serialize(FArchive& Ar, const FAssetRegistrySerializationOptions& Options);

	/** Save without editor-only data */
	bool Save(FArchive& Ar, const FAssetRegistrySerializationOptions& Options);
	bool Load(FArchive& Ar, const FAssetRegistryLoadOptions& Options = FAssetRegistryLoadOptions());

	/** Returns memory size of entire registry, optionally logging sizes */
	uint32 GetAllocatedSize(bool bLogDetailed = false) const;

	/** Checks a filter to make sure there are no illegal entries */
	static bool IsFilterValid(const FARCompiledFilter& Filter);

	/** Returns the number of assets in this state */
	int32 GetNumAssets() const { return NumAssets; }

#if ASSET_REGISTRY_STATE_DUMPING_ENABLED
	/**
	 * Writes out the state in textual form. Use arguments to control which segments to emit.
	 * @param Arguments List of segments to emit. Possible values: 'ObjectPath', 'PackageName', 'Path', 'Class', 'Tag', 'Dependencies' and 'PackageData'
	 * @param OutPages Textual representation will be written to this array; each entry will have LinesPerPage lines of the full dump.
	 * @param LinesPerPage - how many lines should be combined into each string element of OutPages, for e.g. breaking up the dump into separate files.
	 *        To facilitate diffing between similar-but-different registries, the actual number of lines per page will be slightly less than LinesPerPage; we introduce partially deterministic pagebreaks near the end of each page.
	 */
	void Dump(const TArray<FString>& Arguments, TArray<FString>& OutPages, int32 LinesPerPage=1) const;
#endif

private:
	template<class Archive>
	void Load(Archive&& Ar, FAssetRegistryVersion::Type Version, const FAssetRegistryLoadOptions& Options);

	/** Initialize the lookup maps */
	void SetAssetDatas(TArrayView<FAssetData> AssetDatas, const FAssetRegistryLoadOptions& Options);

	/** Find the first non-redirector dependency node starting from InDependency. */
	FDependsNode* ResolveRedirector(FDependsNode* InDependency, TMap<FName, FAssetData*>& InAllowedAssets, TMap<FDependsNode*, FDependsNode*>& InCache);

	/** Finds an existing node for the given package and returns it, or returns null if one isn't found */
	FDependsNode* FindDependsNode(const FAssetIdentifier& Identifier) const;

	/** Creates a node in the CachedDependsNodes map or finds the existing node and returns it */
	FDependsNode* CreateOrFindDependsNode(const FAssetIdentifier& Identifier);

	/** Removes the depends node and updates the dependencies to no longer contain it as as a referencer. */
	bool RemoveDependsNode(const FAssetIdentifier& Identifier);

	/** Filter a set of tags and output a copy of the filtered set. */
	static void FilterTags(const FAssetDataTagMapSharedView& InTagsAndValues, FAssetDataTagMap& OutTagsAndValues, const TSet<FName>* ClassSpecificFilterlist, const FAssetRegistrySerializationOptions & Options);

	void LoadDependencies(FArchive& Ar);
	void LoadDependencies_BeforeFlags(FArchive& Ar, bool bSerializeDependencies, FAssetRegistryVersion::Type Version);

	/** The map of ObjectPath names to asset data for assets saved to disk */
	TMap<FName, FAssetData*> CachedAssetsByObjectPath;

	/** The map of package names to asset data for assets saved to disk */
	TMap<FName, TArray<FAssetData*, TInlineAllocator<1>> > CachedAssetsByPackageName;

	/** The map of long package path to asset data for assets saved to disk */
	TMap<FName, TArray<FAssetData*> > CachedAssetsByPath;

	/** The map of class name to asset data for assets saved to disk */
	TMap<FName, TArray<FAssetData*> > CachedAssetsByClass;

	/** The map of asset tag to asset data for assets saved to disk */
	TMap<FName, TArray<FAssetData*> > CachedAssetsByTag;

	/** A map of object names to dependency data */
	TMap<FAssetIdentifier, FDependsNode*> CachedDependsNodes;

	/** A map of Package Names to Package Data */
	TMap<FName, FAssetPackageData*> CachedPackageData;

	/** When loading a registry from disk, we can allocate all the FAssetData objects in one chunk, to save on 10s of thousands of heap allocations */
	TArray<FAssetData*> PreallocatedAssetDataBuffers;
	TArray<FDependsNode*> PreallocatedDependsNodeDataBuffers;
	TArray<FAssetPackageData*> PreallocatedPackageDataBuffers;

	/** Counters for asset/depends data memory allocation to ensure that every FAssetData and FDependsNode created is deleted */
	int32 NumAssets = 0;
	int32 NumDependsNodes = 0;
	int32 NumPackageData = 0;

	friend class UAssetRegistryImpl;
};
