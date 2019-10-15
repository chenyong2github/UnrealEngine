// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAnimationProvider.h"
#include "Common/PagedArray.h"
#include "Model/MonotonicTimeline.h"

namespace Trace { class IAnalysisSession; }

class FAnimationProvider : public IAnimationProvider
{
public:
	static FName ProviderName;

	FAnimationProvider(Trace::IAnalysisSession& InSession);

	/** IAnimationProvider interface */
	virtual bool ReadSkeletalMeshPoseTimeline(uint64 InObjectId, TFunctionRef<void(const SkeletalMeshPoseTimeline&)> Callback) const override;
	virtual bool ReadSkeletalMeshPoseMessage(uint64 InObjectId, uint64 InMessageId, TFunctionRef<void(const FSkeletalMeshPoseMessage&)> Callback) const override;
	virtual void EnumerateSkeletalMeshPose(const FSkeletalMeshPoseMessage& InMessage, const FSkeletalMeshInfo& InMeshInfo, TFunctionRef<void(const FTransform&, const FTransform&)> Callback) const override;
	virtual void EnumerateTickRecordTimelines(uint64 InObjectId, TFunctionRef<void(uint64, const TickRecordTimeline&)> Callback) const override;
	virtual bool ReadTickRecordTimeline(uint64 InObjectId, uint64 InAssetId, TFunctionRef<void(const TickRecordTimeline&)> Callback) const override;
	virtual bool ReadTickRecordMessage(uint64 InObjectId, uint64 InAssetId, uint64 InMessageId, TFunctionRef<void(const FTickRecordMessage&)> Callback) const override;
	virtual const FSkeletalMeshInfo* FindSkeletalMeshInfo(uint64 InObjectId) const override;

	/** Add a tick record */
	void AppendTickRecord(uint64 InObjectId, uint64 InSubObjectId, double InTime, uint64 InAssetId, float InBlendWeight, float InPlaybackTime, float InRootMotionWeight, float InPlayRate, uint16 InFrameCounter, bool bInLooping);

	/** Add a skeletal mesh */
	void AppendSkeletalMesh(uint64 InObjectId, const TArrayView<const int32>& ParentIndices);

	/** Add a skeletal mesh pose */
	void AppendSkeletalMeshPose(uint64 InObjectId, uint64 InMeshId, double InTime, uint16 InLodIndex, uint16 InFrameCounter, const TArrayView<const FTransform>& InTransforms);

private:
	Trace::IAnalysisSession& Session;

	/** Maps into timeline arrays per-object */
	TMap<uint64, uint32> ObjectIdToTickRecordTimelineStorage;
	TMap<uint64, uint32> ObjectIdToSkeletalMeshPoseTimelines;

	/** All the skeletal mesh info we have seen, grow only for stable indices */
	TArray<FSkeletalMeshInfo> SkeletalMeshInfos;

	/** Map of skeletal mesh->index into SkeletalMeshInfos index */
	TMap<uint64, int32> SkeletalMeshIdToIndexMap;

	struct FPerObjectTimelineStorage
	{
		TMap<uint64, uint32> AssetIdToTickRecordTimeline;
		TArray<TSharedRef<Trace::TMonotonicTimeline<FTickRecordMessage>>> Timelines;
	};

	/** Message storage */
	TArray<TSharedRef<FPerObjectTimelineStorage>> PerObjectTimelineStorage;
	TArray<TSharedRef<Trace::TMonotonicTimeline<FSkeletalMeshPoseMessage>>> SkeletalMeshPoseTimelines;
	TPagedArray<FTransform> SkeletalMeshPoseTransforms;
	TPagedArray<int32> SkeletalMeshParentIndices;
};