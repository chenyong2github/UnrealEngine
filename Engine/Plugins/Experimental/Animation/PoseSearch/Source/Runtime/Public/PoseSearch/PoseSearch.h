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
#include "Animation/AnimSequence.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "AlphaBlend.h"
#include "BoneIndices.h"
#include "GameplayTagContainer.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "PoseSearch/KDTree.h"
#include "ObjectTrace.h"
#include "PoseSearch.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPoseSearch, Log, All);

// Enable this if object tracing is enabled, mimics animation tracing
#define UE_POSE_SEARCH_TRACE_ENABLED OBJECT_TRACE_ENABLED

//////////////////////////////////////////////////////////////////////////
// Forward declarations

class UAnimSequence;
class UBlendSpace;
struct FCompactPose;
struct FPoseContext;
struct FReferenceSkeleton;
struct FPoseSearchDatabaseDerivedData;
class UAnimNotifyState_PoseSearchBase;
class UPoseSearchSchema;
class FBlake3;

namespace UE::PoseSearch
{
class FPoseHistory;
struct FPoseSearchDatabaseAsyncCacheTask;
struct FDebugDrawParams;
struct FSchemaInitializer;
struct FQueryBuildingContext;
struct FSearchContext;

} // namespace UE::PoseSearch

namespace Eigen {
	template<typename _Scalar, int _Rows, int _Cols, int _Options, int _MaxRows, int _MaxCols>
	class Matrix;
	using MatrixXd = Matrix<double, -1, -1, 0, -1, -1>;
	using VectorXd = Matrix<double, -1, 1, 0, -1, 1>;
}

//////////////////////////////////////////////////////////////////////////
// Constants

