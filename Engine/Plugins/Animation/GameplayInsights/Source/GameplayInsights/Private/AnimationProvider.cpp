// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationProvider.h"
#include "GameplayProvider.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

#define LOCTEXT_NAMESPACE "AnimationProvider"

FName FAnimationProvider::ProviderName("AnimationProvider");

FAnimationProvider::FAnimationProvider(Trace::IAnalysisSession& InSession, FGameplayProvider& InGameplayProvider)
	: Session(InSession)
	, GameplayProvider(InGameplayProvider)
	, SkeletalMeshPoseTransforms(InSession.GetLinearAllocator(), 256)
	, SkeletalMeshCurves(InSession.GetLinearAllocator(), 256)
	, SkeletalMeshParentIndices(InSession.GetLinearAllocator(), 256)
	, bHasAnyData(false)
{
	GameplayProvider.OnObjectEndPlay().AddRaw(this, &FAnimationProvider::HandleObjectEndPlay);
}

bool FAnimationProvider::ReadSkeletalMeshPoseTimeline(uint64 InObjectId, TFunctionRef<void(const SkeletalMeshPoseTimeline&, bool)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToSkeletalMeshPoseTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(SkeletalMeshPoseTimelineStorage.Num()))
		{
			const TSharedRef<FSkeletalMeshTimelineStorage>& SkeletalMeshTimelineStorage = SkeletalMeshPoseTimelineStorage[*IndexPtr];
			Callback(*SkeletalMeshTimelineStorage->Timeline, SkeletalMeshTimelineStorage->AllCurveIds.Num() > 0);
			return true;
		}
	}

	return false;
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

void FAnimationProvider::EnumerateSkeletalMeshCurveIds(uint64 InObjectId, TFunctionRef<void(uint32)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToSkeletalMeshPoseTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(SkeletalMeshPoseTimelineStorage.Num()))
		{
			const TSharedRef<FSkeletalMeshTimelineStorage>& TimelineStorage = SkeletalMeshPoseTimelineStorage[*IndexPtr];
			for(uint32 Id : TimelineStorage->AllCurveIds)
			{
				Callback(Id);
			}
		}
	}
}

void FAnimationProvider::EnumerateSkeletalMeshCurves(const FSkeletalMeshPoseMessage& InMessage, TFunctionRef<void(const FSkeletalMeshNamedCurve&)> Callback) const
{
	Session.ReadAccessCheck();

	uint64 StartCurveIndex = InMessage.CurveStartIndex;
	uint64 EndCurveIndex = InMessage.CurveStartIndex + InMessage.NumCurves;

	for(uint64 CurveIndex = StartCurveIndex; CurveIndex < EndCurveIndex; ++CurveIndex)
	{
		Callback(SkeletalMeshCurves[CurveIndex]);
	}
}

void FAnimationProvider::EnumerateTickRecordIds(uint64 InObjectId, TFunctionRef<void(uint64, int32)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToTickRecordTimelineStorage.Find(InObjectId);
	if (IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(TickRecordTimelineStorage.Num()))
		{
			const TSharedRef<FTickRecordTimelineStorage>& TimelineStorage = TickRecordTimelineStorage[*IndexPtr];
			for (auto AssetIdPair : TimelineStorage->AssetIdAndPlayers)
			{
				Callback(AssetIdPair.Key, AssetIdPair.Value);
			}
		}
	}
}

bool FAnimationProvider::ReadTickRecordTimeline(uint64 InObjectId, TFunctionRef<void(const TickRecordTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToTickRecordTimelineStorage.Find(InObjectId);
	if (IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(TickRecordTimelineStorage.Num()))
		{
			Callback(*TickRecordTimelineStorage[*IndexPtr]->Timeline.Get());
			return true;
		}
	}

	return false;
}

bool FAnimationProvider::ReadAnimGraphTimeline(uint64 InObjectId, TFunctionRef<void(const AnimGraphTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToAnimGraphTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(AnimGraphTimelines.Num()))
		{
			Callback(*AnimGraphTimelines[*IndexPtr]);
			return true;
		}
	}

	return false;
}

bool FAnimationProvider::ReadAnimNodesTimeline(uint64 InObjectId, TFunctionRef<void(const AnimNodesTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToAnimNodeTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(AnimNodeTimelines.Num()))
		{
			Callback(*AnimNodeTimelines[*IndexPtr]);
			return true;
		}
	}

	return false;
}

bool FAnimationProvider::ReadAnimNodeValuesTimeline(uint64 InObjectId, TFunctionRef<void(const AnimNodeValuesTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToAnimNodeValueTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(AnimNodeValueTimelines.Num()))
		{
			Callback(*AnimNodeValueTimelines[*IndexPtr]);
			return true;
		}
	}

	return false;
}

