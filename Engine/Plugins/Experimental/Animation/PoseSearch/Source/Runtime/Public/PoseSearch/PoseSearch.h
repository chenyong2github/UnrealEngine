// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/RingBuffer.h"
#include "Engine/DataAsset.h"
#include "Modules/ModuleManager.h"
#include "Misc/EnumClassFlags.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimMetaData.h"
#include "Animation/AnimNodeMessages.h"
#include "Animation/AnimationAsset.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "AlphaBlend.h"
#include "BoneIndices.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"

#include "PoseSearch.generated.h"

class UAnimSequence;
struct FCompactPose;
struct FPoseContext;
struct FReferenceSkeleton;

namespace UE { namespace PoseSearch {

class FPoseHistory;

DECLARE_LOG_CATEGORY_EXTERN(LogPoseSearch, Log, All);

}}

UENUM()
enum class EPoseSearchFeatureType : int32
{
	Invalid			= -1,

	Position		= 0,	
	Rotation		= 1,
	LinearVelocity	= 2,
	AngularVelocity	= 3,

	Num
};

UENUM()
enum class EPoseSearchFeatureDomain : int32
{
	Invalid		= -1,

	Time		= 0,
	Distance	= 1,

	Num
};

/** Describes each feature of a vector, including data type, sampling options, and buffer offset. */
USTRUCT()
struct POSESEARCH_API FPoseSearchFeatureDesc
{
	GENERATED_BODY()

	static constexpr int32 TrajectoryBoneIndex = -1; 

	UPROPERTY()
	int32 SchemaBoneIdx;

	UPROPERTY()
	int32 SubsampleIdx;

	UPROPERTY()
	EPoseSearchFeatureType Type;

	UPROPERTY()
	EPoseSearchFeatureDomain Domain;

	// Set via FPoseSearchFeatureLayout::Init() and ignored by operator==
	UPROPERTY()
	int32 ValueOffset;

	bool operator==(const FPoseSearchFeatureDesc& Other) const;

	FPoseSearchFeatureDesc()
		: SchemaBoneIdx(MAX_int32)
		, SubsampleIdx(MAX_int32)
		, Type(EPoseSearchFeatureType::Invalid)
		, Domain(EPoseSearchFeatureDomain::Invalid)
		, ValueOffset(MAX_int32)
	{}
};


/**
* Explicit description of a pose feature vector.
* Determined by options set in a UPoseSearchSchema and owned by the schema.
* See UPoseSearchSchema::GenerateLayout().
*/
USTRUCT()
struct POSESEARCH_API FPoseSearchFeatureVectorLayout
{
	GENERATED_BODY()

	void Init();
	void Reset();

	UPROPERTY()
	TArray<FPoseSearchFeatureDesc> Features;

	UPROPERTY()
	int32 NumFloats = 0;

	bool IsValid(int32 MaxNumBones) const;

	bool EnumerateBy(int32 ChannelIdx, EPoseSearchFeatureType Type, int32& InOutFeatureIdx) const;
};

UENUM()
enum class EPoseSearchDataPreprocessor : int32
{
	None			= 0,
	Automatic		= 1,
	Normalize		= 2,
	Sphere			= 3 UMETA(Hidden),

	Invalid = -1 UMETA(Hidden)
};

