// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearch.h"
#include "ObjectTrace.h"

// Enable this if object tracing is enabled, mimics animation tracing
#define UE_POSE_SEARCH_TRACE_ENABLED OBJECT_TRACE_ENABLED

#if UE_POSE_SEARCH_TRACE_ENABLED

UE_TRACE_CHANNEL_EXTERN(PoseSearchChannel, POSESEARCH_API);

namespace UE { namespace PoseSearch {

struct POSESEARCH_API FTraceLogger
{
	/** Used for reading trace data */
    static const FName Name;
};

/**
 * Used to trace motion matching state data via the logger, which is then placed into a timeline
 */
struct POSESEARCH_API FTraceMotionMatchingState
{
	/** Bitfield for various state booleans */
	enum class EFlags : uint32
	{
		None = 0u,

		/** Whether the last animation was a forced follow-up animation due to expended animation runway */
		FollowupAnimation = 1u << 0
	};
	
	/** Amount of time since the last pose switch */
	float ElapsedPoseJumpTime = 0.0f;

	/** Storage container for state booleans */
	EFlags Flags = EFlags::None;

	/** Search vectors in normalized and unnormalized forms */
	TArrayView<const float> QueryVector;
	TArrayView<const float> QueryVectorNormalized;

	/** Runtime weights */
	FPoseSearchDynamicWeightParams Weights;

	/** Index of the pose in our database */
	int32 DbPoseIdx = 0;

	/** Object Id of the database asset */
	uint64 DatabaseId = 0;

	/** Node Id of the motion matching node associated with this message */
	int32 NodeId = 0;

	/** Skeletal Mesh Component Id, outer class of the AnimInstance.
	 *  Used for retrieval of traced root transforms from the animation provider.
	 */
	uint64 SkeletalMeshComponentId = 0;

	/** Output the current state info to the logger */
	static void Output(const FAnimationBaseContext& InContext, const FTraceMotionMatchingState& State);
	
	static const FName Name;
};
ENUM_CLASS_FLAGS(FTraceMotionMatchingState::EFlags)


}}

#define UE_TRACE_POSE_SEARCH_MOTION_MATCHING_STATE(Context, MotionMatchingState) \
	UE::PoseSearch::FTraceMotionMatchingState::Output(Context, MotionMatchingState);

#else // UE_POSE_SEARCH_TRACE_ENABLED

// Empty declarations if not enabled
#define UE_TRACE_POSE_SEARCH_MOTION_MATCHING_STATE(Context, MotionMatchingState)

#endif // UE_POSE_SEARCH_TRACE_ENABLED