bool FAnimationProvider::ReadAnimSequencePlayersTimeline(uint64 InObjectId, TFunctionRef<void(const AnimSequencePlayersTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToAnimSequencePlayerTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(AnimSequencePlayerTimelines.Num()))
		{
			Callback(*AnimSequencePlayerTimelines[*IndexPtr]);
			return true;
		}
	}

	return false;
}

bool FAnimationProvider::ReadAnimBlendSpacePlayersTimeline(uint64 InObjectId, TFunctionRef<void(const BlendSpacePlayersTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToBlendSpacePlayerTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(BlendSpacePlayerTimelines.Num()))
		{
			Callback(*BlendSpacePlayerTimelines[*IndexPtr]);
			return true;
		}
	}

	return false;
}

bool FAnimationProvider::ReadStateMachinesTimeline(uint64 InObjectId, TFunctionRef<void(const StateMachinesTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToStateMachineTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(StateMachineTimelines.Num()))
		{
			Callback(*StateMachineTimelines[*IndexPtr]);
			return true;
		}
	}

	return false;
}

bool FAnimationProvider::ReadNotifyTimeline(uint64 InObjectId, TFunctionRef<void(const AnimNotifyTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToAnimNotifyTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(AnimNotifyTimelines.Num()))
		{
			Callback(*AnimNotifyTimelines[*IndexPtr]);
			return true;
		}
	}

	return false;
}

void FAnimationProvider::EnumerateNotifyStateTimelines(uint64 InObjectId, TFunctionRef<void(uint64, const AnimNotifyTimeline&)> Callback) const
{
	const uint32* IndexPtr = ObjectIdToAnimNotifyStateTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(AnimNotifyStateTimelineStorage.Num()))
		{
			const TSharedRef<FAnimNotifyStateTimelineStorage>& TimelineStorage = AnimNotifyStateTimelineStorage[*IndexPtr];
			for(auto NotifyIdPair : TimelineStorage->NotifyIdToAnimNotifyStateTimeline)
			{
				Callback(NotifyIdPair.Key, TimelineStorage->Timelines[NotifyIdPair.Value].Get());
			}
		}
	}
}

bool FAnimationProvider::ReadMontageTimeline(uint64 InObjectId, TFunctionRef<void(const AnimMontageTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToAnimMontageTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(AnimMontageTimelineStorage.Num()))
		{
			Callback(*AnimMontageTimelineStorage[*IndexPtr]->Timeline.Get());
			return true;
		}
	}

	return false;
}

void FAnimationProvider::EnumerateMontageIds(uint64 InObjectId, TFunctionRef<void(uint64)> Callback) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = ObjectIdToAnimMontageTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(AnimMontageTimelineStorage.Num()))
		{
			const TSharedRef<FMontageTimelineStorage>& TimelineStorage = AnimMontageTimelineStorage[*IndexPtr];
			for(uint64 Id : TimelineStorage->AllMontageIds)
			{
				Callback(Id);
			}
		}
	}
}

const FSkeletalMeshInfo* FAnimationProvider::FindSkeletalMeshInfo(uint64 InObjectId) const
{
	Session.ReadAccessCheck();

	const uint32* IndexPtr = SkeletalMeshIdToIndexMap.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		if (*IndexPtr < uint32(SkeletalMeshInfos.Num()))
		{
			return &SkeletalMeshInfos[*IndexPtr];
		}
	}

	return nullptr;
}

const TCHAR* FAnimationProvider::GetName(uint32 InId) const
{
	const TCHAR* const* FoundName = NameMap.Find(InId);
	if(FoundName != nullptr)
	{
		return *FoundName;
	}

	static FText UnknownText(LOCTEXT("UnknownName", "Unknown"));
	return *UnknownText.ToString();
}

FText FAnimationProvider::FormatNodeKeyValue(const FAnimNodeValueMessage& InMessage) const
{
	return FText::Format(LOCTEXT("KeyValueFormat", "{0} = {1}"), FText::FromString(InMessage.Key), FormatNodeValue(InMessage));
}

