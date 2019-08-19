// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/TimingEventsTrack.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FThreadTimingTrack : public FTimingEventsTrack
{
public:
	explicit FThreadTimingTrack(uint64 InTrackId, const FName& InSubType, const FString& InName, const TCHAR* InGroupName)
		: FTimingEventsTrack(InTrackId, FName(TEXT("Thread")), InSubType, InName)
		, GroupName(InGroupName)
		, ThreadId(0)
	{
	}

	virtual ~FThreadTimingTrack() {}

	const TCHAR* GetGroupName() const { return GroupName; };

	void SetThreadId(uint32 InThreadId) { ThreadId = InThreadId; }
	uint32 GetThreadId() const { return ThreadId; }

	virtual void Draw(FTimingViewDrawHelper& Helper) const override;
	virtual void DrawSelectedEventInfo(const FTimingEvent& SelectedTimingEvent, const FTimingTrackViewport& Viewport, const FDrawContext& DrawContext, const FSlateBrush* WhiteBrush, const FSlateFontInfo& Font) const override;
	virtual void InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const override;

	virtual bool SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const override;

private:
	const TCHAR* GroupName;
	uint32 ThreadId;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FCpuTimingTrack : public FThreadTimingTrack
{
public:
	explicit FCpuTimingTrack(uint64 InTrackId, const FString& InName, const TCHAR* InGroupName)
		: FThreadTimingTrack(InTrackId, FName(TEXT("CPU")), InName, InGroupName)
	{
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FGpuTimingTrack : public FThreadTimingTrack
{
public:
	explicit FGpuTimingTrack(uint64 InTrackId, const FString& InName, const TCHAR* InGroupName)
		: FThreadTimingTrack(InTrackId, FName(TEXT("GPU")), InName, InGroupName)
	{
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////
