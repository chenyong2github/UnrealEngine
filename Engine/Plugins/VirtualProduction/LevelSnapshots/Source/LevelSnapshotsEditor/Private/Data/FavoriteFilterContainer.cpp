// Copyright Epic Games, Inc. All Rights Reserved.

#include "FavoriteFilterContainer.h"

#include "LevelSnapshotFilters.h"
#include "UClassMetaDataDefinitions.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "UObject/UObjectIterator.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
	bool IsCommonClass(const TSubclassOf<ULevelSnapshotFilter>& ClassToCheck)
	{
		return ClassToCheck->FindMetaData(UClassMetaDataDefinitions::CommonSnapshotFilter) != nullptr;
	}
	bool IsBlueprintClass(const TSubclassOf<ULevelSnapshotFilter>& ClassToCheck)
	{
		return ClassToCheck->IsInBlueprint();
	}
	
	TArray<TSubclassOf<ULevelSnapshotFilter>> FindNativeFilterClasses(const bool bOnlyCommon)
	{
		TArray<TSubclassOf<ULevelSnapshotFilter>> Result;
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* ClassToCheck = *ClassIt;
			if (!ClassToCheck->IsChildOf(ULevelSnapshotFilter::StaticClass()))
			{
				continue;
			}

			const bool bIsClassInstantiatable = ClassToCheck->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Abstract) || FKismetEditorUtilities::IsClassABlueprintSkeleton(ClassToCheck);
			if (bIsClassInstantiatable)
			{
				continue;
			}

			if (IsBlueprintClass(ClassToCheck))
			{
				continue;
			}

			if (bOnlyCommon && !IsCommonClass(ClassToCheck))
			{
				continue;
			}
			const bool bIsInternalOnlyFilter = ClassToCheck->FindMetaData(UClassMetaDataDefinitions::InternalSnapshotFilter) != nullptr;
			if (bIsInternalOnlyFilter)
			{
				continue;
			}

			
			Result.Add(ClassToCheck);
		}
		return Result;
	}

	TArray<TSubclassOf<ULevelSnapshotBlueprintFilter>> FindBlueprintFilterClasses()
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		
		TArray<FAssetData> BlueprintList;
		AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetFName(), BlueprintList);

		// Returns = 
		const FString BlueprintFilterBaseClassPath = ULevelSnapshotBlueprintFilter::StaticClass()->GetPathName();
		TArray<TSubclassOf<ULevelSnapshotBlueprintFilter>> Result;
		for (const FAssetData& BlueprintClassData : BlueprintList)
		{
			// Is of form = Class''
			const FString FirstNativeParentName = BlueprintClassData.GetTagValueRef<FString>(FBlueprintTags::NativeParentClassPath);
			const bool bInheritsFromBlueprintFilter = FirstNativeParentName.Contains(BlueprintFilterBaseClassPath);

			// Loading all blueprints is too slow... this finds only the ones we need
			if (!bInheritsFromBlueprintFilter)
			{
				continue;
			}

			// If there are a lot of assets, this may be slow.
			UBlueprint* BlueprintAsset = Cast<UBlueprint>(BlueprintClassData.GetAsset());
			UClass* LoadedClass = BlueprintAsset->GeneratedClass;
			if (ensure(LoadedClass) && ensure(BlueprintAsset->ParentClass) && BlueprintAsset->ParentClass->IsChildOf(ULevelSnapshotBlueprintFilter::StaticClass()))
			{
				Result.Add(LoadedClass);
			}
		}
		return Result;
	}
}

void UFavoriteFilterContainer::AddToFavorites(const TSubclassOf<ULevelSnapshotFilter>& NewFavoriteClass)
{
	if (!ensure(NewFavoriteClass))
	{
		return;
	}
	
	const int32 NewIndex = Favorites.AddUnique(NewFavoriteClass);
	const bool bWasNew = NewIndex != INDEX_NONE;
	if (ensure(bWasNew))
	{
		OnFavoritesChanged.Broadcast();
	}
}

