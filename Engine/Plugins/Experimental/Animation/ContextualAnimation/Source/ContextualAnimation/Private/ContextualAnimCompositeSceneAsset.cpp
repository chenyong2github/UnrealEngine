// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimCompositeSceneAsset.h"

#include "ContextualAnimMetadata.h"
#include "Animation/AnimMontage.h"
#include "UObject/ObjectSaveContext.h"
#include "ContextualAnimScenePivotProvider.h"

const FName UContextualAnimCompositeSceneAsset::InteractorRoleName = FName(TEXT("interactor"));
const FName UContextualAnimCompositeSceneAsset::InteractableRoleName = FName(TEXT("interactable"));

UContextualAnimCompositeSceneAsset::UContextualAnimCompositeSceneAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryRole = UContextualAnimCompositeSceneAsset::InteractableRoleName;
}

const FContextualAnimTrackSettings* UContextualAnimCompositeSceneAsset::GetTrackSettings(const FName& Role) const
{
	return Role == PrimaryRole ? &InteractableTrack.Settings : &InteractorTrack.Settings;
}

const FContextualAnimData* UContextualAnimCompositeSceneAsset::GetAnimDataForRoleAtIndex(const FName& Role, int32 Index) const
{
	if(Role == UContextualAnimCompositeSceneAsset::InteractableRoleName)
	{
		return &InteractableTrack.AnimData;
	}
	else if(Role == UContextualAnimCompositeSceneAsset::InteractorRoleName)
	{
		if(InteractorTrack.AnimDataContainer.IsValidIndex(Index))
		{
			return &InteractorTrack.AnimDataContainer[Index];
		}
	}

	return nullptr;
}

void UContextualAnimCompositeSceneAsset::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	// Necessary for FCompactPose that uses a FAnimStackAllocator (TMemStackAllocator) which allocates from FMemStack.
	// When allocating memory from FMemStack we need to explicitly use FMemMark to ensure items are freed when the scope exits. 
	// UWorld::Tick adds a FMemMark to catch any allocation inside the game tick 
	// but any allocation from outside the game tick (like here when generating the alignment tracks off-line) must explicitly add a mark to avoid a leak 
	FMemMark Mark(FMemStack::Get());

	Super::PreSave(ObjectSaveContext);

	InteractableTrack.AnimData.Index = 0;

	int32 NumAnimData = InteractorTrack.AnimDataContainer.Num();
	for (int32 AnimDataIdx = 0; AnimDataIdx < NumAnimData; AnimDataIdx++)
	{
		InteractorTrack.AnimDataContainer[AnimDataIdx].Index = AnimDataIdx;
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

	for (FContextualAnimData& Data : InteractorTrack.AnimDataContainer)
	{
		// Generate alignment tracks relative to scene pivot
		GenerateAlignmentTracks(InteractorTrack.Settings, Data);

		// Generate IK Targets
		GenerateIKTargetTracks(InteractorTrack.Settings, Data);
	}

	UpdateRadius();
}

bool UContextualAnimCompositeSceneAsset::Query(const FName& Role, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const
{
	//@TODO: Intentionally ignoring Role param since it doesn't make sense for this asset. This is just a temp anyway until we remove this asset completely
	return QueryCompositeTrack(&InteractorTrack, OutResult, QueryParams, ToWorldTransform);
}

bool UContextualAnimCompositeSceneAsset::QueryData(FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const
{
	return QueryCompositeTrack(&InteractorTrack, OutResult, QueryParams, ToWorldTransform);
}

void UContextualAnimCompositeSceneAsset::ForEachAnimData(FForEachAnimDataFunction Function) const
{
	if (Function(InteractableRoleName, InteractableTrack.AnimData) == EContextualAnimForEachResult::Break)
	{
		return;
	}

	for (const FContextualAnimData& AnimData : InteractorTrack.AnimDataContainer)
	{
		if (Function(InteractorRoleName, AnimData) == EContextualAnimForEachResult::Break)
		{
			return;
		}
	}
}

TArray<FName> UContextualAnimCompositeSceneAsset::GetRoles() const 
{ 
	return { InteractableRoleName, InteractorRoleName }; 
}