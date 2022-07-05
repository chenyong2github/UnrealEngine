// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"

#include "PoseSearchFeatureChannels.generated.h"

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Position
UCLASS(BlueprintType, EditInlineNew)
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

	UPROPERTY()
	int8 SchemaBoneIdx;

	// UPoseSearchFeatureChannel interface
	virtual void InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer) override;
	virtual void FillWeights(TArray<float>& Weights) const override;
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const override;
	virtual bool BuildQuery(FPoseSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const override;
};

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Pose
UCLASS(BlueprintType, EditInlineNew)
class POSESEARCH_API UPoseSearchFeatureChannel_Pose : public UPoseSearchFeatureChannel
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FPoseSearchBone> SampledBones;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<float> SampleTimes;

	UPROPERTY()
	TArray<int8> SchemaBoneIdx;

	// UObject interface
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	// UPoseSearchFeatureChannel interface
	virtual void InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer) override;
	virtual void FillWeights(TArray<float>& Weights) const override;
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual void ComputeMeanDeviations(const Eigen::MatrixXd& CenteredPoseMatrix, Eigen::VectorXd& MeanDeviations) const override;
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const override;
	virtual bool BuildQuery(FPoseSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const override;

protected:
	void AddPoseFeatures(UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector, const TArray<TArray<FVector2D>>& Phases) const;
	void CalculatePhases(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput, TArray<TArray<FVector2D>>& OutPhases) const;
};

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Trajectory
UCLASS(BlueprintType, EditInlineNew)
class POSESEARCH_API UPoseSearchFeatureChannel_Trajectory : public UPoseSearchFeatureChannel
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseLinearVelocities = true;

	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUsePositions = true;

	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseFacingDirections = false;

	UPROPERTY(EditAnywhere, Category="Settings")
	EPoseSearchFeatureDomain Domain = EPoseSearchFeatureDomain::Time;

	// @todo: temporary location for the channel bone weight to help the weights refactoring (later on it makes sense to have a weight per SampleOffsets, but right now for semplicity the traj will have just one weight)
	UPROPERTY(EditAnywhere, Category = "Settings")
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<float> SampleOffsets;

	// UObject interface
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	// UPoseSearchFeatureChannel interface
	virtual void InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer) override;
	virtual void FillWeights(TArray<float>& Weights) const override;
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual void ComputeMeanDeviations(const Eigen::MatrixXd& CenteredPoseMatrix, Eigen::VectorXd& MeanDeviations) const override;
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const override;
	virtual bool BuildQuery(FPoseSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const override;

protected:
	void IndexAssetPrivate(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector) const;
	float GetSampleTime(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SubsampleIdx, float SampleTime, float RootDistance) const;
};