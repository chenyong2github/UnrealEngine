// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace UE::PoseSearch
{
	
struct FAssetSamplingContext;
class IAssetSampler;

/**
 * Inputs for asset indexing
 */
struct FAssetIndexingContext
{
	const FAssetSamplingContext* SamplingContext = nullptr;
	const UPoseSearchSchema* Schema = nullptr;
	const IAssetSampler* AssetSampler = nullptr;
	bool bMirrored = false;
	FFloatInterval RequestedSamplingRange = FFloatInterval(0.0f, 0.0f);
	
	// Index this asset's data from BeginPoseIdx up to but not including EndPoseIdx
	int32 BeginSampleIdx = 0;
	int32 EndSampleIdx = 0;

	TArrayView<float> GetPoseVector(int32 VectorIdx, TArrayView<float> FeatureVectorTable) const
	{
		check(Schema);
		return MakeArrayView(&FeatureVectorTable[VectorIdx * Schema->SchemaCardinality], Schema->SchemaCardinality);
	}
};

class POSESEARCH_API IAssetIndexer
{
public:
	struct FSampleInfo
	{
		const IAssetSampler* Clip = nullptr;
		FTransform RootTransform;
		float ClipTime = 0.0f;
		bool bClamped = false;

		bool IsValid() const { return Clip != nullptr; }
	};

	virtual ~IAssetIndexer() {}

	virtual const FAssetIndexingContext& GetIndexingContext() const = 0;
	virtual FTransform GetTransform(float SampleTime, bool& Clamped, int8 SchemaBoneIdx = RootSchemaBoneIdx) = 0;
	virtual FTransform GetComponentSpaceTransform(float SampleTime, bool& Clamped, int8 SchemaBoneIdx = RootSchemaBoneIdx) = 0;
	virtual FTransform GetComponentSpaceTransform(float SampleTime, float OriginTime, bool& Clamped, int8 SchemaBoneIdx = RootSchemaBoneIdx) = 0;
};

} // namespace UE::PoseSearch