/**
* Specifies the format of a pose search index. At runtime, queries are built according to the schema for searching.
*/
UCLASS(BlueprintType, Category = "Animation|Pose Search")
class POSESEARCH_API UPoseSearchSchema : public UDataAsset, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:

	static constexpr int32 DefaultSampleRate = 10;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TObjectPtr<USkeleton> Skeleton = nullptr;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "60"), Category = "Schema")
	int32 SampleRate = DefaultSampleRate;

	UPROPERTY(EditAnywhere, Category = "Schema")
	bool bUseBoneVelocities = true;

	UPROPERTY(EditAnywhere, Category = "Schema")
	bool bUseBonePositions = true;

	UPROPERTY(EditAnywhere, Category = "Schema")
	bool bUseTrajectoryVelocities = true;

	UPROPERTY(EditAnywhere, Category = "Schema")
	bool bUseTrajectoryPositions = true;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<FBoneReference> Bones;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<float> PoseSampleTimes;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<float> TrajectorySampleTimes;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<float> TrajectorySampleDistances;

	UPROPERTY(EditAnywhere, Category = "Schema")
	EPoseSearchDataPreprocessor DataPreprocessor = EPoseSearchDataPreprocessor::Automatic;

	UPROPERTY()
	EPoseSearchDataPreprocessor EffectiveDataPreprocessor = EPoseSearchDataPreprocessor::Invalid;

	UPROPERTY()
	float SamplingInterval = 1.0f / DefaultSampleRate;

	UPROPERTY()
	FPoseSearchFeatureVectorLayout Layout;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndices;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndicesWithParents;


	bool IsValid () const;

	int32 NumBones () const { return BoneIndices.Num(); }

	// Returns farthest future sample time >= 0.0f.
	// Returns a negative value when there are no future sample times.
	float GetTrajectoryFutureTimeHorizon () const;

	// Returns farthest past sample time <= 0.0f.
	// Returns a positive value when there are no past sample times.
	float GetTrajectoryPastTimeHorizon () const;

	// Returns farthest future sample distance >= 0.0f.
	// Returns a negative value when there are no future sample distances.
	float GetTrajectoryFutureDistanceHorizon () const;

	// Returns farthest path sample distance <= 0.0f.
	// Returns a positive value when there are no past sample distances.
	float GetTrajectoryPastDistanceHorizon () const;

	TArrayView<const float> GetChannelSampleOffsets (int32 ChannelIdx) const;

public: // UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostLoad() override;

public: // IBoneReferenceSkeletonProvider
	class USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError) override { bInvalidSkeletonIsError = false; return Skeleton; }

private:
	void GenerateLayout();
	void ResolveBoneReferences();
};

USTRUCT()
struct POSESEARCH_API FPoseSearchIndexPreprocessInfo
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NumDimensions = 0;

	UPROPERTY()
	TArray<float> TransformationMatrix;

	UPROPERTY()
	TArray<float> InverseTransformationMatrix;

	UPROPERTY()
	TArray<float> SampleMean;

	void Reset()
	{
		NumDimensions = 0;
		TransformationMatrix.Reset();
		InverseTransformationMatrix.Reset();
		SampleMean.Reset();
	}
};

/**
* A search index for animation poses. The structure of the search index is determined by its UPoseSearchSchema.
* May represent a single animation (see UPoseSearchSequenceMetaData) or a collection (see UPoseSearchDatabase).
*/
USTRUCT()
struct POSESEARCH_API FPoseSearchIndex
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NumPoses = 0;

	UPROPERTY()
	TArray<float> Values;

	UPROPERTY()
	TObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	UPROPERTY()
	FPoseSearchIndexPreprocessInfo PreprocessInfo;

	bool IsValid() const;

	TArrayView<const float> GetPoseValues(int32 PoseIdx) const;

	void Reset();

	void Normalize (TArrayView<float> PoseVector) const;
	void InverseNormalize (TArrayView<float> PoseVector) const;
};

USTRUCT()
struct FPoseSearchExtrapolationParameters
{
	GENERATED_BODY()

public:
	// If the angular root motion speed in degrees is below this value, it will be treated as zero.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float AngularSpeedThreshold = 1.0f;
	
	// If the root motion linear speed is below this value, it will be treated as zero.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float LinearSpeedThreshold = 1.0f;

	// Time from sequence start/end used to extrapolate the trajectory.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float SampleTime = 0.05f;
};

/** Animation metadata object for indexing a single animation. */
UCLASS(BlueprintType, Category = "Animation|Pose Search")
class POSESEARCH_API UPoseSearchSequenceMetaData : public UAnimMetaData
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category="Settings")
	TObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	UPROPERTY(EditAnywhere, Category="Settings")
	FFloatInterval SamplingRange = FFloatInterval(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category = "Settings")
	FPoseSearchExtrapolationParameters ExtrapolationParameters;

	UPROPERTY();
	FPoseSearchIndex SearchIndex;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

