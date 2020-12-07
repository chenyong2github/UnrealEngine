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
#include "BoneIndices.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"

#include "PoseSearch.generated.h"


class UAnimSequence;
struct FCompactPose;
struct FReferenceSkeleton;

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
	uint32 NumFloats = 0;

	bool IsValid(int32 MaxNumBones) const;
};


//USTRUCT()
//struct FPoseSearchSchemaBoneRef
//{
//	GENERATED_BODY()
//
//	/** Index into UPoseSearchSchema::Bones. */
//	UPROPERTY()
//	int32 BoneIdx = -1;
//
//	/**
//	* Optional index into UPoseSearchSchema::Bones.
//	* Causes BoneIdx to be sampled in the reference frame of this bone.
//	*/
//	UPROPERTY()
//	int32 RelativeBoneIdx = -1;
//};


/**
* Specifies the format of a pose search index. At runtime, queries are built according to the schema for searching.
*/
UCLASS(BlueprintType, Category = "Animation|PoseSearch")
class POSESEARCH_API UPoseSearchSchema : public UDataAsset, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:

	static constexpr int32 DefaultSampleRate = 10;

	UPROPERTY(EditAnywhere, Category = "Schema")
	USkeleton* Skeleton;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "60"), Category = "Schema")
	int32 SampleRate;

	UPROPERTY(EditAnywhere, Category = "Schema")
	bool bUseBoneVelocities;

	UPROPERTY(EditAnywhere, Category = "Schema")
	bool bUseBonePositions;

	UPROPERTY(EditAnywhere, Category = "Schema")
	bool bUseTrajectoryVelocities;

	UPROPERTY(EditAnywhere, Category = "Schema")
	bool bUseTrajectoryPositions;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<FBoneReference> Bones;

	/*
	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<FPoseSearchSchemaBoneRef> PoseSampleBones;
	*/

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<int32> PoseSampleOffsets;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<int32> TrajectorySampleOffsets;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<float> TrajectoryDistanceOffsets;

	UPROPERTY()
	float SamplingInterval;

	UPROPERTY()
	FPoseSearchFeatureVectorLayout Layout;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndices;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndicesWithParents;

	UPoseSearchSchema()
		: SampleRate(DefaultSampleRate)
	{}

	bool IsValid () const;

public: // UObject
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	virtual void PostLoad() override;

public: // IBoneReferenceSkeletonProvider
	class USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError) override { bInvalidSkeletonIsError = false; return Skeleton; }

private:
	void GenerateLayout();
	void ResolveBoneReferences();
};

/**
* A search index for animation poses. The structure of the search index is determined by its UPoseSearchSchema.
* May represent a single animation (see UPoseSearchSequenceMetaData) or a collection (see UPoseSearchDatabase).
*/
USTRUCT()
struct POSESEARCH_API FPoseSearchIndex
{
	GENERATED_BODY()
public:
	UPROPERTY()
	int32 NumPoses = 0;

	UPROPERTY()
	TArray<float> Values;

	UPROPERTY()
	const UPoseSearchSchema* Schema = nullptr;

	bool IsValid() const;

	TArrayView<const float> GetPoseValues(int32 PoseIdx) const;

	void Reset();
};

/** Animation metadata object for indexing a single animation. */
UCLASS(BlueprintType, Category = "Animation|PoseSearch")
class POSESEARCH_API UPoseSearchSequenceMetaData : public UAnimMetaData
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category="Settings")
	const UPoseSearchSchema* Schema;

	UPROPERTY(EditAnywhere, Category="Settings")
	FFloatInterval SamplingRange = FFloatInterval(0.0f, 0.0f);

	UPROPERTY();
	FPoseSearchIndex SearchIndex;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

public: // UObject
	virtual void PreSave(const class ITargetPlatform* TargetPlatform);
};

/** An entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|PoseSearch")
struct POSESEARCH_API FPoseSearchDatabaseSequence
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Sequence")
	UAnimSequence* Sequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	FFloatInterval SamplingRange = FFloatInterval(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopAnimation = false;

	UPROPERTY()
	int32 FirstPoseIdx = 0;

	UPROPERTY()
	int32 NumPoses = 0;

	int32 GetPoseIndexFromAssetTime(float AssetTime) const;
};

/** A data asset for indexing a collection of animation sequences. */
UCLASS(BlueprintType, Category = "Animation|PoseSearch")
class POSESEARCH_API UPoseSearchDatabase : public UDataAsset
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category="Database")
	const UPoseSearchSchema* Schema;

	UPROPERTY(EditAnywhere, Category="Database")
	TArray<FPoseSearchDatabaseSequence> Sequences;

	UPROPERTY()
	FPoseSearchIndex SearchIndex;

	int32 FindSequenceForPose(int32 PoseIdx) const;
	int32 GetPoseIndexFromAssetTime(int32 DbSequenceIdx, float AssetTime) const;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

public: // UObject
	virtual void PreSave(const class ITargetPlatform* TargetPlatform);
};


