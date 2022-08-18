// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorZoomController.h"

void FWaveformEditorZoomController::ZoomIn()
{
	if (CanZoomIn())
	{
		ZoomPercentage += ZoomPercentageStep;
		ApplyZoom();
	}

}

bool FWaveformEditorZoomController::CanZoomIn() const
{
	return ZoomPercentage + ZoomPercentageStep <= 100;
}

void FWaveformEditorZoomController::ZoomOut()
{
	if (CanZoomOut())
	{
		ZoomPercentage -= ZoomPercentageStep;
		ApplyZoom();
	}
}

bool FWaveformEditorZoomController::CanZoomOut() const
{
	return ZoomPercentage - ZoomPercentageStep >= 0;
}


void FWaveformEditorZoomController::ZoomByDelta(const float Delta)
{
	if (Delta >= 0.f)
	{
		ZoomIn();
	}
	else
	{
		ZoomOut();
	}
}

uint8 FWaveformEditorZoomController::GetZoomRatio() const
{
	return 100 - ZoomPercentage;
}

void FWaveformEditorZoomController::ApplyZoom()
{
	UE_LOG(LogInit, Log, TEXT("Wave Editor Zoom %d%%"), ZoomPercentage);
	OnZoomRatioChanged.Broadcast(100 - ZoomPercentage);
}
