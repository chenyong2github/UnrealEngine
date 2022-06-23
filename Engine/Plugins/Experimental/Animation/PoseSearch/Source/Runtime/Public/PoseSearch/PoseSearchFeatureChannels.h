// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"

#include "PoseSearchFeatureChannels.generated.h"

USTRUCT()
struct FPoseSearchPoseFeatureInfo
{
	GENERATED_BODY()

	UPROPERTY()
	int8 SchemaBoneIdx = 0;
};

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Pose
UCLASS(BlueprintType, EditInlineNew)
class POSESEARCH_API UPoseSearchFeatureChannel_Pose : public UPoseSearchFeatureChannel
{
	GENERATED_BODY()

public:

	virtual ~UPoseSearchFeatureChannel_Pose() {}

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FPoseSearchBone> SampledBones;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<float> SampleTimes;

	UPROPERTY()
	TArray<FPoseSearchPoseFeatureInfo> FeatureParams;

	// UObject interface
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	// UPoseSearchFeatureChannel interface
	virtual void InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer) override;
	virtual void FillWeights(TArray<float>& Weights) const override;
	virtual void IndexAsset(const UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual FFloatRange GetHorizonRange(EPoseSearchFeatureDomain Domain) const override;
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const override;
	virtual bool BuildQuery(
		FPoseSearchContext& SearchContext,
		FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, const UE::PoseSearch::FFeatureVectorReader& Reader) const override;

protected:
	virtual void AddPoseFeatures(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector, const TArray<TArray<FVector2D>>& Phases) const;
	void CalculatePhases(const UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput, TArray<TArray<FVector2D>>& OutPhases) const;

	enum { PositionCardinality = 3 };
	enum { RotationCardinality = 6 };
	enum { LinearVelocityCardinality = 3 };
	enum { PhaseCardinality = 2 };
};



//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Trajectory
UCLASS(BlueprintType, EditInlineNew)
class POSESEARCH_API UPoseSearchFeatureChannel_Trajectory : public UPoseSearchFeatureChannel
{
	GENERATED_BODY()

public:

	virtual ~UPoseSearchFeatureChannel_Trajectory() {}

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
	virtual void IndexAsset(const UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual FFloatRange GetHorizonRange(EPoseSearchFeatureDomain Domain) const override;
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const override;
	virtual bool BuildQuery(
		FPoseSearchContext& SearchContext,
		FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, const UE::PoseSearch::FFeatureVectorReader& Reader) const override;

protected:
	virtual void IndexTimeFeatures(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector) const;
	virtual void IndexDistanceFeatures(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector) const;

	enum { PositionCardinality = 3 };
	enum { LinearVelocityCardinality = 3 };
	enum { ForwardVectorCardinality = 3 };
};