// @todo: move this enum in PoseSearchFeatureChannel.h since only used by UPoseSearchFeatureChannel_Trajectory
UENUM()
enum class EPoseSearchFeatureDomain : int32
{
	Time,
	Distance,

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchBooleanRequest : uint8
{
	FalseValue,
	TrueValue,
	Indifferent, // if this is used, there will be no cost difference between true and false results

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchMode : int32
{
	BruteForce,
	PCAKDTree,
	PCAKDTree_Validate,	// runs PCAKDTree and performs validation tests
	PCAKDTree_Compare,	// compares BruteForce vs PCAKDTree

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchDataPreprocessor : int32
{
	None,
	Automatic,
	Normalize,
	Sphere UMETA(Hidden),

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchPoseFlags : uint32
{
	None = 0,

	// Don't return this pose as a search result
	BlockTransition = 1 << 0,
};
ENUM_CLASS_FLAGS(EPoseSearchPoseFlags);

UENUM()
enum class ESearchIndexAssetType : int32
{
	Invalid,
	Sequence,
	BlendSpace,
};

namespace UE::PoseSearch {
	
enum class EPoseComparisonFlags : int32
{
	None = 0,
	ContinuingPose = 1 << 0,
};
ENUM_CLASS_FLAGS(EPoseComparisonFlags);

} // namespace UE::PoseSearch

UENUM()
enum class EPoseSearchMirrorOption : int32
{
	UnmirroredOnly UMETA(DisplayName = "Original Only"),
	MirroredOnly UMETA(DisplayName = "Mirrored Only"),
	UnmirroredAndMirrored UMETA(DisplayName = "Original and Mirrored"),

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};



//////////////////////////////////////////////////////////////////////////
// Common structs

USTRUCT()
struct POSESEARCH_API FPoseSearchExtrapolationParameters
{
	GENERATED_BODY()

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

USTRUCT()
struct FPoseSearchBlockTransitionParameters
{
	GENERATED_BODY()

	// Excluding the beginning of sequences can help ensure an exact past trajectory is used when building the features
	UPROPERTY(EditAnywhere, Category = "Settings")
	float SequenceStartInterval = 0.0f;

	// Excluding the end of sequences help ensure an exact future trajectory, and also prevents the selection of
	// a sequence which will end too soon to be worth selecting.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float SequenceEndInterval = 0.0f;
};

// @todo: move it into PoseSearchFeatureChannels after removing SampledBones_DEPRECATED 
UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPoseSearchBoneFlags : uint32
{
	Velocity = 1 << 0,
	Position = 1 << 1,
	Rotation = 1 << 2,
	Phase = 1 << 3,
};
ENUM_CLASS_FLAGS(EPoseSearchBoneFlags);
constexpr bool EnumHasAnyFlags(int32 Flags, EPoseSearchBoneFlags Contains) { return (Flags & int32(Contains)) != 0; }
inline int32& operator|=(int32& Lhs, EPoseSearchBoneFlags Rhs) { return Lhs |= int32(Rhs); }

// @todo: move it into PoseSearchFeatureChannels after removing SampledBones_DEPRECATED 
USTRUCT()
struct POSESEARCH_API FPoseSearchBone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Config)
	FBoneReference Reference;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bUseVelocity_DEPRECATED = false;

	UPROPERTY()
	bool bUsePosition_DEPRECATED = false;

	UPROPERTY()
	bool bUseRotation_DEPRECATED = false;

	UPROPERTY()
	bool bUsePhase_DEPRECATED = false;
#endif

	UPROPERTY(EditAnywhere, meta = (Bitmask, BitmaskEnum = "/Script/PoseSearch.EPoseSearchBoneFlags"), Category = Config)
	int32 Flags = int32(EPoseSearchBoneFlags::Position);

	// @todo: temporary location for the channel bone weight to help the weights refactoring
	UPROPERTY(EditAnywhere, Category = Config)
	float Weight = 1.f;
};

//////////////////////////////////////////////////////////////////////////
// Asset sampling and indexing

namespace UE::PoseSearch {

struct POSESEARCH_API FAssetSamplingContext
{
	// Time delta used for computing pose derivatives
	static constexpr float FiniteDelta = 1 / 60.0f;

	FBoneContainer BoneContainer;

	// Mirror data table pointer copied from Schema for convenience
	TObjectPtr<UMirrorDataTable> MirrorDataTable = nullptr;

	// Compact pose format of Mirror Bone Map
	TCustomBoneIndexArray<FCompactPoseBoneIndex, FCompactPoseBoneIndex> CompactPoseMirrorBones;

	// Pre-calculated component space rotations of reference pose, which allows mirror to work with any joint orientation
	// Only initialized and used when a mirroring table is specified
	TCustomBoneIndexArray<FQuat, FCompactPoseBoneIndex> ComponentSpaceRefRotations;

	void Init(const UPoseSearchSchema* Schema);
	FTransform MirrorTransform(const FTransform& Transform) const;
};

/**
 * Helper interface for sampling data from animation assets
 */
class POSESEARCH_API IAssetSampler
{
public:
	virtual ~IAssetSampler() {};

	virtual float GetPlayLength() const = 0;
	virtual bool IsLoopable() const = 0;

	// Gets the time associated with a particular root distance traveled
	virtual float GetTimeFromRootDistance(float Distance) const = 0;

	// Gets the total root distance traveled 
	virtual float GetTotalRootDistance() const = 0;

	// Gets the final root transformation at the end of the asset's playback time
	virtual FTransform GetTotalRootTransform() const = 0;

	// Extracts pose for this asset for a given context
	virtual void ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const = 0;

	// Extracts the accumulated root distance at the given time, using the extremities of the sequence to extrapolate 
	// beyond the sequence limits when Time is less than zero or greater than the sequence length
	virtual float ExtractRootDistance(float Time) const = 0;

	// Extracts root transform at the given time, using the extremities of the sequence to extrapolate beyond the 
	// sequence limits when Time is less than zero or greater than the sequence length.
	virtual FTransform ExtractRootTransform(float Time) const = 0;

	// Extracts notify states inheriting from UAnimNotifyState_PoseSearchBase present in the sequence at Time.
	// The function does not empty NotifyStates before adding new notifies!
	virtual void ExtractPoseSearchNotifyStates(float Time, TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const = 0;

	virtual const UAnimationAsset* GetAsset() const = 0;
};

/**
 * Inputs for asset indexing
 */
struct FAssetIndexingContext
{
	const FAssetSamplingContext* SamplingContext = nullptr;
	const UPoseSearchSchema* Schema = nullptr;
	const IAssetSampler* MainSampler = nullptr;
	const IAssetSampler* LeadInSampler = nullptr;
	const IAssetSampler* FollowUpSampler = nullptr;
	bool bMirrored = false;
	FFloatInterval RequestedSamplingRange = FFloatInterval(0.0f, 0.0f);
	FPoseSearchBlockTransitionParameters BlockTransitionParameters;

	// Index this asset's data from BeginPoseIdx up to but not including EndPoseIdx
	int32 BeginSampleIdx = 0;
	int32 EndSampleIdx = 0;
};

/**
 * Output of indexer data for this asset
 */
struct FAssetIndexingOutput
{
	// Channel data should be written to this array of feature vector builders
	// Size is EndPoseIdx - BeginPoseIdx and PoseVectors[0] contains data for BeginPoseIdx
	const TArrayView<FPoseSearchFeatureVectorBuilder> PoseVectors;
};

class POSESEARCH_API IAssetIndexer
{
public:
	struct FSampleInfo
	{
		const IAssetSampler* Clip = nullptr;
		FTransform RootTransform;
		float ClipTime = 0.0f;
		float RootDistance = 0.0f;
		bool bClamped = false;

		bool IsValid() const { return Clip != nullptr; }
	};

	virtual ~IAssetIndexer() {}

	virtual const FAssetIndexingContext& GetIndexingContext() const = 0;
	virtual FSampleInfo GetSampleInfo(float SampleTime) const = 0;
	virtual FSampleInfo GetSampleInfoRelative(float SampleTime, const FSampleInfo& Origin) const = 0;
	virtual const float GetSampleTimeFromDistance(float Distance) const = 0;
	virtual FTransform MirrorTransform(const FTransform& Transform) const = 0;
	virtual FTransform GetTransformAndCacheResults(float SampleTime, float OriginTime, int8 SchemaBoneIdx, bool& Clamped) = 0;
};

} // namespace UE::PoseSearch


//////////////////////////////////////////////////////////////////////////
// Feature channels interface

UCLASS(Abstract, BlueprintType, EditInlineNew)
class POSESEARCH_API UPoseSearchFeatureChannel : public UObject, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:
	int32 GetChannelIndex() const { checkSlow(ChannelIdx >= 0); return ChannelIdx; }
	int32 GetChannelCardinality() const { checkSlow(ChannelCardinality >= 0); return ChannelCardinality; }
	int32 GetChannelDataOffset() const { checkSlow(ChannelDataOffset >= 0); return ChannelDataOffset; }

	// Called during UPoseSearchSchema::Finalize to prepare the schema for this channel
	virtual void InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer);
	
	virtual void FillWeights(TArray<float>& Weights) const PURE_VIRTUAL(UPoseSearchFeatureChannel::FillWeights, );

	// Called at database build time to populate pose vectors with this channel's data
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const PURE_VIRTUAL(UPoseSearchFeatureChannel::IndexAsset, );

	virtual void ComputeMeanDeviations(const Eigen::MatrixXd& CenteredPoseMatrix, Eigen::VectorXd& MeanDeviations) const;

	// Hash channel properties to produce a key for database derived data
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const PURE_VIRTUAL(UPoseSearchFeatureChannel::GenerateDDCKey, );

	// Called at runtime to add this channel's data to the query pose vector
	virtual bool BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const PURE_VIRTUAL(UPoseSearchFeatureChannel::BuildQuery, return false;);

	// Draw this channel's data for the given pose vector
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const PURE_VIRTUAL(UPoseSearchFeatureChannel::DebugDraw, );

private:
	// IBoneReferenceSkeletonProvider interface
	// Note this function is exclusively for FBoneReference details customization
	class USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

	friend class ::UPoseSearchSchema;

	UPROPERTY()
	int32 ChannelIdx = -1;

protected:
	UPROPERTY()
	int32 ChannelDataOffset = -1;

	UPROPERTY()
	int32 ChannelCardinality = -1;
};



//////////////////////////////////////////////////////////////////////////
// Schema

namespace UE::PoseSearch {

struct POSESEARCH_API FSchemaInitializer
{
public:
	int32 AddBoneReference(const FBoneReference& BoneReference);

	// Gets the index into the schema's channel array for the channel currently being initialized
	int32 GetCurrentChannelIdx() const { return CurrentChannelIdx; }

	int32 GetCurrentChannelDataOffset() const { return CurrentChannelDataOffset; }
	void SetCurrentChannelDataOffset(int32 DataOffset) { CurrentChannelDataOffset = DataOffset; }

private:
	friend class ::UPoseSearchSchema;

	int32 CurrentChannelIdx = 0;
	int32 CurrentChannelDataOffset = 0;
	TArray<FBoneReference> BoneReferences;
};

} // namespace UE::PoseSearch


/**
* Specifies the format of a pose search index. At runtime, queries are built according to the schema for searching.
*/
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental, meta = (DisplayName = "Motion Database Config"))
class POSESEARCH_API UPoseSearchSchema : public UDataAsset, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:

