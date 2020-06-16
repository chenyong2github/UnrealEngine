// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMetaData.h"
#include "Animation/AnimationAsset.h"
#include "BoneIndices.h"
#include "Modules/ModuleManager.h"
#include "Misc/EnumClassFlags.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "PoseSearch.generated.h"


class UAnimSequenceBase;
struct FCompactPose;

/** Sampling parameters for building a pose search index for an asset. */
UCLASS(BlueprintType, Category = "Animation|PoseSearch")
class POSESEARCH_API UPoseSearchIndexConfig : public UAnimMetaData
{
	GENERATED_BODY()

public:
	static constexpr int32 DefaultSampleRate = 15;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "60"), Category = "Config")
	int32 SampleRate;

	UPROPERTY(EditAnywhere, Category = "Config")
	FFloatRange FrameSamplingRange;

	UPoseSearchIndexConfig()
	    : SampleRate(DefaultSampleRate)
	{}
};


/**
* Specifies the format of a pose search index. At runtime, queries are built according to the schema for searching.
*/
UCLASS(BlueprintType, Category = "Animation|PoseSearch")
class POSESEARCH_API UPoseSearchSchema : public UAnimationAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<FBoneReference> Bones;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<int32> FragmentSampleOffsets;

	UPROPERTY()
	int32 FloatsPerPose;

	UPROPERTY()
	TArray<uint16> BoneIndices;

	UPROPERTY()
	TArray<uint16> BoneIndicesWithParents;

public: // UObject
	virtual void PreSave(const class ITargetPlatform* TargetPlatform);
};


/**
* Contains pose data sampled at editor time for searching at runtime.
*/
UCLASS(BlueprintType, Category = "Animation|PoseSearch")
class POSESEARCH_API UPoseSearchIndex : public UAnimMetaData
{
	GENERATED_BODY()
public:
	UPROPERTY()
	int32 NumPoses;

	UPROPERTY()
	int32 FloatsPerPose;

	UPROPERTY()
	TArray<float> Values;

	UPROPERTY()
	const UPoseSearchSchema* Schema;

	UPROPERTY()
	int32 SequenceSampleRate;

	UPROPERTY()
	FFloatRange SequenceFrameSamplingRange;
};

struct POSESEARCH_API FPoseSearchPoseSnapshot
{
	TArray<FTransform> LocalTransforms;
};


namespace PoseSearchDetail {

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

} // namespace PoseSearchDetail


/** Records poses over time in a ring buffer. PoseSearchBuildQuery() uses this to sample from the present or past poses according to the search schema. */
class POSESEARCH_API FPoseSearchPoseHistory
{
public:
	void Init(int32 InNumPoses, float InTimeHorizon);
	void Init(const FPoseSearchPoseHistory& History);
	bool Sample(float Time, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& OutPose) const;
	void Update(float TimeDelta, const FCompactPose& EvalContext);
	float GetSampleInterval() const;
	float GetTimeHorizon() const { return TimeHorizon; }

private:
	TArray<FPoseSearchPoseSnapshot> Poses;
	TArray<float> Knots;
	PoseSearchDetail::FCircularView Queue;
	float TimeHorizon = 0.0f;
};

struct FPoseSearchBuildQueryScratch
{
	TArray<FTransform> LocalPose;
	TArray<FTransform> ComponentPose;
};

enum class EPoseSearchDebugDrawFlags : uint32
{
	None			= 0,
	DrawQuery		= 1 << 0,
	DrawSearchIndex = 1 << 1,
	DrawBest        = 1 << 2,
	DrawAll		    = MAX_uint32
};
ENUM_CLASS_FLAGS(EPoseSearchDebugDrawFlags);

struct FPoseSearchDebugDrawParams
{
	const UWorld* World = nullptr;
	float DefaultLifeTime = 5.0f;
	EPoseSearchDebugDrawFlags Flags = EPoseSearchDebugDrawFlags::None;
	const UPoseSearchSchema* Schema = nullptr;
	FTransform ComponentTransform = FTransform::Identity;

	bool CanDraw () const { return World && Schema; }
};

struct FPoseSearchResult
{
	int32 PoseIdx = -1;
	float TimeOffsetSeconds = 0.0f;
	float Dissimilarity = MAX_flt;
};


//////////////////////////////////////////////////////////////////////////
// Main PoseSearch API

/**
* Draws a pose search query
*
* @param DrawParams Visualization options
* @param Query The pose search query created with PoseSearchBuildQuery()
*/
void POSESEARCH_API PoseSearchDrawQuery(const FPoseSearchDebugDrawParams& DrawParams, TArrayView<const float> Query);

/**
* Draws a pose search index
*
* @param DrawParams Visualization options
* @param SearchIndex The pose search index to visualize
* @param HighlightPoseIdx Optional index of a pose in the search index to highlight. Specify -1 to not highlight a pose.
*/
void POSESEARCH_API PoseSearchDrawSearchIndex(const FPoseSearchDebugDrawParams& DrawParams, const UPoseSearchIndex& SearchIndex, int32 HighlightPoseIdx = -1);

/**
* Creates a pose search index by sampling from the animation sequence
*
* @param AnimSequence The asset to sample from
* @param SearchConfig Options for sampling from this asset
* @param SearchSchema The format of the search index
* @param SearchIndex Output pose search index
*/
void POSESEARCH_API PoseSearchBuildIndex(const UAnimSequenceBase& AnimSequence, const UPoseSearchIndexConfig& SearchConfig, const UPoseSearchSchema& SearchSchema, UPoseSearchIndex* SearchIndex);

/**
* Builds a pose search query as an array of floats according to the search schema
*
* @param SearchSchema The format of the search index and the query being built
* @param AssetSampleRate The sampling rate used to index the animation asset
* @param History Recorded pose to sample from to build the query
* @param Scratch Working memory to re-use for query construction
* @Query Output pose search query
*/
bool POSESEARCH_API PoseSearchBuildQuery(const UPoseSearchSchema& SearchSchema, int32 AssetSampleRate, const FPoseSearchPoseHistory& History, FPoseSearchBuildQueryScratch* Scratch, TArray<float>* Query);

/**
* Performs a pose search
*
* @param SearchIndex The index to search within. See PoseSearchBuildIndex().
* @param Query The pose query to search for. See PoseSearchBuildQuery().
* @param DrawParams Visualization options
* @return The pose in the SearchIndex that most closely matches the Query.
*/
FPoseSearchResult POSESEARCH_API PoseSearch(const UPoseSearchIndex& SearchIndex, TArrayView<const float> Query, FPoseSearchDebugDrawParams DrawParams = FPoseSearchDebugDrawParams());


//////////////////////////////////////////////////////////////////////////
// FPoseSearchModule

class FPoseSearchModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};