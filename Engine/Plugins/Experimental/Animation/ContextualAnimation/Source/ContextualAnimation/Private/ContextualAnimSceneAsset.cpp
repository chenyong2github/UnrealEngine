// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneAsset.h"
#include "AnimationRuntime.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimTypes.h"
#include "ContextualAnimUtilities.h"
#include "ContextualAnimMetadata.h"
#include "UObject/ObjectSaveContext.h"
#include "ContextualAnimScenePivotProvider.h"

UContextualAnimSceneAsset::UContextualAnimSceneAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UContextualAnimSceneAsset::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	// Necessary for FCompactPose that uses a FAnimStackAllocator (TMemStackAllocator) which allocates from FMemStack.
	// When allocating memory from FMemStack we need to explicitly use FMemMark to ensure items are freed when the scope exits. 
	// UWorld::Tick adds a FMemMark to catch any allocation inside the game tick 
	// but any allocation from outside the game tick (like here when generating the alignment tracks off-line) must explicitly add a mark to avoid a leak 
	FMemMark Mark(FMemStack::Get());

	Super::PreSave(ObjectSaveContext);

	// Set Index for each ContextualAnimData
	int32 NumAnimData = 0;
	for (auto& Entry : DataContainer)
	{
		NumAnimData = FMath::Max(NumAnimData, Entry.Value.AnimDataContainer.Num());
		for (int32 AnimDataIdx = 0; AnimDataIdx < Entry.Value.AnimDataContainer.Num(); AnimDataIdx++)
		{
			Entry.Value.AnimDataContainer[AnimDataIdx].Index = AnimDataIdx;
		}
	}

	// Generate scene pivot for each alignment section
	for (int32 AlignmentSectionIdx = 0; AlignmentSectionIdx < AlignmentSections.Num(); AlignmentSectionIdx++)
	{
		AlignmentSections[AlignmentSectionIdx].ScenePivots.Reset();

		// We need to calculate scene pivot for each set of animations
		for (int32 AnimDataIdx = 0; AnimDataIdx < NumAnimData; AnimDataIdx++)
		{
			if (AlignmentSections[AlignmentSectionIdx].ScenePivotProvider)
			{
				const FTransform ScenePivot = AlignmentSections[AlignmentSectionIdx].ScenePivotProvider->CalculateScenePivot_Source(AnimDataIdx);
				AlignmentSections[AlignmentSectionIdx].ScenePivots.Add(ScenePivot);
			}
			else
			{
				AlignmentSections[AlignmentSectionIdx].ScenePivots.Add(FTransform::Identity);
			}
		}
	}
	

	for (auto& Entry : DataContainer)
	{
		for (FContextualAnimData& Data : Entry.Value.AnimDataContainer)
		{
			// Generate alignment tracks relative to scene pivot
			GenerateAlignmentTracks(Entry.Value.Settings, Data);

			// Generate IK Targets
			GenerateIKTargetTracks(Entry.Value.Settings, Data);
		}
	}

	UpdateRadius();
}

const FContextualAnimTrackSettings* UContextualAnimSceneAsset::GetTrackSettings(const FName& Role) const
{
	const FContextualAnimCompositeTrack* Track = DataContainer.Find(Role);
	return Track ? &Track->Settings : nullptr;
}

const FContextualAnimData* UContextualAnimSceneAsset::GetAnimDataForRoleAtIndex(const FName& Role, int32 Index) const
{
	if(const FContextualAnimCompositeTrack* Track = DataContainer.Find(Role))
	{
		if(Track->AnimDataContainer.IsValidIndex(Index))
		{
			return &Track->AnimDataContainer[Index];
		}
	}

	return nullptr;
}

void UContextualAnimSceneAsset::ForEachAnimData(FForEachAnimDataFunction Function) const
{
	for (const auto& Pair : DataContainer)
	{
		for (const FContextualAnimData& Data : Pair.Value.AnimDataContainer)
		{
			if (Function(Pair.Key, Data) == EContextualAnimForEachResult::Break)
			{
				return;
			}
		}
	}
}

TArray<FName> UContextualAnimSceneAsset::GetRoles() const
{
	TArray<FName> Roles;
	DataContainer.GetKeys(Roles);
	return Roles;
}

bool UContextualAnimSceneAsset::Query(const FName& Role, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const
{
	const FContextualAnimCompositeTrack* Track = DataContainer.Find(Role);
	return Track ? QueryCompositeTrack(Track, OutResult, QueryParams, ToWorldTransform) : false;
}