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

	// reuse continuity pose database feature values if available
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseFeaturesFromContinuityPose = true;

	// if UseSampleTimeOffsetRootBone is true, this UPoseSearchFeatureChannel_Position will calculate the position of Bone from the pose SampleTimeOffset seconds away from the current time pose root bone
	// if false the calculated position of Bone will be in component space from the pose SampleTimeOffset seconds away
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseSampleTimeOffsetRootBone = true;

	// UPoseSearchFeatureChannel interface
	virtual void InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer) override;
	virtual void FillWeights(TArray<float>& Weights) const override;
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const override;
	virtual bool BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const override;
};

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Heading

UENUM(BlueprintType)
enum class HeadingAxis : uint8
{
	X,
	Y,
	Z,

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UCLASS(BlueprintType, EditInlineNew)
class POSESEARCH_API UPoseSearchFeatureChannel_Heading : public UPoseSearchFeatureChannel
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference Bone;

	UPROPERTY(EditAnywhere, Category = "Settings")
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, Category = "Settings")
	float SampleTimeOffset = 0.f;

	UPROPERTY(EditAnywhere, Category = "Settings")
	HeadingAxis HeadingAxis = HeadingAxis::X;	

	UPROPERTY()
	int8 SchemaBoneIdx;

	// reuse continuity pose database feature values if available
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseFeaturesFromContinuityPose = true;

	// if UseSampleTimeOffsetRootBone is true, this UPoseSearchFeatureChannel_Position will calculate the position of Bone from the pose SampleTimeOffset seconds away from the current time pose root bone
	// if false the calculated position of Bone will be in component space from the pose SampleTimeOffset seconds away
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseSampleTimeOffsetRootBone = true;

	// UPoseSearchFeatureChannel interface
	virtual void InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer) override;
	virtual void FillWeights(TArray<float>& Weights) const override;
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const override;
	virtual bool BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const override;

	FVector GetAxis(const FQuat& Rotation) const;
};

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Pose
UCLASS(BlueprintType, EditInlineNew)
class POSESEARCH_API UPoseSearchFeatureChannel_Pose : public UPoseSearchFeatureChannel
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FPoseSearchBone> SampledBones;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<float> SampleTimes;

	UPROPERTY()
	TArray<int8> SchemaBoneIdx;

	// reuse continuity pose database feature values if available
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseFeaturesFromContinuityPose = true;

	// UObject interface
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	// UPoseSearchFeatureChannel interface
	virtual void InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer) override;
	virtual void FillWeights(TArray<float>& Weights) const override;
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual void ComputeMeanDeviations(const Eigen::MatrixXd& CenteredPoseMatrix, Eigen::VectorXd& MeanDeviations) const override;
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const override;
	virtual bool BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const override;

protected:
	void AddPoseFeatures(UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector, const TArray<TArray<FVector2D>>& Phases) const;
	void CalculatePhases(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput, TArray<TArray<FVector2D>>& OutPhases) const;
};

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Trajectory

UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPoseSearchTrajectoryFlags : uint32
{
	Velocity = 1 << 0,
	Position = 1 << 1,
	FacingDirection = 1 << 3,
};
ENUM_CLASS_FLAGS(EPoseSearchTrajectoryFlags);
constexpr bool EnumHasAnyFlags(int32 Flags, EPoseSearchTrajectoryFlags Contains) { return (Flags & int32(Contains)) != 0; }
inline int32& operator|=(int32& Lhs, EPoseSearchTrajectoryFlags Rhs) { return Lhs |= int32(Rhs); }

USTRUCT()
struct POSESEARCH_API FPoseSearchTrajectorySample
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Config)
	float Offset = 0.f; // offset in time or distance depending on UPoseSearchFeatureChannel_Trajectory.Domain

	UPROPERTY(EditAnywhere, meta = (Bitmask, BitmaskEnum = "/Script/PoseSearch.EPoseSearchTrajectoryFlags"), Category = Config)
	int32 Flags = int32(EPoseSearchTrajectoryFlags::Position);

	UPROPERTY(EditAnywhere, Category = Config)
	float Weight = 1.f;
};

UCLASS(BlueprintType, EditInlineNew)
class POSESEARCH_API UPoseSearchFeatureChannel_Trajectory : public UPoseSearchFeatureChannel
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bUseLinearVelocities_DEPRECATED = true;

	UPROPERTY()
	bool bUsePositions_DEPRECATED = true;

	UPROPERTY()
	bool bUseFacingDirections_DEPRECATED = false;
#endif

	UPROPERTY(EditAnywhere, Category="Settings")
	EPoseSearchFeatureDomain Domain = EPoseSearchFeatureDomain::Time;

	UPROPERTY(EditAnywhere, Category = "Settings")
	float Weight = 1.f;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<float> SampleOffsets_DEPRECATED;
#endif
	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FPoseSearchTrajectorySample> Samples;

	// UObject interface
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	// UPoseSearchFeatureChannel interface
	virtual void InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer) override;
	virtual void FillWeights(TArray<float>& Weights) const override;
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual void ComputeMeanDeviations(const Eigen::MatrixXd& CenteredPoseMatrix, Eigen::VectorXd& MeanDeviations) const override;
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const override;
	virtual bool BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const override;

protected:
	void IndexAssetPrivate(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector) const;
	float GetSampleTime(const UE::PoseSearch::IAssetIndexer& Indexer, float Offset, float SampleTime, float RootDistance) const;
};