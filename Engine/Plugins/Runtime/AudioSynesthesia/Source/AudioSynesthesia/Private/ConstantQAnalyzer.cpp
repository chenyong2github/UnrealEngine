// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConstantQAnalyzer.h"

#include "DSP/ConstantQ.h"
#include "DSP/AudioFFT.h"
#include "DSP/FloatArrayMath.h"
#include "DSP/BufferVectorOperations.h"

namespace Audio
{
	FConstantQAnalyzer::FConstantQAnalyzer(const FConstantQAnalyzerSettings& InSettings, const float InSampleRate)
	:	Settings(InSettings)
	,	SampleRate(InSampleRate)
	,	NumUsefulFFTBins((Settings.FFTSize / 2) + 1)
    ,   Window(Settings.WindowType, Settings.FFTSize, 1, false)
	{
		// Need FFTSize atleast 8 to support optimized operations.
		check(Settings.FFTSize >= 8);
		check(SampleRate > 0.f);

		CQTTransform = NewPseudoConstantQKernelTransform(Settings, Settings.FFTSize, SampleRate);

		// Resize internal buffers
		WindowedSamples.Reset(Settings.FFTSize);
		WindowedSamples.AddUninitialized(Settings.FFTSize);

		FFTOutputRealData.Reset(Settings.FFTSize);
		FFTOutputRealData.AddUninitialized(Settings.FFTSize);

		FFTOutputImagData.Reset(Settings.FFTSize);
		FFTOutputImagData.AddUninitialized(Settings.FFTSize);

		SpectrumBuffer.Reset(NumUsefulFFTBins);
		SpectrumBuffer.AddUninitialized(NumUsefulFFTBins);
	}

	void FConstantQAnalyzer::CalculateCQT(const float* InSamples, TArray<float>& OutCQT)
	{
		// Copy input samples and apply window
		FMemory::Memcpy(WindowedSamples.GetData(), InSamples, sizeof(float) * Settings.FFTSize);
		Window.ApplyToBuffer(WindowedSamples.GetData());

		// Take FFT of windowed samples
		const FFTTimeDomainData TimeData = {WindowedSamples.GetData(), Settings.FFTSize};
		FFTFreqDomainData FreqData = {FFTOutputRealData.GetData(), FFTOutputImagData.GetData()};

		PerformFFT(TimeData, FreqData);

		// Calculate spectrum from FFT output
		ComputeSpectrum(Settings.SpectrumType, FreqData, Settings.FFTSize, SpectrumBuffer);
		
		// Convert spectrum to CQT
		CQTTransform->TransformArray(SpectrumBuffer, OutCQT);

		// Apply decibel scaling if appropriate.
		if (EConstantQScaling::Decibel == Settings.Scaling)
		{
			// Convert to dB
			switch (Settings.SpectrumType)
			{
				case ESpectrumType::MagnitudeSpectrum:
					ArrayMagnitudeToDecibelInPlace(OutCQT, -90.f);
					break;

				case ESpectrumType::PowerSpectrum:
					ArrayPowerToDecibelInPlace(OutCQT, -90.f);
					break;

				default:
					check(false);
					//checkf(false, TEXT("Unhandled ESpectrumType %s"), GETENUMSTRING(ESpectrumType, Settings.SpectrumType));
					ArrayPowerToDecibelInPlace(OutCQT, -90.f);
			}
		}
	}

	const FConstantQAnalyzerSettings& FConstantQAnalyzer::GetSettings() const
	{
		return Settings;
	}
}