FText FAnimationProvider::FormatNodeValue(const FAnimNodeValueMessage& InMessage) const
{
	FText Text;

	switch(InMessage.Value.Type)
	{
	case EAnimNodeValueType::Bool:
		Text = InMessage.Value.Bool.bValue ? LOCTEXT("True", "true") : LOCTEXT("False", "false");
		break;
	case EAnimNodeValueType::Int32:
		Text =  FText::AsNumber(InMessage.Value.Int32.Value);
		break;
	case EAnimNodeValueType::Float:
		Text = FText::AsNumber(InMessage.Value.Float.Value);
		break;
	case EAnimNodeValueType::Vector2D:
		Text = FText::Format(LOCTEXT("Vector2Format", "({0}, {1})"), FText::AsNumber(InMessage.Value.Vector.Value.X), FText::AsNumber(InMessage.Value.Vector.Value.Y));
		break;
	case EAnimNodeValueType::Vector:
		Text = FText::Format(LOCTEXT("VectorFormat", "({0}, {1}, {2})"), FText::AsNumber(InMessage.Value.Vector.Value.X), FText::AsNumber(InMessage.Value.Vector.Value.Y), FText::AsNumber(InMessage.Value.Vector.Value.Z));
		break;
	case EAnimNodeValueType::String:
		Text = FText::FromString(InMessage.Value.String.Value);
		break;
	case EAnimNodeValueType::Object:
	{
		const FObjectInfo& ObjectInfo = GameplayProvider.GetObjectInfo(InMessage.Value.Object.Value);
		Text = FText::FromString(ObjectInfo.PathName);
		break;
	}
	case EAnimNodeValueType::Class:
	{
		const FClassInfo& ClassInfo = GameplayProvider.GetClassInfo(InMessage.Value.Class.Value);
		Text = FText::FromString(ClassInfo.PathName);
		break;
	}
	}

	return Text;
}

bool FAnimationProvider::HasAnyData() const
{
	Session.ReadAccessCheck();

	return bHasAnyData;
}

void FAnimationProvider::AppendTickRecord(uint64 InAnimInstanceId, double InTime, uint64 InAssetId, int32 InNodeId, float InBlendWeight, float InPlaybackTime, float InRootMotionWeight, float InPlayRate, float InBlendSpacePositionX, float InBlendSpacePositionY, uint16 InFrameCounter, bool bInLooping, bool bInIsBlendSpace)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<FTickRecordTimelineStorage> TimelineStorage;
	uint32* TimelineStorageIndexPtr = ObjectIdToTickRecordTimelineStorage.Find(InAnimInstanceId);
	if(TimelineStorageIndexPtr != nullptr)
	{
		TimelineStorage = TickRecordTimelineStorage[*TimelineStorageIndexPtr];
	}
	else
	{
		ObjectIdToTickRecordTimelineStorage.Add(InAnimInstanceId, TickRecordTimelineStorage.Num());
		TimelineStorage = TickRecordTimelineStorage.Add_GetRef(MakeShared<FTickRecordTimelineStorage>());
		TimelineStorage->Timeline = MakeShared<Trace::TPointTimeline<FTickRecordMessage>>(Session.GetLinearAllocator());
		TimelineStorage->Timeline->SetEnumerateOutsideRange(true);
	}

	check(TimelineStorage.IsValid());

	TimelineStorage->AssetIdAndPlayers.Add(TTuple<uint64, int32>(InAssetId, InNodeId));

	FTickRecordMessage Message;
	Message.AnimInstanceId = InAnimInstanceId;
	Message.AssetId = InAssetId;
	Message.NodeId = InNodeId;
	Message.BlendWeight = InBlendWeight;
	Message.PlaybackTime = InPlaybackTime;
	Message.RootMotionWeight = InRootMotionWeight;
	Message.PlayRate = InPlayRate;
	Message.BlendSpacePositionX = InBlendSpacePositionX;
	Message.BlendSpacePositionY = InBlendSpacePositionY;
	Message.FrameCounter = InFrameCounter;
	Message.bLooping = bInLooping;
	Message.bIsBlendSpace = bInIsBlendSpace;

	TimelineStorage->Timeline->AppendEvent(InTime, Message);

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendSkeletalMesh(uint64 InObjectId, const TArrayView<const int32>& InParentIndices)
{
	Session.WriteAccessCheck();

	if(SkeletalMeshIdToIndexMap.Find(InObjectId) == nullptr)
	{
		bHasAnyData = true;

		FSkeletalMeshInfo NewSkeletalMeshInfo;
		NewSkeletalMeshInfo.Id = InObjectId;
		NewSkeletalMeshInfo.BoneCount = (uint32)InParentIndices.Num();
		NewSkeletalMeshInfo.ParentIndicesStartIndex = SkeletalMeshParentIndices.Num();

		for(const int32& ParentIndex : InParentIndices)
		{
			SkeletalMeshParentIndices.PushBack() = ParentIndex;
		}

		uint32 NewSkeletalMeshInfoIndex = SkeletalMeshInfos.Add(NewSkeletalMeshInfo);
		SkeletalMeshIdToIndexMap.Add(InObjectId, NewSkeletalMeshInfoIndex);
	}
}

