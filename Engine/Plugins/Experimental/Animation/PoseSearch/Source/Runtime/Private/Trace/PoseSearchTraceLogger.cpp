// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchTraceLogger.h"
#include "Animation/AnimInstanceProxy.h"
#include "Trace/Trace.inl"
#include "Animation/AnimNodeBase.h"

#if UE_POSE_SEARCH_TRACE_ENABLED

UE_TRACE_CHANNEL_DEFINE(PoseSearchChannel);

UE_TRACE_EVENT_BEGIN(PoseSearch, MotionMatchingState)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, SkeletalMeshComponentId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(float, ElapsedPoseJumpTime)
	UE_TRACE_EVENT_FIELD(uint32, Flags)
	UE_TRACE_EVENT_FIELD(uint64, DatabaseId)
	UE_TRACE_EVENT_FIELD(int32, DbPoseIdx)
	UE_TRACE_EVENT_FIELD(int32, ContinuingPoseIdx)
	UE_TRACE_EVENT_FIELD(float[], QueryVector)
	UE_TRACE_EVENT_FIELD(float[], QueryVectorNormalized)
	UE_TRACE_EVENT_FIELD(float, AssetPlayerTime)
	UE_TRACE_EVENT_FIELD(float, DeltaTime)
	UE_TRACE_EVENT_FIELD(float, SimLinearVelocity)
	UE_TRACE_EVENT_FIELD(float, SimAngularVelocity)
	UE_TRACE_EVENT_FIELD(float, AnimLinearVelocity)
	UE_TRACE_EVENT_FIELD(float, AnimAngularVelocity)
	UE_TRACE_EVENT_FIELD(bool[], DatabaseSequenceFilter)
	UE_TRACE_EVENT_FIELD(bool[], DatabaseBlendSpaceFilter)
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

void FTraceMotionMatchingState::Output(const FAnimationBaseContext& InContext, const FTraceMotionMatchingState& State)
{
	if (!ShouldTrace(InContext))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);
	UObject* SkeletalMeshComponent = AnimInstance->GetOuter();

	UE_TRACE_LOG(PoseSearch, MotionMatchingState, PoseSearchChannel)
		<< MotionMatchingState.Cycle(FPlatformTime::Cycles64())
		<< MotionMatchingState.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< MotionMatchingState.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< MotionMatchingState.SkeletalMeshComponentId(FObjectTrace::GetObjectId(SkeletalMeshComponent))
		<< MotionMatchingState.NodeId(InContext.GetCurrentNodeId())
		<< MotionMatchingState.ElapsedPoseJumpTime(State.ElapsedPoseJumpTime)
		<< MotionMatchingState.Flags(static_cast<uint32>(State.Flags))
		<< MotionMatchingState.DatabaseId(State.DatabaseId)
		<< MotionMatchingState.DbPoseIdx(State.DbPoseIdx)
		<< MotionMatchingState.ContinuingPoseIdx(State.ContinuingPoseIdx)
		<< MotionMatchingState.QueryVector(State.QueryVector.GetData(), State.QueryVector.Num())
		<< MotionMatchingState.QueryVectorNormalized(State.QueryVectorNormalized.GetData(), State.QueryVectorNormalized.Num())
		<< MotionMatchingState.AssetPlayerTime(State.AssetPlayerTime)
		<< MotionMatchingState.DeltaTime(State.DeltaTime)
		<< MotionMatchingState.SimLinearVelocity(State.SimLinearVelocity)
		<< MotionMatchingState.SimAngularVelocity(State.SimAngularVelocity)
		<< MotionMatchingState.AnimLinearVelocity(State.AnimLinearVelocity)
		<< MotionMatchingState.AnimAngularVelocity(State.AnimAngularVelocity)
		<< MotionMatchingState.DatabaseSequenceFilter(State.DatabaseSequenceFilter.GetData(), State.DatabaseSequenceFilter.Num())
		<< MotionMatchingState.DatabaseBlendSpaceFilter(State.DatabaseBlendSpaceFilter.GetData(), State.DatabaseBlendSpaceFilter.Num());
}

}}
#endif // UE_POSE_SEARCH_TRACE_ENABLED