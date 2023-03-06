// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchSchema.h"
#include "AnimationRuntime.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchResult.h"
#include "UObject/ObjectSaveContext.h"

bool UPoseSearchSchema::IsValid() const
{
	return Skeleton != nullptr;
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

	for (const TObjectPtr<UPoseSearchFeatureChannel>& ChannelPtr : GetChannels())
	{
		ChannelPtr->BuildQuery(SearchContext, InOutQuery);
	}
}

FBoneIndexType UPoseSearchSchema::GetBoneIndexType(int8 SchemaBoneIdx) const
{
	return SchemaBoneIdx >= 0 && BoneReferences[SchemaBoneIdx].HasValidSetup() ? BoneReferences[SchemaBoneIdx].BoneIndex : RootBoneIndexType;
}

bool UPoseSearchSchema::IsRootBone(int8 SchemaBoneIdx) const
{
	return SchemaBoneIdx < 0 || !BoneReferences[SchemaBoneIdx].HasValidSetup();
}

void UPoseSearchSchema::Finalize()
{
	BoneReferences.Reset();

	SchemaCardinality = 0;

	FinalizedChannels.Reset();
	for (const TObjectPtr<UPoseSearchFeatureChannel>& ChannelPtr : Channels)
	{
		if (ChannelPtr)
		{
			FinalizedChannels.Add(ChannelPtr);
			ChannelPtr->Finalize(this);
		}
	}

	// AddDependentChannels can add channels to FinalizedChannels, so we need a while loop
	int32 ChannelIndex = 0;
	while (ChannelIndex < FinalizedChannels.Num())
	{
		FinalizedChannels[ChannelIndex]->AddDependentChannels(this);
		++ChannelIndex;
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
	Finalize();
}

int8 UPoseSearchSchema::AddBoneReference(const FBoneReference& BoneReference)
{
	const int32 SchemaBoneIdx = BoneReferences.AddUnique(BoneReference);
	check(SchemaBoneIdx >= 0 && SchemaBoneIdx < 128);
	return int8(SchemaBoneIdx);
}

USkeleton* UPoseSearchSchema::GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle)
{
	bInvalidSkeletonIsError = false;
	return Skeleton;
}

#if WITH_EDITOR
void UPoseSearchSchema::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Finalize();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif