// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"

struct FDrawContext;
struct FSlateBrush;
class FTimingTrackViewport;

////////////////////////////////////////////////////////////////////////////////////////////////////

static const float TimeRulerHeight = 22.0f;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimeRulerTrack : public FBaseTimingTrack
{
public:
	FTimeRulerTrack(uint64 InTrackId);
	virtual ~FTimeRulerTrack();

	virtual void Reset() override
	{
		FBaseTimingTrack::Reset();

		H = TimeRulerHeight;
	}

	virtual void UpdateHoveredState(float MX, float MY, const FTimingTrackViewport& Viewport);

	void Draw(FDrawContext& DC, const FTimingTrackViewport& Viewport,
				const FVector2D& MousePosition = FVector2D(0.0f, 0.0f),
				const bool bIsSelecting = false,
				const double SelectionStartTime = 0.0,
				const double SelectionEndTime = 0.0) const;

public:
	// Slate resources
	const FSlateBrush* WhiteBrush;
	const FSlateFontInfo Font;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