void FAnimationProvider::AppendSkeletalMeshComponent(uint64 InObjectId, uint64 InMeshId, double InTime, uint16 InLodIndex, uint16 InFrameCounter, const TArrayView<const FTransform>& InPose, const TArrayView<const FSkeletalMeshNamedCurve>& InCurves)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<FSkeletalMeshTimelineStorage> TimelineStorage;
	uint32* IndexPtr = ObjectIdToSkeletalMeshPoseTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		TimelineStorage = SkeletalMeshPoseTimelineStorage[*IndexPtr];
	}
	else
	{
		TimelineStorage = MakeShared<FSkeletalMeshTimelineStorage>();
		TimelineStorage->Timeline = MakeShared<Trace::TIntervalTimeline<FSkeletalMeshPoseMessage>>(Session.GetLinearAllocator());
		ObjectIdToSkeletalMeshPoseTimelines.Add(InObjectId, SkeletalMeshPoseTimelineStorage.Num());
		SkeletalMeshPoseTimelineStorage.Add(TimelineStorage.ToSharedRef());
	}

	// terminate existing scopes
	uint64 NumEvents = TimelineStorage->Timeline->GetEventCount();
	if(NumEvents > 0)
	{
		// Add end event at current time
		TimelineStorage->Timeline->EndEvent(NumEvents - 1, InTime);
	}

	FSkeletalMeshPoseMessage Message;
	Message.ComponentToWorld = InPose[0];
	Message.TransformStartIndex = SkeletalMeshPoseTransforms.Num();
	Message.CurveStartIndex = SkeletalMeshCurves.Num();
	Message.ComponentId = InObjectId;
	Message.MeshId = InMeshId;
	Message.MeshName = GameplayProvider.GetObjectInfo(Message.MeshId).Name;
	Message.NumTransforms = (uint16)InPose.Num() - 1;
	Message.NumCurves = (uint16)InCurves.Num();
	Message.LodIndex = InLodIndex;
	Message.FrameCounter = InFrameCounter;

	TimelineStorage->Timeline->AppendBeginEvent(InTime, Message);

	for(int32 TransformIndex = 1; TransformIndex < InPose.Num(); ++TransformIndex)
	{
		SkeletalMeshPoseTransforms.PushBack() = InPose[TransformIndex];
	}

	for(const FSkeletalMeshNamedCurve& Curve : InCurves)
	{
		SkeletalMeshCurves.PushBack() = Curve;
		TimelineStorage->AllCurveIds.Add(Curve.Id);
	}

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendSkeletalMeshComponent(uint64 InObjectId, uint64 InMeshId, double InTime, uint16 InLodIndex, uint16 InFrameCounter, const FTransform& InComponentToWorld, const TArrayView<const FTransform>& InPose, const TArrayView<const uint32>& InCurveIds, const TArrayView<const float>& InCurveValues)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<FSkeletalMeshTimelineStorage> TimelineStorage;
	uint32* IndexPtr = ObjectIdToSkeletalMeshPoseTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		TimelineStorage = SkeletalMeshPoseTimelineStorage[*IndexPtr];
	}
	else
	{
		TimelineStorage = MakeShared<FSkeletalMeshTimelineStorage>();
		TimelineStorage->Timeline = MakeShared<Trace::TIntervalTimeline<FSkeletalMeshPoseMessage>>(Session.GetLinearAllocator());
		ObjectIdToSkeletalMeshPoseTimelines.Add(InObjectId, SkeletalMeshPoseTimelineStorage.Num());
		SkeletalMeshPoseTimelineStorage.Add(TimelineStorage.ToSharedRef());
	}

	// terminate existing scopes
	uint64 NumEvents = TimelineStorage->Timeline->GetEventCount();
	if(NumEvents > 0)
	{
		// Add end event at current time
		TimelineStorage->Timeline->EndEvent(NumEvents - 1, InTime);
	}

	const int32 NumCurves = InCurveIds.Num();

	FSkeletalMeshPoseMessage Message;
	Message.ComponentToWorld = InComponentToWorld;
	Message.TransformStartIndex = SkeletalMeshPoseTransforms.Num();
	Message.CurveStartIndex = SkeletalMeshCurves.Num();
	Message.ComponentId = InObjectId;
	Message.MeshId = InMeshId;
	Message.MeshName = GameplayProvider.GetObjectInfo(Message.MeshId).Name;
	Message.NumTransforms = (uint16)InPose.Num();
	Message.NumCurves = (uint16)NumCurves;
	Message.LodIndex = InLodIndex;
	Message.FrameCounter = InFrameCounter;

	TimelineStorage->Timeline->AppendBeginEvent(InTime, Message);

	for(const FTransform& Transform : InPose)
	{
		SkeletalMeshPoseTransforms.PushBack() = Transform;
	}

	for(int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
	{
		SkeletalMeshCurves.PushBack() = { InCurveIds[CurveIndex], InCurveValues[CurveIndex] };
		TimelineStorage->AllCurveIds.Add(InCurveIds[CurveIndex]);
	}

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendName(uint32 InId, const TCHAR* InName)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	NameMap.Add(InId, Session.StoreString(InName));
}

