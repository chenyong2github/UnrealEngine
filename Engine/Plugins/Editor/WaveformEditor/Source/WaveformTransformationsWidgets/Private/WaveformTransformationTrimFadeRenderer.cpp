// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformTransformationTrimFadeRenderer.h"

FWaveformTransformationTrimFadeRenderer::FWaveformTransformationTrimFadeRenderer(const TObjectPtr<UWaveformTransformationTrimFade> TransformationToRender)
{
	check(TransformationToRender);
	TrimFadeTransform = TransformationToRender;
}

int32 FWaveformTransformationTrimFadeRenderer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = DrawTrimHandles(AllottedGeometry, OutDrawElements, LayerId);
	LayerId = DrawFadeCurves(AllottedGeometry, OutDrawElements, LayerId);

	return LayerId;
}

int32 FWaveformTransformationTrimFadeRenderer::DrawTrimHandles(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const bool bRenderLowerBound = StartTimeHandleX >= 0.f;
	const bool bRenderUpperBound = EndTimeHandleX <= AllottedGeometry.Size.X;

	TArray<FVector2D> LinePoints;
	LinePoints.SetNumUninitialized(2);

	if (bRenderLowerBound)
	{
		LinePoints[0] = FVector2D(StartTimeHandleX, 0.f);
		LinePoints[1] = FVector2D(StartTimeHandleX, AllottedGeometry.Size.Y);

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
		LinePoints[0] = FVector2D(EndTimeHandleX, 0.f);
		LinePoints[1] = FVector2D(EndTimeHandleX, AllottedGeometry.Size.Y);

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

int32 FWaveformTransformationTrimFadeRenderer::DrawFadeCurves(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (FadeInCurvePoints.Num() > 0)
	{
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			++LayerId,
			AllottedGeometry.ToPaintGeometry(),
			FadeInCurvePoints,
			ESlateDrawEffect::None,
			FLinearColor::Yellow
		);

	}

	if (FadeOutCurvePoints.Num() > 0)
	{
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			++LayerId,
			AllottedGeometry.ToPaintGeometry(),
			FadeOutCurvePoints,
			ESlateDrawEffect::None,
			FLinearColor::Yellow
		);

	}

	return LayerId;
}

void FWaveformTransformationTrimFadeRenderer::GenerateFadeCurves(const FGeometry& AllottedGeometry)
{
	const float FadeInFrames = TrimFadeTransform->StartFadeTime * TransformationWaveInfo.SampleRate;
	const uint32 FadeInPixelLenght = FadeInFrames * PixelsPerFrame;
	FadeInStartX = FMath::RoundToInt32(StartTimeHandleX);
	FadeInEndX = FMath::RoundToInt32(FMath::Clamp(StartTimeHandleX + FadeInPixelLenght, StartTimeHandleX, EndTimeHandleX));
	
	const uint32 DisplayedFadeInPixelLenght = FadeInEndX - FadeInStartX;
	FadeInCurvePoints.SetNumUninitialized(DisplayedFadeInPixelLenght);

	for (uint32 Pixel = 0; Pixel < DisplayedFadeInPixelLenght; ++Pixel)
	{
		const double FadeFraction = (float)Pixel / FadeInPixelLenght;
		const double CurveValue = Pixel != FadeInPixelLenght - 1 ? 1.f - FMath::Pow(FadeFraction, TrimFadeTransform->StartFadeCurve) : 0.f;

		const uint32 XCoordinate = Pixel + FadeInStartX;
		FadeInCurvePoints[Pixel] = FVector2D(XCoordinate, CurveValue * AllottedGeometry.Size.Y);
	}

	const float FadeOutFrames = TrimFadeTransform->EndFadeTime * TransformationWaveInfo.SampleRate;
	const float FadeOutPixelLength = FadeOutFrames * PixelsPerFrame;
	FadeOutStartX = FMath::RoundToInt32(FMath::Clamp(EndTimeHandleX - FadeOutPixelLength, StartTimeHandleX, EndTimeHandleX));
	FadeOutEndX = FMath::RoundToInt32(EndTimeHandleX);
	
	const uint32 DisplayedFadeOutPixelLength = FadeOutEndX - FadeOutStartX;
	FadeOutCurvePoints.SetNumUninitialized(DisplayedFadeOutPixelLength);
	const uint32 FadeOutPixelOffset = FadeOutPixelLength - DisplayedFadeOutPixelLength;

	for (uint32 Pixel = 0; Pixel < DisplayedFadeOutPixelLength; ++Pixel)
	{
		const double FadeFraction = (float)(Pixel + FadeOutPixelOffset) / FadeOutPixelLength;
		const double CurveValue = Pixel != FadeOutPixelLength - 1 ? FMath::Pow(FadeFraction, TrimFadeTransform->EndFadeCurve) : 1.f;

		const uint32 XCoordinate = Pixel + FadeOutStartX;
		FadeOutCurvePoints[Pixel] = FVector2D(XCoordinate, CurveValue * AllottedGeometry.Size.Y);
	}

}

void FWaveformTransformationTrimFadeRenderer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{ 
	if (!TrimFadeTransform)
	{
		return;
	}

	const float NumFrames = TransformationWaveInfo.NumAvilableSamples / TransformationWaveInfo.NumChannels;
	const double FirstFrame = FMath::Clamp((TrimFadeTransform->StartTime * TransformationWaveInfo.SampleRate) , 0.f, NumFrames);
	const double EndFrame = FMath::Clamp((TrimFadeTransform->EndTime * TransformationWaveInfo.SampleRate), FirstFrame, NumFrames);

	PixelsPerFrame = AllottedGeometry.GetLocalSize().X / NumFrames;

	StartTimeHandleX = FirstFrame * PixelsPerFrame;
	EndTimeHandleX = EndFrame * PixelsPerFrame;

	GenerateFadeCurves(AllottedGeometry);
}