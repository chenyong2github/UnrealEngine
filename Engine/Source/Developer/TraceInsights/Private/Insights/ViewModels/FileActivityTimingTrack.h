// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/TimingEventsTrack.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFileActivitySharedState
{
	friend class FOverviewFileActivityTimingTrack;
	friend class FDetailedFileActivityTimingTrack;

public:
	struct FIoFileActivity
	{
		uint64 Id;
		const TCHAR* Path;
		double StartTime;
		double EndTime;
		int32 EventCount;
		int32 Depth;
	};

	struct FIoTimingEvent
	{
		double StartTime;
		double EndTime;
		uint32 Depth;
		uint32 Type; // Trace::EFileActivityType + "Failed" flag
		uint64 Offset;
		uint64 Size;
		TSharedPtr<FIoFileActivity> FileActivity;
	};

public:
	FFileActivitySharedState() {}

	const TArray<FIoTimingEvent>& GetAllEvents() const { return AllIoEvents; }

	~FFileActivitySharedState() {}

	void Reset();
	void Update();

	void RequestUpdate() { bForceIoEventsUpdate = true; }
	void ToggleMergeLanes() { bMergeIoLanes = !bMergeIoLanes; }
	void ToggleBackgroundEvents() { bShowFileActivityBackgroundEvents = !bShowFileActivityBackgroundEvents; }

private:
	bool bForceIoEventsUpdate;

	bool bMergeIoLanes;
	bool bShowFileActivityBackgroundEvents;

	TArray<TSharedPtr<FIoFileActivity>> FileActivities;
	TMap<uint64, TSharedPtr<FIoFileActivity>> FileActivityMap;

	/** All IO events, cached. */
	TArray<FIoTimingEvent> AllIoEvents;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFileActivityTimingTrack : public FTimingEventsTrack
{
public:
	explicit FFileActivityTimingTrack(uint64 InTrackId, const FName SubType, const FString& InName, TSharedPtr<FFileActivitySharedState> InState)
		: FTimingEventsTrack(InTrackId, FName(TEXT("FileActivity")), SubType, InName)
		, State(InState)
	{
	}
	virtual ~FFileActivityTimingTrack() {}

	virtual void InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const override;

protected:
	bool SearchEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent, bool bIgnoreEventDepth) const;

protected:
	TSharedPtr<FFileActivitySharedState> State;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FOverviewFileActivityTimingTrack : public FFileActivityTimingTrack
{
public:
	explicit FOverviewFileActivityTimingTrack(uint64 InTrackId, TSharedPtr<FFileActivitySharedState> InState)
		: FFileActivityTimingTrack(InTrackId, FName(TEXT("Overview")), TEXT("I/O Overview"), InState)
	{
	}

	virtual void Draw(FTimingViewDrawHelper& Helper) const override;
	virtual bool SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const override
	{
		constexpr bool bIgnoreEventDepth = true;
		return SearchEvent(InStartTime, InEndTime, InPredicate, InOutTimingEvent, bInStopAtFirstMatch, bInSearchForLargestEvent, bIgnoreEventDepth);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FDetailedFileActivityTimingTrack : public FFileActivityTimingTrack
{
public:
	explicit FDetailedFileActivityTimingTrack(uint64 InTrackId, TSharedPtr<FFileActivitySharedState> InState)
		: FFileActivityTimingTrack(InTrackId, FName(TEXT("Detailed")), TEXT("I/O Activity"), InState)
	{
	}

	virtual void Draw(FTimingViewDrawHelper& Helper) const override;
	virtual bool SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const override
	{
		constexpr bool bIgnoreEventDepth = false;
		return SearchEvent(InStartTime, InEndTime, InPredicate, InOutTimingEvent, bInStopAtFirstMatch, bInSearchForLargestEvent, bIgnoreEventDepth);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////
