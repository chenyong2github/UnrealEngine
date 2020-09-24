// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistry/AssetRegistryState.h"
#include "AssetRegistry.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/ArrayReader.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/MetaData.h"
#include "AssetRegistryPrivate.h"
#include "AssetRegistry/ARFilter.h"
#include "DependsNode.h"
#include "PackageReader.h"
#include "NameTableArchive.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"

#if !defined(USE_COMPACT_ASSET_REGISTRY)
#error "USE_COMPACT_ASSET_REGISTRY must be defined"
#endif

#if !USE_COMPACT_ASSET_REGISTRY
void FAssetRegistryState::IngestIniSettingsForCompact(TArray<FString>& AsFName, TArray<FString>& AsPathName, TArray<FString>& AsLocText)
{

}

#else
#include "Blueprint/BlueprintSupport.h"
#include "UObject/PrimaryAssetId.h"

// if any of these cause a link error, then you can't use USE_COMPACT_ASSET_REGISTRY with this build config
FAssetDataTagMapValueStorage& FAssetDataTagMapValueStorage::Get()
{
	static FAssetDataTagMapValueStorage Singleton;
	return Singleton;
}

static TSet<FName> KeysToCompactToFName;
static TSet<FName> KeysToCompactToExportText;
static TSet<FName> KeysToFTextExportText;

void FAssetRegistryState::IngestIniSettingsForCompact(TArray<FString>& AsFName, TArray<FString>& AsPathName, TArray<FString>& AsLocText)
{
	for (auto & Item : AsFName)
	{
		KeysToCompactToFName.Add(*Item);
	}
	for (auto & Item : AsPathName)
	{
		KeysToCompactToExportText.Add(*Item);
	}
	for (auto & Item : AsLocText)
	{
		KeysToFTextExportText.Add(*Item);
	}
}

#define AGGRESSIVE_NAMEIFICATION (0)

bool FAssetDataTagMapValueStorage::KeyShouldHaveFNameValue(FName Key, const FString& Value)
{
	if (FCString::Strcmp(*Value, TEXT("False")) == 0)
	{
		return true;
	}
	if (FCString::Strcmp(*Value, TEXT("True")) == 0)
	{
		return true;
	}

#if AGGRESSIVE_NAMEIFICATION  // this was an experiment, it doesn't save enough at this time to bother
	if (Value.Len() < NAME_SIZE && FName::IsValidXName(Value, INVALID_NAME_CHARACTERS) && !KeyShouldHaveCompactExportTextValue(Key, Value))
	{
		const FName IndexedName(*Value, FNAME_Find);
		if (IndexedName != NAME_None)
		{
			if (IndexedName.ToString().Compare(Value, ESearchCase::CaseSensitive) == 0)
			{
				return true;
			}
		}
		else
		{
			if (FName(*Value).ToString().Compare(Value, ESearchCase::CaseSensitive) == 0)
			{
				return true;
			}
		}
	}
#endif
	return KeysToCompactToFName.Contains(Key);
}
bool FAssetDataTagMapValueStorage::KeyShouldHaveCompactExportTextValue(FName Key, const FString& Value)
{
	return KeysToCompactToExportText.Contains(Key);
}

bool FAssetDataTagMapValueStorage::KeyShouldHaveLocTextExportTextValue(FName Key, const FString& Value)
{
	bool bMaybeLoc = KeysToFTextExportText.Contains(Key);

	if (bMaybeLoc && !FTextStringHelper::IsComplexText(*Value))
	{
		bMaybeLoc = false;
	}

	return bMaybeLoc;
}

#endif



FAssetRegistryState::FAssetRegistryState()
{
	NumAssets = 0;
	NumDependsNodes = 0;
	NumPackageData = 0;
}

FAssetRegistryState::~FAssetRegistryState()
{
	Reset();
}

void FAssetRegistryState::Reset()
{
	// if we have preallocated all the FAssetData's in a single block, free it now, instead of one at a time
	if (PreallocatedAssetDataBuffers.Num())
	{
		for (FAssetData* Buffer : PreallocatedAssetDataBuffers)
		{
			delete[] Buffer;
		}
		PreallocatedAssetDataBuffers.Reset();

		NumAssets = 0;
	}
	else
	{
		// Delete all assets in the cache
		for (TMap<FName, FAssetData*>::TConstIterator AssetDataIt(CachedAssetsByObjectPath); AssetDataIt; ++AssetDataIt)
		{
			if (AssetDataIt.Value())
			{
				delete AssetDataIt.Value();
				NumAssets--;
			}
		}
	}

	// Make sure we have deleted all our allocated FAssetData objects
	ensure(NumAssets == 0);

	if (PreallocatedDependsNodeDataBuffers.Num())
	{
		for (FDependsNode* Buffer : PreallocatedDependsNodeDataBuffers)
		{
			delete[] Buffer;
		}
		PreallocatedDependsNodeDataBuffers.Reset();
		NumDependsNodes = 0;
	}
	else
	{
		// Delete all depends nodes in the cache
		for (TMap<FAssetIdentifier, FDependsNode*>::TConstIterator DependsIt(CachedDependsNodes); DependsIt; ++DependsIt)
		{
			if (DependsIt.Value())
			{
				delete DependsIt.Value();
				NumDependsNodes--;
			}
		}
	}

	// Make sure we have deleted all our allocated FDependsNode objects
	ensure(NumDependsNodes == 0);

	if (PreallocatedPackageDataBuffers.Num())
	{
		for (FAssetPackageData* Buffer : PreallocatedPackageDataBuffers)
		{
			delete[] Buffer;
		}
		PreallocatedPackageDataBuffers.Reset();
		NumPackageData = 0;
	}
	else
	{
		// Delete all depends nodes in the cache
		for (TMap<FName, FAssetPackageData*>::TConstIterator PackageDataIt(CachedPackageData); PackageDataIt; ++PackageDataIt)
		{
			if (PackageDataIt.Value())
			{
				delete PackageDataIt.Value();
				NumPackageData--;
			}
		}
	}

	// Make sure we have deleted all our allocated package data objects
	ensure(NumPackageData == 0);

	// Clear cache
	CachedAssetsByObjectPath.Empty();
	CachedAssetsByPackageName.Empty();
	CachedAssetsByPath.Empty();
	CachedAssetsByClass.Empty();
	CachedAssetsByTag.Empty();
	CachedDependsNodes.Empty();
	CachedPackageData.Empty();
}

void FAssetRegistryState::FilterTags(const FAssetDataTagMapSharedView& InTagsAndValues, FAssetDataTagMap& OutTagsAndValues, const TSet<FName>* ClassSpecificFilterlist, const FAssetRegistrySerializationOptions& Options)
{
	static FName WildcardName(TEXT("*"));
	const TSet<FName>* AllClassesFilterlist = Options.CookFilterlistTagsByClass.Find(WildcardName);

	// Exclude blacklisted tags or include only white listed tags, based on how we were configured in ini
	for (const auto& TagPair : InTagsAndValues)
	{
		const bool bInAllClasseslist = AllClassesFilterlist && (AllClassesFilterlist->Contains(TagPair.Key) || AllClassesFilterlist->Contains(WildcardName));
		const bool bInClassSpecificlist = ClassSpecificFilterlist && (ClassSpecificFilterlist->Contains(TagPair.Key) || ClassSpecificFilterlist->Contains(WildcardName));
		if (Options.bUseAssetRegistryTagsWhitelistInsteadOfBlacklist)
		{
			// It's a white list, only include it if it is in the all classes list or in the class specific list
			if (bInAllClasseslist || bInClassSpecificlist)
			{
				// It is in the white list. Keep it.
				OutTagsAndValues.Add(TagPair.Key, TagPair.Value);
			}
		}
		else
		{
			// It's a blacklist, include it unless it is in the all classes list or in the class specific list
			if (!bInAllClasseslist && !bInClassSpecificlist)
			{
				// It isn't in the blacklist. Keep it.
				OutTagsAndValues.Add(TagPair.Key, TagPair.Value);
			}
		}
	}
}

