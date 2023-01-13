// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "BoneContainer.h"
#include "UObject/ObjectSaveContext.h"
#include "PoseSearchFeatureChannel_Trajectory.generated.h"

UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPoseSearchTrajectoryFlags : uint32
{
	Velocity = 1 << 0,
	Position = 1 << 1,
	VelocityDirection = 1 << 2,
	FacingDirection = 1 << 3,
	VelocityXY = 1 << 4,
	PositionXY = 1 << 5,
	VelocityDirectionXY = 1 << 6,
	FacingDirectionXY = 1 << 7,
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

	UPROPERTY(EditAnywhere, Category = Config, meta = (ExcludeFromHash))
	int32 ColorPresetIndex = 0;
};

UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Trajectory Channel"), CollapseCategories)
class POSESEARCH_API UPoseSearchFeatureChannel_Trajectory : public UPoseSearchFeatureChannel
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FPoseSearchTrajectorySample> Samples;

	// UObject interface
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	// UPoseSearchFeatureChannel interface
	virtual void InitializeSchema(UPoseSearchSchema* Schema) override;
	virtual void FillWeights(TArray<float>& Weights) const override;
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const override;
	virtual void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const override;
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const override;

#if WITH_EDITOR
	virtual void PopulateChannelLayoutSet(UE::PoseSearch::FFeatureChannelLayoutSet& FeatureChannelLayoutSet) const override;
	virtual void ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData, const UPoseSearchSchema* Schema) const override;
#endif

	bool GetEstimatedSpeedRatio(TConstArrayView<float> QueryVector, TConstArrayView<float> PoseVector, float& EstimatedSpeedRatio) const;

protected:
	void IndexAssetPrivate(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, TArrayView<float> FeatureVector) const;
};

