// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SWaveformTransformationRenderLayer.h"
#include "WaveformEditorRenderData.h"
#include "WaveformEditorTransportCoordinator.h"

namespace WaveformTransformationDurationHiglightParams
{
	const FLazyName BackgroundBrushName = TEXT("WhiteBrush");
	const FLinearColor BoxColor = FLinearColor(0.f, 0.f, 0.f, 0.7f);
}

class SWaveformTransformationDurationHiglight : public SWaveformTransformationRenderLayer
{
public:
	SLATE_BEGIN_ARGS(SWaveformTransformationDurationHiglight) {}
		SLATE_DEFAULT_SLOT(FArguments, InArgs)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FWaveformEditorRenderData> InWaveformRenderData)
	{
		WaveformRenderData = InWaveformRenderData;
	}

	int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const float StartTimeRatio = (float)TransformationWaveInfo.StartFrameOffset / WaveformRenderData->GetOriginalWaveformFrames() / TransformationWaveInfo.NumChannels;
		const uint32 EndSample = TransformationWaveInfo.StartFrameOffset + TransformationWaveInfo.NumEditedSamples;
		const float EndTimeRatio = (float)EndSample / WaveformRenderData->GetOriginalWaveformFrames() / TransformationWaveInfo.NumChannels;

		const float LeftBoundXRatio = (StartTimeRatio - WaveformDisplayRange.GetLowerBoundValue()) / ZoomRatio;
		const float RightBoundXRatio = (EndTimeRatio - WaveformDisplayRange.GetLowerBoundValue()) / ZoomRatio;

		const bool bRenderLeftBox = LeftBoundXRatio >= 0.f;
		const bool bRenderRightBox = RightBoundXRatio <= 1.f;

		if (bRenderLeftBox)
		{
			const float RightMarginX = LeftBoundXRatio * AllottedGeometry.Size.X;

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				++LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(0.f, 0.f), FVector2D(RightMarginX, AllottedGeometry.Size.Y)),
				FAppStyle::GetBrush(WaveformTransformationDurationHiglightParams::BackgroundBrushName),
				ESlateDrawEffect::None,
				WaveformTransformationDurationHiglightParams::BoxColor);

		}

		if (bRenderRightBox)
		{
			const float LeftMarginX = RightBoundXRatio * AllottedGeometry.Size.X;

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				++LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(LeftMarginX, 0.f), FVector2D(AllottedGeometry.Size.X, AllottedGeometry.Size.Y)),
				FAppStyle::GetBrush(WaveformTransformationDurationHiglightParams::BackgroundBrushName),
				ESlateDrawEffect::None,
				WaveformTransformationDurationHiglightParams::BoxColor);

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
	TSharedPtr<FWaveformEditorRenderData> WaveformRenderData = nullptr;

	TRange<float> WaveformDisplayRange = TRange<float>::Inclusive(0.f, 1.f);
	float ZoomRatio = 1.f;
};