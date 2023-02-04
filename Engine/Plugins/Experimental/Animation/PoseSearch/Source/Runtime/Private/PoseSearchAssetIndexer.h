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
		
		void Reset();
	};

	struct FStats
	{
		int32 NumAccumulatedSamples = 0;
		float AccumulatedSpeed = 0.f;
		float MaxSpeed = 0.f;
		float AccumulatedAcceleration = 0.f;
		float MaxAcceleration = 0.f;

		void Reset();
	};

	void Reset();
	void Init(const FAssetIndexingContext& IndexingContext, const FBoneContainer& InBoneContainer);
	bool Process();
	const FOutput& GetOutput() const { return Output; }
	const FStats& GetStats() const { return Stats; }

	// IAssetIndexer
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
	void ComputeStats();

	FBoneContainer BoneContainer;
	FAssetIndexingContext IndexingContext;
	TMap<float, CachedEntry> CachedEntries;
	FOutput Output;
	FStats Stats;
};

} // namespace UE::PoseSearch

#endif // WITH_EDITOR