// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchTraceLogger.h"
#include "Animation/AnimInstanceProxy.h"
#include "Trace/Trace.inl"

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
	UE_TRACE_EVENT_FIELD(float[], QueryVector)
	UE_TRACE_EVENT_FIELD(float[], QueryVectorNormalized)
	UE_TRACE_EVENT_FIELD(float[], ChannelWeightScales)
	UE_TRACE_EVENT_FIELD(float[], HistoryWeightScales)
	UE_TRACE_EVENT_FIELD(float[], PredictionWeightScales)
	UE_TRACE_EVENT_FIELD(bool, DebugDisableWeights)
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

	float ChannelWeightScales[] =
	{
		State.Weights.PoseDynamicWeights.ChannelWeightScale,
		State.Weights.TrajectoryDynamicWeights.ChannelWeightScale
	};

	float HistoryWeightScales[] =
	{
		State.Weights.PoseDynamicWeights.HistoryWeightScale,
		State.Weights.TrajectoryDynamicWeights.HistoryWeightScale
	};
	float PredictionWeightScales[] =
	{
		State.Weights.PoseDynamicWeights.PredictionWeightScale,
		State.Weights.TrajectoryDynamicWeights.PredictionWeightScale
	};

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
		<< MotionMatchingState.QueryVector(State.QueryVector.GetData(), State.QueryVector.Num())
		<< MotionMatchingState.QueryVectorNormalized(State.QueryVectorNormalized.GetData(), State.QueryVectorNormalized.Num())
		<< MotionMatchingState.ChannelWeightScales(ChannelWeightScales, UE_ARRAY_COUNT(ChannelWeightScales))
		<< MotionMatchingState.HistoryWeightScales(HistoryWeightScales, UE_ARRAY_COUNT(HistoryWeightScales))
		<< MotionMatchingState.PredictionWeightScales(PredictionWeightScales, UE_ARRAY_COUNT(PredictionWeightScales))
		<< MotionMatchingState.DebugDisableWeights(State.Weights.bDebugDisableWeights);
}

}}
#endif // UE_POSE_SEARCH_TRACE_ENABLED