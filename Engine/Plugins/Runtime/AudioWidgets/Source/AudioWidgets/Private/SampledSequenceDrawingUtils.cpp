// Copyright Epic Games, Inc. All Rights Reserved.

#include "SampledSequenceDrawingUtils.h"

#include "Layout/Geometry.h"

SampledSequenceDrawingUtils::FHorizontalDimensionSlot::FHorizontalDimensionSlot(const uint16 DimensionToDraw, const uint16 TotalNumDimensions, const FGeometry& InAllottedGeometry)
{
	Height = InAllottedGeometry.GetLocalSize().Y / TotalNumDimensions;
	float ChannelSlotMidPoint = Height / 2.f;

	Top = Height * DimensionToDraw;
	Center = Top + ChannelSlotMidPoint;
	Bottom = Top + Height;
}

void SampledSequenceDrawingUtils::GenerateSampleBinsCoordinatesForGeometry(TArray<FSampleBinCoordinates>& OutDrawCoordinates, const FGeometry& InAllottedGeometry, const TArray<TRange<float>>& InSampleBins, const uint16 NDimensions, const FSampledSequenceDrawingParams Params)
{
	if (!ensure(NDimensions != 0))
	{
		return;
	}

	const uint32 PixelWidth = FMath::FloorToInt(InAllottedGeometry.GetLocalSize().X);
	check(PixelWidth * NDimensions == InSampleBins.Num());

	const float HeightScale = InAllottedGeometry.GetLocalSize().Y / (2.f * Params.MaxDisplayedValue * NDimensions) * Params.MaxSequenceHeight * Params.VerticalZoomFactor;


	OutDrawCoordinates.SetNumUninitialized(InSampleBins.Num());
	FSampleBinCoordinates* OutCoordinatesData = OutDrawCoordinates.GetData();

	for (uint16 Channel = 0; Channel < NDimensions; ++Channel)
	{
		const FHorizontalDimensionSlot ChannelBoundaries(Channel, NDimensions, InAllottedGeometry);

		for (uint32 Pixel = 0; Pixel < PixelWidth; ++Pixel)
		{
			uint32 PeakIndex = Pixel * NDimensions + Channel;
			const TRange<float>& MinMaxBin = InSampleBins[PeakIndex];

			const float SampleMaxScaled = MinMaxBin.GetUpperBoundValue() * HeightScale > Params.MinScaledBinValue ? MinMaxBin.GetUpperBoundValue() * HeightScale : Params.MinSequenceHeight;
			const float SampleMinScaled = MinMaxBin.GetLowerBoundValue() * HeightScale < -1 * Params.MinScaledBinValue ? MinMaxBin.GetLowerBoundValue() * HeightScale : -1.f * Params.MinSequenceHeight;

			const float Top = FMath::Max(ChannelBoundaries.Center - SampleMaxScaled, ChannelBoundaries.Top + Params.DimensionSlotMargin);
			const float Bottom = FMath::Min(ChannelBoundaries.Center - SampleMinScaled, ChannelBoundaries.Bottom - Params.DimensionSlotMargin);

			OutCoordinatesData[PeakIndex].Top = FVector2D(Pixel, Top);
			OutCoordinatesData[PeakIndex].Bottom = FVector2D(Pixel, Bottom);
		}
	}
}

void SampledSequenceDrawingUtils::GenerateSequencedSamplesCoordinatesForGeometry(TArray<FVector2D>& OutDrawCoordinates, TArrayView<const float> InSampleData, const FGeometry& InAllottedGeometry, const uint16 NDimensions, const FFixedSampledSequenceGridMetrics InGridMetrics, const FSampledSequenceDrawingParams Params)
{
	OutDrawCoordinates.Empty();

	if (!ensure(NDimensions != 0))
	{
		return;
	}

	const uint32 NumFramesToDisplay = InSampleData.Num() / NDimensions;

	TArray<FHorizontalDimensionSlot> ChannelSlotsBoundaries;

	for (uint16 Channel = 0; Channel < NDimensions; ++Channel)
	{
		ChannelSlotsBoundaries.Emplace(Channel, NDimensions, InAllottedGeometry);
	}

	uint32 FrameIndex = 0;
	const float XDrawMargin = InAllottedGeometry.Size.X + InGridMetrics.PixelsPerFrame;

	for (double FrameX = InGridMetrics.FirstMajorTickX; FrameX < XDrawMargin; FrameX += InGridMetrics.PixelsPerFrame)
	{
		const double XRatio = FrameX / InAllottedGeometry.Size.X;

		if (FrameX >= 0 && FrameIndex < NumFramesToDisplay)
		{
			for (uint16 Channel = 0; Channel < NDimensions; ++Channel)
			{
				const FHorizontalDimensionSlot& ChannelBoundaries = ChannelSlotsBoundaries[Channel];
				const uint32 SampleIndex = FrameIndex * NDimensions + Channel;
				const float SampleValue = InSampleData[SampleIndex];

				const float SampleValueRatio = SampleValue / Params.MaxDisplayedValue * Params.MaxSequenceHeight * Params.VerticalZoomFactor;
				const float TopBoundary = ChannelBoundaries.Top + Params.DimensionSlotMargin;
				const float BottomBoundary = ChannelBoundaries.Bottom - Params.DimensionSlotMargin;
				const float SampleY = FMath::Clamp((SampleValueRatio * ChannelBoundaries.Height / 2.f) + ChannelBoundaries.Center, TopBoundary, BottomBoundary);

				const FVector2D SampleCoordinates(FrameX, SampleY);

				OutDrawCoordinates.Add(SampleCoordinates);
			}

			FrameIndex++;
		}
	}
}
