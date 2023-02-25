// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_PermutationType.h"
#include "PoseSearch/PoseSearchAssetIndexer.h"

void UPoseSearchFeatureChannel_PermutationType::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	Super::BuildQuery(SearchContext, InOutQuery);
}

#if WITH_EDITOR
void UPoseSearchFeatureChannel_PermutationType::IndexAsset(UE::PoseSearch::FAssetIndexer& Indexer) const
{
	float PermutationSampleTimeOffset = 0.f;
	float PermutationOriginTimeOffset = 0.f;
	switch (PermutationType)
	{
	case EPermutationType::UseOriginTime:
		PermutationSampleTimeOffset = 0.f;
		PermutationOriginTimeOffset = 0.f;
		break;
	case EPermutationType::UsePermutationTime:
		PermutationSampleTimeOffset = Indexer.CalculatePermutationTimeOffset();
		PermutationOriginTimeOffset = PermutationSampleTimeOffset;
		break;
	case EPermutationType::UseOriginToPermutationTime:
		PermutationSampleTimeOffset = Indexer.CalculatePermutationTimeOffset();
		PermutationOriginTimeOffset = 0.f;
		break;
	}

	Indexer.SetPermutationTimeOffsets(PermutationSampleTimeOffset, PermutationOriginTimeOffset);
	Super::IndexAsset(Indexer);
	Indexer.ResetPermutationTimeOffsets();
}

FString UPoseSearchFeatureChannel_PermutationType::GetLabel() const
{
	TStringBuilder<256> Label;
	if (const UPoseSearchFeatureChannel* OuterChannel = Cast<UPoseSearchFeatureChannel>(GetOuter()))
	{
		Label.Append(OuterChannel->GetLabel());
		Label.Append(TEXT("_"));
	}
	Label.Append(TEXT("PermType"));
	return Label.ToString();
}
#endif // WITH_EDITOR