	static constexpr int32 DefaultSampleRate = 10;
	static constexpr int32 MaxBoneReferences = MAX_int8;
	static constexpr int32 MaxChannels = MAX_int8;
	static constexpr int32 MaxFeatures = MAX_int8;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TObjectPtr<USkeleton> Skeleton = nullptr;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "60"), Category = "Schema")
	int32 SampleRate = DefaultSampleRate;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Schema")
	TArray<TObjectPtr<UPoseSearchFeatureChannel>> Channels;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bUseTrajectoryVelocities_DEPRECATED = true;

	UPROPERTY()
	bool bUseTrajectoryPositions_DEPRECATED = true;

	UPROPERTY()
	bool bUseTrajectoryForwardVectors_DEPRECATED = false;

	UPROPERTY()
	TArray<FPoseSearchBone> SampledBones_DEPRECATED;

	UPROPERTY()
	TArray<float> PoseSampleTimes_DEPRECATED;

	UPROPERTY()
	TArray<float> TrajectorySampleTimes_DEPRECATED;

	UPROPERTY()
	TArray<float> TrajectorySampleDistances_DEPRECATED;
#endif // WITH_EDITOR


	// If set, this schema will support mirroring pose search databases
	UPROPERTY(EditAnywhere, Category = "Schema")
	TObjectPtr<UMirrorDataTable> MirrorDataTable;

	UPROPERTY(EditAnywhere, Category = "Schema")
	EPoseSearchDataPreprocessor DataPreprocessor = EPoseSearchDataPreprocessor::Automatic;

	UPROPERTY()
	EPoseSearchDataPreprocessor EffectiveDataPreprocessor = EPoseSearchDataPreprocessor::Invalid;

	UPROPERTY()
	float SamplingInterval = 1.0f / DefaultSampleRate;

	UPROPERTY()
	int32 SchemaCardinality = 0;

	UPROPERTY()
	TArray<FBoneReference> BoneReferences;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndices;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndicesWithParents;

	// cost added to the continuing pose from databases that uses this schema
	UPROPERTY(EditAnywhere, Category = "Schema")
	float ContinuingPoseCostBias = 0.f;

	// base cost added to all poses from databases that uses this schema. it can be overridden by UAnimNotifyState_PoseSearchModifyCost
	UPROPERTY(EditAnywhere, Category = "Schema")
	float BaseCostBias = 0.f;

	// If there's a mirroring mismatch between the currently playing asset and a search candidate, this cost will be 
	// added to the candidate, making it less likely to be selected
	UPROPERTY(EditAnywhere, Category = "Schema")
	float MirrorMismatchCostBias = 0.f;

	bool IsValid () const;

	int32 GetNumBones () const { return BoneIndices.Num(); }

public: // UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostLoad() override;

public: // IBoneReferenceSkeletonProvider
	class USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override { bInvalidSkeletonIsError = false; return Skeleton; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	void GenerateDDCKey(FBlake3& InOutKeyHasher) const;
#endif

private:
	void Finalize();
	void ResolveBoneReferences();

public:
	bool BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const;
};



//////////////////////////////////////////////////////////////////////////
// Search index

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

	bool IsValid() const
	{
		return NumDimensions > 0;
	}
};


/**
 * This is kept for each pose in the search index along side the feature vector values and is used to influence the search.
 */
USTRUCT()
struct POSESEARCH_API FPoseSearchPoseMetadata
{
	GENERATED_BODY()

	UPROPERTY()
	EPoseSearchPoseFlags Flags = EPoseSearchPoseFlags::None;

	// @todo: consider float16
	UPROPERTY()
	float CostAddend = 0.0f;

	// @todo: consider float16
	UPROPERTY()
	float ContinuingPoseCostAddend = 0.0f;
};


/**
* Information about a source animation asset used by a search index.
* Some source animation entries may generate multiple FPoseSearchIndexAsset entries.
**/
USTRUCT()
struct POSESEARCH_API FPoseSearchIndexAsset
{
	GENERATED_BODY()
public:
	FPoseSearchIndexAsset()
	{}

	FPoseSearchIndexAsset(
		ESearchIndexAssetType InType,
		int32 InSourceGroupIdx, 
		int32 InSourceAssetIdx, 
		bool bInMirrored, 
		const FFloatInterval& InSamplingInterval,
		FVector InBlendParameters = FVector::Zero())
		: Type(InType)
		, SourceGroupIdx(InSourceGroupIdx)
		, SourceAssetIdx(InSourceAssetIdx)
		, bMirrored(bInMirrored)
		, BlendParameters(InBlendParameters)
		, SamplingInterval(InSamplingInterval)
	{}

	// Default to Sequence for now for backward compatibility but
	// at some point we might want to change this to Invalid.
	UPROPERTY()
	ESearchIndexAssetType Type = ESearchIndexAssetType::Sequence;

	UPROPERTY()
	int32 SourceGroupIdx = INDEX_NONE;

