// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/Model/LoadTimeProfiler.h"
#include "TimingEventSearch.h"

class FTimingEventsTrack;

struct FTimingEvent
{
	// The track this timing event is contained within
	const FTimingEventsTrack* Track;

	// Handle to a previous search, used to accelerate access to underlying event data
	mutable FTimingEventSearchHandle SearchHandle;

	// The start time of the event
	double StartTime;

	// The end time of the event
	double EndTime;

	// For hierarchical events, the cached exclusive time
	double ExclusiveTime;

	// The depth of the event
	uint32 Depth;

	FTimingEvent()
		: Track(nullptr)
		, StartTime(0.0)
		, EndTime(-1.0)
		, ExclusiveTime(0.0)
		, Depth(0)
	{
	}

	FTimingEvent(const FTimingEventsTrack* InTrack, double InStartTime, double InEndTime, uint32 InDepth)
		: Track(InTrack)
		, StartTime(InStartTime)
		, EndTime(InEndTime)
		, ExclusiveTime(0.0)
		, Depth(InDepth)
	{}

	FTimingEvent(const FTimingEvent&) = default;
	FTimingEvent& operator=(const FTimingEvent&) = default;

	FTimingEvent(FTimingEvent&&) = default;
	FTimingEvent& operator=(FTimingEvent&&) = default;

	void Reset()
	{
		Track = nullptr;
		StartTime = 0.0;
		EndTime = -1.0;
		ExclusiveTime = 0.0;
		Depth = 0;
	}

	bool IsValidTrack() const { return Track != nullptr; }

	bool IsValid() const { return Track != nullptr && StartTime <= EndTime; }

	double Duration() const { return EndTime - StartTime; }

	bool Equals(const FTimingEvent& Other) const
	{
		return Track == Other.Track
			&& Depth == Other.Depth
			&& StartTime == Other.StartTime
			&& EndTime == Other.EndTime;
	}

	static bool AreEquals(const FTimingEvent& A, const FTimingEvent& B)
	{
		return A.Equals(B);
	}
};