void FAnimationProvider::HandleObjectEndPlay(uint64 InObjectId, double InTime, const FObjectInfo& InObjectInfo)
{
	// terminate all existing scopes for this object
	uint32* SkelMeshPoseIndexPtr = ObjectIdToSkeletalMeshPoseTimelines.Find(InObjectId);
	if(SkelMeshPoseIndexPtr != nullptr)
	{
		TSharedPtr<Trace::TIntervalTimeline<FSkeletalMeshPoseMessage>> Timeline = SkeletalMeshPoseTimelineStorage[*SkelMeshPoseIndexPtr]->Timeline;
		uint64 NumEvents = Timeline->GetEventCount();
		if(NumEvents > 0)
		{
			Timeline->EndEvent(NumEvents - 1, InTime);
		}
	}
}

void FAnimationProvider::AppendSkeletalMeshFrame(uint64 InObjectId, double InTime, uint16 InFrameCounter)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<Trace::TIntervalTimeline<FSkeletalMeshFrameMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToSkeletalMeshFrameTimelines.Find(InObjectId);
	if(IndexPtr != nullptr)
	{
		check(SkeletalMeshFrameTimelines.IsValidIndex(*IndexPtr));
		Timeline = SkeletalMeshFrameTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TIntervalTimeline<FSkeletalMeshFrameMessage>>(Session.GetLinearAllocator());
		ObjectIdToSkeletalMeshFrameTimelines.Add(InObjectId, SkeletalMeshFrameTimelines.Num());
		SkeletalMeshFrameTimelines.Add(Timeline.ToSharedRef());
	}

	// terminate existing scopes
	uint64 NumEvents = Timeline->GetEventCount();
	if(NumEvents > 0)
	{
		// Add end event at current time
		Timeline->EndEvent(NumEvents - 1, InTime);
	}

	FSkeletalMeshFrameMessage Message;
	Message.ComponentId = InObjectId;
	Message.FrameCounter = InFrameCounter;

	Timeline->AppendBeginEvent(InTime, Message);

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendAnimGraph(uint64 InAnimInstanceId, double InStartTime, double InEndTime, int32 InNodeCount, uint16 InFrameCounter, uint8 InPhase)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<Trace::TIntervalTimeline<FAnimGraphMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToAnimGraphTimelines.Find(InAnimInstanceId);
	if(IndexPtr != nullptr)
	{
		check(AnimGraphTimelines.IsValidIndex(*IndexPtr));
		Timeline = AnimGraphTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TIntervalTimeline<FAnimGraphMessage>>(Session.GetLinearAllocator());
		ObjectIdToAnimGraphTimelines.Add(InAnimInstanceId, AnimGraphTimelines.Num());
		AnimGraphTimelines.Add(Timeline.ToSharedRef());
	}

	FAnimGraphMessage Message;
	Message.AnimInstanceId = InAnimInstanceId;
	Message.NodeCount = InNodeCount;
	Message.FrameCounter = InFrameCounter;
	Message.Phase = (EAnimGraphPhase)InPhase;

	uint64 EventIndex = Timeline->AppendBeginEvent(InStartTime, Message);
	Timeline->EndEvent(EventIndex, InEndTime);

	Session.UpdateDurationSeconds(InStartTime);
}

