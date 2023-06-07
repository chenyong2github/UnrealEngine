// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchResult.h"
#include "Animation/BlendSpace.h"
#include "InstancedStruct.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace UE::PoseSearch
{

//////////////////////////////////////////////////////////////////////////
// FFeatureVectorBuilder
void FFeatureVectorBuilder::Init(const UPoseSearchSchema* InSchema)
{
	check(InSchema && InSchema->IsValid());
	Schema = InSchema;
	Values.Reset();
	Values.SetNumZeroed(Schema->SchemaCardinality);
}

void FFeatureVectorBuilder::Reset()
{
	Schema = nullptr;
	Values.Reset();
}

//////////////////////////////////////////////////////////////////////////
// FSearchResult
void FSearchResult::Update(float NewAssetTime)
{
	if (!IsValid())
	{
		Reset();
	}
	else
	{
		const FSearchIndexAsset& SearchIndexAsset = Database->GetSearchIndex().GetAssetForPose(PoseIdx);
		const FInstancedStruct& DatabaseAsset = Database->GetAnimationAssetStruct(SearchIndexAsset);
		if (DatabaseAsset.GetPtr<FPoseSearchDatabaseSequence>() || DatabaseAsset.GetPtr<FPoseSearchDatabaseAnimComposite>())
		{
			if (Database->GetPoseIndicesAndLerpValueFromTime(NewAssetTime, SearchIndexAsset, PrevPoseIdx, PoseIdx, NextPoseIdx, LerpValue))
			{
				AssetTime = NewAssetTime;
			}
			else
			{
				Reset();
			}
		}
		else if (const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = DatabaseAsset.GetPtr<FPoseSearchDatabaseBlendSpace>())
		{
			TArray<FBlendSampleData> BlendSamples;
			int32 TriangulationIndex = 0;
			DatabaseBlendSpace->BlendSpace->GetSamplesFromBlendInput(SearchIndexAsset.BlendParameters, BlendSamples, TriangulationIndex, true);

			const float PlayLength = DatabaseBlendSpace->BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

			// Asset player time for blendspaces is normalized [0, 1] so we need to convert 
			// to a real time before we advance it
			check(NewAssetTime >= 0.f && NewAssetTime <= 1.f);
			const float RealTime = NewAssetTime * PlayLength;
			if (Database->GetPoseIndicesAndLerpValueFromTime(RealTime, SearchIndexAsset, PrevPoseIdx, PoseIdx, NextPoseIdx, LerpValue))
			{
				AssetTime = NewAssetTime;
			}
			else
			{
				Reset();
			}
		}
		else
		{
			Reset();
		}
	}
}

bool FSearchResult::IsValid() const
{
	return PoseIdx != INDEX_NONE && Database != nullptr;
}

void FSearchResult::Reset()
{
	PoseIdx = INDEX_NONE;
	Database = nullptr;
	AssetTime = 0.0f;
}

const FSearchIndexAsset* FSearchResult::GetSearchIndexAsset(bool bMandatory) const
{
	if (bMandatory)
	{
		check(IsValid());
	}
	else if (!IsValid())
	{
		return nullptr;
	}

	return &Database->GetSearchIndex().GetAssetForPose(PoseIdx);
}

} // namespace UE::PoseSearch
