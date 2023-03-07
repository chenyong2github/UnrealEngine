// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchSchema.h"
#include "AnimationRuntime.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchResult.h"
#include "PoseSearchFeatureChannel_PermutationTime.h"
#include "UObject/ObjectSaveContext.h"

bool UPoseSearchSchema::IsValid() const
{
	return Skeleton != nullptr;
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
	if (SchemaBoneIdx < 0)
	{
		return RootBoneIndexType;
	}
	check(BoneReferences[SchemaBoneIdx].HasValidSetup());
	return BoneReferences[SchemaBoneIdx].BoneIndex;
}

bool UPoseSearchSchema::IsRootBone(int8 SchemaBoneIdx) const
{
	if (SchemaBoneIdx < 0)
	{
		return true;
	}

	check(BoneReferences[SchemaBoneIdx].HasValidSetup());
	return BoneReferences[SchemaBoneIdx].BoneIndex == int32(RootBoneIndexType);
}

int8 UPoseSearchSchema::AddBoneReference(const FBoneReference& BoneReference)
{
	int32 SchemaBoneIdx = 0;
	if (Skeleton)
	{
		bool bDefaultToRootBone = true;
		FBoneReference TempBoneReference = BoneReference;
		if (TempBoneReference.BoneName != NAME_None)
		{
			TempBoneReference.Initialize(Skeleton);
			if (!TempBoneReference.HasValidSetup())
			{
				UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchSchema::AddBoneReference: couldn't initialize FBoneReference '%s' with Skeleton '%s' in UPoseSearchSchema '%s'. Defaulting to root bone instead"),
					*TempBoneReference.BoneName.ToString(), *GetNameSafe(Skeleton), *GetNameSafe(this));
			}
			else
			{
				bDefaultToRootBone = false;
			}
		}

		if (bDefaultToRootBone)
		{
			TempBoneReference.BoneName = Skeleton->GetReferenceSkeleton().GetBoneName(int32(RootBoneIndexType));
			TempBoneReference.Initialize(Skeleton);
			check(TempBoneReference.HasValidSetup());
		}

		SchemaBoneIdx = BoneReferences.AddUnique(TempBoneReference);
		check(SchemaBoneIdx >= 0 && SchemaBoneIdx < 128);
	}
	else if (BoneReferences.IsEmpty())
	{
		BoneReferences.Add(FBoneReference());
	}
	return int8(SchemaBoneIdx);
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

	for (const TObjectPtr<UPoseSearchFeatureChannel>& ChannelPtr : FinalizedChannels)
	{
		check(ChannelPtr);
		if (ChannelPtr->GetPermutationTimeType() != EPermutationTimeType::UseSampleTime)
		{
			// there's at least one channel that uses UsePermutationTime or UseSampleToPermutationTime: we automatically add a UPoseSearchFeatureChannel_PermutationTime if not already in the schema
			UPoseSearchFeatureChannel_PermutationTime::FindOrAddToSchema(this);
			break;
		}
	}

	BoneIndicesWithParents.Reset();
	if (Skeleton)
	{
		// Initialize references to obtain bone indices and fill out bone index array
		for (FBoneReference& BoneRef : BoneReferences)
		{
			check(BoneRef.HasValidSetup());
			BoneIndicesWithParents.Add(BoneRef.BoneIndex);
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