void FAssetRegistryState::InitializeFromExistingAndPrune(const FAssetRegistryState & ExistingState, const TSet<FName>& RequiredPackages, const TSet<FName>& RemovePackages, const TSet<int32> ChunksToKeep, const FAssetRegistrySerializationOptions & Options)
{
	LLM_SCOPE(ELLMTag::AssetRegistry);
	const bool bIsFilteredByChunkId = ChunksToKeep.Num() != 0;
	const bool bIsFilteredByRequiredPackages = RequiredPackages.Num() != 0;
	const bool bIsFilteredByRemovedPackages = RemovePackages.Num() != 0;

	TSet<FName> RequiredDependNodePackages;

	// Duplicate asset data entries
	for (const TPair<FName, FAssetData*>& AssetPair : ExistingState.CachedAssetsByObjectPath)
	{
		const FAssetData* AssetData = AssetPair.Value;

		bool bRemoveAssetData = false;
		bool bRemoveDependencyData = true;

		if (bIsFilteredByChunkId &&
			!AssetData->ChunkIDs.ContainsByPredicate([&](int32 ChunkId) { return ChunksToKeep.Contains(ChunkId); }))
		{
			bRemoveAssetData = true;
		}
		else if (bIsFilteredByRequiredPackages && !RequiredPackages.Contains(AssetData->PackageName))
		{
			bRemoveAssetData = true;
		}
		else if (bIsFilteredByRemovedPackages && RemovePackages.Contains(AssetData->PackageName))
		{
			bRemoveAssetData = true;
		}
		else if (Options.bFilterAssetDataWithNoTags && AssetData->TagsAndValues.Num() == 0 &&
			!FPackageName::IsLocalizedPackage(AssetData->PackageName.ToString()))
		{
			bRemoveAssetData = true;
			bRemoveDependencyData = Options.bFilterDependenciesWithNoTags;
		}

		if (bRemoveAssetData)
		{
			if (!bRemoveDependencyData)
			{
				RequiredDependNodePackages.Add(AssetData->PackageName);
			}
			continue;
		}

		FAssetDataTagMap NewTagsAndValues;
		FAssetRegistryState::FilterTags(AssetData->TagsAndValues, NewTagsAndValues, Options.CookFilterlistTagsByClass.Find(AssetData->AssetClass), Options);

		FAssetData* NewAssetData = new FAssetData(AssetData->PackageName, AssetData->PackagePath, AssetData->AssetName,
			AssetData->AssetClass, NewTagsAndValues, AssetData->ChunkIDs, AssetData->PackageFlags);
		// Add asset to new state
		AddAssetData(NewAssetData);
	}

	// Create package data for all script and required packages
	for (const TPair<FName, FAssetPackageData*>& Pair : ExistingState.CachedPackageData)
	{
		if (Pair.Value)
		{
			// Only add if also in asset data map, or script package
			if (CachedAssetsByPackageName.Find(Pair.Key) ||
				FPackageName::IsScriptPackage(Pair.Key.ToString()))
			{
				FAssetPackageData* NewData = CreateOrGetAssetPackageData(Pair.Key);
				*NewData = *Pair.Value;
			}
		}
	}

	// Find valid dependency nodes for all script and required packages
	TSet<FDependsNode*> ValidDependsNodes;
	ValidDependsNodes.Reserve(ExistingState.CachedDependsNodes.Num());
	for (const TPair<FAssetIdentifier, FDependsNode*>& Pair : ExistingState.CachedDependsNodes)
	{
		FDependsNode* Node = Pair.Value;
		const FAssetIdentifier& Id = Node->GetIdentifier();
		bool bRemoveDependsNode = false;

		if (Options.bFilterSearchableNames && Id.IsValue())
		{
			bRemoveDependsNode = true;
		}
		else if (Id.IsPackage() &&
			!CachedAssetsByPackageName.Contains(Id.PackageName) &&
			!RequiredDependNodePackages.Contains(Id.PackageName) &&
			!FPackageName::IsScriptPackage(Id.PackageName.ToString()))
		{
			bRemoveDependsNode = true;
		}

		if (!bRemoveDependsNode)
		{
			ValidDependsNodes.Add(Node);
		}
	}

	// Duplicate dependency nodes
	for (FDependsNode* OldNode : ValidDependsNodes)
	{
		FDependsNode* NewNode = CreateOrFindDependsNode(OldNode->GetIdentifier());
		NewNode->Reserve(OldNode);
	}
	
	for (FDependsNode* OldNode : ValidDependsNodes)
	{
		FDependsNode* NewNode = CreateOrFindDependsNode(OldNode->GetIdentifier());
		OldNode->IterateOverDependencies([&, OldNode, NewNode](FDependsNode* InDependency, UE::AssetRegistry::EDependencyCategory InCategory, UE::AssetRegistry::EDependencyProperty InFlags, bool bDuplicate) {
			if (ValidDependsNodes.Contains(InDependency))
			{
				// Only add link if it's part of the filtered asset set
				FDependsNode* NewDependency = CreateOrFindDependsNode(InDependency->GetIdentifier());
				NewNode->SetIsDependencyListSorted(InCategory, false);
				NewNode->AddDependency(NewDependency, InCategory, InFlags);
				NewDependency->SetIsReferencersSorted(false);
				NewDependency->AddReferencer(NewNode);
			}
		});
	}

	// Remove any orphaned depends nodes. This will leave cycles in but those might represent useful data
	TArray<FDependsNode*> AllDependsNodes;
	CachedDependsNodes.GenerateValueArray(AllDependsNodes);
	for (FDependsNode* DependsNode : AllDependsNodes)
	{
		if (DependsNode->GetConnectionCount() == 0)
		{
			RemoveDependsNode(DependsNode->GetIdentifier());
		}
	}

	// Restore the sortedness that we turned off for performance when creating each DependsNode
	for (TPair<FAssetIdentifier, FDependsNode*> Pair : CachedDependsNodes)
	{
		FDependsNode* DependsNode = Pair.Value;
		DependsNode->SetIsDependencyListSorted(UE::AssetRegistry::EDependencyCategory::All, true);
		DependsNode->SetIsReferencersSorted(true);
	}
}

void FAssetRegistryState::InitializeFromExisting(const TMap<FName, FAssetData*>& AssetDataMap, const TMap<FAssetIdentifier, FDependsNode*>& DependsNodeMap, const TMap<FName, FAssetPackageData*>& AssetPackageDataMap, const FAssetRegistrySerializationOptions& Options, EInitializationMode InInitializationMode)
{
	LLM_SCOPE(ELLMTag::AssetRegistry);
	if (InInitializationMode == EInitializationMode::Rebuild)
	{
		Reset();
	}

	for (const TPair<FName, FAssetData*>& Pair : AssetDataMap)
	{
		FAssetData* ExistingData = nullptr;

		if (InInitializationMode == EInitializationMode::OnlyUpdateExisting)
		{
			ExistingData = CachedAssetsByObjectPath.FindRef(Pair.Key);
			if (!ExistingData)
			{
				continue;
			}
		}

		if (Pair.Value)
		{
			// Filter asset registry tags now
			const FAssetData& AssetData = *Pair.Value;

			FAssetDataTagMap LocalTagsAndValues;
			FAssetRegistryState::FilterTags(AssetData.TagsAndValues, LocalTagsAndValues, Options.CookFilterlistTagsByClass.Find(AssetData.AssetClass), Options);

			if (InInitializationMode == EInitializationMode::OnlyUpdateExisting)
			{
				// Only modify tags
				if (ExistingData && (LocalTagsAndValues != ExistingData->TagsAndValues.GetMap()))
				{
					FAssetData TempData = *ExistingData;
					TempData.TagsAndValues = FAssetDataTagMapSharedView(MoveTemp(LocalTagsAndValues));
					UpdateAssetData(ExistingData, TempData);
				}
			}
			else
			{
				FAssetData* NewData = new FAssetData(AssetData.PackageName, AssetData.PackagePath, AssetData.AssetName,
					AssetData.AssetClass, LocalTagsAndValues, AssetData.ChunkIDs, AssetData.PackageFlags);
				AddAssetData(NewData);
			}
		}
	}

	TSet<FAssetIdentifier> ScriptPackages;

	if (InInitializationMode != EInitializationMode::OnlyUpdateExisting)
	{
		for (const TPair<FName, FAssetPackageData*>& Pair : AssetPackageDataMap)
		{
			bool bIsScriptPackage = FPackageName::IsScriptPackage(Pair.Key.ToString());

			if (Pair.Value)
			{
				// Only add if also in asset data map, or script package
				if (bIsScriptPackage)
				{
					ScriptPackages.Add(Pair.Key);

					FAssetPackageData* NewData = CreateOrGetAssetPackageData(Pair.Key);
					*NewData = *Pair.Value;
				}
				else if (CachedAssetsByPackageName.Find(Pair.Key))
				{
					FAssetPackageData* NewData = CreateOrGetAssetPackageData(Pair.Key);
					*NewData = *Pair.Value;
				}
			}
		}

		for (const TPair<FAssetIdentifier, FDependsNode*>& Pair : DependsNodeMap)
		{
			FDependsNode* OldNode = Pair.Value;
			FDependsNode* NewNode = CreateOrFindDependsNode(Pair.Key);
			NewNode->Reserve(OldNode);
		}

		for (const TPair<FAssetIdentifier, FDependsNode*>& Pair : DependsNodeMap)
		{
			FDependsNode* OldNode = Pair.Value;
			FDependsNode* NewNode = CreateOrFindDependsNode(Pair.Key);
			Pair.Value->IterateOverDependencies([this, &DependsNodeMap, &ScriptPackages, OldNode, NewNode](FDependsNode* InDependency, UE::AssetRegistry::EDependencyCategory InCategory, UE::AssetRegistry::EDependencyProperty InFlags, bool bDuplicate) {
				const FAssetIdentifier& Identifier = InDependency->GetIdentifier();
				if (DependsNodeMap.Find(Identifier) || ScriptPackages.Contains(Identifier))
				{
					// Only add if this node is in the incoming map
					FDependsNode* NewDependency = CreateOrFindDependsNode(Identifier);
					NewNode->SetIsDependencyListSorted(InCategory, false);
					NewNode->AddDependency(NewDependency, InCategory, InFlags);
					NewDependency->SetIsReferencersSorted(false);
					NewDependency->AddReferencer(NewNode);
				}
			});
		}

		// Restore the sortedness that we turned off for performance when creating each DependsNode
		for (TPair<FAssetIdentifier, FDependsNode*> Pair : CachedDependsNodes)
		{
			FDependsNode* DependsNode = Pair.Value;
			DependsNode->SetIsDependencyListSorted(UE::AssetRegistry::EDependencyCategory::All, true);
			DependsNode->SetIsReferencersSorted(true);
		}
	}
}

void FAssetRegistryState::PruneAssetData(const TSet<FName>& RequiredPackages, const TSet<FName>& RemovePackages, const FAssetRegistrySerializationOptions& Options)
{
	PruneAssetData(RequiredPackages, RemovePackages, TSet<int32>(), Options);
}

