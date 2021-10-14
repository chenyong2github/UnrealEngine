// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FFoliageInfoData;
class AInstancedFoliageActor;
class UActorComponent;
struct FPropertySelectionMap;

/** Tells us whether foliage types should be added / removed / serialized into AInstancedFoliageActor::FoliageInfos */
class FFoliageRestorationInfo
{
	TArray<UActorComponent*> ModifiedComponents;
	TArray<TWeakObjectPtr<UActorComponent>> EditorWorldComponentsToRemove;
	TArray<TWeakObjectPtr<UActorComponent>> SnapshotComponentsToAdd;
public:
	
	static FFoliageRestorationInfo From(AInstancedFoliageActor* Object, const FPropertySelectionMap& SelectionMap);

	bool ShouldSkipFoliageType(const FFoliageInfoData& SavedData) const;

	bool ShouldRemoveFoliageType(const FFoliageInfoData& SavedData) const;
	bool ShouldSerializeFoliageType(const FFoliageInfoData& SavedData) const;
};
