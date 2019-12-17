// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Insights/ViewModels/GraphSeries.h"

// Insights
#include "Insights/ViewModels/GraphTrackEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

#define LOCTEXT_NAMESPACE "GraphTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphSeries::FGraphSeries()
	: Name()
	, Description()
	, bIsVisible(true)
	, bIsDirty(false)
	, bAutoZoom(false)
	, TargetAutoZoomLowValue(0.0)
	, TargetAutoZoomHighValue(1.0)
	, AutoZoomLowValue(0.0)
	, AutoZoomHighValue(1.0)
	, BaselineY(0.0)
	, ScaleY(1.0)
	, Color(0.0f, 0.5f, 1.0f, 1.0f)
	, BorderColor(0.3f, 0.8f, 1.0f, 1.0f)
	//, Events()
	//, Points()
	//, LinePoints()
	//, Boxes()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphSeries::~FGraphSeries()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FGraphSeriesEvent* FGraphSeries::GetEvent(const float X, const float Y, const FTimingTrackViewport& Viewport, bool bCheckLine, bool bCheckBox) const
{
	const float LocalBaselineY = static_cast<float>(GetBaselineY());

	for (const FGraphSeriesEvent& Event : Events)
	{
		const float EventX1 = Viewport.TimeToSlateUnitsRounded(Event.Time);
		const float EventX2 = Viewport.TimeToSlateUnitsRounded(Event.Time + Event.Duration);

		const float EventY = GetRoundedYForValue(Event.Value);

		// Check bounding box of the visual point.
		constexpr float PointTolerance = 5.0f;
		if (X >= EventX1 - PointTolerance && X <= EventX1 + PointTolerance &&
			Y >= EventY - PointTolerance && Y <= EventY + PointTolerance)
		{
			return &Event;
		}

		if (bCheckLine)
		{
			// Check bounding box of the horizontal line.
			constexpr float LineTolerance = 2.0f;
			if (X >= EventX1 - LineTolerance && X <= EventX2 + LineTolerance &&
				Y >= EventY - LineTolerance && Y <= EventY + LineTolerance)
			{
				return &Event;
			}
		}

		if (bCheckBox)
		{
			// Check bounding box of the visual box.
			constexpr float BoxTolerance = 1.0f;
			if (X >= EventX1 - BoxTolerance && X <= EventX2 + BoxTolerance)
			{
				if (EventY < LocalBaselineY)
				{
					if (Y >= EventY - BoxTolerance && Y <= LocalBaselineY + BoxTolerance)
					{
						return &Event;
					}
				}
				else
				{
					if (Y >= LocalBaselineY - BoxTolerance && Y <= EventY + BoxTolerance)
					{
						return &Event;
					}
				}
			}
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString FGraphSeries::FormatValue(double Value) const
{
	return FString::Printf(TEXT("%g"), Value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
