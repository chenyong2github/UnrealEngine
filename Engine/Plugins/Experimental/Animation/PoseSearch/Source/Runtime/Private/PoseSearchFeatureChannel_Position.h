// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "BoneContainer.h"
#include "PoseSearchFeatureChannel_Position.generated.h"

UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Position Channel"), CollapseCategories)
class POSESEARCH_API UPoseSearchFeatureChannel_Position : public UPoseSearchFeatureChannel
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference Bone;

	UPROPERTY(EditAnywhere, Category = "Settings")
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, Category = "Settings")
	float SampleTimeOffset = 0.f;

	UPROPERTY(meta = (ExcludeFromHash))
	int8 SchemaBoneIdx = 0;

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ExcludeFromHash))
	int32 ColorPresetIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Settings")
	EInputQueryPose InputQueryPose = EInputQueryPose::UseContinuingPose;

	// if UseSampleTimeOffsetRootBone is true, this UPoseSearchFeatureChannel_Position will calculate the position of Bone from the pose SampleTimeOffset seconds away from the current time pose root bone
	// if false the calculated position of Bone will be in component space from the pose SampleTimeOffset seconds away
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseSampleTimeOffsetRootBone = true;

	// UPoseSearchFeatureChannel interface
	virtual void InitializeSchema(UPoseSearchSchema* Schema) override;
	virtual void FillWeights(TArray<float>& Weights) const override;
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const override;
};
