// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Modules/ModuleManager.h"
#include "Misc/EnumClassFlags.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimMetaData.h"
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
	uint32 NumFloats;

	bool IsValid() const;
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
	FPoseSearchFeatureVectorLayout Layout;


	// @@@ these should be deprecated. Need to gather bone indices at runtime instead of baking.
	UPROPERTY()
	TArray<uint16> BoneIndices;

	UPROPERTY()
	TArray<uint16> BoneIndicesWithParents;

	UPoseSearchSchema()
		: SampleRate(DefaultSampleRate)
	{}

	bool IsValid () const;

public: // UObject
	virtual void PreSave(const class ITargetPlatform* TargetPlatform);

public: // IBoneReferenceSkeletonProvider
	class USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError) override { bInvalidSkeletonIsError = false; return Skeleton; }

private:
	void GenerateLayout();
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
	int32 NumPoses;

	UPROPERTY()
	TArray<float> Values;

	UPROPERTY()
	const UPoseSearchSchema* Schema;

	bool IsValid() const;
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
	FFloatInterval SamplingRange;

	UPROPERTY();
	FPoseSearchIndex SearchIndex;

	UPoseSearchSequenceMetaData ()
		: SamplingRange(0.0f, 0.0f)
	{}

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
	UAnimSequence* Sequence;

	UPROPERTY(EditAnywhere, Category="Sequence")
	FFloatInterval SamplingRange;

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopAnimation;

	UPROPERTY()
	int32 FirstPoseIdx;

	UPROPERTY()
	int32 NumPoses;

	FPoseSearchDatabaseSequence()
		: SamplingRange(0.0f, 0.0f)
	{}
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

	const FPoseSearchDatabaseSequence* FindSequenceByPoseIdx(int32 PoseIdx) const;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

public: // UObject
	virtual void PreSave(const class ITargetPlatform* TargetPlatform);
};


namespace UE { namespace PoseSearch {

namespace Private {

/**
* Helper to adapt a fixed-size buffer to a circular queue.
* FCircularView simply manages circular indexing arithmetic and is meant be paired with a container.
*/
class FCircularView
{
public:
	FCircularView(uint32 InCapacity = 0)
	    : Capacity(0)
	    , VirtualFirst(0)
	    , VirtualLast(0)
	{
		if (InCapacity)
		{
			Init(InCapacity);
		}
	}

	uint32 GetCapacity() const
	{
		return Capacity;
	}

	uint32 GetFront() const
	{
		checkSlow(!IsEmpty());
		return ToPhysicalIndex(VirtualFirst);
	}

	uint32 GetBack() const
	{
		checkSlow(!IsEmpty());
		return ToPhysicalIndex(VirtualLast - 1);
	}

	uint32 Num() const
	{
		return VirtualLast - VirtualFirst;
	}

	void Init(uint32 InCapacity)
	{
		checkSlow(FMath::IsPowerOfTwo(InCapacity));
		Capacity = InCapacity;
		VirtualFirst = 0;
		VirtualLast = 0;
	}

	bool IsEmpty() const
	{
		return Num() == 0;
	}

	bool IsFull() const
	{
		return Num() == Capacity;
	}

	uint32 GetOffsetFromFront(int32 Offset) const
	{
		checkSlow((uint32)FMath::Abs(Offset) < Num());
		return ToPhysicalIndex((uint32)(VirtualFirst + Offset));
	}

	uint32 GetOffsetFromBack(int32 Offset) const
	{
		return GetOffsetFromFront(Num() - 1 - Offset);
	}

	uint32 operator[](int32 OffsetFromFirst) const
	{
		return GetOffsetFromFront(OffsetFromFirst);
	}

	void PushBack()
	{
		checkSlow(!IsFull());
		++VirtualLast;
	}

	void PopFront()
	{
		checkSlow(!IsEmpty());
		++VirtualFirst;
	}

	void PushFront()
	{
		checkSlow(!IsFull());
		--VirtualFirst;
	}

	void PopBack()
	{
		checkSlow(!IsEmpty());
		--VirtualLast;
	}

private:
	uint32 ToPhysicalIndex(uint32 VirtualIndex) const { return VirtualIndex & (Capacity - 1); }

	// Maximum available elements. Must be a power of two.
	uint32 Capacity;

	// The beginning of the view. Must be converted by ToPhysicalIndex to index physical memory.
	uint32 VirtualFirst;

	// One element past the back of the view. Must be converted by ToPhysicalIndex to index physical memory.
	uint32 VirtualLast;
};

struct FSnapshot
{
	TArray<FTransform> LocalTransforms;
};

} // namespace Private


/** Records poses over time in a ring buffer. BuildQuery() uses this to sample from the present or past poses according to the search schema. */
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

private:
	bool SampleLocalPose(float Time, const FReferenceSkeleton& RefSkeleton, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& LocalPose);

	TArray<Private::FSnapshot> Poses;
	TArray<float> Knots;
	TArray<FTransform> SampledLocalPose;
	TArray<FTransform> SampledComponentPose;
	TArray<FTransform> SampledPrevLocalPose;
	TArray<FTransform> SampledPrevComponentPose;
	Private::FCircularView Queue;
	float TimeHorizon = 0.0f;
};


/** 
* Helper object for writing features into a float buffer according to a feature vector layout.
* Keeps track of which features are present, allowing the feature vector to be built up piecemeal.
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
	const FPoseSearchIndex* SearchIndex = nullptr;
	EDebugDrawFlags Flags = EDebugDrawFlags::None;
	float DefaultLifeTime = 5.0f;
	FTransform ComponentTransform = FTransform::Identity;
	int32 HighlightPoseIdx = -1;
	TArrayView<const float> Query;

	bool CanDraw () const { return World && SearchIndex; }
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

	const FPoseSearchDatabaseSequence* DbSequence = nullptr;
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
* @param Database	The input sequence and output metadata to write the search index to
*/
POSESEARCH_API bool BuildIndex(const UAnimSequence* Sequence, UPoseSearchSequenceMetaData* SequenceMetaData);


/**
* Creates a pose search index for a collection of animations
* 
* @param Database	The input collection of animations and output search index
*/
POSESEARCH_API bool BuildIndex(UPoseSearchDatabase* Database);


/**
* Builds a pose search query as an array of floats according to the search schema
*
* @param Schema			The schema to build a query for
* @param History		Recorded pose to sample from to build the query
* @param Scratch		Working memory to re-use for query construction
* @param Query			Output pose search query. Query.Num() must be equal to Schema.Layout.NumFloats.
*/
POSESEARCH_API bool BuildQuery(const UPoseSearchSchema* Schema, FPoseHistory* History, TArrayView<float> Query);


/**
* Performs a pose search
*
* @param Sequence		The sequence to search within
* @param Query			The pose query to search for. See BuildQuery().
* @param DrawParams		Visualization options
* 
* @return				The pose in the sequence that most closely matches the Query.
*/
POSESEARCH_API FSearchResult Search(const UPoseSearchSequenceMetaData* Sequence, TArrayView<const float> Query, FDebugDrawParams DrawParams = FDebugDrawParams());


/**
* Performs a pose search
*
* @param Database		The database to search within
* @param Query			The pose query to search for. See BuildQuery().
* @param DrawParams		Visualization options
* 
* @return				The pose in the database that most closely matches the Query.
*/
POSESEARCH_API FDbSearchResult Search(const UPoseSearchDatabase* Database, TArrayView<const float> Query, FDebugDrawParams DrawParams = FDebugDrawParams());

}} // namespace UE::PoseSearch