void FAssetRegistryState::PruneAssetData(const TSet<FName>& RequiredPackages, const TSet<FName>& RemovePackages, const TSet<int32> ChunksToKeep, const FAssetRegistrySerializationOptions& Options)
{
	const bool bIsFilteredByChunkId = ChunksToKeep.Num() != 0;
	const bool bIsFilteredByRequiredPackages = RequiredPackages.Num() != 0;
	const bool bIsFilteredByRemovedPackages = RemovePackages.Num() != 0;

	TSet<FName> RequiredDependNodePackages;

	// Generate list up front as the maps will get cleaned up
	TArray<FAssetData*> AllAssetData;
	CachedAssetsByObjectPath.GenerateValueArray(AllAssetData);
	TSet<FDependsNode*> RemoveDependsNodes;

	// Remove assets and mark-for-removal any dependencynodes for assets removed due to having no tags
	for (FAssetData* AssetData : AllAssetData)
	{
		bool bRemoveAssetData = false;
		bool bRemoveDependencyData = true;

		if (bIsFilteredByChunkId &&
			!AssetData->ChunkIDs.ContainsByPredicate([&](int32 ChunkId) { return ChunksToKeep.Contains(ChunkId); }))
		{
			bRemoveAssetData = true;
		}
		else if (bIsFilteredByRequiredPackages && !RequiredPackages.Contains(AssetData->PackageName))
		{
			bRemoveAssetData = true;
		}
		else if (bIsFilteredByRemovedPackages && RemovePackages.Contains(AssetData->PackageName))
		{
			bRemoveAssetData = true;
		}
		else if (Options.bFilterAssetDataWithNoTags && AssetData->TagsAndValues.Num() == 0 &&
			!FPackageName::IsLocalizedPackage(AssetData->PackageName.ToString()))
		{
			bRemoveAssetData = true;
			bRemoveDependencyData = Options.bFilterDependenciesWithNoTags;
		}

		if (bRemoveAssetData)
		{
			bool bRemovedAssetData, bRemovedPackageData;
			FName AssetPackageName = AssetData->PackageName;
			// AssetData might be deleted after this call
			RemoveAssetData(AssetData, false /* bRemoveDependencyData */, bRemovedAssetData, bRemovedPackageData);
			if (!bRemoveDependencyData)
			{
				RequiredDependNodePackages.Add(AssetPackageName);
			}
			else if (bRemovedPackageData)
			{
				FDependsNode** RemovedNode = CachedDependsNodes.Find(AssetPackageName);
				if (RemovedNode)
				{
					RemoveDependsNodes.Add(*RemovedNode);
				}
			}
		}
	}

	TArray<FDependsNode*> AllDependsNodes;
	CachedDependsNodes.GenerateValueArray(AllDependsNodes);

	// Mark-for-removal all other dependsnodes that are filtered out by our settings
	for (FDependsNode* DependsNode : AllDependsNodes)
	{
		const FAssetIdentifier& Id = DependsNode->GetIdentifier();
		bool bRemoveDependsNode = false;
		if (RemoveDependsNodes.Contains(DependsNode))
		{
			continue;
		}

		if (Options.bFilterSearchableNames && Id.IsValue())
		{
			bRemoveDependsNode = true;
		}
		else if (Id.IsPackage() &&
			!CachedAssetsByPackageName.Contains(Id.PackageName) &&
			!RequiredDependNodePackages.Contains(Id.PackageName) &&
			!FPackageName::IsScriptPackage(Id.PackageName.ToString()))
		{
			bRemoveDependsNode = true;
		}
		
		if (bRemoveDependsNode)
		{
			RemoveDependsNodes.Add(DependsNode);
		}
	}

	// Batch-remove all of the marked-for-removal dependsnodes
	for (FDependsNode* DependsNode : AllDependsNodes)
	{
		check(DependsNode != nullptr);
		if (RemoveDependsNodes.Contains(DependsNode))
		{
			CachedDependsNodes.Remove(DependsNode->GetIdentifier());
			NumDependsNodes--;
			// if the depends nodes were preallocated in a block, we can't delete them one at a time, only the whole chunk in the destructor
			if (PreallocatedDependsNodeDataBuffers.Num() == 0)
			{
				delete DependsNode;
			}
		}
		else
		{
			DependsNode->RemoveLinks([&RemoveDependsNodes](const FDependsNode* ExistingDependsNode) { return RemoveDependsNodes.Contains(ExistingDependsNode); });
		}
	}

	// Remove any orphaned depends nodes. This will leave cycles in but those might represent useful data
	CachedDependsNodes.GenerateValueArray(AllDependsNodes);
	for (FDependsNode* DependsNode : AllDependsNodes)
	{
		if (DependsNode->GetConnectionCount() == 0)
		{
			RemoveDependsNode(DependsNode->GetIdentifier());
		}
	}
}

bool FAssetRegistryState::HasAssets(const FName PackagePath) const
{
	const TArray<FAssetData*>* FoundAssetArray = CachedAssetsByPath.Find(PackagePath);
	return FoundAssetArray && FoundAssetArray->Num() > 0;
}

bool FAssetRegistryState::GetAssets(const FARCompiledFilter& Filter, const TSet<FName>& PackageNamesToSkip, TArray<FAssetData>& OutAssetData) const
{
	return EnumerateAssets(Filter, PackageNamesToSkip, [&OutAssetData](const FAssetData& AssetData)
	{
		OutAssetData.Emplace(AssetData);
		return true;
	});
}

bool FAssetRegistryState::EnumerateAssets(const FARCompiledFilter& Filter, const TSet<FName>& PackageNamesToSkip, TFunctionRef<bool(const FAssetData&)> Callback) const
{
	// Verify filter input. If all assets are needed, use EnumerateAllAssets() instead.
	if (Filter.IsEmpty() || !IsFilterValid(Filter))
	{
		return false;
	}

	const uint32 FilterWithoutPackageFlags = Filter.WithoutPackageFlags;
	const uint32 FilterWithPackageFlags = Filter.WithPackageFlags;

	// Form a set of assets matched by each filter
	TArray<TSet<FAssetData*>> DiskFilterSets;

	// On disk package names
	if (Filter.PackageNames.Num() > 0)
	{
		TSet<FAssetData*>& PackageNameFilter = DiskFilterSets[DiskFilterSets.AddDefaulted()];

		for (FName PackageName : Filter.PackageNames)
		{
			const TArray<FAssetData*>* PackageAssets = CachedAssetsByPackageName.Find(PackageName);

			if (PackageAssets != nullptr)
			{
				PackageNameFilter.Append(*PackageAssets);
			}
		}
	}

	// On disk package paths
	if (Filter.PackagePaths.Num() > 0)
	{
		TSet<FAssetData*>& PathFilter = DiskFilterSets[DiskFilterSets.AddDefaulted()];

		for (FName PackagePath : Filter.PackagePaths)
		{
			const TArray<FAssetData*>* PathAssets = CachedAssetsByPath.Find(PackagePath);

			if (PathAssets != nullptr)
			{
				PathFilter.Append(*PathAssets);
			}
		}
	}

	// On disk classes
	if (Filter.ClassNames.Num() > 0)
	{
		TSet<FAssetData*>& ClassFilter = DiskFilterSets[DiskFilterSets.AddDefaulted()];

		for (FName ClassName : Filter.ClassNames)
		{
			const TArray<FAssetData*>* ClassAssets = CachedAssetsByClass.Find(ClassName);

			if (ClassAssets != nullptr)
			{
				ClassFilter.Append(*ClassAssets);
			}
		}
	}

	// On disk object paths
	if (Filter.ObjectPaths.Num() > 0)
	{
		TSet<FAssetData*>& ObjectPathsFilter = DiskFilterSets[DiskFilterSets.AddDefaulted()];

		for (FName ObjectPath : Filter.ObjectPaths)
		{
			FAssetData* AssetDataPtr = CachedAssetsByObjectPath.FindRef(ObjectPath);

			if (AssetDataPtr != nullptr)
			{
				ObjectPathsFilter.Add(AssetDataPtr);
			}
		}
	}

	// On disk tags and values
	if (Filter.TagsAndValues.Num() > 0)
	{
		TSet<FAssetData*>& TagAndValuesFilter = DiskFilterSets[DiskFilterSets.AddDefaulted()];

		for (auto FilterTagIt = Filter.TagsAndValues.CreateConstIterator(); FilterTagIt; ++FilterTagIt)
		{
			const FName Tag = FilterTagIt.Key();
			const TOptional<FString>& Value = FilterTagIt.Value();

			const TArray<FAssetData*>* TagAssets = CachedAssetsByTag.Find(Tag);

			if (TagAssets != nullptr)
			{
				for (FAssetData* AssetData : *TagAssets)
				{
					if (AssetData != nullptr)
					{
						bool bAccept;
						if (!Value.IsSet())
						{
							bAccept = AssetData->TagsAndValues.Contains(Tag);
						}
						else
						{
							bAccept = AssetData->TagsAndValues.ContainsKeyValue(Tag, Value.GetValue());
						}
						if (bAccept)
						{
							TagAndValuesFilter.Add(AssetData);
						}
					}
				}
			}
		}
	}

	// If we have any filter sets, add the assets which are contained in the sets to OutAssetData
	if (DiskFilterSets.Num() > 0)
	{
		// Initialize the combined filter set to the first set, in case we can skip combining.
		TSet<FAssetData*>* CombinedFilterSetPtr = &DiskFilterSets[0];
		TSet<FAssetData*> IntersectedFilterSet;

		// If we have more than one set, we must combine them. We take the intersection
		if (DiskFilterSets.Num() > 1)
		{
			IntersectedFilterSet = *CombinedFilterSetPtr;
			CombinedFilterSetPtr = &IntersectedFilterSet;

			for (int32 SetIdx = 1; SetIdx < DiskFilterSets.Num() && IntersectedFilterSet.Num() > 0; ++SetIdx)
			{
				// If the other set is smaller, swap it so we iterate the smaller set
				TSet<FAssetData*> OtherFilterSet = DiskFilterSets[SetIdx];
				if (OtherFilterSet.Num() < IntersectedFilterSet.Num())
				{
					Swap(OtherFilterSet, IntersectedFilterSet);
				}

				for (auto It = IntersectedFilterSet.CreateIterator(); It; ++It)
				{
					if (!OtherFilterSet.Contains(*It))
					{
						It.RemoveCurrent();
						continue;
					}
				}
			}
		}

		// Iterate over the final combined filter set to add to OutAssetData
		for (const FAssetData* AssetData : *CombinedFilterSetPtr)
		{
			if (PackageNamesToSkip.Contains(AssetData->PackageName))
			{
				// Skip assets in passed in package list
				continue;
			}

			if (AssetData->HasAnyPackageFlags(FilterWithoutPackageFlags))
			{
				continue;
			}

			if (!AssetData->HasAllPackageFlags(FilterWithPackageFlags))
			{
				continue;
			}

			if (!Callback(*AssetData))
			{
				return true;
			}
		}
	}

	return true;
}

bool FAssetRegistryState::GetAllAssets(const TSet<FName>& PackageNamesToSkip, TArray<FAssetData>& OutAssetData) const
{
	return EnumerateAllAssets(PackageNamesToSkip, [&OutAssetData](const FAssetData& AssetData)
	{
		OutAssetData.Emplace(AssetData);
		return true;
	});
}

bool FAssetRegistryState::EnumerateAllAssets(const TSet<FName>& PackageNamesToSkip, TFunctionRef<bool(const FAssetData&)> Callback) const
{
	// All unloaded disk assets
	for (const TPair<FName, FAssetData*>& AssetDataPair : CachedAssetsByObjectPath)
	{
		const FAssetData* AssetData = AssetDataPair.Value;

		if (AssetData != nullptr)
		{
			// Make sure the asset's package was not loaded then the object was deleted/renamed
			if (!PackageNamesToSkip.Contains(AssetData->PackageName))
			{
				if (!Callback(*AssetData))
				{
					return true;
				}
			}
		}
	}
	return true;
}

