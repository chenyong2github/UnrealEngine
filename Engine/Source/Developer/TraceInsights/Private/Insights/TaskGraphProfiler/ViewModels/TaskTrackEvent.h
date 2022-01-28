// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/TaskGraphProfiler/TaskGraphProfilerManager.h"
#include "Insights/ViewModels/TimingEvent.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTaskTrackEvent : public FTimingEvent
{
	INSIGHTS_DECLARE_RTTI(FTaskTrackEvent, FTimingEvent)

public:
	FTaskTrackEvent(const TSharedRef<const FBaseTimingTrack> InTrack, double InStartTime, double InEndTime, uint32 InDepth, ETaskEventType InTaskEventType);
	virtual ~FTaskTrackEvent() {}

	FString GetStartLabel() const;
	FString GetEndLabel() const;

	FString GetEventName() const;

	ETaskEventType GetTaskEventType() const { return TaskEventType; }

	uint32 GetTaskId() const { return TaskId; }
	void SetTaskId(uint32 InTaskId) { TaskId = InTaskId; }

private:
	ETaskEventType TaskEventType;
	uint32 TaskId = ~0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights