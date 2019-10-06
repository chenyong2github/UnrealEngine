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

	virtual FString FormatValue(double Value) const override;

public:
	ESeriesType Type;
	uint32 Id; // frame type, timer id or stats counter id

	bool bIsFloatingPoint; // for stats counters
	bool bIsMemory; // for stats counters
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingGraphTrack : public FGraphTrack
{
public:
	explicit FTimingGraphTrack(uint64 InTrackId);
	virtual ~FTimingGraphTrack();

	virtual void Update(const FTimingTrackViewport& Viewport) override;

	void AddDefaultFrameSeries();

	TSharedPtr<FTimingGraphSeries> GetStatsCounterSeries(uint32 CounterId);
	TSharedPtr<FTimingGraphSeries> AddStatsCounterSeries(uint32 CounterId, FLinearColor Color);
	void RemoveStatsCounterSeries(uint32 CounterId);

protected:
	void UpdateFrameSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport);
	void UpdateTimerSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport);
	void UpdateStatsCounterSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport);
};

////////////////////////////////////////////////////////////////////////////////////////////////////
