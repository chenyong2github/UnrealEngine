// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimationProvider.h"
#include "GameplayProvider.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

FName FAnimationProvider::ProviderName("AnimationProvider");

FAnimationProvider::FAnimationProvider(Trace::IAnalysisSession& InSession)
	: Session(InSession)
	, SkeletalMeshPoseTransforms(InSession.GetLinearAllocator(), 256)
	, SkeletalMeshParentIndices(InSession.GetLinearAllocator(), 256)
{
}

bool FAnimationProvider::ReadSkeletalMeshPoseTimeline(uint64 InObjectId, TFunctionRef<void(const SkeletalMeshPoseTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToSkeletalMeshPoseTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(SkeletalMeshPoseTimelines.Num()))
		{
			Callback(*SkeletalMeshPoseTimelines[*IndexPtr]);
			return true;
		}
	}

	return false;
}

bool FAnimationProvider::ReadSkeletalMeshPoseMessage(uint64 InObjectId, uint64 InMessageId, TFunctionRef<void(const FSkeletalMeshPoseMessage&)> Callback) const
{
	return ReadSkeletalMeshPoseTimeline(InObjectId, [&Callback, &InMessageId](const SkeletalMeshPoseTimeline& InTimeline)
	{
		if(InMessageId < InTimeline.GetEventCount())
		{
			Callback(InTimeline.GetEvent(InMessageId));
		}
	});
}

void FAnimationProvider::EnumerateSkeletalMeshPose(const FSkeletalMeshPoseMessage& InMessage, const FSkeletalMeshInfo& InMeshInfo, TFunctionRef<void(const FTransform&, const FTransform&)> Callback) const
{
	Session.ReadAccessCheck();

	if(InMeshInfo.BoneCount == InMessage.NumTransforms - 1)
	{
		// First transform is always component to world
		const FTransform& ComponentToWorld = SkeletalMeshPoseTransforms[InMessage.TransformStartIndex];
		uint64 StartTransformIndex = InMessage.TransformStartIndex + 1;
		uint64 RootBoneIndex = InMeshInfo.ParentIndicesStartIndex;
		uint64 EndTransformIndex = InMessage.TransformStartIndex + InMessage.NumTransforms;
		uint64 TransformIndex;
		uint64 BoneIndex;
		for(TransformIndex = StartTransformIndex, BoneIndex = RootBoneIndex; TransformIndex < EndTransformIndex; ++TransformIndex, ++BoneIndex)
		{
			int32 ParentIndex = SkeletalMeshParentIndices[BoneIndex];
			uint64 ParentTransformIndex = ParentIndex == INDEX_NONE ? TransformIndex : (StartTransformIndex + (uint64)ParentIndex);
			Callback(SkeletalMeshPoseTransforms[TransformIndex] * ComponentToWorld, SkeletalMeshPoseTransforms[ParentTransformIndex] * ComponentToWorld);
		}
	}
}

void FAnimationProvider::EnumerateTickRecordTimelines(uint64 InObjectId, TFunctionRef<void(uint64, const TickRecordTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToTickRecordTimelineStorage.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(PerObjectTimelineStorage.Num()))
		{
			const TSharedRef<FPerObjectTimelineStorage>& TimelineStorage = PerObjectTimelineStorage[*IndexPtr];
			for(auto AssetIdPair : TimelineStorage->AssetIdToTickRecordTimeline)
			{
				Callback(AssetIdPair.Key, TimelineStorage->Timelines[AssetIdPair.Value].Get());
			}
		}
	}
}

bool FAnimationProvider::ReadTickRecordTimeline(uint64 InObjectId, uint64 InAssetId, TFunctionRef<void(const TickRecordTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToTickRecordTimelineStorage.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(PerObjectTimelineStorage.Num()))
		{
			const TSharedRef<FPerObjectTimelineStorage>& TimelineStorage = PerObjectTimelineStorage[*IndexPtr];
			const uint32* TimelineIndexPtr = TimelineStorage->AssetIdToTickRecordTimeline.Find(InAssetId);
			if(TimelineIndexPtr != nullptr)
			{
				Callback(TimelineStorage->Timelines[*TimelineIndexPtr].Get());
				return true;
			}
		}
	}

	return false;
}

bool FAnimationProvider::ReadTickRecordMessage(uint64 InObjectId, uint64 InAssetId, uint64 InMessageId, TFunctionRef<void(const FTickRecordMessage&)> Callback) const
{
	return ReadTickRecordTimeline(InObjectId, InAssetId, [&Callback, &InMessageId](const TickRecordTimeline& InTimeline)
	{
		if(InMessageId < InTimeline.GetEventCount())
		{
			Callback(InTimeline.GetEvent(InMessageId));
		}
	});
}

const FSkeletalMeshInfo* FAnimationProvider::FindSkeletalMeshInfo(uint64 InObjectId) const
{
	const int32* ObjectIndex = SkeletalMeshIdToIndexMap.Find(InObjectId);
	if(ObjectIndex != nullptr)
	{
		return &SkeletalMeshInfos[*ObjectIndex];
	}

	return nullptr;
}