	// Index of the source asset in search index's container (i.e. UPoseSearchDatabase)
	UPROPERTY()
	int32 SourceAssetIdx = INDEX_NONE;

	UPROPERTY()
	bool bMirrored = false;

	UPROPERTY()
	FVector BlendParameters = FVector::Zero();

	UPROPERTY()
	FFloatInterval SamplingInterval;

	UPROPERTY()
	int32 FirstPoseIdx = INDEX_NONE;

	UPROPERTY()
	int32 NumPoses = 0;

	bool IsPoseInRange(int32 PoseIdx) const
	{
		return (PoseIdx >= FirstPoseIdx) && (PoseIdx < FirstPoseIdx + NumPoses);
	}
};

USTRUCT()
struct POSESEARCH_API FGroupSearchIndex
{
	GENERATED_BODY()

	UE::PoseSearch::FKDTree KDTree;

	UPROPERTY()
	TArray<float> PCAProjectionMatrix;

	UPROPERTY()
	TArray<float> Mean;

	UPROPERTY()
	int32 StartPoseIndex = 0;
	
	UPROPERTY()
	int32 EndPoseIndex = 0;

	UPROPERTY()
	int32 GroupIndex = 0;

	UPROPERTY()
	TArray<float> Weights;
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
	TArray<float> PCAValues;

	UPROPERTY()
	TArray<FGroupSearchIndex> Groups;

	UPROPERTY()
	TArray<FPoseSearchPoseMetadata> PoseMetadata;

	UPROPERTY()
	TObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	UPROPERTY()
	FPoseSearchIndexPreprocessInfo PreprocessInfo;

	UPROPERTY()
	TArray<FPoseSearchIndexAsset> Assets;

	// minimum of the database metadata CostAddend: it represents the minimum cost of any search for the associated database (we'll skip the search in case the search result total cost is already less than MinCostAddend)
	UPROPERTY()
	float MinCostAddend = -MAX_FLT;

	bool IsValid() const;
	bool IsEmpty() const;

	TArrayView<const float> GetPoseValues(int32 PoseIdx) const;

	int32 FindAssetIndex(const FPoseSearchIndexAsset* Asset) const;
	const FGroupSearchIndex* FindGroup(int32 GroupIndex) const;

	const FPoseSearchIndexAsset* FindAssetForPose(int32 PoseIdx) const;
	float GetAssetTime(int32 PoseIdx, const FPoseSearchIndexAsset* Asset) const;

	void Reset();

	void Normalize (TArrayView<float> PoseVector) const;
	void InverseNormalize (TArrayView<float> PoseVector) const;

	// individual cost addends calculation methods
	float ComputeMirrorMismatchAddend(int32 PoseIdx, UE::PoseSearch::FSearchContext& SearchContext) const;
	float ComputeNotifyAddend(int32 PoseIdx) const;
	float ComputeContinuingPoseCostAddend(int32 PoseIdx, UE::PoseSearch::EPoseComparisonFlags PoseComparisonFlags) const;
};

//////////////////////////////////////////////////////////////////////////
// Database

USTRUCT()
struct POSESEARCH_API FPoseSearchDatabaseAnimationAssetBase
{
	GENERATED_BODY()

	virtual ~FPoseSearchDatabaseAnimationAssetBase() {}
	virtual UAnimationAsset* GetAnimationAsset() const { return nullptr; }
	virtual bool IsLooping() const { return false; }
};

