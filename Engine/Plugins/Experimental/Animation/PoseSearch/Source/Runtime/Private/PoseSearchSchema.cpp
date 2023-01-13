// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchSchema.h"
#include "AnimationRuntime.h"
#include "PoseSearch/PoseSearch.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"

bool UPoseSearchSchema::IsValid() const
{
	bool bValid = Skeleton != nullptr;

	for (const FBoneReference& BoneRef : BoneReferences)
	{
		bValid &= BoneRef.HasValidSetup();
	}

	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel: Channels)
	{
		bValid &= Channel != nullptr;
	}

	bValid &= (BoneReferences.Num() == BoneIndices.Num());

	return bValid;
}

void UPoseSearchSchema::ResolveBoneReferences()
{
	// Initialize references to obtain bone indices
	for (FBoneReference& BoneRef : BoneReferences)
	{
		BoneRef.Initialize(Skeleton);
	}

	// Fill out bone index array
	BoneIndices.SetNum(BoneReferences.Num());
	for (int32 BoneRefIdx = 0; BoneRefIdx != BoneReferences.Num(); ++BoneRefIdx)
	{
		BoneIndices[BoneRefIdx] = BoneReferences[BoneRefIdx].BoneIndex;
	}

	// Build separate index array with parent indices guaranteed to be present. Sort for EnsureParentsPresent.
	BoneIndicesWithParents = BoneIndices;
	BoneIndicesWithParents.Sort();

	if (Skeleton)
	{
		FAnimationRuntime::EnsureParentsPresent(BoneIndicesWithParents, Skeleton->GetReferenceSkeleton());
	}

	// BoneIndicesWithParents should at least contain the root to support mirroring root motion
	if (BoneIndicesWithParents.Num() == 0)
	{
		BoneIndicesWithParents.Add(0);
	}
}

void UPoseSearchSchema::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_PoseSearch_BuildQuery);

	InOutQuery.Init(this);

	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel : Channels)
	{
		Channel->BuildQuery(SearchContext, InOutQuery);
	}
}
