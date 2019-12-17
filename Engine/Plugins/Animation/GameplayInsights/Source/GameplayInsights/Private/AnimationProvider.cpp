// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimationProvider.h"
#include "GameplayProvider.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

FName FAnimationProvider::ProviderName("AnimationProvider");

FAnimationProvider::FAnimationProvider(Trace::IAnalysisSession& InSession, FGameplayProvider& InGameplayProvider)
	: Session(InSession)
	, GameplayProvider(InGameplayProvider)
	, SkeletalMeshPoseTransforms(InSession.GetLinearAllocator(), 256)
	, SkeletalMeshCurves(InSession.GetLinearAllocator(), 256)
	, SkeletalMeshParentIndices(InSession.GetLinearAllocator(), 256)
{
	GameplayProvider.OnObjectEndPlay().AddRaw(this, &FAnimationProvider::HandleObjectEndPlay);
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

void FAnimationProvider::GetSkeletalMeshComponentSpacePose(const FSkeletalMeshPoseMessage& InMessage, const FSkeletalMeshInfo& InMeshInfo, FTransform& OutComponentToWorld, TArray<FTransform>& OutTransforms) const
{
	Session.ReadAccessCheck();

	if(InMeshInfo.BoneCount == InMessage.NumTransforms)
	{
		// Pre-alloc array
		OutTransforms.SetNumUninitialized(InMessage.NumTransforms);

		// First transform is always component to world
		OutComponentToWorld = InMessage.ComponentToWorld;
		uint64 StartTransformIndex = InMessage.TransformStartIndex;
		uint64 EndTransformIndex = InMessage.TransformStartIndex + InMessage.NumTransforms;
		uint64 SourceTransformIndex;
		int32 TargetTransformIndex = 0;
		for(SourceTransformIndex = StartTransformIndex; SourceTransformIndex < EndTransformIndex; ++SourceTransformIndex, ++TargetTransformIndex)
		{
			OutTransforms[TargetTransformIndex] = SkeletalMeshPoseTransforms[SourceTransformIndex];
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

const TCHAR* FAnimationProvider::GetName(uint32 InId) const
{
	return NameMap.FindRef(InId);
}

void FAnimationProvider::AppendTickRecord(uint64 InObjectId, uint64 InSubObjectId, double InTime, uint64 InAssetId, float InBlendWeight, float InPlaybackTime, float InRootMotionWeight, float InPlayRate, uint16 InFrameCounter, bool bInLooping)
{
	Session.WriteAccessCheck();

	TSharedPtr<Trace::TPointTimeline<FTickRecordMessage>> Timeline;
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
		Timeline = MakeShared<Trace::TPointTimeline<FTickRecordMessage>>(Session.GetLinearAllocator());
		TimelineStorage->AssetIdToTickRecordTimeline.Add(InAssetId, TimelineStorage->Timelines.Num());
		TimelineStorage->Timelines.Add(Timeline.ToSharedRef());
	}

	FTickRecordMessage Message;
	Message.ComponentId = InObjectId;
	Message.AnimInstanceId = InSubObjectId;
	Message.AssetId = InAssetId;
	Message.BlendWeight = InBlendWeight;
	Message.PlaybackTime = InPlaybackTime;
	Message.RootMotionWeight = InRootMotionWeight;
	Message.PlayRate = InPlayRate;
	Message.FrameCounter = InFrameCounter;
	Message.Looping = bInLooping;

	Timeline->AppendEvent(InTime, Message);

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

void FAnimationProvider::AppendSkeletalMeshComponent(uint64 InObjectId, uint64 InMeshId, double InTime, uint16 InLodIndex, uint16 InFrameCounter, const TArrayView<const FTransform>& InPose, const TArrayView<const FSkeletalMeshNamedCurve>& InCurves)
{
	Session.WriteAccessCheck();

	TSharedPtr<Trace::TIntervalTimeline<FSkeletalMeshPoseMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToSkeletalMeshPoseTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		Timeline = SkeletalMeshPoseTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TIntervalTimeline<FSkeletalMeshPoseMessage>>(Session.GetLinearAllocator());
		ObjectIdToSkeletalMeshPoseTimelines.Add(InObjectId, SkeletalMeshPoseTimelines.Num());
		SkeletalMeshPoseTimelines.Add(Timeline.ToSharedRef());
	}

	// terminate existing scopes
	uint64 NumEvents = Timeline->GetEventCount();
	if(NumEvents > 0)
	{
		// Add end event at current time
		Timeline->EndEvent(NumEvents - 1, InTime);
	}

	FSkeletalMeshPoseMessage Message;
	Message.ComponentToWorld = InPose[0];
	Message.TransformStartIndex = SkeletalMeshPoseTransforms.Num();
	Message.CurveStartIndex = SkeletalMeshCurves.Num();
	Message.ComponentId = InObjectId;
	Message.MeshId = InMeshId;
	Message.NumTransforms = (uint16)InPose.Num() - 1;
	Message.NumCurves = (uint16)InCurves.Num();
	Message.LodIndex = InLodIndex;
	Message.FrameCounter = InFrameCounter;

	Timeline->AppendBeginEvent(InTime, Message);

	for(int32 TransformIndex = 1; TransformIndex < InPose.Num(); ++TransformIndex)
	{
		SkeletalMeshPoseTransforms.PushBack() = InPose[TransformIndex];
	}

	for(const FSkeletalMeshNamedCurve& Curve : InCurves)
	{
		SkeletalMeshCurves.PushBack() = Curve;
	}

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendName(uint32 InId, const TCHAR* InName)
{
	Session.WriteAccessCheck();

	NameMap.Add(InId, Session.StoreString(InName));
}

void FAnimationProvider::HandleObjectEndPlay(uint64 InObjectId, double InTime, const FObjectInfo& InObjectInfo)
{
	// terminate all existing scopes for this object
	uint32* SkelMeshPoseIndexPtr = ObjectIdToSkeletalMeshPoseTimelines.Find(InObjectId);
	if(SkelMeshPoseIndexPtr != nullptr)
	{
		TSharedPtr<Trace::TIntervalTimeline<FSkeletalMeshPoseMessage>> Timeline = SkeletalMeshPoseTimelines[*SkelMeshPoseIndexPtr];
		uint64 NumEvents = Timeline->GetEventCount();
		if(NumEvents > 0)
		{
			Timeline->EndEvent(NumEvents - 1, InTime);
		}
	}
}