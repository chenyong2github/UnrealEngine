// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/ConstantQ.h"
#include "CoreMinimal.h"
#include "DSP/FloatArrayMath.h"

namespace
{
	float GetConstantQCenterFrequency(const int32 InBandIndex, const float InBaseFrequency, const float InBandsPerOctave)
	{
		check(InBandsPerOctave > 0.f);
		return InBaseFrequency * FMath::Pow(2.f, static_cast<float>(InBandIndex) / InBandsPerOctave);
	}

	float GetConstantQBandWidth(const float InBandCenter, const float InBandsPerOctave, const float InBandWidthStretch)
	{
		check(InBandsPerOctave > 0.f);
		return InBandWidthStretch * InBandCenter * (FMath::Pow(2.f, 1.f / InBandsPerOctave) - 1.f);
	}

	void FillArrayWithTruncatedGaussian(const float InCenterFreq, const float InBandWidth, const int32 InFFTSize, const float InSampleRate, TArray<float>& OutOffsetArray, int32& OutOffsetIndex)
	{
		check(InBandWidth > 0.f);
		check(InFFTSize > 0);
		check(InCenterFreq >= 0.f);
		check(InSampleRate > 0.f);

		// Determine points where gaussian will value will go below a small number
		const float SignificantHalfBandWidth = InBandWidth * FMath::Sqrt(-2.f * FMath::Loge(SMALL_NUMBER));
		const float LowestSignificantFreq = FMath::Clamp(InCenterFreq - SignificantHalfBandWidth, 0.f, InSampleRate / 2.f);
		const float HighestSignificantFreq = FMath::Clamp(InCenterFreq + SignificantHalfBandWidth, 0.f, InSampleRate / 2.f);
		int32 LowestSignificantIndex = FMath::CeilToInt(InFFTSize * LowestSignificantFreq / InSampleRate);
		int32 HighestSignificantIndex = FMath::FloorToInt(InFFTSize * HighestSignificantFreq / InSampleRate);
 		int32 Num = HighestSignificantIndex - LowestSignificantIndex + 1;

		if (Num < 1)
		{
			Num = 1;
		}

		// Prepare outputs
		OutOffsetIndex = LowestSignificantIndex;
		OutOffsetArray.Reset(Num);
		OutOffsetArray.AddUninitialized(Num);

		// Fill array with truncated Gaussian
		const float BandWidthSquared = InBandWidth * InBandWidth;
		for (int32 i = 0; i < Num; i++)
		{
			float FFTBinHz = static_cast<float>(i + LowestSignificantIndex) * InSampleRate / InFFTSize;
			float DeltaHz = FFTBinHz - InCenterFreq;
			OutOffsetArray[i] = FMath::Exp(-0.5 * (DeltaHz * DeltaHz) / BandWidthSquared);
		}
	}
}

namespace Audio
{
	TUniquePtr<FContiguousSparse2DKernelTransform> NewPseudoConstantQKernelTransform(const FPseudoConstantQKernelSettings& InSettings, const int32 InFFTSize, const float InSampleRate)
	{
		check(InSampleRate > 0.f);
		check(InFFTSize > 0);

		const int32 NumUsefulFFTBins = (InFFTSize / 2) + 1;

		TUniquePtr<FContiguousSparse2DKernelTransform> Transform = MakeUnique<FContiguousSparse2DKernelTransform>(NumUsefulFFTBins, InSettings.NumBands);

		for (int32 CQTBandIndex = 0; CQTBandIndex < InSettings.NumBands; CQTBandIndex++)
		{
			// Determine band center and width for this CQT band
			const float BandCenter = GetConstantQCenterFrequency(CQTBandIndex, InSettings.KernelLowestCenterFreq, InSettings.NumBandsPerOctave);
			const float BandWidth = GetConstantQBandWidth(BandCenter, InSettings.NumBandsPerOctave, InSettings.BandWidthStretch);

			if ((BandCenter - BandWidth) > InSampleRate)
			{
				continue;
			}

			if (BandCenter > (2.f * InSampleRate))
			{
				continue;
			}

			// Create gaussian centered around center freq and with appropriate bandwidth
			TArray<float> OffsetBandWeights;
			int32 OffsetBandWeightsIndex = 0;
			FillArrayWithTruncatedGaussian(BandCenter, BandWidth, InFFTSize, InSampleRate, OffsetBandWeights, OffsetBandWeightsIndex);


			// set infs and nans to zero
			for (int32 i = 0; i < OffsetBandWeights.Num(); i++)
			{
				if (!FMath::IsFinite(OffsetBandWeights[i]))
				{
					OffsetBandWeights[i] = 0.f;
				}
			}

			// Need to have a lower bound for 
			float DigitalBandWidth = FMath::Max(1.f / static_cast<float>(InFFTSize), BandWidth / InSampleRate);

			// Sanity check the CQT bins to make sure we the bandwidth wasn't so small that the array is essentially empty
			if (OffsetBandWeights.Num() > 0)
			{
				if (OffsetBandWeights.FilterByPredicate([](float InVal) { return InVal >= 0.5f; }).Num() < 1)
				{
					// Hit a condition where all values in band weights are below 0.5f. 
					// It's a bit of an arbitrary threshold, but it tells us that the bandwidth
					// is low enough and the FFT granularity course enough to where our pseudo 
					// cqt windows will likely miss data. In this case we force the window to 
					// have a single value of 1 at the nearest FFT bin.
					FMemory::Memset(OffsetBandWeights.GetData(), 0, OffsetBandWeights.Num() * sizeof(float));
					int32 NearestIndex = FMath::RoundToInt(InFFTSize * BandCenter / InSampleRate) - OffsetBandWeightsIndex;

					if (NearestIndex < OffsetBandWeights.Num())
					{
						NearestIndex = FMath::Clamp(NearestIndex, 0, OffsetBandWeights.Num() - 1);
						OffsetBandWeights[NearestIndex] = 1.f / static_cast<float>(InFFTSize);

						// Need to update digital bandwidth to be bandwidth of FFTbin
						DigitalBandWidth = 1.f / static_cast<float>(InFFTSize);
					}
				}
			}




			// Normalize window
			float NormDenom = 1.f;
			switch (InSettings.Normalization)
			{
				case EPseudoConstantQNormalization::EqualAmplitude:
					NormDenom = 1.f;
					break;
				case EPseudoConstantQNormalization::EqualEuclideanNorm:
					NormDenom = FMath::Sqrt(DigitalBandWidth * InFFTSize * FMath::Sqrt(PI  * 2.f));
					break;
				case EPseudoConstantQNormalization::EqualEnergy:
				default:
					NormDenom = DigitalBandWidth * InFFTSize * FMath::Sqrt(PI  * 2.f);
					break;
			}

			if ((NormDenom > 0.f) && (NormDenom != 1.f))
			{
				ArrayMultiplyByConstantInPlace(OffsetBandWeights, 1.f / NormDenom);
			}

			// Truncate if necessary
			if (OffsetBandWeightsIndex >= NumUsefulFFTBins)
			{
				OffsetBandWeightsIndex = 0;
				OffsetBandWeights.Empty();
			}
			else if ((OffsetBandWeightsIndex + OffsetBandWeights.Num()) > NumUsefulFFTBins)
			{
				OffsetBandWeights.SetNum(NumUsefulFFTBins - OffsetBandWeightsIndex);
			}

			// Store row in transform
			Transform->SetRow(CQTBandIndex, OffsetBandWeightsIndex, OffsetBandWeights);
		}

		return Transform;
	}
}