void FAnimationProvider::AppendAnimNodeStart(uint64 InAnimInstanceId, double InStartTime, uint16 InFrameCounter, int32 InNodeId, int32 PreviousNodeId, float InWeight, float InRootMotionWeight, const TCHAR* InTargetNodeName, uint8 InPhase)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<Trace::TPointTimeline<FAnimNodeMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToAnimNodeTimelines.Find(InAnimInstanceId);
	if(IndexPtr != nullptr)
	{
		check(AnimNodeTimelines.IsValidIndex(*IndexPtr));
		Timeline = AnimNodeTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TPointTimeline<FAnimNodeMessage>>(Session.GetLinearAllocator());
		ObjectIdToAnimNodeTimelines.Add(InAnimInstanceId, AnimNodeTimelines.Num());
		AnimNodeTimelines.Add(Timeline.ToSharedRef());
	}

	FAnimNodeMessage Message;
	Message.NodeName = Session.StoreString(InTargetNodeName);
	Message.AnimInstanceId = InAnimInstanceId;
	Message.PreviousNodeId = PreviousNodeId;
	Message.NodeId = InNodeId;
	Message.Weight = InWeight;
	Message.RootMotionWeight = InRootMotionWeight;
	Message.FrameCounter = InFrameCounter;
	Message.Phase = (EAnimGraphPhase)InPhase;

	Timeline->AppendEvent(InStartTime, Message);

	Session.UpdateDurationSeconds(InStartTime);
}

void FAnimationProvider::AppendAnimSequencePlayer(uint64 InAnimInstanceId, double InTime, int32 InNodeId, float InPosition, float InLength, uint16 InFrameCounter)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<Trace::TPointTimeline<FAnimSequencePlayerMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToAnimSequencePlayerTimelines.Find(InAnimInstanceId);
	if(IndexPtr != nullptr)
	{
		check(AnimSequencePlayerTimelines.IsValidIndex(*IndexPtr));
		Timeline = AnimSequencePlayerTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TPointTimeline<FAnimSequencePlayerMessage>>(Session.GetLinearAllocator());
		ObjectIdToAnimSequencePlayerTimelines.Add(InAnimInstanceId, AnimSequencePlayerTimelines.Num());
		AnimSequencePlayerTimelines.Add(Timeline.ToSharedRef());
	}

	FAnimSequencePlayerMessage Message;
	Message.AnimInstanceId = InAnimInstanceId;
	Message.NodeId = InNodeId;
	Message.Position = InPosition;
	Message.Length = InLength;
	Message.FrameCounter = InFrameCounter;

	Timeline->AppendEvent(InTime, Message);

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendBlendSpacePlayer(uint64 InAnimInstanceId, double InTime, int32 InNodeId, uint64 InBlendSpaceId, float InPositionX, float InPositionY, float InPositionZ)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<Trace::TPointTimeline<FBlendSpacePlayerMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToBlendSpacePlayerTimelines.Find(InAnimInstanceId);
	if(IndexPtr != nullptr)
	{
		check(BlendSpacePlayerTimelines.IsValidIndex(*IndexPtr));
		Timeline = BlendSpacePlayerTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TPointTimeline<FBlendSpacePlayerMessage>>(Session.GetLinearAllocator());
		ObjectIdToBlendSpacePlayerTimelines.Add(InAnimInstanceId, BlendSpacePlayerTimelines.Num());
		BlendSpacePlayerTimelines.Add(Timeline.ToSharedRef());
	}

	FBlendSpacePlayerMessage Message;
	Message.AnimInstanceId = InAnimInstanceId;
	Message.BlendSpaceId = InBlendSpaceId;
	Message.NodeId = InNodeId;
	Message.PositionX = InPositionX;
	Message.PositionY = InPositionY;
	Message.PositionZ = InPositionZ;

	Timeline->AppendEvent(InTime, Message);

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, bool bInValue)
{
	FAnimNodeValueMessage Message;
	Message.Value.Bool.bValue = bInValue;
	Message.Value.Type = EAnimNodeValueType::Bool;

	AppendAnimNodeValue(InAnimInstanceId, InTime, InFrameCounter, InNodeId, InKey, Message);
}

void FAnimationProvider::AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, int32 InValue)
{
	FAnimNodeValueMessage Message;
	Message.Value.Int32.Value = InValue;
	Message.Value.Type = EAnimNodeValueType::Int32;

	AppendAnimNodeValue(InAnimInstanceId, InTime, InFrameCounter, InNodeId, InKey, Message);
}

void FAnimationProvider::AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, float InValue)
{
	FAnimNodeValueMessage Message;
	Message.Value.Float.Value = InValue;
	Message.Value.Type = EAnimNodeValueType::Float;

	AppendAnimNodeValue(InAnimInstanceId, InTime, InFrameCounter, InNodeId, InKey, Message);
}

void FAnimationProvider::AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, const FVector2D& InValue)
{
	FAnimNodeValueMessage Message;
	Message.Value.Vector2D.Value = InValue;
	Message.Value.Type = EAnimNodeValueType::Vector2D;

	AppendAnimNodeValue(InAnimInstanceId, InTime, InFrameCounter, InNodeId, InKey, Message);
}

