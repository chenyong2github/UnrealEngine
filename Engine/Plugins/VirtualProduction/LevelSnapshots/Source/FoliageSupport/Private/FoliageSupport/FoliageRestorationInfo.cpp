// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageSupport/FoliageRestorationInfo.h"

#include "Data/AddedAndRemovedComponentInfo.h"
#include "Data/RestorableObjectSelection.h"
#include "FoliageSupport/FoliageInfoData.h"
#include "PropertySelectionMap.h"

#include "InstancedFoliageActor.h"

FFoliageRestorationInfo FFoliageRestorationInfo::From(AInstancedFoliageActor* Object, const FPropertySelectionMap& SelectionMap)
{
	FFoliageRestorationInfo Result;
	
	const FRestorableObjectSelection ObjectSelection = SelectionMap.GetObjectSelection(Object);
	if (const FAddedAndRemovedComponentInfo* ComponentInfo = ObjectSelection.GetComponentSelection())
	{
		Result.EditorWorldComponentsToRemove = ComponentInfo->EditorWorldComponentsToRemove.Array();
		Result.SnapshotComponentsToAdd = ComponentInfo->SnapshotComponentsToAdd.Array();
	}

	for (UHierarchicalInstancedStaticMeshComponent* FoliageComp : TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*>(Object))
	{
		if (SelectionMap.HasChanges(FoliageComp))
		{
			Result.ModifiedComponents.Add(FoliageComp);
		}
	}

	return Result;
}

bool FFoliageRestorationInfo::ShouldSkipFoliageType(const FFoliageInfoData& SavedData) const
{
	return SavedData.GetImplType() != EFoliageImplType::StaticMesh && SavedData.GetImplType() != EFoliageImplType::Actor;
}

bool FFoliageRestorationInfo::ShouldRemoveFoliageType(const FFoliageInfoData& SavedData) const
{
	if (TOptional<FName> CompName = SavedData.GetComponentName())
	{
		return EditorWorldComponentsToRemove.ContainsByPredicate(
			[CompName](const TWeakObjectPtr<UActorComponent>& Component)
			{
				return Component.IsValid() && Component->GetFName().IsEqual(*CompName);
			});
	}

	// Actor foliage is currently not supported: just restore everything
	check(SavedData.GetImplType() != EFoliageImplType::StaticMesh);
	return false;
}

bool FFoliageRestorationInfo::ShouldSerializeFoliageType(const FFoliageInfoData& SavedData) const
{
	if (TOptional<FName> CompName = SavedData.GetComponentName())
	{
		const bool bIsAddedComponent = SnapshotComponentsToAdd.ContainsByPredicate(
			[CompName](const TWeakObjectPtr<UActorComponent>& Component)
			{
				return Component.IsValid() && Component->GetFName().IsEqual(*CompName);
			});
		const bool bIsModifiedComponent = ModifiedComponents.ContainsByPredicate(
			[CompName](const UActorComponent* Component)
			{
				return Component->GetFName().IsEqual(*CompName);
			});

		return bIsAddedComponent || bIsModifiedComponent;
	}

	// Actor foliage is currently not supported: just restore everything
	check(SavedData.GetImplType() != EFoliageImplType::StaticMesh);
	return false;
}
