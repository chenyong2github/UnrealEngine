// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/TimingEvent.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ETaskTrackEventType : uint32
{
	Launched,
	Dispatched,
	Scheduled,
	Executed,
	Completed,
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTaskTrackEvent : public FTimingEvent
{
	INSIGHTS_DECLARE_RTTI(FTaskTrackEvent, FTimingEvent)

public:
	FTaskTrackEvent(const TSharedRef<const FBaseTimingTrack> InTrack, double InStartTime, double InEndTime, uint32 InDepth, ETaskTrackEventType InTaskEventType);
	virtual ~FTaskTrackEvent() {}

	FString GetStartLabel() const;
	FString GetEndLabel() const;

	FString GetEventName() const;

	ETaskTrackEventType GetTaskEventType() const { return TaskEventType; }

	uint32 GetTaskId() const { return TaskId; }
	void SetTaskId(uint32 InTaskId) { TaskId = InTaskId; }

private:
	ETaskTrackEventType TaskEventType;
	uint32 TaskId = ~0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
