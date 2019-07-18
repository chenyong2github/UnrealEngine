// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/GraphTrack.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingGraphSeries : public FGraphSeries
{
public:
	enum class ESeriesType
	{
		Frame,
		Timer,
		StatsCounter
	};

public:
	ESeriesType Type;
	uint32 Id; // frame type, timer id or stats counter id
	bool bIsFloatingPoint; // for stats counters
	double ValueOffset; // offset added to Y values (used to pan graph vertically)
	double ValueScale; // scale of Y values (used to scale graph vertically)
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingGraphTrack : public FGraphTrack
{
public:
	FTimingGraphTrack(uint64 InTrackId);
	virtual ~FTimingGraphTrack();

	virtual void Update(const FTimingTrackViewport& Viewport) override;

	TSharedPtr<FTimingGraphSeries> GetStatsCounterSeries(uint32 CounterId);
	void AddStatsCounterSeries(uint32 CounterId, FLinearColor Color, double ValueOffset = 0.0, double ValueScale = 1.0);
	void RemoveStatsCounterSeries(uint32 CounterId);

protected:
	void UpdateFrameSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport);
	void UpdateTimerSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport);
	void UpdateStatsCounterSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport);
};

////////////////////////////////////////////////////////////////////////////////////////////////////
