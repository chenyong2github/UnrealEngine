// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Animation/Skeleton.h"

//////////////////////////////////////////////////////////////////////////
// class ICostBreakDownData
namespace UE::PoseSearch
{

static inline float ArraySum(TConstArrayView<float> View, int32 StartIndex, int32 Offset)
{
	float Sum = 0.f;
	const int32 EndIndex = StartIndex + Offset;
	for (int i = StartIndex; i < EndIndex; ++i)
	{
		Sum += View[i];
	}
	return Sum;
}

void ICostBreakDownData::AddEntireBreakDownSection(const FText& Label, const UPoseSearchSchema* Schema, int32 DataOffset, int32 Cardinality)
{
	BeginBreakDownSection(Label);

	const int32 Count = Num();
	for (int32 i = 0; i < Count; ++i)
	{
		if (IsCostVectorFromSchema(i, Schema))
		{
			const float CostBreakdown = ArraySum(GetCostVector(i, Schema), DataOffset, Cardinality);
			SetCostBreakDown(CostBreakdown, i, Schema);
		}
	}

	EndBreakDownSection(Label);
}

} // namespace UE::PoseSearch

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel
#if WITH_EDITOR
void UPoseSearchFeatureChannel::PopulateChannelLayoutSet(UE::PoseSearch::FFeatureChannelLayoutSet& FeatureChannelLayoutSet) const
{
	FeatureChannelLayoutSet.Add(GetName(), UE::PoseSearch::FKeyBuilder(this).Finalize(), ChannelDataOffset, ChannelCardinality);
}

void UPoseSearchFeatureChannel::ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData, const UPoseSearchSchema* Schema) const
{
	CostBreakDownData.AddEntireBreakDownSection(FText::FromString(GetName()), Schema, ChannelDataOffset, ChannelCardinality);
}
#endif // WITH_EDITOR

class USkeleton* UPoseSearchFeatureChannel::GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle)
{
	bInvalidSkeletonIsError = false;

	UObject* Outer = GetOuter();
	while (Outer != nullptr)
	{
		if (const UPoseSearchSchema* Schema = Cast<UPoseSearchSchema>(Outer))
		{
			return Schema->Skeleton;
		}
		Outer = Outer->GetOuter();
	}
	return nullptr;
}