public: // UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchChannelHorizonParams
{
	GENERATED_BODY()

	// Total score contribution of all samples within this horizon, normalized with other horizons
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	// Whether to interpolate samples within this horizon
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Advanced")
	bool bInterpolate = false;

	// Horizon sample weights will be interpolated from InitialValue to 1.0 - InitialValue and then normalized
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Advanced", meta = (EditCondition = "bInterpolate", ClampMin="0.0", ClampMax="1.0"))
	float InitialValue = 0.1f;

	// Curve type for horizon interpolation 
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Advanced", meta = (EditCondition = "bInterpolate"))
	EAlphaBlendOption InterpolationMethod = EAlphaBlendOption::Linear;
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchChannelWeightParams
{
	GENERATED_BODY()

	// Contribution of this score component. Normalized with other channels.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float ChannelWeight = 1.0f;

	// History horizon params (for sample offsets <= 0)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelHorizonParams HistoryParams;

	// Prediction horizon params (for sample offsets > 0)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelHorizonParams PredictionParams;

	// Contribution of each type within this channel
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	TMap<EPoseSearchFeatureType, float> TypeWeights;

	FPoseSearchChannelWeightParams();
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchChannelDynamicWeightParams
{
	GENERATED_BODY()

	// Multiplier for the contribution of this score component. Final weight will be normalized with other channels after scaling.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float ChannelWeightScale = 1.0f;

	// Multiplier for history score contribution. Normalized with prediction weight after scaling.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float HistoryWeightScale = 1.0f;

	// Multiplier for prediction score contribution. Normalized with history weight after scaling.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float PredictionWeightScale = 1.0f;

	bool operator==(const FPoseSearchChannelDynamicWeightParams& Rhs) const
	{
		return (ChannelWeightScale == Rhs.ChannelWeightScale)
			&& (HistoryWeightScale == Rhs.HistoryWeightScale)
			&& (PredictionWeightScale == Rhs.PredictionWeightScale);
	}
	bool operator!=(const FPoseSearchChannelDynamicWeightParams& Rhs) const { return !(*this == Rhs); }
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchWeightParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelWeightParams PoseWeight;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelWeightParams TrajectoryWeight;
};

USTRUCT(BlueprintType, Category="Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDynamicWeightParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelDynamicWeightParams PoseDynamicWeights;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelDynamicWeightParams TrajectoryDynamicWeights;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	bool bDebugDisableWeights = false;

	bool operator==(const FPoseSearchDynamicWeightParams& Rhs) const
	{
		return (PoseDynamicWeights == Rhs.PoseDynamicWeights)
			&& (TrajectoryDynamicWeights == Rhs.TrajectoryDynamicWeights)
			&& (bDebugDisableWeights == Rhs.bDebugDisableWeights);
	}
	bool operator!=(const FPoseSearchDynamicWeightParams& Rhs) const { return !(*this == Rhs); }
};

USTRUCT()
struct POSESEARCH_API FPoseSearchWeights
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<float> Weights;

	bool IsInitialized() const { return !Weights.IsEmpty(); }
	void Init(const FPoseSearchWeightParams& WeightParams, const UPoseSearchSchema* Schema, const FPoseSearchDynamicWeightParams& RuntimeParams);
};

USTRUCT()
struct POSESEARCH_API FPoseSearchWeightsContext
{
	GENERATED_BODY()

public:
	// Check if the database or runtime weight parameters have changed and then computes and caches new group weights
	void Update(const FPoseSearchDynamicWeightParams& DynamicWeights, const UPoseSearchDatabase * Database);

	const FPoseSearchWeights* GetGroupWeights (int32 WeightsGroupIdx) const;
	
private:
	UPROPERTY(Transient)
	TWeakObjectPtr<const UPoseSearchDatabase> Database = nullptr;

	UPROPERTY(Transient)
	FPoseSearchDynamicWeightParams DynamicWeights;

	UPROPERTY(Transient)
	TArray<FPoseSearchWeights> ComputedGroupWeights;
};


/** An entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseSequence
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> Sequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	FFloatInterval SamplingRange = FFloatInterval(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopAnimation = false;

	// Used for sampling past pose information at the beginning of the main sequence.
	// This setting is intended for transitions between cycles. It is optional and only used
	// for one shot anims with past sampling. When past sampling is used without a lead in sequence,
	// the sampling range of the main sequence will be clamped if necessary.
	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> LeadInSequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopLeadInAnimation = false;

	// Used for sampling future pose information at the end of the main sequence.
	// This setting is intended for transitions between cycles. It is optional and only used
	// for one shot anims with future sampling. When future sampling is used without a follow up sequence,
	// the sampling range of the main sequence will be clamped if necessary.
	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> FollowUpSequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopFollowUpAnimation = false;

	UPROPERTY()
	int32 FirstPoseIdx = 0;

	UPROPERTY()
	int32 NumPoses = 0;
};

/** A data asset for indexing a collection of animation sequences. */
UCLASS(BlueprintType, Category = "Animation|Pose Search")
class POSESEARCH_API UPoseSearchDatabase : public UDataAsset
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Database")
	const UPoseSearchSchema* Schema;

	UPROPERTY(EditAnywhere, Category = "Database")
	FPoseSearchWeightParams Weights;

	UPROPERTY(EditAnywhere, Category = "Database")
	FPoseSearchExtrapolationParameters ExtrapolationParameters;

	// Drag and drop animations here to add them in bulk to Sequences
	UPROPERTY(EditAnywhere, Category = "Database", DisplayName="Drag And Drop Anims Here")
	TArray<TObjectPtr<UAnimSequence>> SimpleSequences;

	UPROPERTY(EditAnywhere, Category="Database")
	TArray<FPoseSearchDatabaseSequence> Sequences;

	UPROPERTY()
	FPoseSearchIndex SearchIndex;

	int32 FindSequenceForPose(int32 PoseIdx) const;
	int32 GetPoseIndexFromAssetTime(int32 DbSequenceIdx, float AssetTime) const;
	float GetSequenceLength(int32 DbSequenceIdx) const;
	bool DoesSequenceLoop(int32 DbSequenceIdx) const;
	FFloatInterval GetEffectiveSamplingRange(int32 DbSequenceIdx) const;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

public: // UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void CollectSimpleSequences();
};


