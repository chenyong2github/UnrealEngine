// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTrack.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

class FAnimationTickRecordsTrack : public TGameplayTrackMixin<FTimingEventsTrack>
{
public:
	static const FName TypeName;
	static const FName SubTypeName;

	FAnimationTickRecordsTrack(const FAnimationSharedData& InSharedData, uint64 InObjectId, uint64 InAssetId, const TCHAR* InName);

	virtual void Draw(ITimingViewDrawHelper& Helper) const override;
	virtual void InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const override;
	virtual bool SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const override;

	/** Get the asset ID that this track uses */
	uint64 GetAssetId() const { return AssetId; }

private:
	/** The shared data */
	const FAnimationSharedData& SharedData;

	/** The asset ID that this track uses */
	uint64 AssetId;
};