void FAnimationProvider::AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, const FVector& InValue)
{
	FAnimNodeValueMessage Message;
	Message.Value.Vector.Value = InValue;
	Message.Value.Type = EAnimNodeValueType::Vector;

	AppendAnimNodeValue(InAnimInstanceId, InTime, InFrameCounter, InNodeId, InKey, Message);
}

void FAnimationProvider::AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, const TCHAR* InValue)
{
	FAnimNodeValueMessage Message;
	Message.Value.String.Value = Session.StoreString(InValue);
	Message.Value.Type = EAnimNodeValueType::String;

	AppendAnimNodeValue(InAnimInstanceId, InTime, InFrameCounter, InNodeId, InKey, Message);
}

void FAnimationProvider::AppendAnimNodeValueObject(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, uint64 InValue)
{
	FAnimNodeValueMessage Message;
	Message.Value.Object.Value = InValue;
	Message.Value.Type = EAnimNodeValueType::Object;

	AppendAnimNodeValue(InAnimInstanceId, InTime, InFrameCounter, InNodeId, InKey, Message);
}

void FAnimationProvider::AppendAnimNodeValueClass(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, uint64 InValue)
{
	FAnimNodeValueMessage Message;
	Message.Value.Class.Value = InValue;
	Message.Value.Type = EAnimNodeValueType::Class;

	AppendAnimNodeValue(InAnimInstanceId, InTime, InFrameCounter, InNodeId, InKey, Message);
}

void FAnimationProvider::AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, FAnimNodeValueMessage& InMessage)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<Trace::TPointTimeline<FAnimNodeValueMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToAnimNodeValueTimelines.Find(InAnimInstanceId);
	if(IndexPtr != nullptr)
	{
		Timeline = AnimNodeValueTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TPointTimeline<FAnimNodeValueMessage>>(Session.GetLinearAllocator());
		ObjectIdToAnimNodeValueTimelines.Add(InAnimInstanceId, AnimNodeValueTimelines.Num());
		AnimNodeValueTimelines.Add(Timeline.ToSharedRef());
	}

	InMessage.Key = Session.StoreString(InKey);
	InMessage.AnimInstanceId = InAnimInstanceId;
	InMessage.NodeId = InNodeId;
	InMessage.FrameCounter = InFrameCounter;

	Timeline->AppendEvent(InTime, InMessage);

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendStateMachineState(uint64 InAnimInstanceId, double InTime, int32 InNodeId, int32 InStateMachineIndex, int32 InStateIndex, float InStateWeight, float InElapsedTime)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<Trace::TPointTimeline<FAnimStateMachineMessage>> Timeline;
	uint32* IndexPtr = ObjectIdToStateMachineTimelines.Find(InAnimInstanceId);
	if(IndexPtr != nullptr)
	{
		Timeline = StateMachineTimelines[*IndexPtr];
	}
	else
	{
		Timeline = MakeShared<Trace::TPointTimeline<FAnimStateMachineMessage>>(Session.GetLinearAllocator());
		ObjectIdToStateMachineTimelines.Add(InAnimInstanceId, StateMachineTimelines.Num());
		StateMachineTimelines.Add(Timeline.ToSharedRef());
	}

	FAnimStateMachineMessage Message;
	Message.AnimInstanceId = InAnimInstanceId;
	Message.NodeId = InNodeId;
	Message.StateMachineIndex = InStateMachineIndex;
	Message.StateIndex = InStateIndex;
	Message.StateWeight = InStateWeight;
	Message.ElapsedTime = InElapsedTime;

	Timeline->AppendEvent(InTime, Message);

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendNotify(uint64 InAnimInstanceId, double InTime, uint64 InAssetId, uint64 InNotifyId, uint32 InNameId, float InNotifyTime, float InNotifyDuration, EAnimNotifyMessageType InNotifyEventType)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	// Check if stateful or event-based
	if(InNotifyEventType == EAnimNotifyMessageType::Begin || InNotifyEventType == EAnimNotifyMessageType::End)
	{
		TSharedPtr<Trace::TIntervalTimeline<FAnimNotifyMessage>> Timeline;
		TSharedPtr<FAnimNotifyStateTimelineStorage> TimelineStorage;
		uint32* TimelineStorageIndexPtr = ObjectIdToAnimNotifyStateTimelines.Find(InAnimInstanceId);
		if(TimelineStorageIndexPtr != nullptr)
		{
			TimelineStorage = AnimNotifyStateTimelineStorage[*TimelineStorageIndexPtr];
		}
		else
		{
			ObjectIdToAnimNotifyStateTimelines.Add(InAnimInstanceId, AnimNotifyStateTimelineStorage.Num());
			TimelineStorage = AnimNotifyStateTimelineStorage.Add_GetRef(MakeShared<FAnimNotifyStateTimelineStorage>());
		}

		check(TimelineStorage.IsValid());

		uint32* TimelineIndexPtr = TimelineStorage->NotifyIdToAnimNotifyStateTimeline.Find(InNotifyId);
		if(TimelineIndexPtr != nullptr)
		{
			Timeline = TimelineStorage->Timelines[*TimelineIndexPtr];
		}
		else
		{
			Timeline = MakeShared<Trace::TIntervalTimeline<FAnimNotifyMessage>>(Session.GetLinearAllocator());
			TimelineStorage->NotifyIdToAnimNotifyStateTimeline.Add(InNotifyId, TimelineStorage->Timelines.Num());
			TimelineStorage->Timelines.Add(Timeline.ToSharedRef());
		}

		if(InNotifyEventType == EAnimNotifyMessageType::Begin)
		{
			FAnimNotifyMessage Message;
			Message.AnimInstanceId = InAnimInstanceId;
			Message.AssetId = InAssetId;
			Message.NotifyId = InNotifyId;
			Message.NameId = InNameId;
			Message.Name = GetName(InNameId);
			Message.Time = InNotifyTime; 
			Message.Duration = InNotifyDuration;
			Message.NotifyEventType = InNotifyEventType;

			Timeline->AppendBeginEvent(InTime, Message);
		}
		else if(Timeline->GetEventCount() > 0)
		{
			Timeline->EndEvent(Timeline->GetEventCount() - 1, InTime);
		}
	}
	else
	{
		TSharedPtr<Trace::TPointTimeline<FAnimNotifyMessage>> Timeline;
		uint32* IndexPtr = ObjectIdToAnimNotifyTimelines.Find(InAnimInstanceId);
		if(IndexPtr != nullptr)
		{
			Timeline = AnimNotifyTimelines[*IndexPtr];
		}
		else
		{
			Timeline = MakeShared<Trace::TPointTimeline<FAnimNotifyMessage>>(Session.GetLinearAllocator());
			ObjectIdToAnimNotifyTimelines.Add(InAnimInstanceId, AnimNotifyTimelines.Num());
			AnimNotifyTimelines.Add(Timeline.ToSharedRef());
		}

		FAnimNotifyMessage Message;
		Message.AnimInstanceId = InAnimInstanceId;
		Message.AssetId = InAssetId;
		Message.NotifyId = InNotifyId;
		Message.NameId = InNameId;
		Message.Name = GetName(InNameId);
		Message.Time = InNotifyTime; 
		Message.Duration = InNotifyDuration;
		Message.NotifyEventType = InNotifyEventType;

		Timeline->AppendEvent(InTime, Message);
	}

	Session.UpdateDurationSeconds(InTime);
}

