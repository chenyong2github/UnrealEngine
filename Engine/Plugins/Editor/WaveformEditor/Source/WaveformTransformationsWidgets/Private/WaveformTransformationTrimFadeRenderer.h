// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WaveformTransformationRendererBase.h"
#include "WaveformTransformationTrimFade.h"

class FWaveformTransformationTrimFadeRenderer : public FWaveformTransformationRendererBase
{
public:
	explicit FWaveformTransformationTrimFadeRenderer(const TObjectPtr<UWaveformTransformationTrimFade> TransformationToRender);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	int32 DrawTrimHandles(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	int32 DrawFadeCurves(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void GenerateFadeCurves(const FGeometry& AllottedGeometry);

	TObjectPtr<UWaveformTransformationTrimFade> TrimFadeTransform = nullptr;

	float StartTimeHandleX = 0.f;
	float EndTimeHandleX = 0.f;
	uint32 FadeInStartX = 0;
	uint32 FadeInEndX = 0;
	uint32 FadeOutStartX = 0;
	uint32 FadeOutEndX = 0;
	TArray<FVector2D> FadeInCurvePoints;
	TArray<FVector2D> FadeOutCurvePoints;

	double PixelsPerFrame = 0.0;

	bool bScrubbingLeftHandle = false;
	bool bScrubbingRightHandle = false;
	bool bScrubbingFadeIn = false;
	bool bScrubbingFadeOut = false;

	
	const float InteractionPixelXDelta = 10;
	const float InteractionRatioYDelta = 0.07f;
	const float MouseWheelStep = 0.25f;
};