/** 
* Helper object for writing features into a float buffer according to a feature vector layout.
* Keeps track of which features are present, allowing the feature vector to be built up piecemeal.
* FFeatureVectorBuilder is used to build search queries at runtime and for adding samples during search index construction.
*/

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchFeatureVectorBuilder
{
	GENERATED_BODY()
public:
	void Init(const UPoseSearchSchema* Schema);
	void Reset();
	void ResetFeatures();

	const UPoseSearchSchema* GetSchema() const { return Schema.Get(); }

	TArrayView<const float> GetValues() const { return Values; }
	TArrayView<const float> GetNormalizedValues() const { return ValuesNormalized; }

	void SetTransform(FPoseSearchFeatureDesc Feature, const FTransform& Transform);
	void SetTransformVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetPosition(FPoseSearchFeatureDesc Feature, const FVector& Translation);
	void SetRotation(FPoseSearchFeatureDesc Feature, const FQuat& Rotation);
	void SetLinearVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetAngularVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetVector(FPoseSearchFeatureDesc Feature, const FVector& Vector);
	void BuildFromTrajectory(const FTrajectorySampleRange& Trajectory);
	bool TrySetPoseFeatures(UE::PoseSearch::FPoseHistory* History);

	void CopyFromSearchIndex(const FPoseSearchIndex& SearchIndex, int32 PoseIdx);
	void CopyFeature(const FPoseSearchFeatureVectorBuilder& OtherBuilder, int32 FeatureIdx);

	void MergeReplace(const FPoseSearchFeatureVectorBuilder& OtherBuilder);

	bool IsInitialized() const;
	bool IsInitializedForSchema(const UPoseSearchSchema* Schema) const;
	bool IsComplete() const;
	bool IsCompatible(const FPoseSearchFeatureVectorBuilder& OtherBuilder) const;

	void Normalize(const FPoseSearchIndex& ForSearchIndex);

private:
	void BuildFromTrajectoryTimeBased(const FTrajectorySampleRange& Trajectory);
	void BuildFromTrajectoryDistanceBased(const FTrajectorySampleRange& Trajectory);

	UPROPERTY(Transient)
	TWeakObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	TArray<float> Values;
	TArray<float> ValuesNormalized;
	TBitArray<> FeaturesAdded;
	int32 NumFeaturesAdded = 0;
};


