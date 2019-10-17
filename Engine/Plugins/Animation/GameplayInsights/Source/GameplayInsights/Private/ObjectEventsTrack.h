// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTrack.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

class FGameplaySharedData;
struct FObjectEventMessage;
class FTimingEventSearchParameters;

class FObjectEventsTrack : public TGameplayTrackMixin<FTimingEventsTrack>
{
public:
	static const FName TypeName;
	static const FName SubTypeName;

	FObjectEventsTrack(const FGameplaySharedData& InSharedData, uint64 InObjectID, const TCHAR* InName);

	virtual void Draw(ITimingViewDrawHelper& Helper) const override;
	virtual void InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const override;
	virtual bool SearchTimingEvent(const FTimingEventSearchParameters& InSearchParameters, FTimingEvent& InOutTimingEvent) const override;

private:
	// Helper function used to find an object event
	bool FindObjectEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FObjectEventMessage&)> InFoundPredicate) const;

private:
	const FGameplaySharedData& SharedData;
};