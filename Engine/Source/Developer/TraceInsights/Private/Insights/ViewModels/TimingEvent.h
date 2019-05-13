// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/TimerNode.h"

class FTimingEventsTrack;

struct FTimingEvent
{
	const FTimingEventsTrack* Track;
	uint64 TypeId;
	uint32 Depth;
	double StartTime;
	double EndTime;
	double ExclusiveTime;

	void Reset()
	{
		Track = nullptr;
		TypeId = FTimerNode::InvalidId;
		Depth = 0;
		StartTime = 0.0;
		EndTime = 0.0;
		ExclusiveTime = 0.0;
	}

	bool IsValidTrack() const { return Track != nullptr; }
	bool IsValid() const { return Track != nullptr && TypeId != FTimerNode::InvalidId; }

	double Duration() const { return EndTime - StartTime; }

	bool Equals(const FTimingEvent& Other) const
	{
		return Track == Other.Track
			&& TypeId == Other.TypeId
			&& Depth == Other.Depth
			&& StartTime == Other.StartTime
			&& EndTime == Other.EndTime;
			//&& ExclusiveTime == Other.ExclusiveTime;
	}

	static bool AreEquals(const FTimingEvent& A, const FTimingEvent& B)
	{
		return A.Equals(B);
	}
};