bool FAssetRegistryState::GetDependencies(const FAssetIdentifier& AssetIdentifier,
										  TArray<FAssetIdentifier>& OutDependencies,
										  EAssetRegistryDependencyType::Type InDependencyType) const
{
	bool bResult = false;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE::AssetRegistry::FDependencyQuery Flags(InDependencyType);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!!(InDependencyType & EAssetRegistryDependencyType::Packages))
	{
		bResult = GetDependencies(AssetIdentifier, OutDependencies, UE::AssetRegistry::EDependencyCategory::Package, Flags) || bResult;
	}
	if (!!(InDependencyType & EAssetRegistryDependencyType::SearchableName))
	{
		bResult = GetDependencies(AssetIdentifier, OutDependencies, UE::AssetRegistry::EDependencyCategory::SearchableName) || bResult;
	}
	if (!!(InDependencyType & EAssetRegistryDependencyType::Manage))
	{
		bResult = GetDependencies(AssetIdentifier, OutDependencies, UE::AssetRegistry::EDependencyCategory::Manage, Flags) || bResult;
	}
	return bResult;
}

bool FAssetRegistryState::GetDependencies(const FAssetIdentifier& AssetIdentifier,
										  TArray<FAssetIdentifier>& OutDependencies,
										  UE::AssetRegistry::EDependencyCategory Category, const UE::AssetRegistry::FDependencyQuery& Flags) const
{
	const FDependsNode* const* NodePtr = CachedDependsNodes.Find(AssetIdentifier);
	const FDependsNode* Node = nullptr;
	if (NodePtr != nullptr)
	{
		Node = *NodePtr;
	}

	if (Node != nullptr)
	{
		Node->GetDependencies(OutDependencies, Category, Flags);
		return true;
	}
	else
	{
		return false;
	}
}

bool FAssetRegistryState::GetDependencies(const FAssetIdentifier& AssetIdentifier,
										  TArray<FAssetDependency>& OutDependencies,
										  UE::AssetRegistry::EDependencyCategory Category, const UE::AssetRegistry::FDependencyQuery& Flags) const
{
	const FDependsNode* const* NodePtr = CachedDependsNodes.Find(AssetIdentifier);
	const FDependsNode* Node = nullptr;
	if (NodePtr != nullptr)
	{
		Node = *NodePtr;
	}

	if (Node != nullptr)
	{
		Node->GetDependencies(OutDependencies, Category, Flags);
		return true;
	}
	else
	{
		return false;
	}
}

bool FAssetRegistryState::GetReferencers(const FAssetIdentifier& AssetIdentifier,
										 TArray<FAssetIdentifier>& OutReferencers,
										 EAssetRegistryDependencyType::Type InReferenceType) const
{
	bool bResult = false;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE::AssetRegistry::FDependencyQuery Flags(InReferenceType);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!!(InReferenceType & EAssetRegistryDependencyType::Packages))
	{
		bResult = GetReferencers(AssetIdentifier, OutReferencers, UE::AssetRegistry::EDependencyCategory::Package, Flags) || bResult;
	}
	if (!!(InReferenceType & EAssetRegistryDependencyType::SearchableName))
	{
		bResult = GetReferencers(AssetIdentifier, OutReferencers, UE::AssetRegistry::EDependencyCategory::SearchableName) || bResult;
	}
	if (!!(InReferenceType & EAssetRegistryDependencyType::Manage))
	{
		bResult = GetReferencers(AssetIdentifier, OutReferencers, UE::AssetRegistry::EDependencyCategory::Manage, Flags) || bResult;
	}
	return bResult;
}

bool FAssetRegistryState::GetReferencers(const FAssetIdentifier& AssetIdentifier,
										 TArray<FAssetIdentifier>& OutReferencers,
										 UE::AssetRegistry::EDependencyCategory Category, const UE::AssetRegistry::FDependencyQuery& Flags) const
{
	const FDependsNode* const* NodePtr = CachedDependsNodes.Find(AssetIdentifier);
	const FDependsNode* Node = nullptr;

	if (NodePtr != nullptr)
	{
		Node = *NodePtr;
	}

	if (Node != nullptr)
	{
		TArray<FDependsNode*> DependencyNodes;
		Node->GetReferencers(DependencyNodes, Category, Flags);

		OutReferencers.Reserve(DependencyNodes.Num());
		for (FDependsNode* DependencyNode : DependencyNodes)
		{
			OutReferencers.Add(DependencyNode->GetIdentifier());
		}

		return true;
	}
	else
	{
		return false;
	}
}

bool FAssetRegistryState::GetReferencers(const FAssetIdentifier& AssetIdentifier,
										 TArray<FAssetDependency>& OutReferencers,
										 UE::AssetRegistry::EDependencyCategory Category, const UE::AssetRegistry::FDependencyQuery& Flags) const
{
	const FDependsNode* const* NodePtr = CachedDependsNodes.Find(AssetIdentifier);
	const FDependsNode* Node = nullptr;

	if (NodePtr != nullptr)
	{
		Node = *NodePtr;
	}

	if (Node != nullptr)
	{
		Node->GetReferencers(OutReferencers, Category, Flags);
		return true;
	}
	else
	{
		return false;
	}
}

bool FAssetRegistryState::Serialize(FArchive& OriginalAr, const FAssetRegistrySerializationOptions& Options)
{
	// This is only used for the runtime version of the AssetRegistry
	if (OriginalAr.IsSaving())
	{
		check(CachedAssetsByObjectPath.Num() == NumAssets);

		FAssetRegistryVersion::Type Version = FAssetRegistryVersion::LatestVersion;
		FAssetRegistryVersion::SerializeVersion(OriginalAr, Version);

		// Set up name table archive
		FNameTableArchiveWriter Ar(OriginalAr);

		// serialize number of objects
		int32 AssetCount = CachedAssetsByObjectPath.Num();
		Ar << AssetCount;

		// Write asset data first
		for (TPair<FName, FAssetData*>& Pair : CachedAssetsByObjectPath)
		{
			FAssetData& AssetData(*Pair.Value);
			AssetData.SerializeForCache(Ar);
		}

		// Serialize Dependencies
		// Write placeholder data for the size
		int64 OffsetToDependencySectionSize = Ar.Tell();
		int64 DependencySectionSize = 0;
		Ar << DependencySectionSize;
		int64 DependencySectionStart = Ar.Tell();

		if (!Options.bSerializeDependencies)
		{
			int32 NumDependencies = 0;
			Ar << NumDependencies;
		}
		else
		{
			TMap<FDependsNode*, FDependsNode*> RedirectCache;
			TMap<FDependsNode*, int32> DependsIndexMap;
			TArray<FDependsNode*> Dependencies;
			DependsIndexMap.Reserve(CachedAssetsByObjectPath.Num());

			// Scan dependency nodes, we won't save all of them if we filter out certain types
			for (TPair<FAssetIdentifier, FDependsNode*>& Pair : CachedDependsNodes)
			{
				FDependsNode* Node = Pair.Value;

				if (Node->GetIdentifier().IsPackage() 
					|| (Options.bSerializeSearchableNameDependencies && Node->GetIdentifier().IsValue())
					|| (Options.bSerializeManageDependencies && Node->GetIdentifier().GetPrimaryAssetId().IsValid()))
				{
					DependsIndexMap.Add(Node, Dependencies.Num());
					Dependencies.Add(Node);
				}
			}

			int32 NumDependencies = Dependencies.Num();
			Ar << NumDependencies;

			TUniqueFunction<int32(FDependsNode*, bool bAsReferencer)> GetSerializeIndexFromNode = [this, &RedirectCache, &DependsIndexMap](FDependsNode* InDependency, bool bAsReferencer)
			{
				if (!bAsReferencer)
				{
					InDependency = ResolveRedirector(InDependency, CachedAssetsByObjectPath, RedirectCache);
				}
				if (!InDependency)
				{
					return -1;
				}
				int32* DependencyIndex = DependsIndexMap.Find(InDependency);
				if (!DependencyIndex)
				{
					return -1;
				}
				return *DependencyIndex;
			};

			FDependsNode::FSaveScratch Scratch;
			for (FDependsNode* DependentNode : Dependencies)
			{
				DependentNode->SerializeSave(Ar, GetSerializeIndexFromNode, Scratch);
			}
		}
		// Write the real value to the placeholder data for the DependencySectionSize
		int64 DependencySectionEnd = Ar.Tell();
		DependencySectionSize = DependencySectionEnd - DependencySectionStart;
		Ar.Seek(OffsetToDependencySectionSize);
		Ar << DependencySectionSize;
		check(Ar.Tell() == DependencySectionStart);
		Ar.Seek(DependencySectionEnd);

		// Serialize the PackageData
		int32 PackageDataCount = 0;
		if (Options.bSerializePackageData)
		{
			PackageDataCount = CachedPackageData.Num();
			Ar << PackageDataCount;

			for (TPair<FName, FAssetPackageData*>& Pair : CachedPackageData)
			{
				Ar << Pair.Key;
				Pair.Value->SerializeForCache(Ar);
			}
		}
		else
		{
			Ar << PackageDataCount;
		}
	}
	// load in by building the TMap
	else
	{
		FAssetRegistryVersion::Type Version = FAssetRegistryVersion::LatestVersion;
		FAssetRegistryVersion::SerializeVersion(OriginalAr, Version);

		if (Version < FAssetRegistryVersion::RemovedMD5Hash)
		{
			// Cannot read states before this version
			return false;
		}

		// Set up name table archive
		FNameTableArchiveReader Ar(OriginalAr);

		// serialize number of objects
		int32 LocalNumAssets = 0;
		Ar << LocalNumAssets;

		// allocate one single block for all asset data structs (to reduce tens of thousands of heap allocations)
		FAssetData* PreallocatedAssetDataBuffer = new FAssetData[LocalNumAssets];
		PreallocatedAssetDataBuffers.Add(PreallocatedAssetDataBuffer);

		for (int32 AssetIndex = 0; AssetIndex < LocalNumAssets; AssetIndex++)
		{
			// make a new asset data object
			FAssetData* NewAssetData = &PreallocatedAssetDataBuffer[AssetIndex];

			// load it
			NewAssetData->SerializeForCache(Ar);

			AddAssetData(NewAssetData);
		}

		if (Version >= FAssetRegistryVersion::AddedDependencyFlags)
		{
			int64 DependencySectionSize;
			Ar << DependencySectionSize;
			int64 DependencySectionEnd = Ar.Tell() + DependencySectionSize;

			if (!Options.bSerializeDependencies)
			{
				Ar.Seek(DependencySectionEnd);
			}
			else
			{
				int32 LocalNumDependsNodes = 0;
				Ar << LocalNumDependsNodes;

				FDependsNode* PreallocatedDependsNodeDataBuffer = nullptr;
				if (LocalNumDependsNodes > 0)
				{
					PreallocatedDependsNodeDataBuffer = new FDependsNode[LocalNumDependsNodes];
					PreallocatedDependsNodeDataBuffers.Add(PreallocatedDependsNodeDataBuffer);
					CachedDependsNodes.Reserve(LocalNumDependsNodes);
				}
				TUniqueFunction<FDependsNode*(int32)> GetNodeFromSerializeIndex = [&PreallocatedDependsNodeDataBuffer, LocalNumDependsNodes](int32 Index) -> FDependsNode*
				{
					if (Index < 0 || LocalNumDependsNodes <= Index)
					{
						return nullptr;
					}
					return &PreallocatedDependsNodeDataBuffer[Index];
				};

				FDependsNode::FLoadScratch Scratch;
				for (int32 DependsNodeIndex = 0; DependsNodeIndex < LocalNumDependsNodes; DependsNodeIndex++)
				{
					FDependsNode* DependsNode = &PreallocatedDependsNodeDataBuffer[DependsNodeIndex];
					DependsNode->SerializeLoad(Ar, GetNodeFromSerializeIndex, Scratch, Options);
					CachedDependsNodes.Add(DependsNode->GetIdentifier(), DependsNode);
				}
				if (Ar.IsError())
				{
					Ar.Seek(DependencySectionEnd);
				}
			}
		}
		else
		{
			LegacySerializeLoad_BeforeAssetRegistryDependencyFlags(Ar, Options, Version);
		}

		int32 LocalNumPackageData = 0;
		Ar << LocalNumPackageData;
		FAssetPackageData* PreallocatedPackageDataBuffer = nullptr;
		if (Options.bSerializePackageData && LocalNumPackageData > 0)
		{
			PreallocatedPackageDataBuffer = new FAssetPackageData[LocalNumPackageData];
			PreallocatedPackageDataBuffers.Add(PreallocatedPackageDataBuffer);
			CachedPackageData.Reserve(LocalNumPackageData);
		}

		for (int32 PackageDataIndex = 0; PackageDataIndex < LocalNumPackageData; PackageDataIndex++)
		{
			FName PackageName;
			Ar << PackageName;
			
			if (Options.bSerializePackageData)
			{
				FAssetPackageData& NewPackageData = PreallocatedPackageDataBuffer[PackageDataIndex];
				if (Version < FAssetRegistryVersion::AddedCookedMD5Hash)
				{
					Ar << NewPackageData.DiskSize;
					Ar << NewPackageData.PackageGuid;
				}
				else
				{
					NewPackageData.SerializeForCache(Ar);
				}
				CachedPackageData.Add(PackageName, &NewPackageData);
			}
			else
			{
				FAssetPackageData FakeData;
				FakeData.SerializeForCache(Ar);
			}
		}
#if USE_COMPACT_ASSET_REGISTRY
		Shrink();
#endif
	}

	return !OriginalAr.IsError();
}