/** An entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseSequence : public FPoseSearchDatabaseAnimationAssetBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> Sequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	FFloatInterval SamplingRange = FFloatInterval(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category = "Sequence")
	EPoseSearchMirrorOption MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;

	// Used for sampling past pose information at the beginning of the main sequence.
	// This setting is intended for transitions between cycles. It is optional and only used
	// for one shot anims with past sampling. When past sampling is used without a lead in sequence,
	// the sampling range of the main sequence will be clamped if necessary.
	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> LeadInSequence = nullptr;

	// Used for sampling future pose information at the end of the main sequence.
	// This setting is intended for transitions between cycles. It is optional and only used
	// for one shot anims with future sampling. When future sampling is used without a follow up sequence,
	// the sampling range of the main sequence will be clamped if necessary.
	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> FollowUpSequence = nullptr;

	UPROPERTY(EditAnywhere, Category = "Group")
	FGameplayTagContainer GroupTags;

	FFloatInterval GetEffectiveSamplingRange() const;

	virtual UAnimationAsset* GetAnimationAsset() const override;
	virtual bool IsLooping() const override;
};

/** An blend space entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseBlendSpace : public FPoseSearchDatabaseAnimationAssetBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "BlendSpace")
	TObjectPtr<UBlendSpace> BlendSpace = nullptr;

	UPROPERTY(EditAnywhere, Category = "BlendSpace")
	EPoseSearchMirrorOption MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;

	// If to use the blendspace grid locations as parameter sample locations.
	// When enabled, NumberOfHorizontalSamples and NumberOfVerticalSamples are ignored.
	UPROPERTY(EditAnywhere, Category = "BlendSpace")
	bool bUseGridForSampling = true;

	UPROPERTY(EditAnywhere, Category = "BlendSpace", meta = (ClampMin = "1", UIMin = "1", UIMax = "25"))
	int32 NumberOfHorizontalSamples = 5;

	UPROPERTY(EditAnywhere, Category = "BlendSpace", meta = (ClampMin = "1", UIMin = "1", UIMax = "25"))
	int32 NumberOfVerticalSamples = 5;

	UPROPERTY(EditAnywhere, Category = "Group")
	FGameplayTagContainer GroupTags;

	virtual UAnimationAsset* GetAnimationAsset() const override;
	virtual bool IsLooping() const override;

public:

	void GetBlendSpaceParameterSampleRanges(
		int32& HorizontalBlendNum,
		int32& VerticalBlendNum,
		float& HorizontalBlendMin,
		float& HorizontalBlendMax,
		float& VerticalBlendMin,
		float& VerticalBlendMax) const;
};

USTRUCT()
struct POSESEARCH_API FPoseSearchDatabaseGroup
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FGameplayTag Tag;
};

USTRUCT(BlueprintType)
struct FPoseSearchCost
{
	GENERATED_BODY()

public:
	FPoseSearchCost() = default;
	FPoseSearchCost(float InDissimilarity, float InCostAddend)
		: Dissimilarity(InDissimilarity)
		, CostAddend(InCostAddend)
		, TotalCost(Dissimilarity + CostAddend)
	{}

	bool IsValid() const { return TotalCost != MAX_flt; }

	void Set(float InDissimilarity, float InCostAddend)
	{
		Dissimilarity = InDissimilarity;
		CostAddend = InCostAddend;
		TotalCost = Dissimilarity + CostAddend;
	}

	float GetDissimilarity() const { return Dissimilarity; }

	void SetDissimilarity(float InDissimilarity)
	{
		Dissimilarity = InDissimilarity;
		TotalCost = Dissimilarity + CostAddend;
	}

	float GetCostAddend() const { return CostAddend; }

	void SetCostAddend(float InCostAddend)
	{
		CostAddend = InCostAddend;
		TotalCost = Dissimilarity + CostAddend;
	}

	float GetTotalCost() const { return TotalCost; }

	bool operator<(const FPoseSearchCost& Other) const { return TotalCost < Other.TotalCost; }

	void Reset()
	{
		Dissimilarity = MAX_flt;
		CostAddend = 0.0f;
		TotalCost = MAX_flt;
	}

protected:

	UPROPERTY()
	float Dissimilarity = MAX_flt;

	UPROPERTY()
	float CostAddend = 0.0f;

	UPROPERTY()
	float TotalCost = MAX_flt;
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

	TArray<float>& EditValues() { return Values; }
	TArrayView<const float> GetValues() const { return Values; }
	TArrayView<const float> GetNormalizedValues() const { return ValuesNormalized; }

	void CopyFromSearchIndex(const FPoseSearchIndex& SearchIndex, int32 PoseIdx);

	bool IsInitialized() const;
	bool IsInitializedForSchema(const UPoseSearchSchema* Schema) const;
	bool IsCompatible(const FPoseSearchFeatureVectorBuilder& OtherBuilder) const;

	void Normalize(const FPoseSearchIndex& ForSearchIndex);

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	TArray<float> Values;
	TArray<float> ValuesNormalized;
};


namespace UE::PoseSearch
{

enum class EDebugDrawFlags : uint32
{
	None			    = 0,

	// Draw the entire search index as a point cloud
	DrawSearchIndex     = 1 << 0,

	// Draw pose features for each pose vector
	IncludePose         = 1 << 1,

	// Draw trajectory features for each pose vector
	IncludeTrajectory   = 1 << 2,

	// Draw all pose vector features
	IncludeAllFeatures  = IncludePose | IncludeTrajectory,

	/**
	 * Keep rendered data until the next call to FlushPersistentDebugLines().
	 * Combine with DrawSearchIndex to draw the search index only once.
	 */
	Persistent = 1 << 3,
	
	// Label samples with their indices
	DrawSampleLabels = 1 << 4,

	// Fade colors
	DrawSamplesWithColorGradient = 1 << 5,

	// Draw Bone Names
	DrawBoneNames = 1 << 6,

	// Draws simpler shapes to improve performance
	DrawFast = 1 << 7,
};
ENUM_CLASS_FLAGS(EDebugDrawFlags);

struct POSESEARCH_API FDebugDrawParams
{
	const UWorld* World = nullptr;
	const UPoseSearchDatabase* Database = nullptr;
	const UPoseSearchSequenceMetaData* SequenceMetaData = nullptr;
	EDebugDrawFlags Flags = EDebugDrawFlags::DrawBoneNames;
	uint32 ChannelMask = (uint32)-1;

	float DefaultLifeTime = 5.0f;
	float PointSize = 1.0f;

	FTransform RootTransform = FTransform::Identity;

	// If set, draw the corresponding pose from the search index
	int32 PoseIdx = INDEX_NONE;

	// If set, draw using this uniform color instead of feature-based coloring
	const FLinearColor* Color = nullptr;

	// If set, interpret the buffer as a pose vector and draw it
	TArrayView<const float> PoseVector;

	// Optional prefix for sample labels
	FStringView LabelPrefix;

#if WITH_EDITORONLY_DATA
	FDebugFloatHistory* SearchCostHistoryBruteForce = nullptr;
	FDebugFloatHistory* SearchCostHistoryKDTree = nullptr;
#endif

	// Optional Mesh for gathering SocketTransform(s)
	TWeakObjectPtr<const USkinnedMeshComponent> Mesh = nullptr;

	bool CanDraw() const;
	FLinearColor GetColor(const UPoseSearchFeatureChannel* channel) const;
	const FPoseSearchIndex* GetSearchIndex() const;
	const UPoseSearchSchema* GetSchema() const;
};

class IPoseHistoryProvider : public UE::Anim::IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE(IPoseHistoryProvider);

public:

	virtual const FPoseHistory& GetPoseHistory() const = 0;
	virtual FPoseHistory& GetPoseHistory() = 0;
};


struct FSearchResult
{
	FPoseSearchCost PoseCost;
	int32 PoseIdx = INDEX_NONE;

	int32 PrevPoseIdx = INDEX_NONE;
	int32 NextPoseIdx = INDEX_NONE;

	// lerp value to find AssetTime from PrevPoseIdx -> AssetTime -> NextPoseIdx, within range [-0.5, 0.5]
	float LerpValue = 0.f;

	const FPoseSearchIndexAsset* SearchIndexAsset = nullptr;
	TWeakObjectPtr<const UPoseSearchDatabase> Database = nullptr;
	FPoseSearchFeatureVectorBuilder ComposedQuery;

	// cost of the current pose with the query from database in the result, if possible
	FPoseSearchCost ContinuingPoseCost; 

	float AssetTime = 0.0f;

#if WITH_EDITOR
	FIoHash SearchIndexHash = FIoHash::Zero;
#endif // WITH_EDITOR

	// Attempts to set the internal state to match the provided asset time including updating the internal DbPoseIdx. 
	// If the provided asset time is out of bounds for the currently playing asset then this function will reset the 
	// state back to the default state.
	void Update(float NewAssetTime);

