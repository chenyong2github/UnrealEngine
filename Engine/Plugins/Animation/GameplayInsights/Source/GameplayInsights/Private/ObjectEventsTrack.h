// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTrack.h"
#include "Insights/ViewModels/TimingEventsTrack.h"
#include "Containers/ArrayView.h"

class FGameplaySharedData;

class FObjectEventsTrack : public TGameplayTrackMixin<FTimingEventsTrack>
{
public:
	static const FName TypeName;
	static const FName SubTypeName;

	FObjectEventsTrack(const FGameplaySharedData& InSharedData, uint64 InObjectID, const TCHAR* InName);

	virtual void Draw(ITimingViewDrawHelper& Helper) const override;
	virtual void InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const override;
	virtual bool SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const override;

private:
	const FGameplaySharedData& SharedData;
};