void FAssetRegistryState::LegacySerializeLoad_BeforeAssetRegistryDependencyFlags(FArchive& Ar, const FAssetRegistrySerializationOptions& Options, FAssetRegistryVersion::Type Version)
{
	int32 LocalNumDependsNodes = 0;
	Ar << LocalNumDependsNodes;

	FDependsNode Placeholder;
	FDependsNode* PreallocatedDependsNodeDataBuffer = nullptr;
	if (Options.bSerializeDependencies && LocalNumDependsNodes > 0)
	{
		PreallocatedDependsNodeDataBuffer = new FDependsNode[LocalNumDependsNodes];
		PreallocatedDependsNodeDataBuffers.Add(PreallocatedDependsNodeDataBuffer);
		CachedDependsNodes.Reserve(LocalNumDependsNodes);
	}
	TUniqueFunction<FDependsNode* (int32)> GetNodeFromSerializeIndex = [&PreallocatedDependsNodeDataBuffer, LocalNumDependsNodes](int32 Index)->FDependsNode *
	{
		if (Index < 0 || LocalNumDependsNodes <= Index)
		{
			return nullptr;
		}
		return &PreallocatedDependsNodeDataBuffer[Index];
	};

	uint32 HardBits, SoftBits, HardManageBits, SoftManageBits;
	FDependsNode::LegacySerializeLoad_BeforeAssetRegistryDependencyFlags_GetPropertySetBits(HardBits, SoftBits, HardManageBits, SoftManageBits);

	TArray<FDependsNode*> DependsNodes;
	for (int32 DependsNodeIndex = 0; DependsNodeIndex < LocalNumDependsNodes; DependsNodeIndex++)
	{
		// Create the node if we're actually saving dependencies, otherwise just fake serialize
		FDependsNode* DependsNode = nullptr;
		if (Options.bSerializeDependencies)
		{
			DependsNode = &PreallocatedDependsNodeDataBuffer[DependsNodeIndex];
		}
		else
		{
			DependsNode = &Placeholder;
		}

		// Call the DependsNode legacy serialization function
		DependsNode->LegacySerializeLoad_BeforeAssetRegistryDependencyFlags(Ar, Version, PreallocatedDependsNodeDataBuffer, LocalNumDependsNodes, Options, HardBits, SoftBits, HardManageBits, SoftManageBits);

		// Register the DependsNode with its AssetIdentifier
		if (Options.bSerializeDependencies)
		{
			CachedDependsNodes.Add(DependsNode->GetIdentifier(), DependsNode);
		}
	}
}

void FAssetRegistryState::StripAssetRegistryKeyForObject(FName ObjectPath, FName Key)
{
	FAssetData** Found = CachedAssetsByObjectPath.Find(ObjectPath);
	if (Found)
	{
		(*Found)->TagsAndValues.StripKey(Key);
	}
}

uint32 FAssetRegistryState::GetAllocatedSize(bool bLogDetailed) const
{
	uint32 TotalBytes = 0;

	uint32 MapMemory = CachedAssetsByObjectPath.GetAllocatedSize();
	MapMemory += CachedAssetsByPackageName.GetAllocatedSize();
	MapMemory += CachedAssetsByPath.GetAllocatedSize();
	MapMemory += CachedAssetsByClass.GetAllocatedSize();
	MapMemory += CachedAssetsByTag.GetAllocatedSize();
	MapMemory += CachedDependsNodes.GetAllocatedSize();
	MapMemory += CachedPackageData.GetAllocatedSize();
	MapMemory += PreallocatedAssetDataBuffers.GetAllocatedSize();
	MapMemory += PreallocatedDependsNodeDataBuffers.GetAllocatedSize();
	MapMemory += PreallocatedPackageDataBuffers.GetAllocatedSize();

	uint32 MapArrayMemory = 0;
	auto SubArray = 
		[&MapArrayMemory](const TMap<FName, TArray<FAssetData*>>& A)
	{
		for (auto& Pair : A)
		{
			MapArrayMemory += Pair.Value.GetAllocatedSize();
		}
	};
	SubArray(CachedAssetsByPackageName);
	SubArray(CachedAssetsByPath);
	SubArray(CachedAssetsByClass);
	SubArray(CachedAssetsByTag);

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("Index Size: %dk"), MapMemory / 1024);
	}

	uint32 AssetDataSize = 0, TagOverHead = 0, TotalTagSize = 0;
	TMap<FName, uint32> TagSizes;

	for (const TPair<FName, FAssetData*>& AssetDataPair : CachedAssetsByObjectPath)
	{
		const FAssetData& AssetData = *AssetDataPair.Value;
		
		AssetDataSize += sizeof(AssetData);
		AssetDataSize += AssetData.ChunkIDs.GetAllocatedSize();

		TagOverHead += AssetData.TagsAndValues.GetAllocatedSize();

		for (const auto& TagPair : AssetData.TagsAndValues)
		{
			uint32 StringSize = TagPair.Value.GetAllocatedSize();
			
			TotalTagSize += StringSize;
			TagSizes.FindOrAdd(TagPair.Key) += StringSize;


		}
	}
#if USE_COMPACT_ASSET_REGISTRY
	uint32 CompactOverhead = FAssetDataTagMapValueStorage::Get().GetAllocatedSize();
	uint32 CompactStrings = FAssetDataTagMapValueStorage::Get().GetStringSize();
	uint32 CompactStringsDeDup = FAssetDataTagMapValueStorage::Get().GetUniqueStringSize();
#endif

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetData Count: %d"), CachedAssetsByObjectPath.Num());
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetData Static Size: %dk"), AssetDataSize / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetData Tag Overhead: %dk"), TagOverHead / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("TArray<FAssetData*>: %dk"), MapArrayMemory / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Strings: %dk"), TotalTagSize / 1024);
#if USE_COMPACT_ASSET_REGISTRY
		UE_LOG(LogAssetRegistry, Log, TEXT("Compact Strings (used to double check): %dk"), CompactStrings / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Compact Strings (case insensitive deduplicated): %dk"), CompactStringsDeDup / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Compact Tag Overhead: %dk"), CompactOverhead / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("FAssetData* potential savings: %dk"), (MapArrayMemory + sizeof(void*) * CachedAssetsByObjectPath.Num()) / 1024 / 2);
#endif

		for (const TPair<FName, uint32>& SizePair : TagSizes)
		{
			UE_LOG(LogAssetRegistry, Log, TEXT("Tag %s Size: %dk"), *SizePair.Key.ToString(), SizePair.Value / 1024);
		}
	}

	uint32 DependNodesSize = 0, DependenciesSize = 0;

	for (const TPair<FAssetIdentifier, FDependsNode*>& DependsNodePair : CachedDependsNodes)
	{
		const FDependsNode& DependsNode = *DependsNodePair.Value;
		DependNodesSize += sizeof(DependsNode);

		DependenciesSize += DependsNode.GetAllocatedSize();
	}

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("Dependency Node Count: %d"), CachedDependsNodes.Num());
		UE_LOG(LogAssetRegistry, Log, TEXT("Dependency Node Static Size: %dk"), DependNodesSize / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Dependency Arrays Size: %dk"), DependenciesSize / 1024);
	}

	uint32 PackageDataSize = CachedPackageData.Num() * sizeof(FAssetPackageData);

	TotalBytes = MapMemory + AssetDataSize + TagOverHead + TotalTagSize + DependNodesSize + DependenciesSize + PackageDataSize + MapArrayMemory
#if USE_COMPACT_ASSET_REGISTRY
		+ CompactOverhead