namespace UE { namespace PoseSearch {

/** Records poses over time in a ring buffer. FFeatureVectorBuilder uses this to sample from the present or past poses according to the search schema. */
// @@@Rename to FPoseQueryContext?
class POSESEARCH_API FPoseHistory
{
public:
	void Init(int32 InNumPoses, float InTimeHorizon);
	void Init(const FPoseHistory& History);
	bool TrySamplePose(float SecondsAgo, const FReferenceSkeleton& RefSkeleton, const TArray<FBoneIndexType>& RequiredBones);

	void Update(float SecondsElapsed, const FPoseContext& PoseContext);
	float GetSampleTimeInterval() const;
	TArrayView<const FTransform> GetLocalPoseSample() const { return SampledLocalPose; }
	TArrayView<const FTransform> GetComponentPoseSample() const { return SampledComponentPose; }
	TArrayView<const FTransform> GetPrevLocalPoseSample() const { return SampledPrevLocalPose; }
	TArrayView<const FTransform> GetPrevComponentPoseSample() const { return SampledPrevComponentPose; }
	float GetTimeHorizon() const { return TimeHorizon; }
	FPoseSearchFeatureVectorBuilder& GetQueryBuilder() { return QueryBuilder; }

private:
	bool TrySampleLocalPose(float Time, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& LocalPose);

	struct FPose
	{
		TArray<FTransform> LocalTransforms;
	};

	TRingBuffer<FPose> Poses;
	TRingBuffer<float> Knots;
	TArray<FTransform> SampledLocalPose;
	TArray<FTransform> SampledComponentPose;
	TArray<FTransform> SampledPrevLocalPose;
	TArray<FTransform> SampledPrevComponentPose;
	FPoseSearchFeatureVectorBuilder QueryBuilder;

	float TimeHorizon = 0.0f;
};

class IPoseHistoryProvider : public UE::Anim::IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE(IPoseHistoryProvider);

public:

	virtual const FPoseHistory& GetPoseHistory() const = 0;
	virtual FPoseHistory& GetPoseHistory() = 0;
};


/** Helper object for extracting features from a float buffer according to the feature vector layout. */
class POSESEARCH_API FFeatureVectorReader
{
public:
	void Init(const FPoseSearchFeatureVectorLayout* Layout);
	void SetValues(TArrayView<const float> Values);
	bool IsValid() const;

	bool GetTransform(FPoseSearchFeatureDesc Feature, FTransform* OutTransform) const;
	bool GetPosition(FPoseSearchFeatureDesc Feature, FVector* OutPosition) const;
	bool GetRotation(FPoseSearchFeatureDesc Feature, FQuat* OutRotation) const;
	bool GetLinearVelocity(FPoseSearchFeatureDesc Feature, FVector* OutLinearVelocity) const;
	bool GetAngularVelocity(FPoseSearchFeatureDesc Feature, FVector* OutAngularVelocity) const;
	bool GetVector(FPoseSearchFeatureDesc Feature, FVector* OutVector) const;

	const FPoseSearchFeatureVectorLayout* GetLayout() const { return Layout; }

private:
	const FPoseSearchFeatureVectorLayout* Layout = nullptr;
	TArrayView<const float> Values;
};


//////////////////////////////////////////////////////////////////////////
// Main PoseSearch API

enum class EDebugDrawFlags : uint32
{
	None			    = 0,

	/** Draw the entire search index */
	DrawSearchIndex     = 1 << 0,

	/** Draw pose features for each pose vector */
	IncludePose         = 1 << 1,

	/** Draw trajectory features for each pose vector */
	IncludeTrajectory   = 1 << 2,

	/** Draw all pose vector features */
	IncludeAllFeatures  = IncludePose | IncludeTrajectory,

	Persistent = 1 << 3,
};
ENUM_CLASS_FLAGS(EDebugDrawFlags);

struct POSESEARCH_API FDebugDrawParams
{
	const UWorld* World = nullptr;
	const UPoseSearchDatabase* Database = nullptr;
	const UPoseSearchSequenceMetaData* SequenceMetaData = nullptr;
	EDebugDrawFlags Flags = EDebugDrawFlags::IncludeAllFeatures;