void FAnimationProvider::AppendMontage(uint64 InAnimInstanceId, double InTime, uint64 InMontageId, uint32 InCurrentSectionNameId, uint32 InNextSectionNameId, float InWeight, float InDesiredWeight, uint16 InFrameCounter)
{
	Session.WriteAccessCheck();

	bHasAnyData = true;

	TSharedPtr<FMontageTimelineStorage> TimelineStorage;
	uint32* IndexPtr = ObjectIdToAnimMontageTimelines.Find(InAnimInstanceId);
	if(IndexPtr != nullptr)
	{
		TimelineStorage = AnimMontageTimelineStorage[*IndexPtr];
	}
	else
	{
		TimelineStorage = MakeShared<FMontageTimelineStorage>();
		TimelineStorage->Timeline = MakeShared<Trace::TPointTimeline<FAnimMontageMessage>>(Session.GetLinearAllocator());
		TimelineStorage->Timeline->SetEnumerateOutsideRange(true);
		ObjectIdToAnimMontageTimelines.Add(InAnimInstanceId, AnimMontageTimelineStorage.Num());
		AnimMontageTimelineStorage.Add(TimelineStorage.ToSharedRef());
	}

	TimelineStorage->AllMontageIds.Add(InMontageId);

	FAnimMontageMessage Message;
	Message.AnimInstanceId = InAnimInstanceId;
	Message.MontageId = InMontageId;
	Message.CurrentSectionNameId = InCurrentSectionNameId;
	Message.NextSectionNameId = InNextSectionNameId;
	Message.Weight = InWeight;
	Message.DesiredWeight = InDesiredWeight;
	Message.FrameCounter = InFrameCounter;

	TimelineStorage->Timeline->AppendEvent(InTime, Message);

	Session.UpdateDurationSeconds(InTime);
}

#undef LOCTEXT_NAMESPACE