namespace UE { namespace PoseSearch {

/** Records poses over time in a ring buffer. FFeatureVectorBuilder uses this to sample from the present or past poses according to the search schema. */
// @@@Rename to FPoseQueryContext?
class POSESEARCH_API FPoseHistory
{
public:
	void Init(int32 InNumPoses, float InTimeHorizon);
	void Init(const FPoseHistory& History);
	bool Sample(float SecondsAgo, const FReferenceSkeleton& RefSkeleton, const TArray<FBoneIndexType>& RequiredBones);
	void Update(float SecondsElapsed, const FCompactPose& EvalContext);
	float GetSampleInterval() const;
	TArrayView<const FTransform> GetLocalPoseSample() const { return SampledLocalPose; }
	TArrayView<const FTransform> GetComponentPoseSample() const { return SampledComponentPose; }
	TArrayView<const FTransform> GetPrevLocalPoseSample() const { return SampledPrevLocalPose; }
	TArrayView<const FTransform> GetPrevComponentPoseSample() const { return SampledPrevComponentPose; }
	float GetTimeHorizon() const { return TimeHorizon; }
	TArray<float>& GetQueryBuffer() { return QueryBuffer; }

private:
	bool SampleLocalPose(float Time, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& LocalPose);

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
	TArray<float> QueryBuffer;

	float TimeHorizon = 0.0f;
};

class IPoseHistoryProvider : public UE::Anim::IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE(IPoseHistoryProvider);

public:

	virtual const FPoseHistory& GetPoseHistory() const = 0;
	virtual FPoseHistory& GetPoseHistory() = 0;
};


/** 
* Helper object for writing features into a float buffer according to a feature vector layout.
* Keeps track of which features are present, allowing the feature vector to be built up piecemeal.
* FFeatureVectorBuilder is used to build search queries at runtime and for adding samples during search index construction.
*/
class POSESEARCH_API FFeatureVectorBuilder
{
public:
	void Init(const FPoseSearchFeatureVectorLayout* Layout, TArrayView<float> Buffer);
	void ResetFeatures();

	void SetTransform(FPoseSearchFeatureDesc Feature, const FTransform& Transform);
	void SetTransformDerivative(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);

	void SetPosition(FPoseSearchFeatureDesc Feature, const FVector& Translation);
	void SetRotation(FPoseSearchFeatureDesc Feature, const FQuat& Rotation);
	void SetLinearVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetAngularVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);

	void SetVector(FPoseSearchFeatureDesc Feature, const FVector& Vector);

	bool SetPoseFeatures(const UPoseSearchSchema* Schema, FPoseHistory* History);

	void Copy(TArrayView<const float> FeatureVector);

	bool IsComplete() const;

private:
	const FPoseSearchFeatureVectorLayout* Layout = nullptr;
	TArrayView<float> Values;
	TBitArray<> FeaturesAdded;
	int32 NumFeaturesAdded = 0;
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
	None			= 0,
	DrawQuery		= 1 << 0,
	DrawSearchIndex = 1 << 1,
	DrawBest        = 1 << 2,
	DrawAll		    = MAX_uint32
};
ENUM_CLASS_FLAGS(EDebugDrawFlags);

struct FDebugDrawParams
{
	const UWorld* World = nullptr;
	const UPoseSearchDatabase* Database = nullptr;
	const UPoseSearchSequenceMetaData* SequenceMetaData = nullptr;
	EDebugDrawFlags Flags = EDebugDrawFlags::None;
	float DefaultLifeTime = 5.0f;
	FTransform RootTransform = FTransform::Identity;
	int32 HighlightPoseIdx = -1;
	TArrayView<const float> Query;

	bool CanDraw () const;
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
* @return					Whether the index was built successfully
*/
POSESEARCH_API bool BuildIndex(const UAnimSequence* Sequence, UPoseSearchSequenceMetaData* SequenceMetaData);


/**
* Creates a pose search index for a collection of animations
* 
* @param Database	The input collection of animations and output search index
* 
* @return			Whether the index was built successfully
*/
POSESEARCH_API bool BuildIndex(UPoseSearchDatabase* Database);


/**
* Performs a pose search on a sequence's UPoseSearchSequenceMetaData.
*
* @param Sequence		The sequence to search within. Must contain a UPoseSearchSequenceMetaData object.
* @param Query			The pose query to search for. To build a query, see FFeatureVectorBuilder. Must have been built using the same schema as the UPoseSearchSequenceMetaData.
* @param DrawParams		Visualization options
* 
* @return				The pose in the sequence that most closely matches the Query.
*/
POSESEARCH_API FSearchResult Search(const UAnimSequenceBase* Sequence, TArrayView<const float> Query, FDebugDrawParams DrawParams = FDebugDrawParams());


/**
* Performs a pose search on a UPoseSearchDatabase.
*
* @param Database		The database to search within
* @param Query			The pose query to search for. To build a query, see FFeatureVectorBuilder. Must have been built using the same schema as the Database.
* @param DrawParams		Visualization options
* 
* @return				The pose in the database that most closely matches the Query.
*/
POSESEARCH_API FDbSearchResult Search(const UPoseSearchDatabase* Database, TArrayView<const float> Query, FDebugDrawParams DrawParams = FDebugDrawParams());


/**
* Evaluate pose comparison metric between a pose in the search index and an input query
* 
* @param SearchIndex	The search index containing the pose to compare to the query
* @param PoseIdx		The index of the pose in the search index to compare to the query
* @param Query			The query to compare against. Must have been built using the same schema as the SearchIndex.
* 
* @return Dissimilarity between the two poses
*/
POSESEARCH_API float ComparePoses(const FPoseSearchIndex& SearchIndex, int32 PoseIdx, TArrayView<const float> Query);

}} // namespace UE::PoseSearch