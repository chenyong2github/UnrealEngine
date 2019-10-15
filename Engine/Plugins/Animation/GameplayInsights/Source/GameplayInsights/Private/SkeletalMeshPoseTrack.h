// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTrack.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

class ULineBatchComponent;

class FSkeletalMeshPoseTrack : public TGameplayTrackMixin<FTimingEventsTrack>
{
public:
	static const FName TypeName;
	static const FName SubTypeName;

	FSkeletalMeshPoseTrack(const FAnimationSharedData& InSharedData, uint64 InObjectID, const TCHAR* InName);

	virtual void Reset() override;
	virtual void Draw(ITimingViewDrawHelper& Helper) const override;
	virtual void InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const override;
	virtual bool SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const override;
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;

	// Access drawing flags
	bool ShouldDrawMarkerTime() const { return bDrawMarkerTime; }
	bool ShouldDrawSelectedEvent() const { return bDrawSelectedEvent; }
	bool ShouldDrawHoveredEvent() const { return bDrawHoveredEvent; }
	bool ShouldDrawSelection() const { return bDrawSelection; }

#if WITH_ENGINE
	// Draw poses in the specified time range
	void DrawPoses(ULineBatchComponent* InLineBatcher, double SelectionStartTime, double SelectionEndTime);
#endif

private:
	/** The shared data */
	const FAnimationSharedData& SharedData;

	/** Whether to draw poses at specified times */
	bool bDrawMarkerTime;
	bool bDrawSelectedEvent;
	bool bDrawHoveredEvent;
	bool bDrawSelection;
};