// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchTraceAnalyzer.h"

#include "HAL/LowLevelMemTracker.h"
#include "PoseSearchTraceProvider.h"
#include "Runtime/Private/Trace/PoseSearchTraceLogger.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace UE { namespace PoseSearch {

FTraceAnalyzer::FTraceAnalyzer(TraceServices::IAnalysisSession& InSession, FTraceProvider& InTraceProvider) : Session(InSession), TraceProvider(InTraceProvider)
{
}

void FTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	FInterfaceBuilder& Builder = Context.InterfaceBuilder;

	ANSICHAR LoggerName[NAME_SIZE];
	ANSICHAR MotionMatchingStateName[NAME_SIZE];

	FTraceLogger::Name.GetPlainANSIString(LoggerName);
	FTraceMotionMatchingState::Name.GetPlainANSIString(MotionMatchingStateName);

	Builder.RouteEvent(RouteId_MotionMatchingState, LoggerName, MotionMatchingStateName);
}

bool FTraceAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	LLM_SCOPE_BYNAME(TEXT("Insights/PoseSearch::FTraceAnalyzer"));

	TraceServices::FAnalysisSessionEditScope Scope(Session);
	const FEventData& EventData = Context.EventData;

	// Gather event data values
	const double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
	const uint16 FrameCounter = EventData.GetValue<uint16>("FrameCounter");
	const uint64 AnimInstanceId = EventData.GetValue<uint64>("AnimInstanceId");
	const uint64 SkeletalMeshComponentId = EventData.GetValue<uint64>("SkeletalMeshComponentId");
	const int32 NodeId = EventData.GetValue<int32>("NodeId");

	switch (RouteId)
	{
		case RouteId_MotionMatchingState:
			{
				FTraceMotionMatchingStateMessage Message;
				Message.DatabaseId = EventData.GetValue<uint64>("DatabaseId");
				Message.Flags = static_cast<FTraceMotionMatchingState::EFlags>(EventData.GetValue<uint32>("Flags"));
				Message.DbPoseIdx = EventData.GetValue<int32>("DbPoseIdx");
				Message.ContinuingPoseIdx = EventData.GetValue<int32>("ContinuingPoseIdx");
				Message.ElapsedPoseJumpTime = EventData.GetValue<float>("ElapsedPoseJumpTime");
				Message.QueryVector = EventData.GetArrayView<float>("QueryVector");
                Message.QueryVectorNormalized = EventData.GetArrayView<float>("QueryVectorNormalized");

				Message.AssetPlayerTime = EventData.GetValue<float>("AssetPlayerTime");
				Message.DeltaTime = EventData.GetValue<float>("DeltaTime");
				Message.SimLinearVelocity = EventData.GetValue<float>("SimLinearVelocity");
				Message.SimAngularVelocity = EventData.GetValue<float>("SimAngularVelocity");
				Message.AnimLinearVelocity = EventData.GetValue<float>("AnimLinearVelocity");
				Message.AnimAngularVelocity = EventData.GetValue<float>("AnimAngularVelocity");
				Message.DatabaseSequenceFilter = EventData.GetArrayView<bool>("DatabaseSequenceFilter");
				Message.DatabaseBlendSpaceFilter = EventData.GetArrayView<bool>("DatabaseBlendSpaceFilter");

				TArrayView<const float> ChannelWeightScales = EventData.GetArrayView<float>("ChannelWeightScales");
				TArrayView<const float> HistoryWeightScales = EventData.GetArrayView<float>("HistoryWeightScales");
				TArrayView<const float> PredictionWeightScales = EventData.GetArrayView<float>("PredictionWeightScales");

				if ((ChannelWeightScales.Num() == 2) && (HistoryWeightScales.Num() == 2) && (PredictionWeightScales.Num() == 2))
				{
					Message.Weights.PoseDynamicWeights.ChannelWeightScale = ChannelWeightScales[0];
					Message.Weights.TrajectoryDynamicWeights.ChannelWeightScale = ChannelWeightScales[1];
					Message.Weights.PoseDynamicWeights.HistoryWeightScale = HistoryWeightScales[0];
					Message.Weights.TrajectoryDynamicWeights.HistoryWeightScale = HistoryWeightScales[1];
					Message.Weights.PoseDynamicWeights.PredictionWeightScale = PredictionWeightScales[0];
					Message.Weights.TrajectoryDynamicWeights.PredictionWeightScale = PredictionWeightScales[1];
					Message.Weights.bDebugDisableWeights = EventData.GetValue<bool>("DebugDisableWeights");
				}

				// Common data
				Message.NodeId = NodeId;
				Message.AnimInstanceId = AnimInstanceId;
				Message.SkeletalMeshComponentId = SkeletalMeshComponentId;
				Message.FrameCounter = FrameCounter;

				TraceProvider.AppendMotionMatchingState(Message, Time);
				break;
			}
		default:
			{
				// Should not happen
				checkNoEntry();
			}
	}

	return true;
}
}}
