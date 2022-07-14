// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearch.h"


#if UE_POSE_SEARCH_TRACE_ENABLED

UE_TRACE_CHANNEL_EXTERN(PoseSearchChannel, POSESEARCH_API);

namespace UE { namespace PoseSearch {

struct POSESEARCH_API FTraceLogger
{
	/** Used for reading trace data */
    static const FName Name;
};

// Message types for appending / reading to the timeline
/** Base message type for common data */
struct POSESEARCH_API FTraceMessage
{
	uint64 Cycle = 0;

	uint64 AnimInstanceId = 0;

	/** Skeletal Mesh Component Id, outer class of the AnimInstance.
	 *  Used for retrieval of traced root transforms from the animation provider.
	 */
	uint64 SkeletalMeshComponentId = 0;

	/** Node Id of the motion matching node associated with this message */
	int32 NodeId = 0;

	uint16 FrameCounter = 0;
};
POSESEARCH_API FArchive& operator<<(FArchive& Ar, FTraceMessage& State);

struct POSESEARCH_API FTraceMotionMatchingStatePoseEntry
{
	int32 DbPoseIdx = 0;
	float Cost = 0.f;
};
POSESEARCH_API FArchive& operator<<(FArchive& Ar, FTraceMotionMatchingStatePoseEntry& Entry);

struct POSESEARCH_API FTraceMotionMatchingStateDatabaseEntry
{
	// @todo: can we use UPoseSearchDatabase* instead of DatabaseId?
	//UPoseSearchDatabase const* Database = nullptr;
	uint64 DatabaseId = 0;
	TArray<FTraceMotionMatchingStatePoseEntry> PoseEntries;
};
POSESEARCH_API FArchive& operator<<(FArchive& Ar, FTraceMotionMatchingStateDatabaseEntry& Entry);

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
	TArray<float> QueryVector;
	TArray<float> QueryVectorNormalized;

	/** If true, groups are being filtered by DatabaseGroupQuery */
	TArray<bool> DatabaseSequenceFilter;

	/** If true, groups are being filtered by DatabaseGroupQuery */
	TArray<bool> DatabaseBlendSpaceFilter;

	/** Index of the pose in our database */
	int32 DbPoseIdx = 0;

	/** Object Id of the database asset */
	uint64 DatabaseId = 0;

	/** Index of the continuing pose in our database */
	int32 ContinuingPoseIdx = 0;

	float AssetPlayerTime = 0.0f;
	float DeltaTime = 0.0f;
	float SimLinearVelocity = 0.0f;
	float SimAngularVelocity = 0.0f;
	float AnimLinearVelocity = 0.0f;
	float AnimAngularVelocity = 0.0f;
	
	TArray<FTraceMotionMatchingStateDatabaseEntry> DatabaseEntries;

	/** Output the current state info to the logger */
	void Output(const FAnimationBaseContext& InContext);

	static const UPoseSearchDatabase* GetDatabaseFromId(uint64 DatabaseId)
	{
		if (DatabaseId == 0)
		{
			return nullptr;
		}

		UObject* DatabaseObject = FObjectTrace::GetObjectFromId(DatabaseId);
		// @TODO: Load the object if unloaded
		if (DatabaseObject == nullptr)
		{
			return nullptr;
		}
		check(DatabaseObject->IsA<UPoseSearchDatabase>());

		const UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(DatabaseObject);
		return Database;
	}

	static uint64 GetIdFromDatabase(const UPoseSearchDatabase* Database)
	{
		return FObjectTrace::GetObjectId(Database);
	}

	void SetDatabase(const UPoseSearchDatabase* Database)
	{
		DatabaseId = GetIdFromDatabase(Database);
	}

	const UPoseSearchDatabase* GetPoseSearchDatabase() const
	{
		return GetDatabaseFromId(DatabaseId);
	}
	
	static const FName Name;
};
ENUM_CLASS_FLAGS(FTraceMotionMatchingState::EFlags)

POSESEARCH_API FArchive& operator<<(FArchive& Ar, FTraceMotionMatchingState& State);

}}

#endif // UE_POSE_SEARCH_TRACE_ENABLED

