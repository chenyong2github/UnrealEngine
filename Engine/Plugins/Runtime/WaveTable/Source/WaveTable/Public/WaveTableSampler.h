// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "DSP/Filter.h"
#include "HAL/Platform.h"
#include "WaveTableSettings.h"


namespace WaveTable
{
	class WAVETABLE_API FWaveTableSampler
	{
	public:
		enum class EInterpolationMode : uint8
		{
			None,
			Linear,
			Cubic,

			COUNT
		};

		struct WAVETABLE_API FSettings
		{
			float Amplitude = 1.0f;
			float Offset = 0.0f;

			// How many times to read through the incoming
			// buffer as a ratio to a single output buffer
			float Freq = 1.0f;

			// Offset from beginning to end of array [0.0, 1.0]
			float Phase = 0.0f;

			EInterpolationMode InterpolationMode = EInterpolationMode::Linear;
		};

		FWaveTableSampler();
		FWaveTableSampler(FSettings&& InSettings);

		void Process(TArrayView<const float> InTableView, TArrayView<float> OutSamplesView);
		void Process(TArrayView<const float> InTableView, TArrayView<const float> InFreqModulator, TArrayView<const float> InPhaseModulator, TArrayView<float> OutSamplesView);

		void SetInterpolationMode(EInterpolationMode InMode);
		void SetFreq(float InFreq);
		void SetPhase(float InPhase);

	private:
		void ComputeIndexFrequency(TArrayView<const float> InTableView, TArrayView<const float> InFreqModulator, TArrayView<float> OutSamplesView) const;
		void ComputeIndexPhase(TArrayView<const float> InTableView, TArrayView<const float> InPhaseModulator, TArrayView<float> OutSamplesView);

		float LastIndex = 0.0;

		TArray<float> PhaseModScratch;
		FSettings Settings;
	};
} // namespace WaveTable