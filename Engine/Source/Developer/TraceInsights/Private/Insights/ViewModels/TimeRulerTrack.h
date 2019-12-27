// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"

struct FDrawContext;
struct FSlateBrush;
class FTimingTrackViewport;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimeRulerTrack : public FBaseTimingTrack
{
public:
	FTimeRulerTrack();
	virtual ~FTimeRulerTrack();

	virtual void Reset() override;

	void SetSelection(const bool bInIsSelecting, const double InSelectionStartTime, const double InSelectionEndTime);
	void SetTimeMarker(const bool bInIsDragging, const double InTimeMarker);

	virtual void PostUpdate(const ITimingTrackUpdateContext& Context) override;
	void Draw(const ITimingTrackDrawContext& Context) const override;
	void PostDraw(const ITimingTrackDrawContext& Context) const override;

public:
	// Slate resources
	const FSlateBrush* WhiteBrush;
	const FSlateFontInfo Font;

	// Smoothed mouse pos text width to avoid flickering
	mutable float CrtMousePosTextWidth;

	// Smoothed time marker text width to avoid flickering
	mutable float CrtTimeMarkerTextWidth;

private:
	bool bIsSelecting;
	double SelectionStartTime;
	double SelectionEndTime;
	bool bIsDragging;
	double TimeMarker;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
