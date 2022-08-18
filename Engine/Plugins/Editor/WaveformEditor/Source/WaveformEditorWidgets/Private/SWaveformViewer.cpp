// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWaveformViewer.h"
#include "WaveformEditorRenderData.h"

void SWaveformViewer::Construct(const FArguments& InArgs, TSharedRef<FWaveformEditorRenderData> InRenderData, TSharedRef<FWaveformEditorTransportCoordinator> InTransportCoordinator)
{
	RenderData = InRenderData;
	SampleData = RenderData->GetSampleData();

	RenderData->OnRenderDataUpdated.AddSP(this, &SWaveformViewer::OnRenderDataUpdated);

	TransportCoordinator = InTransportCoordinator;
	TransportCoordinator->OnDisplayRangeUpdated.AddSP(this, &SWaveformViewer::OnDisplayRangeUpdated);

	DisplayRange = TRange<float>::Inclusive(0.f, RenderData->GetOriginalWaveformDurationInSeconds());

	Style = InArgs._Style;

	check(Style)
	WaveformColor = &Style->WaveformColor;
	MajorGridLineColor = &Style->MajorGridLineColor;
	MinorGridLineColor = &Style->MinorGridLineColor;
	BackgroundColor = &Style->WaveformBackgroundColor;
	BackgroundBrush = &Style->BackgroundBrush;
	DesiredWidth = Style->DesiredWidth;
	DesiredHeight = Style->DesiredHeight;

}

void SWaveformViewer::OnRenderDataUpdated()
{
	SampleData = RenderData->GetSampleData();
	bForceRedraw = true;

}

void SWaveformViewer::OnDisplayRangeUpdated(const TRange<float> NewDisplayRange)
{
	const float LengthInSeconds = RenderData->GetOriginalWaveformDurationInSeconds();
	DisplayRange.SetLowerBoundValue(NewDisplayRange.GetLowerBoundValue() * LengthInSeconds);
	DisplayRange.SetUpperBoundValue(NewDisplayRange.GetUpperBoundValue() * LengthInSeconds);
	bForceRedraw = true;
}

int32 SWaveformViewer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const 
{
	float PixelWidth = MyCullingRect.GetSize().X;

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		++LayerId,
		AllottedGeometry.ToPaintGeometry(),
		BackgroundBrush,
		ESlateDrawEffect::None, 
		BackgroundColor->GetSpecifiedColor()
	);

	if (PixelWidth > 0)
	{
		DrawGridLines(AllottedGeometry, OutDrawElements, LayerId);

		TArray<FVector2D> WaveformPoints;
 		WaveformPoints.SetNumUninitialized(2);

		for (const FWaveformLineCoordinates& PeakCoordinates : CachedDrawCoordinates)
		{
			WaveformPoints[0] = PeakCoordinates.PointA;
			WaveformPoints[1] = PeakCoordinates.PointB;

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				WaveformPoints,
				ESlateDrawEffect::None, 
				WaveformColor->GetSpecifiedColor()
			);
		}

		++LayerId;
	}

	return LayerId;
}

FVector2D SWaveformViewer::ComputeDesiredSize(float) const
{
	return FVector2D(DesiredWidth, DesiredHeight);
}

void SWaveformViewer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	uint32 DiscretePixelWidth = FMath::FloorToInt(AllottedGeometry.GetAbsoluteSize().X);
	if (DiscretePixelWidth <= 0)
	{
		return;
	}

	if (DiscretePixelWidth != CachedPixelWidth || bForceRedraw)
	{
		CachedPixelWidth = DiscretePixelWidth;
		CachedPixelHeight = AllottedGeometry.GetAbsoluteSize().Y;
		WaveformDrawingUtils::GetBinnedPeaksFromWaveformRawData(CachedPeaks, CachedPixelWidth, SampleData.GetData(), RenderData->GetNumSamples(), RenderData->GetSampleRate(), RenderData->GetNumChannels(), DisplayRange.GetLowerBoundValue(), DisplayRange.GetUpperBoundValue());
		GenerateWaveformLines(CachedDrawCoordinates, CachedPeaks, AllottedGeometry);
		bForceRedraw = false;
	}
	else if (CachedPixelHeight != AllottedGeometry.GetAbsoluteSize().Y)
	{
		CachedPixelHeight = AllottedGeometry.GetAbsoluteSize().Y;
		GenerateWaveformLines(CachedDrawCoordinates, CachedPeaks, AllottedGeometry);
	}
}

