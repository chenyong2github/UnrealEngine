// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchTraceProvider.h"
#include "Model/PointTimeline.h"

namespace UE { namespace PoseSearch {

const FName FTraceProvider::ProviderName("PoseSearchTraceProvider");

FTraceProvider::FTraceProvider(TraceServices::IAnalysisSession& InSession) : Session(InSession)
{
}

bool FTraceProvider::ReadMotionMatchingStateTimeline(uint64 InObjectId, int32 InNodeId, TFunctionRef<void(const FMotionMatchingStateTimeline&)> Callback) const
{
	Session.ReadAccessCheck();
	return MotionMatchingStateTimelineStorage.ReadTimeline(InObjectId, InNodeId, Callback);
}


void FTraceProvider::AppendMotionMatchingState(const FTraceMotionMatchingStateMessage& InMessage, double InTime)
{
	Session.WriteAccessCheck();

	TSharedRef<TraceServices::TPointTimeline<FTraceMotionMatchingStateMessage>> Timeline = MotionMatchingStateTimelineStorage.GetTimeline(Session, InMessage.AnimInstanceId, InMessage.NodeId);
	Timeline->AppendEvent(InTime, InMessage);
	
	Session.UpdateDurationSeconds(InTime);
}


}}
