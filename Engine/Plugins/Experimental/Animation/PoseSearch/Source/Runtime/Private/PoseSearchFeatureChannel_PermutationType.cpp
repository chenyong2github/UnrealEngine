// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_PermutationType.h"
#include "PoseSearch/PoseSearchAssetIndexer.h"
#include "PoseSearch/PoseSearchContext.h"

void UPoseSearchFeatureChannel_PermutationType::GetPermutationTimeOffsets(float DesiredPermutationTimeOffset, float &OutPermutationSampleTimeOffset, float& OutPermutationOriginTimeOffset) const
{
	switch (PermutationType)
	{
	case EPermutationType::UseOriginTime:
		OutPermutationSampleTimeOffset = 0.f;
		OutPermutationOriginTimeOffset = 0.f;
		break;
	case EPermutationType::UsePermutationTime:
		OutPermutationSampleTimeOffset = DesiredPermutationTimeOffset;
		OutPermutationOriginTimeOffset = DesiredPermutationTimeOffset;
		break;
	case EPermutationType::UseOriginToPermutationTime:
		OutPermutationSampleTimeOffset = DesiredPermutationTimeOffset;
		OutPermutationOriginTimeOffset = 0.f;
		break;
	default:
		OutPermutationSampleTimeOffset = 0.f;
		OutPermutationOriginTimeOffset = 0.f;
		break;
	}
}

void UPoseSearchFeatureChannel_PermutationType::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	float PermutationSampleTimeOffset = 0.f;
	float PermutationOriginTimeOffset = 0.f;
	GetPermutationTimeOffsets(SearchContext.DesiredPermutationTimeOffset, PermutationSampleTimeOffset, PermutationOriginTimeOffset);

	SearchContext.SetPermutationTimeOffsets(PermutationSampleTimeOffset, PermutationOriginTimeOffset);
	Super::BuildQuery(SearchContext, InOutQuery);
	SearchContext.ResetPermutationTimeOffsets();
}

#if WITH_EDITOR
void UPoseSearchFeatureChannel_PermutationType::IndexAsset(UE::PoseSearch::FAssetIndexer& Indexer) const
{
	float PermutationSampleTimeOffset = 0.f;
	float PermutationOriginTimeOffset = 0.f;
	GetPermutationTimeOffsets(Indexer.CalculatePermutationTimeOffset(), PermutationSampleTimeOffset, PermutationOriginTimeOffset);

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