	bool IsValid() const;

	void Reset();
};

/**
 * Cost details for pose analysis in the rewind debugger
 */
struct FPoseCostDetails
{
	FPoseSearchCost PoseCost;

	// Contribution from ModifyCost anim notify
	float NotifyCostAddend = 0.0f;

	// Contribution from mirroring cost
	float MirrorMismatchCostAddend = 0.0f;

	// Cost breakdown per channel (e.g. pose cost, time-based trajectory cost, distance-based trajectory cost, etc.)
	TArray<float> ChannelCosts;

	// Difference vector computed as W*((P-Q)^2) without the cost modifier applied
	// Where P is the pose vector, Q is the query vector, W is the weights vector, and multiplication/exponentiation are element-wise operations
	TArray<float> CostVector;
};

}


/** A data asset for indexing a collection of animation sequences. */
UCLASS(Abstract, BlueprintType, Experimental)
class POSESEARCH_API UPoseSearchSearchableAsset : public UDataAsset
{
	GENERATED_BODY()
public:

	virtual UE::PoseSearch::FSearchResult Search(UE::PoseSearch::FSearchContext& SearchContext) const PURE_VIRTUAL(UPoseSearchSearchableAsset::Search, return UE::PoseSearch::FSearchResult(););
};


/** A data asset for indexing a collection of animation sequences. */
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental, meta = (DisplayName = "Motion Database"))
class POSESEARCH_API UPoseSearchDatabase : public UPoseSearchSearchableAsset
{
	GENERATED_BODY()
public:
	// Motion Database Config asset to use with this database.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Database", DisplayName="Config")
	TObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	UPROPERTY(EditAnywhere, Category = "Database")
	FPoseSearchExtrapolationParameters ExtrapolationParameters;

	UPROPERTY(EditAnywhere, Category = "Database")
	FPoseSearchBlockTransitionParameters BlockTransitionParameters = { 0.0f, 0.2f };

	UPROPERTY(EditAnywhere, Category = "Database")
	TArray<FPoseSearchDatabaseGroup> Groups;

	// Drag and drop animations here to add them in bulk to Sequences
	UPROPERTY(EditAnywhere, Category = "Database", DisplayName="Drag And Drop Anims Here")
	TArray<TObjectPtr<UAnimSequence>> SimpleSequences;

	UPROPERTY(EditAnywhere, Category="Database")
	TArray<FPoseSearchDatabaseSequence> Sequences;

	// Drag and drop blendspaces here to add them in bulk to Blend Spaces
	UPROPERTY(EditAnywhere, Category = "Database", DisplayName = "Drag And Drop Blend Spaces Here")
	TArray<TObjectPtr<UBlendSpace>> SimpleBlendSpaces;

	UPROPERTY(EditAnywhere, Category = "Database")
	TArray<FPoseSearchDatabaseBlendSpace> BlendSpaces;

	UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "1", ClampMax = "64", UIMin = "1", UIMax = "64"))
	int32 NumberOfPrincipalComponents = 4;

	UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "1", ClampMax = "256", UIMin = "1", UIMax = "256"))
	int32 KDTreeMaxLeafSize = 8;
	
	UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "1", ClampMax = "600", UIMin = "1", UIMax = "600"))
	int32 KDTreeQueryNumNeighbors = 100;

	UPROPERTY(EditAnywhere, Category = "Performance")
	EPoseSearchMode PoseSearchMode = EPoseSearchMode::BruteForce;

	// if true, this database search will be skipped if cannot decrease the pose cost, and poses will not be listed into the PoseSearchDebugger
	UPROPERTY(EditAnywhere, Category = "Performance")
	bool bSkipSearchIfPossible = false;

	FPoseSearchIndex* GetSearchIndex();
	const FPoseSearchIndex* GetSearchIndex() const;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

	float GetAssetTime(int32 PoseIdx, const FPoseSearchIndexAsset* SearchIndexAsset = nullptr) const;
	int32 GetPoseIndexFromTime(float AssetTime, const FPoseSearchIndexAsset* SearchIndexAsset) const;
	bool GetPoseIndicesAndLerpValueFromTime(float Time, const FPoseSearchIndexAsset* SearchIndexAsset, int32& PrevPoseIdx, int32& PoseIdx, int32& NextPoseIdx, float& LerpValue) const;

	const FPoseSearchDatabaseAnimationAssetBase& GetAnimationSourceAsset(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	const FPoseSearchDatabaseSequence& GetSequenceSourceAsset(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	const FPoseSearchDatabaseBlendSpace& GetBlendSpaceSourceAsset(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	const bool IsSourceAssetLooping(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	const FGameplayTagContainer* GetSourceAssetGroupTags(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	const FString GetSourceAssetName(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	int32 GetNumberOfPrincipalComponents() const;
	
#if WITH_EDITOR
	void GenerateDDCKey(FBlake3& InOutKeyHasher) const;
private:
	static void AddDbSequenceToWriter(const FPoseSearchDatabaseSequence& DbSequence, FBlake3& InOutWriter);
	static void AddRawSequenceToWriter(const UAnimSequence* Sequence, FBlake3& InOutWriter);
	static void AddPoseSearchNotifiesToWriter(const UAnimSequence* Sequence, FBlake3& InOutWriter);
	static void AddDbBlendSpaceToWriter(const FPoseSearchDatabaseBlendSpace& DbBlendSpace, FBlake3& InOutWriter);
#endif // WITH_EDITOR

public: // UObject
	virtual void PostLoad() override;
	virtual void PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext) override;
	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	virtual bool IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform) override;
#endif

private:
	void CollectSimpleSequences();
	void CollectSimpleBlendSpaces();

public:
	// Populates the FPoseSearchIndex::Assets array by evaluating the data in the Sequences array
	bool TryInitSearchIndexAssets(FPoseSearchIndex& OutSearchIndex);

private:
	FPoseSearchDatabaseDerivedData* PrivateDerivedData;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnDerivedDataRebuildMulticaster);
	FOnDerivedDataRebuildMulticaster OnDerivedDataRebuild;

	DECLARE_MULTICAST_DELEGATE(FOnAssetChangeMulticaster);
	FOnDerivedDataRebuildMulticaster OnAssetChange;

	DECLARE_MULTICAST_DELEGATE(FOnGroupChangeMulticaster);
	FOnDerivedDataRebuildMulticaster OnGroupChange;
#endif // WITH_EDITOR

public:
#if WITH_EDITOR

	typedef FOnDerivedDataRebuildMulticaster::FDelegate FOnDerivedDataRebuild;
	void RegisterOnDerivedDataRebuild(const FOnDerivedDataRebuild& Delegate);
	void UnregisterOnDerivedDataRebuild(void* Unregister);
	void NotifyDerivedDataBuildStarted();

	typedef FOnAssetChangeMulticaster::FDelegate FOnAssetChange;
	void RegisterOnAssetChange(const FOnAssetChange& Delegate);
	void UnregisterOnAssetChange(void* Unregister);
	void NotifyAssetChange();

	typedef FOnGroupChangeMulticaster::FDelegate FOnGroupChange;
	void RegisterOnGroupChange(const FOnGroupChange& Delegate);
	void UnregisterOnGroupChange(void* Unregister);
	void NotifyGroupChange();

	void BeginCacheDerivedData();

	FIoHash GetSearchIndexHash() const;

	bool IsDerivedDataBuildPending() const;
#endif // WITH_EDITOR

	bool IsDerivedDataValid();

public:
	virtual UE::PoseSearch::FSearchResult Search(UE::PoseSearch::FSearchContext& SearchContext) const override;

	void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& OutQuery) const;

	FPoseSearchCost ComparePoses(UE::PoseSearch::FSearchContext& SearchContext, int32 PoseIdx, UE::PoseSearch::EPoseComparisonFlags PoseComparisonFlags, int32 GroupIdx, const TArrayView<const float>& QueryValues) const;
	FPoseSearchCost ComparePoses(UE::PoseSearch::FSearchContext& SearchContext, int32 PoseIdx, UE::PoseSearch::EPoseComparisonFlags PoseComparisonFlags, const TArrayView<const float>& QueryValues, UE::PoseSearch::FPoseCostDetails& OutPoseCostDetails) const;

protected:
	UE::PoseSearch::FSearchResult SearchPCAKDTree(UE::PoseSearch::FSearchContext& SearchContext) const;
	UE::PoseSearch::FSearchResult SearchBruteForce(UE::PoseSearch::FSearchContext& SearchContext) const;
};


//////////////////////////////////////////////////////////////////////////
// Sequence metadata

/** Animation metadata object for indexing a single animation. */
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental)
class POSESEARCH_API UPoseSearchSequenceMetaData : public UAnimMetaData
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = "Settings")
	TObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FFloatInterval SamplingRange = FFloatInterval(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category = "Settings")
	FPoseSearchExtrapolationParameters ExtrapolationParameters;

	UPROPERTY()
	FPoseSearchIndex SearchIndex;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

	UE::PoseSearch::FSearchResult Search(UE::PoseSearch::FSearchContext& SearchContext) const;

protected:
	FPoseSearchCost ComparePoses(int32 PoseIdx, UE::PoseSearch::EPoseComparisonFlags PoseComparisonFlags, const TArrayView<const float>& QueryValues) const;

public: // UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

};


//////////////////////////////////////////////////////////////////////////
// Feature vector reader and builder

namespace UE::PoseSearch
{

/** Helper class for extracting and encoding features into a float buffer */
class POSESEARCH_API FFeatureVectorHelper
{
public:
	enum { EncodeQuatCardinality = 6 };
	static void EncodeQuat(TArrayView<float> Values, int32& DataOffset, const FQuat& Quat);
	static void EncodeQuat(TArrayView<float> Values, int32& DataOffset, TArrayView<const float> PrevValues, TArrayView<const float> CurPrevValues, TArrayView<const float> NextPrevValues, float LerpValue);
	static FQuat DecodeQuat(TArrayView<const float> Values, int32& DataOffset);

	enum { EncodeVectorCardinality = 3 };
	static void EncodeVector(TArrayView<float> Values, int32& DataOffset, const FVector& Vector);
	static void EncodeVector(TArrayView<float> Values, int32& DataOffset, TArrayView<const float> PrevValues, TArrayView<const float> CurPrevValues, TArrayView<const float> NextPrevValues, float LerpValue, bool bNormalize = false);
	static FVector DecodeVector(TArrayView<const float> Values, int32& DataOffset);

	enum { EncodeVector2DCardinality = 2 };
	static void EncodeVector2D(TArrayView<float> Values, int32& DataOffset, const FVector2D& Vector2D);
	static void EncodeVector2D(TArrayView<float> Values, int32& DataOffset, TArrayView<const float> PrevValues, TArrayView<const float> CurPrevValues, TArrayView<const float> NextPrevValues, float LerpValue);
	static FVector2D DecodeVector2D(TArrayView<const float> Values, int32& DataOffset);

	// populates MeanDeviations[DataOffset] ... MeanDeviations[DataOffset + Cardinality] with a single value the mean deviation calcualted from a cenetered matrix
	static void ComputeMeanDeviations(const Eigen::MatrixXd& CenteredPoseMatrix, Eigen::VectorXd& MeanDeviations, int32& DataOffset, int32 Cardinality);

private:
	static FQuat DecodeQuatInternal(TArrayView<const float> Values, int32 DataOffset);
	static FVector DecodeVectorInternal(TArrayView<const float> Values, int32 DataOffset);
	static FVector2D DecodeVector2DInternal(TArrayView<const float> Values, int32 DataOffset);
};

/**
* Records poses over time in a ring buffer.
* FFeatureVectorBuilder uses this to sample from the present or past poses according to the search schema.
*/
class POSESEARCH_API FPoseHistory
{
public:

	enum class ERootUpdateMode
	{
		RootMotionDelta,
		ComponentTransformDelta,
	};

