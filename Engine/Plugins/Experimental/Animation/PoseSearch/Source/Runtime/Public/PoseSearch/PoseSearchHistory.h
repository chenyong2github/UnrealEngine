// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimNodeMessages.h"
#include "BonePose.h"
#include "Containers/RingBuffer.h"
#include "UObject/ObjectKey.h"

class USkeleton;
class UWorld;

namespace UE::PoseSearch
{

struct FSearchResult;

struct POSESEARCH_API FPoseHistory
{
	void Init(int32 InNumPoses, float InTimeHorizon, const TArray<FBoneIndexType>& RequiredBones);
	void Update(float SecondsElapsed, FCSPose<FCompactPose>& ComponentSpacePose, FTransform ComponentTransform);
	float GetSampleTimeInterval() const;
	float GetTimeHorizon() const { return TimeHorizon; }
	bool GetComponentSpaceTransformAtTime(float Time, FBoneIndexType BoneIndexType, FTransform& OutBoneTransform, bool bExtrapolate = true) const;
	void GetRootTransformAtTime(float Time, FTransform& OutRootTransform, bool bExtrapolate = true) const;
	void DebugDraw(const UWorld* World, const USkeleton* Skeleton) const;

private:

	struct FEntry
	{
		FTransform RootTransform;
		TArray<FTransform> ComponentSpaceTransforms;
		float Time = 0.f;
	};

	typedef uint16 FComponentSpaceTransformIndex;
	TMap<FBoneIndexType, FComponentSpaceTransformIndex> BoneToTransformMap;
	TRingBuffer<FEntry> Entries;
	float TimeHorizon = 0.f;
};

class IPoseHistoryProvider : public UE::Anim::IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE(IPoseHistoryProvider);
public:
	virtual const FPoseHistory& GetPoseHistory() const = 0;
};

struct FHistoricalPoseIndex
{
	bool operator==(const FHistoricalPoseIndex& Index) const
	{
		return PoseIndex == Index.PoseIndex && DatabaseKey == Index.DatabaseKey;
	}

	friend FORCEINLINE uint32 GetTypeHash(const FHistoricalPoseIndex& Index)
	{
		return HashCombineFast(::GetTypeHash(Index.PoseIndex), GetTypeHash(Index.DatabaseKey));
	}

	int32 PoseIndex = INDEX_NONE;
	FObjectKey DatabaseKey;
};

struct FPoseIndicesHistory
{
	void Update(const FSearchResult& SearchResult, float DeltaTime, float MaxTime);
	void Reset() { IndexToTime.Reset(); }
	TMap<FHistoricalPoseIndex, float> IndexToTime;
};

} // namespace UE::PoseSearch


