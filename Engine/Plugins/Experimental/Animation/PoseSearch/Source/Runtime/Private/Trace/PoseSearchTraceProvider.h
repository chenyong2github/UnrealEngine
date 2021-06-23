// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/PointTimeline.h"
#include "PoseSearchTraceLogger.h"
#include "Model/IntervalTimeline.h"
#include "TraceServices/Model/AnalysisSession.h"


namespace UE { namespace PoseSearch {

// Message types for appending / reading to the timeline
/** Base message type for common data */
struct FTraceMessage
{
	uint64 AnimInstanceId;
	int32 NodeId;
	uint16 FrameCounter;
};

/** Motion matching state message container */
struct FTraceMotionMatchingStateMessage : FTraceMessage
{
	FTraceMotionMatchingState::EFlags Flags;
	float ElapsedPoseJumpTime;
	TArray<float> QueryVector;
	int32 DbPoseIdx;
	uint64 DatabaseId;
};


/**
 * Provider to the widgets for pose search functionality, largely mimicking FAnimationProvider
 */
class POSESEARCH_API FTraceProvider : public TraceServices::IProvider
{
public:
	FTraceProvider(TraceServices::IAnalysisSession& InSession);

	/** Read the object-relevant timeline info and provide it via callback */
	using FMotionMatchingStateTimeline = TraceServices::ITimeline<FTraceMotionMatchingStateMessage>;
	bool ReadMotionMatchingStateTimeline(uint64 InAnimInstanceId, int32 InNodeId, TFunctionRef<void(const FMotionMatchingStateTimeline&)> Callback) const;

	/** Append to the timeline on our object */
	void AppendMotionMatchingState(const FTraceMotionMatchingStateMessage& InMessage, double InTime);

	static const FName ProviderName;

private:
	/**
	 * Convenience struct for creating timelines per message type.
	 * We store an array of timelines for every possible AnimInstance Id that gets appended.
	 */
	template <class MessageType, class TimelineType = TraceServices::TPointTimeline<MessageType>>
	struct TTimelineStorage
	{
		// Map the timelines as AnimInstanceId -> NodeId -> Timeline
		using FNodeToTimelineMap = TMap<int32, uint32>;
		using FAnimInstanceToNodeMap = TMap<uint64, FNodeToTimelineMap>;

		/** Retrieves the timeline for internal use, creating it if it does not exist */
		TSharedRef<TimelineType> GetTimeline(TraceServices::IAnalysisSession& InSession, uint64 InAnimInstanceId, int32 InNodeId)
		{
			const uint32* TimelineIndex = nullptr;
			
			FNodeToTimelineMap* NodeToTimelineMap = AnimInstanceIdToTimelines.Find(InAnimInstanceId);
			if (NodeToTimelineMap == nullptr)
			{
				// Create the timeline map if this is the first time accessing with this anim instance
				NodeToTimelineMap = &AnimInstanceIdToTimelines.Add(InAnimInstanceId, {});
			}
			else
			{
				// Anim instance already used, attempt to find the NodeId's timeline
				TimelineIndex = NodeToTimelineMap->Find(InNodeId);
			}

			if (TimelineIndex == nullptr)
			{
				// Append our timeline to the storage and our object + the storage index to the map
				TSharedRef<TimelineType> Timeline = MakeShared<TimelineType>(InSession.GetLinearAllocator());
				const uint32 NewIndex = Timelines.Add(Timeline);
				NodeToTimelineMap->Add(InNodeId, NewIndex);
				return Timeline;
			}

			return Timelines[*TimelineIndex];
		}

		/** Retrieve a timeline from an anim instance + node and execute the callback */
		bool ReadTimeline(uint64 InAnimInstanceId, int32 InNodeId, TFunctionRef<void(const TraceServices::ITimeline<MessageType>&)> Callback) const
		{
			const FNodeToTimelineMap* NodeToTimelineMap = AnimInstanceIdToTimelines.Find(InAnimInstanceId);
			if (NodeToTimelineMap == nullptr)
			{
				return false;
			}

			const uint32* TimelineIndex = NodeToTimelineMap->Find(InNodeId);
			if (TimelineIndex == nullptr || !Timelines.IsValidIndex(*TimelineIndex))
			{
				return false;
			}

			Callback(*Timelines[*TimelineIndex]);
			return true;
		}

		/** Maps AnimInstanceIds to a map of NodeIds to timelines. AnimInstanceId -> NodeId -> Timeline */
		TMap<uint64, TMap<int32, uint32>> AnimInstanceIdToTimelines;

		/** Timelines per node */
		TArray<TSharedRef<TimelineType>> Timelines;
	};

	// Storage for each message type
	struct FMotionMatchingStateTimelineStorage : TTimelineStorage<FTraceMotionMatchingStateMessage>
	{
	} MotionMatchingStateTimelineStorage;


	TraceServices::IAnalysisSession& Session;
};
}}
