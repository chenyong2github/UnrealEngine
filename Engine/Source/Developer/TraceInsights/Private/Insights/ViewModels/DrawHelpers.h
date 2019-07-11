// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Math/Color.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FDrawContext;
struct FSlateBrush;

class FTimingTrackViewport;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FDrawHelpers
{
public:
	/**
	 * Draw background.
	 */
	static void DrawBackground(const FDrawContext& DrawContext,
							   const FTimingTrackViewport& Viewport,
							   const FSlateBrush* BackgroundAreaBrush,
							   const FLinearColor& ValidAreaColor,
							   const FLinearColor& InvalidAreaColor,
							   const FLinearColor& EdgeColor,
							   const float X,
							   const float Y,
							   const float W,
							   const float H,
							   float& OutValidAreaX,
							   float& OutValidAreaW);

	/**
	 * Draw time range selection.
	 */
	static void DrawTimeRangeSelection(const FDrawContext& DrawContext,
									   const FTimingTrackViewport& Viewport,
									   const double StartTime,
									   const double EndTime,
									   const FSlateBrush* Brush,
									   const FSlateFontInfo& Font);
};

////////////////////////////////////////////////////////////////////////////////////////////////////
