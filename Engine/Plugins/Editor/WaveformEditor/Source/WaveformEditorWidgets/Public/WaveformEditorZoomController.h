// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnZoomRatioChanged, const uint8 /* New Zoom Ratio */);

class WAVEFORMEDITORWIDGETS_API FWaveformEditorZoomController
{
public:
	FWaveformEditorZoomController() = default;

	void ZoomIn();
	bool CanZoomIn() const;
	void ZoomOut();
	bool CanZoomOut() const;
	void ZoomByDelta(const float Delta);

	uint8 GetZoomRatio() const;

	FOnZoomRatioChanged OnZoomRatioChanged;

private:
	void ApplyZoom();

	uint8 ZoomPercentage = 0;
	uint8 ZoomPercentageStep = 5;
};