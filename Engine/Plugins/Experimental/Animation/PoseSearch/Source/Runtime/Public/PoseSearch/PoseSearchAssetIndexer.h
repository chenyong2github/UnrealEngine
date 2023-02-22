// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "BonePose.h"
#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchSchema.h"

struct FPoseSearchPoseMetadata;

namespace UE::PoseSearch
{

struct FAssetSamplerBase;
struct FAssetSamplingContext;

/**
 * Inputs for asset indexing
 */
struct FAssetIndexingContext
{
	const FAssetSamplingContext* SamplingContext = nullptr;
	const UPoseSearchSchema* Schema = nullptr;
	TSharedPtr<FAssetSamplerBase> AssetSampler;
	bool bMirrored = false;
	FFloatInterval RequestedSamplingRange = FFloatInterval(0.0f, 0.0f);
};

class POSESEARCH_API FAssetIndexer
{
public:
	struct FOutput
	{
		int32 FirstIndexedSample = 0;
		int32 LastIndexedSample = 0;
		int32 NumIndexedPoses = 0;

		TArrayView<float> FeatureVectorTable;
		TArrayView<FPoseSearchPoseMetadata> PoseMetadata;
	};

	struct FStats
	{
		int32 NumAccumulatedSamples = 0;
		float AccumulatedSpeed = 0.f;
		float MaxSpeed = 0.f;
		float AccumulatedAcceleration = 0.f;
		float MaxAcceleration = 0.f;
	};

	void Reset();
	void Init(const FAssetIndexingContext& IndexingContext, const FBoneContainer& InBoneContainer);
	void Process(int32 AssetIdx);
	const FOutput& GetOutput() const { return Output; }
	FOutput& EditOutput() { return Output; }
	const FStats& GetStats() const { return Stats; }

	FQuat GetSampleRotation(float SampleTimeOffset, int32 SampleIdx, int8 SchemaSampleBoneIdx = RootSchemaBoneIdx, int8 SchemaOriginBoneIdx = RootSchemaBoneIdx);
	FVector GetSamplePosition(float SampleTimeOffset, int32 SampleIdx, int8 SchemaSampleBoneIdx = RootSchemaBoneIdx, int8 SchemaOriginBoneIdx = RootSchemaBoneIdx);
	FVector GetSampleVelocity(float SampleTimeOffset, int32 SampleIdx, int8 SchemaSampleBoneIdx = RootSchemaBoneIdx, int8 SchemaOriginBoneIdx = RootSchemaBoneIdx, bool bUseCharacterSpaceVelocities = true);

	int32 GetBeginSampleIdx() const { return Output.FirstIndexedSample; }
	int32 GetEndSampleIdx() const { return Output.LastIndexedSample + 1; }
	
	TArrayView<float> GetPoseVector(int32 SampleIdx, TArrayView<float> FeatureVectorTable) const;
	const UPoseSearchSchema* GetSchema() const;

private:
	int32 GetVectorIdx(int32 SampleIdx) const;
	FTransform GetTransform(float SampleTime, bool& bClamped, int8 SchemaBoneIdx = RootSchemaBoneIdx);
	FTransform GetComponentSpaceTransform(float SampleTime, bool& bClamped, int8 SchemaBoneIdx = RootSchemaBoneIdx);
	FVector GetSamplePositionInternal(float SampleTime, float OriginTime, bool& bClamped, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx);

	struct FSampleInfo
	{
		TSharedPtr<FAssetSamplerBase> Clip;
		FTransform RootTransform;
		float ClipTime = 0.f;
		bool bClamped = false;
	};

	struct CachedEntry
	{
		float SampleTime;
		bool bClamped;

		FTransform RootTransform;
		FCSPose<FCompactPose> ComponentSpacePose;
	};

	FSampleInfo GetSampleInfo(float SampleTime) const;
	FPoseSearchPoseMetadata GetMetadata(int32 SampleIdx, int32 AssetIdx) const;
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
