// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IFixedSampledSequenceGridService.h"
#include "Math/Vector2D.h"
#include "Math/Range.h"

struct FGeometry;

namespace SampledSequenceDrawingUtils
{
	/**
	* Groups samples evenly into a number of desired bins.
	* Each bin contains the min and max values of the grouped samples.
	*
	* @param OutBins			TArray where the bins will be written to
	* @param NumDesiredBins		Number of output bins
	* @param RawDataPtr			Ptr to the beginning of the samples
	* @param TotalNumSamples	The total number of samples of the input time series
	* @param NDimensions		Number of interleaved dimensions in the time series
	* @param StartRatio			The ratio of the total number of frames (in a range of 0-1) at which grouping should start
	* @param EndRatio			The ratio of the total number of frames (in a range of 0-1) at which grouping should end.
	*
	*/
	template<typename SamplesType>
	void GroupInterleavedSamplesIntoMinMaxBins(TArray<TRange<SamplesType>>& OutBins, const uint32 NumDesiredBins, const SamplesType* RawDataPtr, const uint32 TotalNumSamples, const uint16 NDimensions = 1, const double StartRatio = 0.0, double EndRatio = 1.0)
	{
		check(StartRatio >= 0.f && StartRatio < EndRatio);
		check(EndRatio <= 1.f);

		uint32 NumPeaks = NumDesiredBins * NDimensions;
		OutBins.SetNumUninitialized(NumPeaks);
		const uint32 NumFrames = TotalNumSamples / NDimensions;

		double FramesPerBin = ((EndRatio - StartRatio) * NumFrames) / NumDesiredBins;
		double IterationStartFrame = StartRatio * NumFrames;
		uint32 FirstBinnedFrame = FMath::RoundToInt(StartRatio * NumFrames);

		for (uint32 Peak = 0; Peak < NumPeaks; Peak += NDimensions)
		{
			uint32 LastBinnedFrame = IterationStartFrame + FramesPerBin;

			for (uint16 Channel = 0; Channel < NDimensions; ++Channel)
			{
				SamplesType MaxSampleValue = TNumericLimits<SamplesType>::Min();
				SamplesType MinSampleValue = TNumericLimits<SamplesType>::Max();

				for (uint32 Frame = FirstBinnedFrame; Frame < LastBinnedFrame; ++Frame)
				{
					const uint32 SampleIndex = Frame * NDimensions + Channel;
					check(SampleIndex < TotalNumSamples);
					SamplesType SampleValue = RawDataPtr[SampleIndex];

					MinSampleValue = FMath::Min(MinSampleValue, SampleValue);
					MaxSampleValue = FMath::Max(MaxSampleValue, SampleValue);

				}

				OutBins[Peak + Channel] = TRange<SamplesType>(MinSampleValue, MaxSampleValue);
			}

			IterationStartFrame += FramesPerBin;
			FirstBinnedFrame = LastBinnedFrame;
		}
	}

	/**
	* Groups samples of a time series into an equal number of desired bins.
	* Each bin contains the min and max values of the grouped samples.
	*
	* @param OutBins			TArray where the bins will be written to
	* @param NumDesiredBins		Number of output bins
	* @param RawDataPtr			Ptr to the beginning of the samples
	* @param TotalNumSamples	The total number of samples of the input time series
	* @param SampleRate			SampleRate of the time series
	* @param NDimensions		Number of interleaved dimensions in the time series
	* @param StartTime			The time at which grouping should start
	* @param EndTime 			The time at which grouping should end.
	*
	* Note: With a negative EndTime, the method will calculate automatically the EndTime by doing
	* TotalNumSamples / (SampleRate * NDimensions)
	*
	*/
	template<typename SamplesType>
	void GroupInterleavedSampledTSIntoMinMaxBins(TArray<TRange<SamplesType>>& OutBins, const uint32 NumDesiredBins, const SamplesType* RawDataPtr, const uint32 TotalNumSamples, const uint32 SampleRate, const uint16 NDimensions = 1, const float StartTime = 0.f, float EndTime = -1.f)
	{
		const double TotalTime = TotalNumSamples / ((float)SampleRate * NDimensions);
		const double StartRatio = StartTime / TotalTime;
		const double EndRatio = EndTime >= 0.f ? FMath::Clamp(EndTime / TotalTime, StartRatio, 1.0) : 1.0;

		GroupInterleavedSamplesIntoMinMaxBins(OutBins, NumDesiredBins, RawDataPtr, TotalNumSamples, NDimensions, StartRatio, EndRatio);
	}

	/**
		* Params for drawing sequences of samples/sample bins
		* @param MaxDisplayedValue					The highest value a sample can take
		* @param DimensionSlotMargin				Margin to keep from the dimension slot boundaries (pixels)
		* @param MinSequenceHeight					The minimum height (ratio) the drawn sequence can take in a channel slot
		* @param MaxSequenceHeight					The maximum height (ratio) the drawn sequence can take in a channel slot
		* @param MinScaledBinValue					Minimum value a scaled bin can have
		* @param VerticalZoomFactor					Sequence zoom factor
	*/
	struct FSampledSequenceDrawingParams
	{
		double MaxDisplayedValue = 1.f;
		float DimensionSlotMargin = 2.f;
		float MaxSequenceHeight = 0.9f;
		float MinSequenceHeight = 0.1f;
		float MinScaledBinValue = 0.001f;
		float VerticalZoomFactor = 1.f;
	};

	/**
		* Represents an horizontal slot in which samples or sample bins can be drawn
	*/
	struct FHorizontalDimensionSlot
	{
		explicit FHorizontalDimensionSlot(const uint16 DimensionToDraw, const uint16 TotalNumDimensions, const FGeometry& InAllottedGeometry);

		float Top;
		float Center;
		float Bottom;
		float Height;
	};

	struct FSampleBinCoordinates
	{
		FVector2D Top;
		FVector2D Bottom;
	};

	/**
		* Generates an array of coordinates to draw binned samples as vertical lines.
		* Coordinates will be generated for an horizontal view
		* 
		* @param OutDrawCoordinates			The generated coordinates
		* @param InAllottedGeometry			The geometry to draw into
		* @param InSampleBins				The bins to draw
		* @param NDimensions				Number of interleaved dimensions in the time series
		* @param Params						Drawing Params
	*/
	void GenerateSampleBinsCoordinatesForGeometry(TArray<FSampleBinCoordinates>& OutDrawCoordinates, const FGeometry& InAllottedGeometry, const TArray<TRange<float>>& InSampleBins, const uint16 NDimensions, const FSampledSequenceDrawingParams Params = FSampledSequenceDrawingParams());

	/**
	* Generates an array of coordinates to draw single samples in succession horizontally.
	*
	* @param OutDrawCoordinates			The generated coordinates
	* @param InSampleData				The samples to draw
	* @param InAllottedGeometry			The geometry to draw into
	* @param NDimensions				Number of interleaved dimensions in the time series
	* @param InGridMetrics				GridMetrics followed to lay down the samples
	* @param Params						Drawing Params
	*/
	void GenerateSequencedSamplesCoordinatesForGeometry(TArray<FVector2D>& OutDrawCoordinates, TArrayView<const float> InSampleData, const FGeometry& InAllottedGeometry, const uint16 NDimensions, const FFixedSampledSequenceGridMetrics InGridMetrics, const FSampledSequenceDrawingParams Params = FSampledSequenceDrawingParams());

}