// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SWaveformTransformationRenderLayer.h"
#include "WaveformEditorRenderData.h"
#include "WaveformTransformationTrimFade.h"

class SWaveformTransformationTrimFadeLayer : public SWaveformTransformationRenderLayer
{
public:
	SLATE_BEGIN_ARGS(SWaveformTransformationTrimFadeLayer) {}
		SLATE_DEFAULT_SLOT(FArguments, InArgs)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TObjectPtr<UWaveformTransformationTrimFade> InTransformToRender, TSharedRef<FWaveformEditorRenderData> InWaveformRenderData)
	{
		TrimFadeTransform = InTransformToRender;
		WaveformRenderData = InWaveformRenderData;
	}

	int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const float StartTimeRatio = (float)TransformationWaveInfo.StartFrameOffset / WaveformRenderData->GetOriginalWaveformFrames() / TransformationWaveInfo.NumChannels;
		const uint32 EndSample = TransformationWaveInfo.StartFrameOffset + TransformationWaveInfo.NumEditedSamples;
		const float EndTimeRatio = (float)EndSample / WaveformRenderData->GetOriginalWaveformFrames() / TransformationWaveInfo.NumChannels;

		const float TrimStartBarXRatio = (StartTimeRatio - WaveformDisplayRange.GetLowerBoundValue()) / ZoomRatio;
		const float TrimEndBarXRatio = (EndTimeRatio - WaveformDisplayRange.GetLowerBoundValue()) / ZoomRatio;

		const bool bRenderLowerBound = TrimStartBarXRatio >= 0.f;
		const bool bRenderUpperBound = TrimEndBarXRatio <= 1.f;
		
		TArray<FVector2D> LinePoints;
		LinePoints.SetNumUninitialized(2);

		if (bRenderLowerBound)
		{
			const float LowerBoundX = TrimStartBarXRatio * AllottedGeometry.Size.X;

			LinePoints[0] = FVector2D(LowerBoundX, 0.f);
			LinePoints[1] = FVector2D(LowerBoundX, AllottedGeometry.Size.Y);

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				++LayerId,
				AllottedGeometry.ToPaintGeometry(),
				LinePoints,
				ESlateDrawEffect::None,
				FLinearColor::Green,
				false
			);
		}

		if (bRenderUpperBound)
		{
			const float UpperBoundX = TrimEndBarXRatio * AllottedGeometry.Size.X;

			LinePoints[0] = FVector2D(UpperBoundX, 0.f);
			LinePoints[1] = FVector2D(UpperBoundX, AllottedGeometry.Size.Y);

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				++LayerId,
				AllottedGeometry.ToPaintGeometry(),
				LinePoints,
				ESlateDrawEffect::None,
				FLinearColor::Red,
				false
			);
		}


		return LayerId;
	}

	void UpdateDisplayRange(const TRange<float> NewDisplayRange)
	{
		WaveformDisplayRange = NewDisplayRange;
	}

	void OnZoomLevelChanged(const uint8 NewLevel)
	{
		ZoomRatio = FMath::Clamp(NewLevel / 100.f, UE_KINDA_SMALL_NUMBER, 1.f);
	}

private:
	TObjectPtr<UWaveformTransformationTrimFade> TrimFadeTransform = nullptr;
	TSharedPtr<FWaveformEditorRenderData> WaveformRenderData = nullptr;

	TRange<float> WaveformDisplayRange = TRange<float>::Inclusive(0.f, 1.f);
	float ZoomRatio = 1.f;
};