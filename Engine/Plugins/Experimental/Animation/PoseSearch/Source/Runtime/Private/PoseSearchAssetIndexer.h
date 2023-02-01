// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Animation/AnimationPoseData.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "PoseSearch/PoseSearchAssetIndexer.h"
#include "PoseSearch/PoseSearchIndex.h"

namespace UE::PoseSearch
{
struct FAssetIndexingContext;

class FAssetIndexer : public IAssetIndexer
{
public:

	struct FOutput
	{
		int32 FirstIndexedSample = 0;
		int32 LastIndexedSample = 0;
		int32 NumIndexedPoses = 0;

		TArray<float> FeatureVectorTable;
		TArray<FPoseSearchPoseMetadata> PoseMetadata;
		TBitArray<> AllFeaturesNotAdded;
	} Output;

	void Reset();
	void Init(const FAssetIndexingContext& IndexingContext, const FBoneContainer& InBoneContainer);
	bool Process();

public: // IAssetIndexer

	const FAssetIndexingContext& GetIndexingContext() const override { return IndexingContext; }
	FTransform GetTransform(float SampleTime, bool& Clamped, int8 SchemaBoneIdx = RootSchemaBoneIdx) override;
	FTransform GetComponentSpaceTransform(float SampleTime, bool& Clamped, int8 SchemaBoneIdx = RootSchemaBoneIdx) override;
	FTransform GetComponentSpaceTransform(float SampleTime, float OriginTime, bool& Clamped, int8 SchemaBoneIdx = RootSchemaBoneIdx) override;

private:
	struct CachedEntry
	{
		float SampleTime;
		bool Clamped;

		FTransform RootTransform;
		FCSPose<FCompactPose> ComponentSpacePose;
	};

	FSampleInfo GetSampleInfo(float SampleTime) const;
	FPoseSearchPoseMetadata GetMetadata(int32 SampleIdx) const;
	FTransform MirrorTransform(const FTransform& Transform) const;
	CachedEntry& GetEntry(float SampleTime);
	FTransform CalculateComponentSpaceTransform(CachedEntry& Entry, int8 SchemaBoneIdx);

	FBoneContainer BoneContainer;
	FAssetIndexingContext IndexingContext;
	TMap<float, CachedEntry> CachedEntries;
};

} // namespace UE::PoseSearch

#endif // WITH_EDITOR