#endif
		;

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("PackageData Count: %d"), CachedPackageData.Num());
		UE_LOG(LogAssetRegistry, Log, TEXT("PackageData Static Size: %dk"), PackageDataSize / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Total State Size: %dk"), TotalBytes / 1024);
	}
#if USE_COMPACT_ASSET_REGISTRY
	check(CompactStrings == TotalTagSize); // Otherwise there is a leak, now maybe some other subsystem takes ownership of these, then this check is not valid.
#endif

	return TotalBytes;
}

FDependsNode* FAssetRegistryState::ResolveRedirector(FDependsNode* InDependency,
													 TMap<FName, FAssetData*>& InAllowedAssets,
													 TMap<FDependsNode*, FDependsNode*>& InCache)
{
	if (InCache.Contains(InDependency))
	{
		return InCache[InDependency];
	}

	FDependsNode* CurrentDependency = InDependency;
	FDependsNode* Result = nullptr;

	TSet<FName> EncounteredDependencies;

	while (Result == nullptr)
	{
		checkSlow(CurrentDependency);

		if (EncounteredDependencies.Contains(CurrentDependency->GetPackageName()))
		{
			break;
		}

		EncounteredDependencies.Add(CurrentDependency->GetPackageName());

		if (CachedAssetsByPackageName.Contains(CurrentDependency->GetPackageName()))
		{
			// Get the list of assets contained in this package
			TArray<FAssetData*>& Assets = CachedAssetsByPackageName[CurrentDependency->GetPackageName()];

			for (FAssetData* Asset : Assets)
			{
				if (Asset->IsRedirector())
				{
					FDependsNode* ChainedRedirector = nullptr;
					// This asset is a redirector, so we want to look at its dependencies and find the asset that it is redirecting to
					CurrentDependency->IterateOverDependencies([&](FDependsNode* InDepends, UE::AssetRegistry::EDependencyCategory Category, UE::AssetRegistry::EDependencyProperty Property, bool bDuplicate) {
						if (bDuplicate)
						{
							return; // Already looked at this dependency node
						}
						if (InAllowedAssets.Contains(InDepends->GetPackageName()))
						{
							// This asset is in the allowed asset list, so take this as the redirect target
							Result = InDepends;
						}
						else if (CachedAssetsByPackageName.Contains(InDepends->GetPackageName()))
						{
							// This dependency isn't in the allowed list, but it is a valid asset in the registry.
							// Because this is a redirector, this should mean that the redirector is pointing at ANOTHER
							// redirector (or itself in some horrible situations) so we'll move to that node and try again
							ChainedRedirector = InDepends;
						}
					}, UE::AssetRegistry::EDependencyCategory::Package);

					if (ChainedRedirector)
					{
						// Found a redirector, break for loop
						CurrentDependency = ChainedRedirector;
						break;
					}
				}
				else
				{
					Result = CurrentDependency;
				}

				if (Result)
				{
					// We found an allowed asset from the original dependency node. We're finished!
					break;
				}
			}
		}
		else
		{
			Result = CurrentDependency;
		}
	}

	InCache.Add(InDependency, Result);
	return Result;
}

