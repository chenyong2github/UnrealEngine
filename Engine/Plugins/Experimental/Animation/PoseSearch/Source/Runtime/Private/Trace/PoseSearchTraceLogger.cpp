// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchTraceLogger.h"
#include "Animation/AnimInstanceProxy.h"
#include "Trace/Trace.inl"
#include "Animation/AnimNodeBase.h"

#if UE_POSE_SEARCH_TRACE_ENABLED

UE_TRACE_CHANNEL_DEFINE(PoseSearchChannel);

UE_TRACE_EVENT_BEGIN(PoseSearch, MotionMatchingState)
	UE_TRACE_EVENT_FIELD(uint8[], Data)
UE_TRACE_EVENT_END()

namespace UE { namespace PoseSearch {

const FName FTraceLogger::Name("PoseSearch");
const FName FTraceMotionMatchingState::Name("MotionMatchingState");

static bool ShouldTrace(const FAnimationBaseContext& InContext)
{
	const bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(PoseSearchChannel);
	if (!bChannelEnabled)
	{
		return false;
	}

	if (InContext.GetCurrentNodeId() == INDEX_NONE)
	{
		return false;
	}

	check(InContext.AnimInstanceProxy);
	return !CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent());
}

FArchive& operator<<(FArchive& Ar, FTraceMessage& State)
{
	Ar << State.Cycle;
	Ar << State.AnimInstanceId;
	Ar << State.SkeletalMeshComponentId;
	Ar << State.NodeId;
	Ar << State.FrameCounter;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FTraceMotionMatchingStatePoseEntry& Entry)
{
	Ar << Entry.DbPoseIdx;
	Ar << Entry.Cost;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FTraceMotionMatchingStateDatabaseEntry& Entry)
{
	Ar << Entry.DatabaseId;
	Ar << Entry.PoseEntries;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FTraceMotionMatchingState& State)
{
	Ar << State.ElapsedPoseJumpTime;
	Ar << State.Flags;
	Ar << State.QueryVector;
	Ar << State.QueryVectorNormalized;
	Ar << State.DatabaseSequenceFilter;
	Ar << State.DatabaseBlendSpaceFilter;
	Ar << State.DbPoseIdx;
	Ar << State.DatabaseId;
	Ar << State.ContinuingPoseIdx;
	Ar << State.AssetPlayerTime;
	Ar << State.DeltaTime;
	Ar << State.SimLinearVelocity;
	Ar << State.SimAngularVelocity;
	Ar << State.AnimLinearVelocity;
	Ar << State.AnimAngularVelocity;
	Ar << State.DatabaseEntries;
	return Ar;
}

void FTraceMotionMatchingState::Output(const FAnimationBaseContext& InContext)
{
	if (!ShouldTrace(InContext))
	{
		return;
	}

	TArray<uint8> ArchiveData;
	FMemoryWriter Archive(ArchiveData);

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);
	UObject* SkeletalMeshComponent = AnimInstance->GetOuter();

	FTraceMessage TraceMessage;
	TraceMessage.Cycle = FPlatformTime::Cycles64();
	TraceMessage.AnimInstanceId = FObjectTrace::GetObjectId(AnimInstance);
	TraceMessage.SkeletalMeshComponentId = FObjectTrace::GetObjectId(SkeletalMeshComponent);
	TraceMessage.NodeId = InContext.GetCurrentNodeId();
	TraceMessage.FrameCounter = FObjectTrace::GetObjectWorldTickCounter(AnimInstance);

	Archive << TraceMessage;
	Archive << *this;

	UE_TRACE_LOG(PoseSearch, MotionMatchingState, PoseSearchChannel)
		<< MotionMatchingState.Data(ArchiveData.GetData(), ArchiveData.Num());
}

}}
#endif // UE_POSE_SEARCH_TRACE_ENABLED