void UFavoriteFilterContainer::RemoveFromFavorites(const TSubclassOf<ULevelSnapshotFilter>& NoLongerFavoriteClass)
{
	if (!ensure(NoLongerFavoriteClass))
	{
		return;
	}

	const bool bIsCommon = IsCommonClass(NoLongerFavoriteClass);
	if (!bIsCommon)
	{
		const bool bIsBlueprint = IsBlueprintClass(NoLongerFavoriteClass);
		if (bIsBlueprint && bIncludeAllBlueprintClasses)
		{
			bIncludeAllBlueprintClasses = false;
		}
		if (!bIsBlueprint && bIncludeAllNativeClasses)
		{
			bIncludeAllNativeClasses = false;
		}
	}
	
	const int32 NumberItemsRemoved = Favorites.RemoveSingle(NoLongerFavoriteClass);
	const bool bWasItemRemoved = NumberItemsRemoved != INDEX_NONE;
	if (ensure(bWasItemRemoved))
	{
		OnFavoritesChanged.Broadcast();
	}
}

void UFavoriteFilterContainer::ClearFavorites()
{
	Favorites.Empty();
	OnFavoritesChanged.Broadcast();
}

void UFavoriteFilterContainer::SetIncludeAllNativeClasses(bool bShouldIncludeNative)
{
	if (bShouldIncludeNative == bIncludeAllNativeClasses)
	{
		return;
	}
	bIncludeAllNativeClasses = bShouldIncludeNative;

	const TArray<TSubclassOf<ULevelSnapshotFilter>>& NativeClasses = GetAvailableNativeFilters();
	for (const TSubclassOf<ULevelSnapshotFilter>& Filter : NativeClasses)
	{
		if (bShouldIncludeNative)
		{
			Favorites.AddUnique(Filter);
		}
		else
		{
			Favorites.RemoveSingle(Filter);
		}
	}
	
	OnFavoritesChanged.Broadcast();
}

void UFavoriteFilterContainer::SetIncludeAllBlueprintClasses(bool bShouldIncludeBlueprint)
{
	if (bShouldIncludeBlueprint == bIncludeAllBlueprintClasses)
	{
		return;
	}
	bIncludeAllBlueprintClasses = bShouldIncludeBlueprint;

	const TArray<TSubclassOf<ULevelSnapshotBlueprintFilter>>& BlueprintClasses = GetAvailableBlueprintFilters();
	for (const TSubclassOf<ULevelSnapshotBlueprintFilter>& Filter : BlueprintClasses)
	{
		if (bShouldIncludeBlueprint)
		{
			Favorites.AddUnique(Filter);
		}
		else
		{
			Favorites.RemoveSingle(Filter);
		}
	}
	
	OnFavoritesChanged.Broadcast();
}

bool UFavoriteFilterContainer::ShouldIncludeAllNativeClasses() const
{
	return bIncludeAllNativeClasses;
}

bool UFavoriteFilterContainer::ShouldIncludeAllBlueprintClasses() const
{
	return bIncludeAllBlueprintClasses;
}

const TArray<TSubclassOf<ULevelSnapshotFilter>>& UFavoriteFilterContainer::GetFavorites() const
{
	return Favorites;
}

const TArray<TSubclassOf<ULevelSnapshotFilter>> UFavoriteFilterContainer::GetCommonFilters() const
{
	// Assume no native C++ classes are added at runtime.
	static TArray<TSubclassOf<ULevelSnapshotFilter>> CachedResult = FindNativeFilterClasses(true);
	return CachedResult;
}

const TArray<TSubclassOf<ULevelSnapshotFilter>>& UFavoriteFilterContainer::GetAvailableNativeFilters() const
{
	// Assume no native C++ classes are added at runtime.
	static TArray<TSubclassOf<ULevelSnapshotFilter>> CachedResult = FindNativeFilterClasses(false);
	return CachedResult;
}

TArray<TSubclassOf<ULevelSnapshotBlueprintFilter>> UFavoriteFilterContainer::GetAvailableBlueprintFilters() const
{
	// Regenerate every time to find blueprints newly added or removed by the user in the editor
	return FindBlueprintFilterClasses();
}
