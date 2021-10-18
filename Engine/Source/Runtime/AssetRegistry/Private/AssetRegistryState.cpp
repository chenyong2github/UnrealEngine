// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistry/AssetRegistryState.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/MetaData.h"
#include "AssetRegistryArchive.h"
#include "AssetRegistryPrivate.h"
#include "AssetRegistry/ARFilter.h"
#include "DependsNode.h"
#include "PackageReader.h"
#include "NameTableArchive.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "Blueprint/BlueprintSupport.h"
#include "UObject/NameBatchSerialization.h"
#include "UObject/PrimaryAssetId.h"


FAssetRegistryState& FAssetRegistryState::operator=(FAssetRegistryState&& Rhs)
{
	Reset();

	CachedAssetsByObjectPath			= MoveTemp(Rhs.CachedAssetsByObjectPath);
	CachedAssetsByPackageName			= MoveTemp(Rhs.CachedAssetsByPackageName);
	CachedAssetsByPath					= MoveTemp(Rhs.CachedAssetsByPath);
	CachedAssetsByClass					= MoveTemp(Rhs.CachedAssetsByClass);
	CachedAssetsByTag					= MoveTemp(Rhs.CachedAssetsByTag);
	CachedDependsNodes					= MoveTemp(Rhs.CachedDependsNodes);
	CachedPackageData					= MoveTemp(Rhs.CachedPackageData);
	PreallocatedAssetDataBuffers		= MoveTemp(Rhs.PreallocatedAssetDataBuffers);
	PreallocatedDependsNodeDataBuffers	= MoveTemp(Rhs.PreallocatedDependsNodeDataBuffers);
	PreallocatedPackageDataBuffers		= MoveTemp(Rhs.PreallocatedPackageDataBuffers);
	Swap(NumAssets,				Rhs.NumAssets);
	Swap(NumDependsNodes,		Rhs.NumDependsNodes);
	Swap(NumPackageData,		Rhs.NumPackageData);

	return *this;
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
				OutTagsAndValues.Add(TagPair.Key, TagPair.Value.ToLoose());
			}
		}
		else
		{
			// It's a blacklist, include it unless it is in the all classes list or in the class specific list
			if (!bInAllClasseslist && !bInClassSpecificlist)
			{
				// It isn't in the blacklist. Keep it.
				OutTagsAndValues.Add(TagPair.Key, TagPair.Value.ToLoose());
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

		NewAssetData->TaggedAssetBundles = AssetData->TaggedAssetBundles;

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
				if (ExistingData)
				{
					// Bundle tags might have changed even if other tags haven't
					ExistingData->TaggedAssetBundles = AssetData.TaggedAssetBundles;

					// If tags have changed we need to update CachedAssetsByTag
					if (LocalTagsAndValues != ExistingData->TagsAndValues)
					{
						FAssetData TempData = *ExistingData;
						TempData.TagsAndValues = FAssetDataTagMapSharedView(MoveTemp(LocalTagsAndValues));
						UpdateAssetData(ExistingData, TempData);
					}
				}
			}
			else
			{
				FAssetData* NewData = new FAssetData(AssetData.PackageName, AssetData.PackagePath, AssetData.AssetName,
					AssetData.AssetClass, LocalTagsAndValues, AssetData.ChunkIDs, AssetData.PackageFlags);
				NewData->TaggedAssetBundles = AssetData.TaggedAssetBundles;

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

bool FAssetRegistryState::HasAssets(const FName PackagePath, bool bARFiltering) const
{
	const TArray<FAssetData*>* FoundAssetArray = CachedAssetsByPath.Find(PackagePath);
	if (FoundAssetArray)
	{
		if (bARFiltering)
		{
			return FoundAssetArray->ContainsByPredicate([](FAssetData* AssetData)
			{
				return AssetData && !UE::AssetRegistry::FFiltering::ShouldSkipAsset(AssetData->AssetClass, AssetData->PackageFlags);
			});
		}
		else
		{
			return FoundAssetArray->Num() > 0;
		}
	}
	return false;
}

bool FAssetRegistryState::GetAssets(const FARCompiledFilter& Filter, const TSet<FName>& PackageNamesToSkip, TArray<FAssetData>& OutAssetData, bool bARFiltering) const
{
	return EnumerateAssets(Filter, PackageNamesToSkip, [&OutAssetData](const FAssetData& AssetData)
	{
		OutAssetData.Emplace(AssetData);
		return true;
	},
	bARFiltering);
}

template<class ArrayType>
TArray<FAssetData*> FindAssets(const TMap<FName, ArrayType>& Map, const TSet<FName>& Keys)
{
	TArray<TArrayView<FAssetData* const>> Matches;
	Matches.Reserve(Keys.Num());
	uint32 TotalMatches = 0;

	for (FName Key : Keys)
	{
		if (const ArrayType* Assets = Map.Find(Key))
		{
			Matches.Add(MakeArrayView(*Assets));
			TotalMatches += Assets->Num();
		}
	}

	TArray<FAssetData*> Out;
	Out.Reserve(TotalMatches);
	for (TArrayView<FAssetData* const> Assets : Matches)
	{
		Out.Append(Assets.GetData(), Assets.Num());
	}

	return Out;
}

bool FAssetRegistryState::EnumerateAssets(const FARCompiledFilter& Filter, const TSet<FName>& PackageNamesToSkip, TFunctionRef<bool(const FAssetData&)> Callback, bool bARFiltering) const
{
	// Verify filter input. If all assets are needed, use EnumerateAllAssets() instead.
	if (Filter.IsEmpty() || !IsFilterValid(Filter))
	{
		return false;
	}

	const uint32 FilterWithoutPackageFlags = Filter.WithoutPackageFlags;
	const uint32 FilterWithPackageFlags = Filter.WithPackageFlags;

	// The assets that match each filter
	TArray<TArray<FAssetData*>, TInlineAllocator<5>> FilterResults;
	
	// On disk package names
	if (Filter.PackageNames.Num() > 0)
	{
		FilterResults.Emplace(FindAssets(CachedAssetsByPackageName, Filter.PackageNames));
	}

	// On disk package paths
	if (Filter.PackagePaths.Num() > 0)
	{
		FilterResults.Emplace(FindAssets(CachedAssetsByPath, Filter.PackagePaths));
	}

	// On disk classes
	if (Filter.ClassNames.Num() > 0)
	{
		FilterResults.Emplace(FindAssets(CachedAssetsByClass, Filter.ClassNames));
	}

	// On disk object paths
	if (Filter.ObjectPaths.Num() > 0)
	{
		TArray<FAssetData*>& ObjectPathsFilter = FilterResults.Emplace_GetRef();
		ObjectPathsFilter.Reserve(Filter.ObjectPaths.Num());

		for (FName ObjectPath : Filter.ObjectPaths)
		{
			if (FAssetData* AssetDataPtr = CachedAssetsByObjectPath.FindRef(ObjectPath))
			{
				ObjectPathsFilter.Add(AssetDataPtr);
			}
		}
	}

	// On disk tags and values
	if (Filter.TagsAndValues.Num() > 0)
	{
		TSet<FAssetData*> TagAndValuesFilter;
		// Sometimes number of assets matching this filter is correlated to number of assets matching previous filters 
		if (FilterResults.Num())
		{
			TagAndValuesFilter.Reserve(FilterResults[0].Num());
		}

		for (auto FilterTagIt = Filter.TagsAndValues.CreateConstIterator(); FilterTagIt; ++FilterTagIt)
		{
			const FName Tag = FilterTagIt.Key();
			const TOptional<FString>& Value = FilterTagIt.Value();

			if (const TArray<FAssetData*>* TagAssets = CachedAssetsByTag.Find(Tag))
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

		FilterResults.Emplace(TagAndValuesFilter.Array());
	}

	// Perform callback for assets that match all filters
	if (FilterResults.Num() > 0)
	{
		auto SkipAssetData = [&](const FAssetData* AssetData) 
		{ 
			if (PackageNamesToSkip.Contains(AssetData->PackageName) |			//-V792
				AssetData->HasAnyPackageFlags(FilterWithoutPackageFlags) |		//-V792
				!AssetData->HasAllPackageFlags(FilterWithPackageFlags))			//-V792
			{
				return true;
			}

			return bARFiltering && UE::AssetRegistry::FFiltering::ShouldSkipAsset(AssetData->AssetClass, AssetData->PackageFlags);
		};

		if (FilterResults.Num() > 1)
		{
			// Count how many filters each asset pass
			TMap<FAssetData*, uint32> PassCounts;
			for (const TArray<FAssetData*>& FilterEvaluation : FilterResults)
			{
				PassCounts.Reserve(FilterEvaluation.Num());
				
				for (FAssetData* AssetData : FilterEvaluation)
				{
					PassCounts.FindOrAdd(AssetData)++;	
				}
			}

			// Include assets that match all filters
			for (TPair<FAssetData*, uint32> PassCount : PassCounts)
			{
				const FAssetData* AssetData = PassCount.Key;
				check(PassCount.Value <= (uint32)FilterResults.Num());
				if (PassCount.Value != FilterResults.Num() || SkipAssetData(AssetData))
				{
					continue;
				}
				else if (!Callback(*AssetData))
				{
					return true;
				}
			}
		}
		else
		{
			// All matched assets passed the single filter
			for (const FAssetData* AssetData : FilterResults[0])
			{
				if (SkipAssetData(AssetData))
				{
					continue;
				}
				else if (!Callback(*AssetData))
				{
					return true;
				}
			}
		}
	}

	return true;
}

bool FAssetRegistryState::GetAllAssets(const TSet<FName>& PackageNamesToSkip, TArray<FAssetData>& OutAssetData, bool bARFiltering) const
{
	return EnumerateAllAssets(PackageNamesToSkip, [&OutAssetData](const FAssetData& AssetData)
	{
		OutAssetData.Emplace(AssetData);
		return true;
	},
	bARFiltering);
}

bool FAssetRegistryState::EnumerateAllAssets(const TSet<FName>& PackageNamesToSkip, TFunctionRef<bool(const FAssetData&)> Callback, bool bARFiltering) const
{
	// All unloaded disk assets
	for (const TPair<FName, FAssetData*>& AssetDataPair : CachedAssetsByObjectPath)
	{
		const FAssetData* AssetData = AssetDataPair.Value;

		if (AssetData &&
			!PackageNamesToSkip.Contains(AssetData->PackageName) &&
			(!bARFiltering || !UE::AssetRegistry::FFiltering::ShouldSkipAsset(AssetData->AssetClass, AssetData->PackageFlags)))
		{
			if (!Callback(*AssetData))
			{
				return true;
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

bool FAssetRegistryState::Serialize(FArchive& Ar, const FAssetRegistrySerializationOptions& Options)
{
	return Ar.IsSaving() ? Save(Ar, Options) : Load(Ar, FAssetRegistryLoadOptions(Options));
}

bool FAssetRegistryState::Save(FArchive& OriginalAr, const FAssetRegistrySerializationOptions& Options)
{
	SCOPED_BOOT_TIMING("FAssetRegistryState::Save");

	check(!OriginalAr.IsLoading());

	// This is only used for the runtime version of the AssetRegistry

#if !ALLOW_NAME_BATCH_SAVING
	checkf(false, TEXT("Cannot save cooked AssetRegistryState in this configuration"));
#else
	check(CachedAssetsByObjectPath.Num() == NumAssets);

	FAssetRegistryVersion::Type Version = FAssetRegistryVersion::LatestVersion;
	FAssetRegistryVersion::SerializeVersion(OriginalAr, Version);

	// Set up fixed asset registry writer
	FAssetRegistryWriter Ar(FAssetRegistryWriterOptions(Options), OriginalAr);

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
			DependentNode->SerializeSave(Ar, GetSerializeIndexFromNode, Scratch, Options);
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
#endif // ALLOW_NAME_BATCH_SAVING

	return !OriginalAr.IsError();
}

bool FAssetRegistryState::Load(FArchive& OriginalAr, const FAssetRegistryLoadOptions& Options)
{
	LLM_SCOPE(ELLMTag::AssetRegistry);

	FAssetRegistryVersion::Type Version = FAssetRegistryVersion::LatestVersion;
	FAssetRegistryVersion::SerializeVersion(OriginalAr, Version);

	FSoftObjectPathSerializationScope SerializationScope(NAME_None, NAME_None, ESoftObjectPathCollectType::NeverCollect, ESoftObjectPathSerializeType::AlwaysSerialize);

	if (Version < FAssetRegistryVersion::RemovedMD5Hash)
	{
		// Cannot read states before this version
		return false;
	}
	else if (Version < FAssetRegistryVersion::FixedTags)
	{
		FNameTableArchiveReader NameTableReader(OriginalAr);
		Load(NameTableReader, Version, Options);
	}
	else
	{
		FAssetRegistryReader Reader(OriginalAr, Options.ParallelWorkers);

		if (Reader.IsError())
		{
			return false;
		}

		// Load won't resolve asset registry tag values loaded in parallel
		// and can run before WaitForTasks
		Load(Reader, Version, Options);

		Reader.WaitForTasks();
	}

	return !OriginalAr.IsError();
}

template<class Archive>
void FAssetRegistryState::Load(Archive&& Ar, FAssetRegistryVersion::Type Version, const FAssetRegistryLoadOptions& Options)
{
	// serialize number of objects
	int32 LocalNumAssets = 0;
	Ar << LocalNumAssets;

	// allocate one single block for all asset data structs (to reduce tens of thousands of heap allocations)
	TArrayView<FAssetData> PreallocatedAssetDataBuffer(new FAssetData[LocalNumAssets], LocalNumAssets);
	PreallocatedAssetDataBuffers.Add(PreallocatedAssetDataBuffer.GetData());
	for (FAssetData& NewAssetData : PreallocatedAssetDataBuffer)
	{
		NewAssetData.SerializeForCache(Ar);
	}

	SetAssetDatas(PreallocatedAssetDataBuffer, Options);

	if (Version < FAssetRegistryVersion::AddedDependencyFlags)
	{
		LoadDependencies_BeforeFlags(Ar, Options.bLoadDependencies, Version);
	}
	else
	{
		int64 DependencySectionSize;
		Ar << DependencySectionSize;
		int64 DependencySectionEnd = Ar.Tell() + DependencySectionSize;

		if (Options.bLoadDependencies)
		{
			LoadDependencies(Ar);
		}
			
		if (!Options.bLoadDependencies || Ar.IsError())
		{
			Ar.Seek(DependencySectionEnd);
		}
	}

	int32 LocalNumPackageData = 0;
	Ar << LocalNumPackageData;

	if (LocalNumPackageData > 0)
	{
		if (Options.bLoadPackageData)
		{
			TArrayView<FAssetPackageData> PreallocatedPackageDataBuffer(new FAssetPackageData[LocalNumPackageData], LocalNumPackageData);
			PreallocatedPackageDataBuffers.Add(PreallocatedPackageDataBuffer.GetData());
			CachedPackageData.Reserve(LocalNumPackageData);
			for (FAssetPackageData& NewPackageData : PreallocatedPackageDataBuffer)
			{
				FName PackageName;
				Ar << PackageName;
		
				if (Version < FAssetRegistryVersion::AddedCookedMD5Hash)
				{
					Ar << NewPackageData.DiskSize;
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					Ar << NewPackageData.PackageGuid;
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
				}
				else
				{
					NewPackageData.SerializeForCache(Ar);
				}

				CachedPackageData.Add(PackageName, &NewPackageData);
			}	
		}
		else
		{
			for (int32 PackageDataIndex = 0; PackageDataIndex < LocalNumPackageData; PackageDataIndex++)
			{
				FName PackageName;
				Ar << PackageName;

				FAssetPackageData FakeData;
				FakeData.SerializeForCache(Ar);
			}
		}
	}
}

void FAssetRegistryState::LoadDependencies(FArchive& Ar)
{
	int32 LocalNumDependsNodes = 0;
	Ar << LocalNumDependsNodes;

	if (LocalNumDependsNodes <= 0)
	{
		return;
	}

	FDependsNode* PreallocatedDependsNodeDataBuffer = new FDependsNode[LocalNumDependsNodes];
	PreallocatedDependsNodeDataBuffers.Add(PreallocatedDependsNodeDataBuffer);
	CachedDependsNodes.Reserve(LocalNumDependsNodes);
	
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
		DependsNode->SerializeLoad(Ar, GetNodeFromSerializeIndex, Scratch);
		CachedDependsNodes.Add(DependsNode->GetIdentifier(), DependsNode);
	}
}

void FAssetRegistryState::LoadDependencies_BeforeFlags(FArchive& Ar, bool bSerializeDependencies, FAssetRegistryVersion::Type Version)
{
	int32 LocalNumDependsNodes = 0;
	Ar << LocalNumDependsNodes;

	FDependsNode Placeholder;
	FDependsNode* PreallocatedDependsNodeDataBuffer = nullptr;
	if (bSerializeDependencies && LocalNumDependsNodes > 0)
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
	FDependsNode::GetPropertySetBits_BeforeFlags(HardBits, SoftBits, HardManageBits, SoftManageBits);

	TArray<FDependsNode*> DependsNodes;
	for (int32 DependsNodeIndex = 0; DependsNodeIndex < LocalNumDependsNodes; DependsNodeIndex++)
	{
		// Create the node if we're actually saving dependencies, otherwise just fake serialize
		FDependsNode* DependsNode = nullptr;
		if (bSerializeDependencies)
		{
			DependsNode = &PreallocatedDependsNodeDataBuffer[DependsNodeIndex];
		}
		else
		{
			DependsNode = &Placeholder;
		}

		// Call the DependsNode legacy serialization function
		DependsNode->SerializeLoad_BeforeFlags(Ar, Version, PreallocatedDependsNodeDataBuffer, LocalNumDependsNodes, bSerializeDependencies, HardBits, SoftBits, HardManageBits, SoftManageBits);

		// Register the DependsNode with its AssetIdentifier
		if (bSerializeDependencies)
		{
			CachedDependsNodes.Add(DependsNode->GetIdentifier(), DependsNode);
		}
	}
}

uint32 FAssetRegistryState::GetAllocatedSize(bool bLogDetailed) const
{
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
		[&MapArrayMemory](const auto& A)
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

	uint32 AssetDataSize = 0;
	FAssetDataTagMapSharedView::FMemoryCounter TagMemoryUsage;

	for (const TPair<FName, FAssetData*>& AssetDataPair : CachedAssetsByObjectPath)
	{
		const FAssetData& AssetData = *AssetDataPair.Value;
		
		AssetDataSize += sizeof(AssetData);
		AssetDataSize += AssetData.ChunkIDs.GetAllocatedSize();
		TagMemoryUsage.Include(AssetData.TagsAndValues); 
	}

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetData Count: %d"), CachedAssetsByObjectPath.Num());
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetData Static Size: %dk"), AssetDataSize / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Loose Tags: %dk"), TagMemoryUsage.GetLooseSize() / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Fixed Tags: %dk"), TagMemoryUsage.GetFixedSize() / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("TArray<FAssetData*>: %dk"), MapArrayMemory / 1024);
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

	uint32 TotalBytes = MapMemory + AssetDataSize + TagMemoryUsage.GetFixedSize()  + TagMemoryUsage.GetLooseSize() + DependNodesSize + DependenciesSize + PackageDataSize + MapArrayMemory;

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("PackageData Count: %d"), CachedPackageData.Num());
		UE_LOG(LogAssetRegistry, Log, TEXT("PackageData Static Size: %dk"), PackageDataSize / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("Total State Size: %dk"), TotalBytes / 1024);
	}

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
			TArray<FAssetData*, TInlineAllocator<1>>& Assets = CachedAssetsByPackageName[CurrentDependency->GetPackageName()];

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

void FAssetRegistryState::SetAssetDatas(TArrayView<FAssetData> AssetDatas, const FAssetRegistryLoadOptions& Options)
{
	UE_CLOG(NumAssets != 0, LogAssetRegistry, Fatal, TEXT("Can only load into empty asset registry states. Load into temporary and append using InitializeFromExisting() instead."));

	NumAssets = AssetDatas.Num();

	auto SetPathCache = [&]() 
	{
		CachedAssetsByObjectPath.Empty(AssetDatas.Num());
		for (FAssetData& AssetData : AssetDatas)
		{
			CachedAssetsByObjectPath.Add(AssetData.ObjectPath, &AssetData);
		}
		ensure(NumAssets == CachedAssetsByObjectPath.Num());
	};

	// FAssetDatas sharing package name are very rare.
	// Reserve up front and don't bother shrinking. 
	auto SetPackageNameCache = [&]() 
	{
		CachedAssetsByPackageName.Empty(AssetDatas.Num());
		for (FAssetData& AssetData : AssetDatas)
		{
			TArray<FAssetData*, TInlineAllocator<1>>& PackageAssets = CachedAssetsByPackageName.FindOrAdd(AssetData.PackageName);
			PackageAssets.Add(&AssetData);
		}
	};

	auto SetOtherCaches = [&]()
	{
		auto ShrinkMultimap = [](TMap<FName, TArray<FAssetData*> >& Map)
		{
			Map.Shrink();
			for (auto& Pair : Map)
			{
				Pair.Value.Shrink();
			}
		};

		CachedAssetsByPath.Empty();
		for (FAssetData& AssetData : AssetDatas)
		{
			TArray<FAssetData*>& PathAssets = CachedAssetsByPath.FindOrAdd(AssetData.PackagePath);
			PathAssets.Add(&AssetData);
		}
		ShrinkMultimap(CachedAssetsByPath);

		CachedAssetsByClass.Empty();
		for (FAssetData& AssetData : AssetDatas)
		{
			TArray<FAssetData*>& ClassAssets = CachedAssetsByClass.FindOrAdd(AssetData.AssetClass);
			ClassAssets.Add(&AssetData);
		}
		ShrinkMultimap(CachedAssetsByClass);

		CachedAssetsByTag.Empty();
		for (FAssetData& AssetData : AssetDatas)
		{
			for (const TPair<FName, FAssetTagValueRef>& Pair : AssetData.TagsAndValues)
			{
				TArray<FAssetData*>& TagAssets = CachedAssetsByTag.FindOrAdd(Pair.Key);
				TagAssets.Add(&AssetData);
			}
		}
		ShrinkMultimap(CachedAssetsByTag);
	};

	if (Options.ParallelWorkers <= 1)
	{
		SetPathCache();
		SetPackageNameCache();
		SetOtherCaches();
	}
	else
	{
		TFuture<void> Task1 = Async(EAsyncExecution::TaskGraph, [=](){ SetPathCache(); });
		TFuture<void> Task2 = Async(EAsyncExecution::TaskGraph, [=](){ SetPackageNameCache(); });
		SetOtherCaches();
		Task1.Wait();
		Task2.Wait();
	}
}

void FAssetRegistryState::AddAssetData(FAssetData* AssetData)
{
	FAssetData*& ExistingByObjectPath = CachedAssetsByObjectPath.FindOrAdd(AssetData->ObjectPath);
	if (ExistingByObjectPath)
	{
		UE_LOG(LogAssetRegistry, Error, TEXT("AddAssetData called with ObjectPath %s which already exists. ")
			TEXT("This will overwrite and leak the existing AssetData."), *AssetData->ObjectPath.ToString());
	}
	else
	{
		++NumAssets;
	}
	ExistingByObjectPath = AssetData;

	TArray<FAssetData*, TInlineAllocator<1>>& PackageAssets = CachedAssetsByPackageName.FindOrAdd(AssetData->PackageName);
	TArray<FAssetData*>& PathAssets = CachedAssetsByPath.FindOrAdd(AssetData->PackagePath);
	TArray<FAssetData*>& ClassAssets = CachedAssetsByClass.FindOrAdd(AssetData->AssetClass);

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
		int32 NumRemoved = CachedAssetsByObjectPath.Remove(AssetData->ObjectPath);
		check(NumRemoved <= 1);
		if (NumRemoved == 0)
		{
			UE_LOG(LogAssetRegistry, Error, TEXT("UpdateAssetData called on AssetData %s that is not present in the AssetRegistry."),
				*AssetData->ObjectPath.ToString());
		}
		NumAssets -= NumRemoved;
		FAssetData*& Existing = CachedAssetsByObjectPath.FindOrAdd(NewAssetData.ObjectPath, AssetData);
		if (Existing)
		{
			UE_LOG(LogAssetRegistry, Error, TEXT("UpdateAssetData called with a change in ObjectPath from Old=\"%s\" to New=\"%s\", ")
				TEXT("but the new ObjectPath is already present with another AssetData. This will overwrite and leak the existing AssetData."),
				*AssetData->ObjectPath.ToString(), *NewAssetData.ObjectPath.ToString());
		}
		else
		{
			++NumAssets;
		}
		Existing = AssetData;
	}

	// Update PackageName
	if (AssetData->PackageName != NewAssetData.PackageName)
	{
		TArray<FAssetData*, TInlineAllocator<1>>* OldPackageAssets = CachedAssetsByPackageName.Find(AssetData->PackageName);
		TArray<FAssetData*, TInlineAllocator<1>>& NewPackageAssets = CachedAssetsByPackageName.FindOrAdd(NewAssetData.PackageName);

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

	if (!ensure(AssetData))
	{
		return;
	}

	int32 NumRemoved = CachedAssetsByObjectPath.Remove(AssetData->ObjectPath);
	check(NumRemoved <= 1);
	if (NumRemoved == 0)
	{
		UE_LOG(LogAssetRegistry, Error, TEXT("RemoveAssetData called on AssetData %s that is not present in the AssetRegistry."),
			*AssetData->ObjectPath.ToString());
		return;
	}

	TArray<FAssetData*, TInlineAllocator<1>>* OldPackageAssets = CachedAssetsByPackageName.Find(AssetData->PackageName);
	TArray<FAssetData*>* OldPathAssets = CachedAssetsByPath.Find(AssetData->PackagePath);
	TArray<FAssetData*>* OldClassAssets = CachedAssetsByClass.Find(AssetData->AssetClass);
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

	auto PrintAssetDataMap = [&AddLine, &PageBuffer](FString Name, const auto& AssetMap)
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
			const auto& AssetArray = AssetMap.FindChecked(Key);
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
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			PageBuffer.Append(PackageData->PackageGuid.ToString()); // TODO: Add AppendString for Guid
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
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
