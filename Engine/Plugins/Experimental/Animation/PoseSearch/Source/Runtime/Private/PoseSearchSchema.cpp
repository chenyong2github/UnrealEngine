// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchSchema.h"
#include "AnimationRuntime.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchResult.h"
#include "UObject/ObjectSaveContext.h"

bool UPoseSearchSchema::IsValid() const
{
	bool bValid = Skeleton != nullptr;

	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel: Channels)
	{
		bValid &= Channel != nullptr;
	}

	return bValid;
}

void UPoseSearchSchema::ResolveBoneReferences()
{
	BoneIndicesWithParents.Reset();
	if (Skeleton)
	{
		// Initialize references to obtain bone indices and fill out bone index array
		for (FBoneReference& BoneRef : BoneReferences)
		{
			BoneRef.Initialize(Skeleton);
			if (BoneRef.HasValidSetup())
			{
				BoneIndicesWithParents.Add(BoneRef.BoneIndex);
			}
		}

		// Build separate index array with parent indices guaranteed to be present. Sort for EnsureParentsPresent.
		BoneIndicesWithParents.Sort();
		FAnimationRuntime::EnsureParentsPresent(BoneIndicesWithParents, Skeleton->GetReferenceSkeleton());
	}

	// BoneIndicesWithParents should at least contain the root to support mirroring root motion
	if (BoneIndicesWithParents.IsEmpty())
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

void UPoseSearchSchema::Finalize(bool bRemoveEmptyChannels)
{
	using namespace UE::PoseSearch;

	if (bRemoveEmptyChannels)
	{
		Channels.RemoveAll([](TObjectPtr<UPoseSearchFeatureChannel>& Channel) { return !Channel; });
	}

	BoneReferences.Reset();

	int32 CurrentChannelDataOffset = 0;

	SchemaCardinality = 0;
	for (int32 ChannelIdx = 0; ChannelIdx != Channels.Num(); ++ChannelIdx)
	{
		if (UPoseSearchFeatureChannel* Channel = Channels[ChannelIdx].Get())
		{
			Channel->InitializeSchema(this);
		}
	}

	ResolveBoneReferences();
}

void UPoseSearchSchema::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Finalize();

	Super::PreSave(ObjectSaveContext);
}

void UPoseSearchSchema::PostLoad()
{
	Super::PostLoad();
	ResolveBoneReferences();
}

#if WITH_EDITOR
void UPoseSearchSchema::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Finalize(false);
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UPoseSearchSchema::ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData) const
{
	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel : Channels)
	{
		if (Channel)
		{
			Channel->ComputeCostBreakdowns(CostBreakDownData, this);
		}
	}
}
#endif