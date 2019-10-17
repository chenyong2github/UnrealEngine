// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/ViewModels/TimingEventsTrack.h"

class FTimingEventSearchParameters;

/** Defines FLoadingTrackGetEventNameDelegate delegate interface. Returns the name for a timing event in a Loading track. */
DECLARE_DELEGATE_RetVal_TwoParams(const TCHAR*, FLoadingTrackGetEventNameDelegate, uint32 /*Depth*/, const Trace::FLoadTimeProfilerCpuEvent& /*Event*/);

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLoadingSharedState
{
public:
	void Reset();

	const TCHAR* GetEventNameByExportEventType(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;
	const TCHAR* GetEventNameByPackageName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;
	const TCHAR* GetEventNameByExportClassName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;
	const TCHAR* GetEventName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;

	const TCHAR* GetEventNameEx(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;

	void SetColorSchema(int32 Schema);

private:
	FLoadingTrackGetEventNameDelegate LoadingGetEventNameFn;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLoadingTimingTrack : public FTimingEventsTrack
{
public:
	explicit FLoadingTimingTrack(uint64 InTrackId, uint32 InTimelineIndex, const FName& InSubType, const FString& InName, TSharedPtr<FLoadingSharedState> InState)
		: FTimingEventsTrack(InTrackId, FName(TEXT("Loading")), InSubType, InName)
		, State(InState)
		, TimelineIndex(InTimelineIndex)
	{
	}
	virtual ~FLoadingTimingTrack() {}

	virtual void InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const override;
	virtual void Draw(ITimingViewDrawHelper& Helper) const override;
	virtual bool SearchTimingEvent(const FTimingEventSearchParameters& InSearchParameters, FTimingEvent& InOutTimingEvent) const override;

protected:
	// Helper function to find an event given search parameters
	bool FindLoadTimeProfilerCpuEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const Trace::FLoadTimeProfilerCpuEvent&)> InFoundPredicate) const;

protected:
	TSharedPtr<FLoadingSharedState> State;
	uint32 TimelineIndex;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