	float DefaultLifeTime = 5.0f;
	float PointSize = 1.0f;

	FTransform RootTransform = FTransform::Identity;

	/** If set, draw the corresponding pose from the search index */
	int32 PoseIdx = INDEX_NONE;

	/** If set, draw using this uniform color instead of feature-based coloring */
	const FLinearColor* Color = nullptr;

	/** If set, interpret the buffer as a pose vector and draw it */
	TArrayView<const float> PoseVector;

	bool CanDraw() const;
	const FPoseSearchIndex* GetSearchIndex() const;
	const UPoseSearchSchema* GetSchema() const;
};

struct FSearchResult
{
	int32 PoseIdx = -1;
	float TimeOffsetSeconds = 0.0f;
	float Dissimilarity = MAX_flt;

	bool IsValid() const { return PoseIdx >= 0; }
};

struct FDbSearchResult : public FSearchResult
{
	FDbSearchResult() = default;

	FDbSearchResult(const FSearchResult& Result)
		: FSearchResult(Result)
	{}

	int32 DbSequenceIdx = INDEX_NONE;
};


/**
* Visualize pose search debug information
*
* @param DrawParams		Visualization options
*/
POSESEARCH_API void Draw(const FDebugDrawParams& DrawParams);


/**
* Creates a pose search index for an animation sequence
* 
* @param Sequence			The input sequence create a search index for
* @param SequenceMetaData	The input sequence indexing info and output search index
* 
* @return Whether the index was built successfully
*/
POSESEARCH_API bool BuildIndex(const UAnimSequence* Sequence, UPoseSearchSequenceMetaData* SequenceMetaData);


/**
* Creates a pose search index for a collection of animations
* 
* @param Database	The input collection of animations and output search index
* 
* @return Whether the index was built successfully
*/
POSESEARCH_API bool BuildIndex(UPoseSearchDatabase* Database);


/**
* Performs a pose search on a sequence's UPoseSearchSequenceMetaData.
*
* @param Sequence		The sequence to search within. Must contain a UPoseSearchSequenceMetaData object.
* @param Query			The pose query to search for. To build a query, see FFeatureVectorBuilder. Must have been built using the same schema as the UPoseSearchSequenceMetaData.
* @param WeightsContext	Optional weights context used to influence pose search query results
* @param DrawParams		Visualization options
* 
* @return The pose in the sequence that most closely matches the Query.
*/
POSESEARCH_API FSearchResult Search(const UAnimSequenceBase* Sequence, TArrayView<const float> Query, FDebugDrawParams DrawParams = FDebugDrawParams());


/**
* Performs a pose search on a UPoseSearchDatabase.
*
* @param Database			The database to search within
* @param Query				The pose query to search for. To build a query, see FFeatureVectorBuilder. Must have been built using the same schema as the Database.
* @param WeightsContext		Optional weights context used to influence pose search query results
* @param EndTimeToExclude	Samples this close to the end of an animation sequence will be ignored by the search. 
* @param DrawParams			Visualization options
* 
* @return The pose in the database that most closely matches the Query.
*/
POSESEARCH_API FDbSearchResult Search(const UPoseSearchDatabase* Database, TArrayView<const float> Query, const FPoseSearchWeightsContext* WeightsContext = nullptr, const float EndTimeToExclude = 0.0f, FDebugDrawParams DrawParams = FDebugDrawParams());


/**
* Evaluate pose comparison metric between a pose in the search index and an input query
* 
* @param SearchIndex	The search index containing the pose to compare to the query
* @param PoseIdx		The index of the pose in the search index to compare to the query
* @param Query			The query to compare against. Must have been built using the same schema as the SearchIndex.
* @param WeightsContext	Optional weights context used to influence pose comparison
* 
* @return Dissimilarity between the two poses
*/
POSESEARCH_API float ComparePoses(const FPoseSearchIndex& SearchIndex, int32 PoseIdx, TArrayView<const float> Query, const FPoseSearchWeightsContext* WeightsContext = nullptr);

}} // namespace UE::PoseSearch