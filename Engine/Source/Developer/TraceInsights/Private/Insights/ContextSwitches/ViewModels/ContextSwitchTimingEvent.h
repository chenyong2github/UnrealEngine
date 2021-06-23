// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/TimingEvent.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FContextSwitchTimingEvent : public FTimingEvent
{
	INSIGHTS_DECLARE_RTTI(FContextSwitchTimingEvent, FTimingEvent)

public:
	FContextSwitchTimingEvent(const TSharedRef<const FBaseTimingTrack> InTrack, double InStartTime, double InEndTime, uint32 InDepth);
	virtual ~FContextSwitchTimingEvent() {}

	void SetCoreNumber(uint32 InCoreNumber) { CoreNumber = InCoreNumber; }
	uint32 GetCoreNumber() const { return CoreNumber; }

private:
	uint32 CoreNumber = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
