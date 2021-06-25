// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/SineWaveTableOsc.h"

namespace Audio
{
	static const int32 SINE_WAVE_TABLE_SIZE = 4096;

	FSineWaveTableOsc::FSineWaveTableOsc()
	{
		SetFrequencyHz(FrequencyHz);
	}

	FSineWaveTableOsc::~FSineWaveTableOsc()
	{
	}

	void FSineWaveTableOsc::Init(const float InSampleRate, const float InFrequencyHz, const float InPhase)
	{
		SampleRate = InSampleRate;
		FrequencyHz = InFrequencyHz;
		InitialPhase = FMath::Clamp(InPhase, 0.0f, 1.0f);

		Reset();
		UpdatePhaseIncrement();
	}

	void FSineWaveTableOsc::SetSampleRate(const float InSampleRate)
	{
		SampleRate = InSampleRate;
		UpdatePhaseIncrement();
	}

	void FSineWaveTableOsc::Reset()
	{
		ReadIndex = InitialPhase * (WaveTableBuffer.Num() - 1.0f);		
	}

	void FSineWaveTableOsc::SetFrequencyHz(const float InFrequencyHz)
	{
		FrequencyHz = InFrequencyHz;
		UpdatePhaseIncrement();
	}

	void FSineWaveTableOsc::SetPhase(const float InPhase)
	{
		InitialPhase = FMath::Clamp(InPhase, 0.0f, 1.0f);
		Reset();
	}

	void FSineWaveTableOsc::UpdatePhaseIncrement()
	{
		PhaseIncrement = (float)WaveTableBuffer.Num() * FrequencyHz / (float)SampleRate;
	}

	void FSineWaveTableOsc::Generate(float* OutSample)
	{
		const int32 ReadIndexPrev = (int32)ReadIndex;
		const float Alpha = ReadIndex - (float) ReadIndexPrev;

		const int32 ReadIndexNext = (ReadIndexPrev + 1) % WaveTableBuffer.Num();
		*OutSample = FMath::Lerp(WaveTableBuffer[ReadIndexPrev], WaveTableBuffer[ReadIndexNext], Alpha);
		
		// Increment ReadIndex and wrap around if necessary
		ReadIndex += PhaseIncrement;
		while (ReadIndex >= WaveTableBuffer.Num())
		{
			ReadIndex -= WaveTableBuffer.Num();
		}
	}

	void FSineWaveTableOsc::Generate(float* OutBuffer, const int32 NumSamples)
	{
		for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
		{
			Generate(&OutBuffer[SampleIndex]);
		}
	}

	const TArray<float>& FSineWaveTableOsc::GetWaveTable()
	{
		auto MakeSineTable = []() -> const TArray<float>
		{
			// Generate the table
			TArray<float> WaveTable;
			WaveTable.AddUninitialized(SINE_WAVE_TABLE_SIZE);
			float* WaveTableData = WaveTable.GetData();
			for (int32 i = 0; i < SINE_WAVE_TABLE_SIZE; ++i)
			{
				float Phase = (float)i / SINE_WAVE_TABLE_SIZE;
				WaveTableData[i] = FMath::Sin(Phase * 2.f * PI);
			}
			return WaveTable;
		};

		static const TArray<float> SineWaveTable = MakeSineTable();
		return SineWaveTable;
	}
}