	void Init(int32 InNumPoses, float InTimeHorizon);
	void Init(const FPoseHistory& History);

	bool Update(
		float SecondsElapsed,
		const FPoseContext& PoseContext,
		FTransform ComponentTransform,
		FText* OutError,
		ERootUpdateMode UpdateMode = ERootUpdateMode::RootMotionDelta);

	float GetSampleTimeInterval() const;
	float GetTimeHorizon() const { return TimeHorizon; }
	bool TrySampleLocalPose(float Time, const TArray<FBoneIndexType>* RequiredBones, TArray<FTransform>* LocalPose, FTransform* RootTransform) const;

private:

	struct FPose
	{
		FTransform RootTransform;
		TArray<FTransform> LocalTransforms;
		float Time = 0.0f;
	};
	TRingBuffer<FPose> Poses;
	float TimeHorizon = 0.0f;
};

struct POSESEARCH_API FSearchContext
{
	EPoseSearchBooleanRequest QueryMirrorRequest = EPoseSearchBooleanRequest::Indifferent;
	const FGameplayTagQuery* DatabaseTagQuery = nullptr;
	UE::PoseSearch::FDebugDrawParams DebugDrawParams;
	UE::PoseSearch::FPoseHistory* History = nullptr;
	const FTrajectorySampleRange* Trajectory = nullptr;
	TObjectPtr<const USkeletalMeshComponent> OwningComponent = nullptr;
	UE::PoseSearch::FSearchResult CurrentResult;
	const FBoneContainer* BoneContainer = nullptr;
	const FGameplayTagContainer* ActiveTagsContainer = nullptr;
	float PoseJumpThresholdTime = 0.f;
	bool bForceInterrupt = false;
	// can the continuing pose advance? (if not we skip evaluating it)
	bool bCanAdvance = true;

	FTransform TryGetTransformAndCacheResults(float SampleTime, const UPoseSearchSchema* Schema, int8 SchemaBoneIdx, bool& Error);
	void ClearCachedEntries();

	void ResetCurrentBestCost();
	void UpdateCurrentBestCost(const FPoseSearchCost& PoseSearchCost);
	float GetCurrentBestTotalCost() const { return CurrentBestTotalCost; }

	bool GetOrBuildQuery(const UPoseSearchDatabase* Database, FPoseSearchFeatureVectorBuilder& FeatureVectorBuilder);
	void CacheCurrentResultFeatureVectors();

	static constexpr int8 SchemaRootBoneIdx = -1;

	FPoseSearchFeatureVectorBuilder CurrentResultPoseVector;
	FPoseSearchFeatureVectorBuilder CurrentResultPrevPoseVector;
	FPoseSearchFeatureVectorBuilder CurrentResultNextPoseVector;

private:
	struct FCachedEntry
	{
		float SampleTime = 0.f;

		// associated transform to BoneIndexType in ComponentSpace (except for the root bone stored in global space)
		FTransform Transform;

		// if -1 it represents the root bone
		FBoneIndexType BoneIndexType = -1;
	};

	// @todo: make it a fixed size array (or hash map if we end up having many CachedEntry) to avoid allocations
	TArray<FCachedEntry> CachedEntries;
	
	struct FCachedQuery
	{
		const UPoseSearchDatabase* Database = nullptr;
		FPoseSearchFeatureVectorBuilder FeatureVectorBuilder;
	};

	TArray<FCachedQuery> CachedQueries;

	float CurrentBestTotalCost = MAX_flt;

#if UE_POSE_SEARCH_TRACE_ENABLED

public:
	struct FPoseCandidate
	{
		float Cost = 0.f;
		int32 PoseIdx = 0;
		const UPoseSearchDatabase* Database = nullptr;

		bool operator<(const FPoseCandidate& Other) const { return Cost > Other.Cost; }
	};

	struct FBestPoseCandidates : private TArray<FPoseCandidate>
	{
		typedef TArray<FPoseCandidate> Super;
		using Super::IsEmpty;

		int32 MaxPoseCandidates = 100;

		void Add(float Cost, int32 PoseIdx, const UPoseSearchDatabase* Database)
		{
			if (Num() < MaxPoseCandidates || Cost < HeapTop().Cost)
			{
				while (Num() >= MaxPoseCandidates)
				{
					ElementType Unused;
					Pop(Unused);
				}

				FSearchContext::FPoseCandidate PoseCandidate;
				PoseCandidate.Cost = Cost;
				PoseCandidate.PoseIdx = PoseIdx;
				PoseCandidate.Database = Database;
				HeapPush(PoseCandidate);
			}
		}

		void Pop(FPoseCandidate& OutItem)
		{
			HeapPop(OutItem, false);
		}
	};
	
	FBestPoseCandidates BestCandidates;
#endif
};

//////////////////////////////////////////////////////////////////////////
// Main PoseSearch API

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
POSESEARCH_API bool BuildIndex(UPoseSearchDatabase* Database, FPoseSearchIndex& OutSearchIndex);

} // namespace UE::PoseSearch


UENUM()
enum class EPoseSearchPostSearchStatus : uint8
{
	// Continue looking for results 
	Continue,

	// Halt and return the best result
	Stop
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseSetEntry
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	TObjectPtr<UPoseSearchSearchableAsset> Searchable = nullptr;

	UPROPERTY(EditAnywhere, Category = Settings)
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, Category = Settings)
	EPoseSearchPostSearchStatus PostSearchStatus = EPoseSearchPostSearchStatus::Continue;
};

/** A data asset which holds a collection searchable assets. */
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental)
class POSESEARCH_API UPoseSearchDatabaseSet : public UPoseSearchSearchableAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FPoseSearchDatabaseSetEntry> AssetsToSearch;

	// if there's a valid continuing pose and bEvaluateContinuingPoseFirst is true, the continuing pose will be evaluated as first search,
	// otherwise it'll be evaluated with the related database: if the database is not active the continuing pose evaluation will be skipped
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bEvaluateContinuingPoseFirst = true;

public:
	virtual UE::PoseSearch::FSearchResult Search(UE::PoseSearch::FSearchContext& SearchContext) const override;
};