void SWaveformViewer::GenerateWaveformLines(TArray<FWaveformLineCoordinates>& OutDrawCoordinates, const TArray<WaveformDrawingUtils::SampleRange>& InWaveformPeaks, const FGeometry& InAllottedGeometry, const float VerticalZoomFactor)
{
	const int NChannels = RenderData->GetNumChannels();

	if (!ensure(NChannels != 0))
	{
		return;
	}

	const float ChannelSlotHeight = InAllottedGeometry.GetAbsoluteSize().Y / NChannels;
	const float ChannelSlotMidPoint = ChannelSlotHeight / 2;
	const float HeightScale = InAllottedGeometry.GetAbsoluteSize().Y / (2.f * 32767 * NChannels) * VerticalZoomFactor;
	
	const uint32 PixelWidth = FMath::FloorToInt(InAllottedGeometry.GetAbsoluteSize().X);
	check(PixelWidth * NChannels == InWaveformPeaks.Num());

	float MaxDistanceFromBoundary = 2.f;

	OutDrawCoordinates.SetNumUninitialized(InWaveformPeaks.Num());
	FWaveformLineCoordinates* OutCoordinatesData = OutDrawCoordinates.GetData();

	for (uint16 Channel = 0; Channel < NChannels; ++Channel)
	{
		const float WaveformTopBoundary = ChannelSlotHeight * Channel;
		const float WaveformBottomBoundary = WaveformTopBoundary + ChannelSlotHeight;
		const float WaveformDrawCenter = WaveformTopBoundary + ChannelSlotMidPoint;

		for (uint32 Pixel = 0; Pixel < PixelWidth; ++Pixel)
		{
			uint32 PeakIndex = Pixel * NChannels + Channel;
			const WaveformDrawingUtils::SampleRange& SamplePeaks = InWaveformPeaks[PeakIndex];

			const float SampleMaxScaled = SamplePeaks.GetUpperBoundValue() * HeightScale > 0.001f ? SamplePeaks.GetUpperBoundValue() * HeightScale : 0.1f;
			const float SampleMinScaled = SamplePeaks.GetLowerBoundValue() * HeightScale < -0.001f ? SamplePeaks.GetLowerBoundValue() * HeightScale : -0.1f;

			const float Top = FMath::Max(WaveformDrawCenter - SampleMaxScaled, WaveformTopBoundary + MaxDistanceFromBoundary);
			const float Bottom = FMath::Min(WaveformDrawCenter - SampleMinScaled, WaveformBottomBoundary - MaxDistanceFromBoundary);

			OutCoordinatesData[PeakIndex].PointA = FVector2D(Pixel, Top);
			OutCoordinatesData[PeakIndex].PointB = FVector2D(Pixel, Bottom);
		}
	}
}

void SWaveformViewer::DrawGridLines(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	TArray<FVector2D> LinePoints;
	LinePoints.SetNumUninitialized(2);

	const double MinorGridXStep = GridMetrics.MajorGridXStep / GridMetrics.NumMinorGridDivisions;

	for (double CurrentMajorLineX = GridMetrics.FirstMajorTickX; CurrentMajorLineX < AllottedGeometry.Size.X; CurrentMajorLineX += GridMetrics.MajorGridXStep)
	{
		const double MajorLineX = CurrentMajorLineX;

		LinePoints[0] = FVector2D(MajorLineX, 0.f);
		LinePoints[1] = FVector2D(MajorLineX, AllottedGeometry.Size.Y);

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			++LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			MajorGridLineColor->GetSpecifiedColor(),
			false);


		for (int32 MinorLineIndex = 1; MinorLineIndex < GridMetrics.NumMinorGridDivisions; ++MinorLineIndex)
		{
			const double MinorLineX = MajorLineX + MinorGridXStep * MinorLineIndex;

			LinePoints[0] = FVector2D(MinorLineX, 0.);
			LinePoints[1] = FVector2D(MinorLineX, AllottedGeometry.Size.Y);

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				++LayerId,
				AllottedGeometry.ToPaintGeometry(),
				LinePoints,
				ESlateDrawEffect::None,
				MinorGridLineColor->GetSpecifiedColor(),
				false);

		}
	}
}

void SWaveformViewer::UpdateGridMetrics(const FWaveEditorGridMetrics& InMetrics)
{
	GridMetrics = InMetrics;
}