void FAnimationProvider::AppendTickRecord(uint64 InObjectId, uint64 InSubObjectId, double InTime, uint64 InAssetId, float InBlendWeight, float InPlaybackTime, float InRootMotionWeight, float InPlayRate, uint16 InFrameCounter, bool bInLooping)
{
	Session.WriteAccessCheck();

	TSharedPtr<Trace::TMonotonicTimeline<FTickRecordMessage>> Timeline;
	TSharedPtr<FPerObjectTimelineStorage> TimelineStorage;
	uint32* TimelineStorageIndexPtr = ObjectIdToTickRecordTimelineStorage.Find(InObjectId);
	if(TimelineStorageIndexPtr != nullptr)
	{
		TimelineStorage = PerObjectTimelineStorage[*TimelineStorageIndexPtr];
	}
	else
	{
		ObjectIdToTickRecordTimelineStorage.Add(InObjectId, PerObjectTimelineStorage.Num());
		TimelineStorage = PerObjectTimelineStorage.Add_GetRef(MakeShared<FPerObjectTimelineStorage>());
	}

	check(TimelineStorage.IsValid());

	uint32* TimelineIndexPtr = TimelineStorage->AssetIdToTickRecordTimeline.Find(InAssetId);
	if(TimelineIndexPtr != nullptr)
	{
		Timeline = TimelineStorage->Timelines[*TimelineIndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TMonotonicTimeline<FTickRecordMessage>>(Session.GetLinearAllocator());
		TimelineStorage->AssetIdToTickRecordTimeline.Add(InAssetId, TimelineStorage->Timelines.Num());
		TimelineStorage->Timelines.Add(Timeline.ToSharedRef());
	}

	// terminate existing scopes - check for continuity
	uint64 NumEvents = Timeline->GetEventCount();
	if(NumEvents > 0)
	{
		if(InFrameCounter - 1 != Timeline->GetEvent(NumEvents - 1).FrameCounter)
		{
			// discontinuous frame count, so add an end event at last time
			Timeline->AppendEndEvent(Timeline->GetEndTime());
		}
		else
		{
			// continuous frame time, add end event at current time
			Timeline->AppendEndEvent(InTime);
		}
	}

	FTickRecordMessage Message;
	Message.MessageId = Timeline->GetEventCount();
	Message.ComponentId = InObjectId;
	Message.AnimInstanceId = InSubObjectId;
	Message.AssetId = InAssetId;
	Message.BlendWeight = InBlendWeight;
	Message.PlaybackTime = InPlaybackTime;
	Message.RootMotionWeight = InRootMotionWeight;
	Message.PlayRate = InPlayRate;
	Message.FrameCounter = InFrameCounter;
	Message.Looping = bInLooping;

	Timeline->AppendBeginEvent(InTime, Message);

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendSkeletalMesh(uint64 InObjectId, const TArrayView<const int32>& InParentIndices)
{
	Session.WriteAccessCheck();

	if(SkeletalMeshIdToIndexMap.Find(InObjectId) == nullptr)
	{
		FSkeletalMeshInfo NewSkeletalMeshInfo;
		NewSkeletalMeshInfo.Id = InObjectId;
		NewSkeletalMeshInfo.BoneCount = (uint32)InParentIndices.Num();
		NewSkeletalMeshInfo.ParentIndicesStartIndex = SkeletalMeshParentIndices.Num();

		for(const int32& ParentIndex : InParentIndices)
		{
			SkeletalMeshParentIndices.PushBack() = ParentIndex;
		}

		int32 NewSkeletalMeshInfoIndex = SkeletalMeshInfos.Add(NewSkeletalMeshInfo);
		SkeletalMeshIdToIndexMap.Add(InObjectId, NewSkeletalMeshInfoIndex);
	}
}

void FAnimationProvider::AppendSkeletalMeshPose(uint64 InObjectId, uint64 InMeshId, double InTime, uint16 InLodIndex, uint16 InFrameCounter, const TArrayView<const FTransform>& InPose)
{
	Session.WriteAccessCheck();

	TSharedPtr<Trace::TMonotonicTimeline<FSkeletalMeshPoseMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToSkeletalMeshPoseTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		Timeline = SkeletalMeshPoseTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TMonotonicTimeline<FSkeletalMeshPoseMessage>>(Session.GetLinearAllocator());
		ObjectIdToSkeletalMeshPoseTimelines.Add(InObjectId, SkeletalMeshPoseTimelines.Num());
		SkeletalMeshPoseTimelines.Add(Timeline.ToSharedRef());
	}

	// terminate existing scopes - check for continuity
	uint64 NumEvents = Timeline->GetEventCount();
	if(NumEvents > 0)
	{
		if(InFrameCounter - 1 != Timeline->GetEvent(NumEvents - 1).FrameCounter)
		{
			// discontinuous frame count, so add an end event at last time
			Timeline->AppendEndEvent(Timeline->GetEndTime());
		}
		else
		{
			// continuous frame time, add end event at current time
			Timeline->AppendEndEvent(InTime);
		}
	}

	FSkeletalMeshPoseMessage Message;
	Message.MessageId = Timeline->GetEventCount();
	Message.TransformStartIndex = SkeletalMeshPoseTransforms.Num();
	Message.ComponentId = InObjectId;
	Message.MeshId = InMeshId;
	Message.NumTransforms = (uint16)InPose.Num();
	Message.LodIndex = InLodIndex;
	Message.FrameCounter = InFrameCounter;

	Timeline->AppendBeginEvent(InTime, Message);

	for(const FTransform& Transform : InPose)
	{
		SkeletalMeshPoseTransforms.PushBack() = Transform;
	}

	Session.UpdateDurationSeconds(InTime);
}
