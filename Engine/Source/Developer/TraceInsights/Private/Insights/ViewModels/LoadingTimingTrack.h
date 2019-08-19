// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/ViewModels/TimingEventsTrack.h"

/** Defines FLoadingTrackGetEventNameDelegate delegate interface. Returns the name for a timing event in a Loading track. */
DECLARE_DELEGATE_RetVal_TwoParams(const TCHAR*, FLoadingTrackGetEventNameDelegate, uint32 /*Depth*/, const Trace::FLoadTimeProfilerCpuEvent& /*Event*/);

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLoadingSharedState
{
public:
	void Reset();

	const TCHAR* GetEventNameByPackageEventType(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const;
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
	explicit FLoadingTimingTrack(uint64 InTrackId, const FName& InSubType, const FString& InName, TSharedPtr<FLoadingSharedState> InState)
		: FTimingEventsTrack(InTrackId, FName(TEXT("Loading")), InSubType, InName)
		, State(InState)
	{
	}
	virtual ~FLoadingTimingTrack() {}

	virtual void InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const override;

protected:
	TSharedPtr<FLoadingSharedState> State;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLoadingMainThreadTimingTrack : public FLoadingTimingTrack
{
public:
	explicit FLoadingMainThreadTimingTrack(uint64 InTrackId, TSharedPtr<FLoadingSharedState> InState)
		: FLoadingTimingTrack(InTrackId, FName(TEXT("MainThread")), TEXT("Loading - Main Thread"), InState)
	{
	}

	virtual void Draw(FTimingViewDrawHelper& Helper) const override;
	virtual bool SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLoadingAsyncThreadTimingTrack : public FLoadingTimingTrack
{
public:
	explicit FLoadingAsyncThreadTimingTrack(uint64 InTrackId, TSharedPtr<FLoadingSharedState> InState)
		: FLoadingTimingTrack(InTrackId, FName(TEXT("AsyncThread")), TEXT("Loading - Async Thread"), InState)
	{
	}

	virtual void Draw(FTimingViewDrawHelper& Helper) const override;
	virtual bool SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