void FAssetRegistryState::AddAssetData(FAssetData* AssetData)
{
	NumAssets++;

	TArray<FAssetData*>& PackageAssets = CachedAssetsByPackageName.FindOrAdd(AssetData->PackageName);
	TArray<FAssetData*>& PathAssets = CachedAssetsByPath.FindOrAdd(AssetData->PackagePath);
	TArray<FAssetData*>& ClassAssets = CachedAssetsByClass.FindOrAdd(AssetData->AssetClass);

	CachedAssetsByObjectPath.Add(AssetData->ObjectPath, AssetData);
	PackageAssets.Add(AssetData);
	PathAssets.Add(AssetData);
	ClassAssets.Add(AssetData);

	for (auto TagIt = AssetData->TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
	{
		FName Key = TagIt.Key();

		TArray<FAssetData*>& TagAssets = CachedAssetsByTag.FindOrAdd(Key);
		TagAssets.Add(AssetData);
	}
}

void FAssetRegistryState::UpdateAssetData(const FAssetData& NewAssetData)
{
	if (FAssetData* AssetData = CachedAssetsByObjectPath.FindRef(NewAssetData.ObjectPath))
	{
		UpdateAssetData(AssetData, NewAssetData);
	}
}

void FAssetRegistryState::UpdateAssetData(FAssetData* AssetData, const FAssetData& NewAssetData)
{
	// Determine if tags need to be remapped
	bool bTagsChanged = AssetData->TagsAndValues.Num() != NewAssetData.TagsAndValues.Num();

	// If the old and new asset data has the same number of tags, see if any are different (its ok if values are different)
	if (!bTagsChanged)
	{
		for (auto TagIt = AssetData->TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
		{
			if (!NewAssetData.TagsAndValues.Contains(TagIt.Key()))
			{
				bTagsChanged = true;
				break;
			}
		}
	}

	// Update ObjectPath
	if (AssetData->PackageName != NewAssetData.PackageName || AssetData->AssetName != NewAssetData.AssetName)
	{
		CachedAssetsByObjectPath.Remove(AssetData->ObjectPath);
		CachedAssetsByObjectPath.Add(NewAssetData.ObjectPath, AssetData);
	}

	// Update PackageName
	if (AssetData->PackageName != NewAssetData.PackageName)
	{
		TArray<FAssetData*>* OldPackageAssets = CachedAssetsByPackageName.Find(AssetData->PackageName);
		TArray<FAssetData*>& NewPackageAssets = CachedAssetsByPackageName.FindOrAdd(NewAssetData.PackageName);

		OldPackageAssets->Remove(AssetData);
		NewPackageAssets.Add(AssetData);
	}

	// Update PackagePath
	if (AssetData->PackagePath != NewAssetData.PackagePath)
	{
		TArray<FAssetData*>* OldPathAssets = CachedAssetsByPath.Find(AssetData->PackagePath);
		TArray<FAssetData*>& NewPathAssets = CachedAssetsByPath.FindOrAdd(NewAssetData.PackagePath);

		OldPathAssets->Remove(AssetData);
		NewPathAssets.Add(AssetData);
	}

	// Update AssetClass
	if (AssetData->AssetClass != NewAssetData.AssetClass)
	{
		TArray<FAssetData*>* OldClassAssets = CachedAssetsByClass.Find(AssetData->AssetClass);
		TArray<FAssetData*>& NewClassAssets = CachedAssetsByClass.FindOrAdd(NewAssetData.AssetClass);

		OldClassAssets->Remove(AssetData);
		NewClassAssets.Add(AssetData);
	}

	// Update Tags
	if (bTagsChanged)
	{
		for (auto TagIt = AssetData->TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
		{
			const FName FNameKey = TagIt.Key();

			if (!NewAssetData.TagsAndValues.Contains(FNameKey))
			{
				TArray<FAssetData*>* OldTagAssets = CachedAssetsByTag.Find(FNameKey);
				OldTagAssets->RemoveSingleSwap(AssetData);
			}
		}

		for (auto TagIt = NewAssetData.TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
		{
			const FName FNameKey = TagIt.Key();

			if (!AssetData->TagsAndValues.Contains(FNameKey))
			{
				TArray<FAssetData*>& NewTagAssets = CachedAssetsByTag.FindOrAdd(FNameKey);
				NewTagAssets.Add(AssetData);
			}
		}
	}

	// Copy in new values
	*AssetData = NewAssetData;
}

void FAssetRegistryState::RemoveAssetData(FAssetData* AssetData, bool bRemoveDependencyData, bool& bOutRemovedAssetData, bool& bOutRemovedPackageData)
{
	bOutRemovedAssetData = false;
	bOutRemovedPackageData = false;

	if (ensure(AssetData))
	{
		TArray<FAssetData*>* OldPackageAssets = CachedAssetsByPackageName.Find(AssetData->PackageName);
		TArray<FAssetData*>* OldPathAssets = CachedAssetsByPath.Find(AssetData->PackagePath);
		TArray<FAssetData*>* OldClassAssets = CachedAssetsByClass.Find(AssetData->AssetClass);

		CachedAssetsByObjectPath.Remove(AssetData->ObjectPath);
		OldPackageAssets->RemoveSingleSwap(AssetData);
		OldPathAssets->RemoveSingleSwap(AssetData);
		OldClassAssets->RemoveSingleSwap(AssetData);

		for (auto TagIt = AssetData->TagsAndValues.CreateConstIterator(); TagIt; ++TagIt)
		{
			TArray<FAssetData*>* OldTagAssets = CachedAssetsByTag.Find(TagIt.Key());
			OldTagAssets->RemoveSingleSwap(AssetData);
		}

		// Only remove dependencies and package data if there are no other known assets in the package
		if (OldPackageAssets->Num() == 0)
		{
			CachedAssetsByPackageName.Remove(AssetData->PackageName);

			// We need to update the cached dependencies references cache so that they know we no
			// longer exist and so don't reference them.
			if (bRemoveDependencyData)
			{
				RemoveDependsNode(AssetData->PackageName);
			}

			// Remove the package data as well
			RemovePackageData(AssetData->PackageName);
			bOutRemovedPackageData = true;
		}

		// if the assets were preallocated in a block, we can't delete them one at a time, only the whole chunk in the destructor
		if (PreallocatedAssetDataBuffers.Num() == 0)
		{
			delete AssetData;
		}
		NumAssets--;
		bOutRemovedAssetData = true;
	}
}

FDependsNode* FAssetRegistryState::FindDependsNode(const FAssetIdentifier& Identifier) const
{
	FDependsNode*const* FoundNode = CachedDependsNodes.Find(Identifier);
	if (FoundNode)
	{
		return *FoundNode;
	}
	else
	{
		return nullptr;
	}
}

FDependsNode* FAssetRegistryState::CreateOrFindDependsNode(const FAssetIdentifier& Identifier)
{
	FDependsNode* FoundNode = FindDependsNode(Identifier);
	if (FoundNode)
	{
		return FoundNode;
	}

	FDependsNode* NewNode = new FDependsNode(Identifier);
	NumDependsNodes++;
	CachedDependsNodes.Add(Identifier, NewNode);

	return NewNode;
}

bool FAssetRegistryState::RemoveDependsNode(const FAssetIdentifier& Identifier)
{
	FDependsNode** NodePtr = CachedDependsNodes.Find(Identifier);

	if (NodePtr != nullptr)
	{
		FDependsNode* Node = *NodePtr;
		if (Node != nullptr)
		{
			TArray<FDependsNode*> DependencyNodes;
			Node->GetDependencies(DependencyNodes);

			// Remove the reference to this node from all dependencies
			for (FDependsNode* DependencyNode : DependencyNodes)
			{
				DependencyNode->RemoveReferencer(Node);
			}

			TArray<FDependsNode*> ReferencerNodes;
			Node->GetReferencers(ReferencerNodes);

			// Remove the reference to this node from all referencers
			for (FDependsNode* ReferencerNode : ReferencerNodes)
			{
				ReferencerNode->RemoveDependency(Node);
			}

			// Remove the node and delete it
			CachedDependsNodes.Remove(Identifier);
			NumDependsNodes--;

			// if the depends nodes were preallocated in a block, we can't delete them one at a time, only the whole chunk in the destructor
			if (PreallocatedDependsNodeDataBuffers.Num() == 0)
			{
				delete Node;
			}

			return true;
		}
	}

	return false;
}

void FAssetRegistryState::Shrink()
{
	for (auto& Pair : CachedAssetsByObjectPath)
	{
		Pair.Value->Shrink();
	}
	auto ShrinkIn =
		[](TMap<FName, TArray<FAssetData*> >& Map)
	{
		Map.Shrink();
		for (auto& Pair : Map)
		{
			Pair.Value.Shrink();
		}
	};
	CachedAssetsByObjectPath.Shrink();
	ShrinkIn(CachedAssetsByPackageName);
	ShrinkIn(CachedAssetsByPath);
	ShrinkIn(CachedAssetsByClass);
	ShrinkIn(CachedAssetsByTag);
	ShrinkIn(CachedAssetsByPackageName);
	CachedDependsNodes.Shrink();
	CachedPackageData.Shrink();
	CachedAssetsByObjectPath.Shrink();
#if USE_COMPACT_ASSET_REGISTRY
	FAssetDataTagMapValueStorage::Get().Shrink();
#endif
}

void FAssetRegistryState::GetPrimaryAssetsIds(TSet<FPrimaryAssetId>& OutPrimaryAssets) const
{
	for (TMap<FName, FAssetData*>::ElementType Element : CachedAssetsByObjectPath)
	{
		if (Element.Value)
		{
			FPrimaryAssetId PrimaryAssetId = Element.Value->GetPrimaryAssetId();
			if (PrimaryAssetId.IsValid())
			{
				OutPrimaryAssets.Add(PrimaryAssetId);
			}
		}
	}
}

const FAssetPackageData* FAssetRegistryState::GetAssetPackageData(FName PackageName) const
{
	FAssetPackageData* const* FoundData = CachedPackageData.Find(PackageName);
	if (FoundData)
	{
		return *FoundData;
	}
	else
	{
		return nullptr;
	}
}

FAssetPackageData* FAssetRegistryState::CreateOrGetAssetPackageData(FName PackageName)
{
	FAssetPackageData** FoundData = CachedPackageData.Find(PackageName);
	if (FoundData)
	{
		return *FoundData;
	}

	FAssetPackageData* NewData = new FAssetPackageData();
	NumPackageData++;
	CachedPackageData.Add(PackageName, NewData);

	return NewData;
}

bool FAssetRegistryState::RemovePackageData(FName PackageName)
{
	FAssetPackageData** DataPtr = CachedPackageData.Find(PackageName);

	if (DataPtr != nullptr)
	{
		FAssetPackageData* Data = *DataPtr;
		if (Data != nullptr)
		{
			CachedPackageData.Remove(PackageName);
			NumPackageData--;

			// if the package data was preallocated in a block, we can't delete them one at a time, only the whole chunk in the destructor
			if (PreallocatedPackageDataBuffers.Num() == 0)
			{
				delete Data;
			}

			return true;
		}
	}
	return false;
}

bool FAssetRegistryState::IsFilterValid(const FARCompiledFilter& Filter)
{
	if (Filter.PackageNames.Contains(NAME_None) ||
		Filter.PackagePaths.Contains(NAME_None) ||
		Filter.ObjectPaths.Contains(NAME_None) || 
		Filter.ClassNames.Contains(NAME_None) ||
		Filter.TagsAndValues.Contains(NAME_None)
		)
	{
		return false;
	}

	return true;
}

#if ASSET_REGISTRY_STATE_DUMPING_ENABLED
namespace UE
{
namespace AssetRegistry
{
	void PropertiesToString(EDependencyProperty Properties, FStringBuilderBase& Builder, EDependencyCategory CategoryFilter)
	{
		bool bFirst = true;
		auto AppendPropertyName = [&Properties, &Builder, &bFirst](EDependencyProperty TestProperty, const TCHAR* NameWith, const TCHAR* NameWithout)
		{
			if (!bFirst)
			{
				Builder.Append(TEXT(","));
			}
			if (!!(Properties & TestProperty))
			{
				Builder.Append(NameWith);
			}
			else
			{
				Builder.Append(NameWithout);
			}
			bFirst = false;
		};
		if (!!(CategoryFilter & EDependencyCategory::Package))
		{
			AppendPropertyName(EDependencyProperty::Hard, TEXT("Hard"), TEXT("Soft"));
			AppendPropertyName(EDependencyProperty::Game, TEXT("Game"), TEXT("EditorOnly"));
			AppendPropertyName(EDependencyProperty::Build, TEXT("Build"), TEXT("NotBuild"));
		}
		if (!!(CategoryFilter & EDependencyCategory::Manage))
		{
			AppendPropertyName(EDependencyProperty::Direct, TEXT("Direct"), TEXT("Indirect"));
		}
		static_assert((EDependencyProperty::PackageMask | EDependencyProperty::SearchableNameMask | EDependencyProperty::ManageMask) == EDependencyProperty::AllMask,
			"Need to handle new flags in this function");
	}
}
}

void FAssetRegistryState::Dump(const TArray<FString>& Arguments, TArray<FString>& OutPages, int32 LinesPerPage) const
{
	int32 ExpectedNumLines = 14 + CachedAssetsByObjectPath.Num() * 5 + CachedDependsNodes.Num() + CachedPackageData.Num();
	const int32 EstimatedLinksPerNode = 10*2; // Each dependency shows up once as a dependency and once as a reference
	const int32 EstimatedCharactersPerLine = 100;
	const bool bDumpDependencyDetails = Arguments.Contains(TEXT("DependencyDetails"));
	if (bDumpDependencyDetails)
	{
		ExpectedNumLines += CachedDependsNodes.Num() * (3 + EstimatedLinksPerNode);
	}
	LinesPerPage = FMath::Max(LinesPerPage, 1);
	const int32 ExpectedNumPages = ExpectedNumLines / LinesPerPage;
	const int32 PageEndSearchLength = LinesPerPage / 20;
	const uint32 HashStartValue = MAX_uint32 - 49979693; // Pick a large starting value to bias against picking empty string
	const uint32 HashMultiplier = 67867967;
	TStringBuilder<16> PageBuffer;
	TStringBuilder<16> OverflowText;

	OutPages.Reserve(ExpectedNumPages);
	PageBuffer.AddUninitialized(LinesPerPage * EstimatedCharactersPerLine);// TODO: Add Reserve function to TStringBuilder
	PageBuffer.Reset();
	OverflowText.AddUninitialized(PageEndSearchLength * EstimatedCharactersPerLine);
	OverflowText.Reset();
	int32 NumLinesInPage = 0;
	const int32 LineTerminatorLen = TCString<TCHAR>::Strlen(LINE_TERMINATOR);

	auto FinishPage = [&PageBuffer, &NumLinesInPage, HashStartValue, HashMultiplier, PageEndSearchLength, &OutPages, &OverflowText, LineTerminatorLen]()
	{
		int32 PageEndIndex = PageBuffer.Len();
		const TCHAR* BufferEnd = PageBuffer.GetData() + PageEndIndex;
		int32 NumOverflowLines = 0;
		// We want to facilitate diffing dumps between two different versions that should be similar, but naively breaking up the dump into pages makes this difficult
		// because after one missing or added line, every page from that point on will be offset and therefore different, making false positive differences
		// To make pages after one missing or added line the same, we look for a good page ending based on the text of all the lines near the end of the current page
		// By choosing specific-valued texts as page breaks, we will usually randomly get lucky and have the two diffs pick the same line for the end of the page
		if (NumLinesInPage > PageEndSearchLength)
		{
			const TCHAR* WinningLineEnd = BufferEnd;
			uint32 WinningLineValue = 0;
			int32 WinningSearchIndex = 0;
			const TCHAR* LineEnd = BufferEnd;
			for (int32 SearchIndex = 0; SearchIndex < PageEndSearchLength; ++SearchIndex)
			{
				uint32 LineValue = HashStartValue;
				const TCHAR* LineStart = LineEnd;
				while (LineStart[-LineTerminatorLen] != LINE_TERMINATOR[0] || TCString<TCHAR>::Strncmp(LINE_TERMINATOR, LineStart - LineTerminatorLen, LineTerminatorLen) != 0)
				{
					--LineStart;
					LineValue = LineValue * HashMultiplier + static_cast<uint32>(TChar<TCHAR>::ToLower(*LineStart));
				}
				if (SearchIndex == 0 || LineValue < WinningLineValue) // We arbitrarily choose the smallest hash as the winning value
				{
					WinningLineValue = LineValue;
					WinningLineEnd = LineEnd;
					WinningSearchIndex = SearchIndex;
				}
				LineEnd = LineStart - LineTerminatorLen;
			}
			if (WinningLineEnd != BufferEnd)
			{
				PageEndIndex = WinningLineEnd - PageBuffer.GetData();
				NumOverflowLines = WinningSearchIndex;
			}
		}

		OutPages.Emplace(PageEndIndex, PageBuffer.GetData());
		if (PageEndIndex != PageBuffer.Len())
		{
			PageEndIndex += LineTerminatorLen; // Skip the newline
			OverflowText.Reset();
			OverflowText.Append(PageBuffer.GetData() + PageEndIndex, PageBuffer.Len() - PageEndIndex);
			PageBuffer.Reset();
			PageBuffer.Append(OverflowText);
			PageBuffer.Append(LINE_TERMINATOR);
			NumLinesInPage = NumOverflowLines;
		}
		else
		{
			PageBuffer.Reset();
			NumLinesInPage = 0;
		}
	};
	auto AddLine = [&PageBuffer, LinesPerPage, &NumLinesInPage, &FinishPage, &OutPages]()
	{
		if (LinesPerPage == 1)
		{
			OutPages.Emplace(PageBuffer.Len(), PageBuffer.GetData());
			PageBuffer.Reset();
		}
		else
		{
			++NumLinesInPage;
			if (NumLinesInPage != LinesPerPage)
			{
				PageBuffer.Append(LINE_TERMINATOR);
			}
			else
			{
				FinishPage();
			}
		}
	};

	auto PrintAssetDataMap = [&AddLine, &PageBuffer](FString Name, const TMap<FName, TArray<FAssetData*>>& AssetMap)
	{
		PageBuffer.Appendf(TEXT("--- Begin %s ---"), *Name);
		AddLine();

		TArray<FName> Keys;
		AssetMap.GenerateKeyArray(Keys);
		Keys.Sort(FNameLexicalLess());

		TArray<FAssetData*> Items;
		Items.Reserve(1024);

		int32 ValidCount = 0;
		for (const FName& Key : Keys)
		{
			const TArray<FAssetData*> AssetArray = AssetMap.FindChecked(Key);
			if (AssetArray.Num() == 0)
			{
				continue;
			}
			++ValidCount;

			Items.Reset();
			Items.Append(AssetArray);
			Items.Sort([](const FAssetData& A, const FAssetData& B)
				{ return A.ObjectPath.ToString() < B.ObjectPath.ToString(); }
				);

			PageBuffer.Append(TEXT("	"));
			Key.AppendString(PageBuffer);
			PageBuffer.Appendf(TEXT(" : %d item(s)"), Items.Num());
			AddLine();
			for (const FAssetData* Data : Items)
			{
				PageBuffer.Append(TEXT("	 "));
				Data->ObjectPath.AppendString(PageBuffer);
				AddLine();
			}
		}

		PageBuffer.Appendf(TEXT("--- End %s : %d entries ---"), *Name, ValidCount);
		AddLine();
	};

	if (Arguments.Contains(TEXT("ObjectPath")))
	{
		PageBuffer.Append(TEXT("--- Begin CachedAssetsByObjectPath ---"));
		AddLine();

		TArray<FName> Keys;
		CachedAssetsByObjectPath.GenerateKeyArray(Keys);
		Keys.Sort(FNameLexicalLess());

		for (FName ObjectPath : Keys)
		{
			PageBuffer.Append(TEXT("	"));
			ObjectPath.AppendString(PageBuffer);
			AddLine();
		}

		PageBuffer.Appendf(TEXT("--- End CachedAssetsByObjectPath : %d entries ---"), CachedAssetsByObjectPath.Num());
		AddLine();
	}

	if (Arguments.Contains(TEXT("PackageName")))
	{
		PrintAssetDataMap(TEXT("CachedAssetsByPackageName"), CachedAssetsByPackageName);
	}

	if (Arguments.Contains(TEXT("Path")))
	{
		PrintAssetDataMap(TEXT("CachedAssetsByPath"), CachedAssetsByPath);
	}

	if (Arguments.Contains(TEXT("Class")))
	{
		PrintAssetDataMap(TEXT("CachedAssetsByClass"), CachedAssetsByClass);
	}

	if (Arguments.Contains(TEXT("Tag")))
	{
		PrintAssetDataMap(TEXT("CachedAssetsByTag"), CachedAssetsByTag);
	}

	if (Arguments.Contains(TEXT("Dependencies")) && !bDumpDependencyDetails)
	{
		PageBuffer.Appendf(TEXT("--- Begin CachedDependsNodes ---"));
		AddLine();

		TArray<FDependsNode*> Nodes;
		CachedDependsNodes.GenerateValueArray(Nodes);
		Nodes.Sort([](const FDependsNode& A, const FDependsNode& B)
			{ return A.GetIdentifier().ToString() < B.GetIdentifier().ToString(); }
			);

		for (const FDependsNode* Node : Nodes)
		{
			PageBuffer.Append(TEXT("	"));
			Node->GetIdentifier().AppendString(PageBuffer);
			PageBuffer.Appendf(TEXT(" : %d connection(s)"), Node->GetConnectionCount());
			AddLine();
		}

		PageBuffer.Appendf(TEXT("--- End CachedDependsNodes : %d entries ---"), CachedDependsNodes.Num());
		AddLine();
	}

	if (bDumpDependencyDetails)
	{
		using namespace UE::AssetRegistry;
		PageBuffer.Append(TEXT("--- Begin CachedDependsNodes ---"));
		AddLine();

		auto SortByAssetID = [](const FDependsNode& A, const FDependsNode& B) { return A.GetIdentifier().ToString() < B.GetIdentifier().ToString(); };
		TArray<FDependsNode*> Nodes;
		CachedDependsNodes.GenerateValueArray(Nodes);
		Nodes.Sort(SortByAssetID);

		if (Arguments.Contains(TEXT("LegacyDependencies")))
		{
			EDependencyCategory CategoryTypes[] = { EDependencyCategory::Package, EDependencyCategory::Package,EDependencyCategory::SearchableName,EDependencyCategory::Manage, EDependencyCategory::Manage, EDependencyCategory::None };
			EDependencyQuery CategoryQueries[] = { EDependencyQuery::Hard, EDependencyQuery::Soft, EDependencyQuery::NoRequirements, EDependencyQuery::Direct, EDependencyQuery::Indirect, EDependencyQuery::NoRequirements };
			const TCHAR* CategoryNames[] = { TEXT("Hard"), TEXT("Soft"), TEXT("SearchableName"), TEXT("HardManage"), TEXT("SoftManage"), TEXT("References") };
			const int NumCategories = UE_ARRAY_COUNT(CategoryTypes);
			check(NumCategories == UE_ARRAY_COUNT(CategoryNames) && NumCategories == UE_ARRAY_COUNT(CategoryQueries));

			TArray<FDependsNode*> Links;
			for (const FDependsNode* Node : Nodes)
			{
				PageBuffer.Append(TEXT("	"));
				Node->GetIdentifier().AppendString(PageBuffer);
				AddLine();
				for (int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
				{
					EDependencyCategory CategoryType = CategoryTypes[CategoryIndex];
					EDependencyQuery CategoryQuery = CategoryQueries[CategoryIndex];
					const TCHAR* CategoryName = CategoryNames[CategoryIndex];
					Links.Reset();
					if (CategoryType != EDependencyCategory::None)
					{
						Node->GetDependencies(Links, CategoryType, CategoryQuery);
					}
					else
					{
						Node->GetReferencers(Links);
					}
					if (Links.Num() > 0)
					{
						PageBuffer.Appendf(TEXT("		%s"), CategoryName);
						AddLine();
						Links.Sort(SortByAssetID);
						for (FDependsNode* LinkNode : Links)
						{
							PageBuffer.Append(TEXT("			"));
							LinkNode->GetIdentifier().AppendString(PageBuffer);
							AddLine();
						}
					}
				}
			}
		}
		else
		{
			EDependencyCategory CategoryTypes[] = { EDependencyCategory::Package, EDependencyCategory::SearchableName,EDependencyCategory::Manage, EDependencyCategory::None };
			const TCHAR* CategoryNames[] = { TEXT("Package"), TEXT("SearchableName"), TEXT("Manage"), TEXT("References") };
			const int NumCategories = UE_ARRAY_COUNT(CategoryTypes);
			check(NumCategories == UE_ARRAY_COUNT(CategoryNames));

			TArray<FAssetDependency> Dependencies;
			TArray<FDependsNode*> References;
			for (const FDependsNode* Node : Nodes)
			{
				PageBuffer.Append(TEXT("	"));
				Node->GetIdentifier().AppendString(PageBuffer);
				AddLine();
				for (int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
				{
					EDependencyCategory CategoryType = CategoryTypes[CategoryIndex];
					const TCHAR* CategoryName = CategoryNames[CategoryIndex];
					if (CategoryType != EDependencyCategory::None)
					{
						Dependencies.Reset();
						Node->GetDependencies(Dependencies, CategoryType);
						if (Dependencies.Num() > 0)
						{
							PageBuffer.Appendf(TEXT("		%s"), CategoryName);
							AddLine();
							Dependencies.Sort([](const FAssetDependency& A, const FAssetDependency& B) { return A.AssetId.ToString() < B.AssetId.ToString(); });
							for (const FAssetDependency& AssetDependency : Dependencies)
							{
								PageBuffer.Append(TEXT("			"));
								AssetDependency.AssetId.AppendString(PageBuffer);
								PageBuffer.Append(TEXT("\t\t{"));
								PropertiesToString(AssetDependency.Properties, PageBuffer, AssetDependency.Category);
								PageBuffer.Append(TEXT("}"));
								AddLine();
							}
						}
					}
					else
					{
						References.Reset();
						Node->GetReferencers(References);
						if (References.Num() > 0)
						{
							PageBuffer.Appendf(TEXT("		%s"), CategoryName);
							AddLine();
							References.Sort(SortByAssetID);
							for (const FDependsNode* Reference : References)
							{
								PageBuffer.Append(TEXT("			"));
								Reference->GetIdentifier().AppendString(PageBuffer);
								AddLine();
							}
						}
					}
				}
			}
		}

		PageBuffer.Appendf(TEXT("--- End CachedDependsNodes : %d entries ---"), CachedDependsNodes.Num());
		AddLine();
	}
	if (Arguments.Contains(TEXT("PackageData")))
	{
		PageBuffer.Append(TEXT("--- Begin CachedPackageData ---"));
		AddLine();

		TArray<FName> Keys;
		CachedPackageData.GenerateKeyArray(Keys);
		Keys.Sort(FNameLexicalLess());

		for (const FName& Key : Keys)
		{
			const FAssetPackageData* PackageData = CachedPackageData.FindChecked(Key);
			PageBuffer.Append(TEXT("	"));
			Key.AppendString(PageBuffer);
			PageBuffer.Append(TEXT(" : "));
			PageBuffer.Append(PackageData->PackageGuid.ToString()); // TODO: Add AppendString for Guid
			PageBuffer.Appendf(TEXT(" : %d bytes"), PackageData->DiskSize);
			AddLine();
		}

		PageBuffer.Appendf(TEXT("--- End CachedPackageData : %d entries ---"), CachedPackageData.Num());
		AddLine();
	}

	if (PageBuffer.Len() > 0)
	{
		if (LinesPerPage == 1)
		{
			AddLine();
		}
		else
		{
			FinishPage();
		}
	}
}

#endif // ASSET_REGISTRY_STATE_DUMPING_ENABLED
