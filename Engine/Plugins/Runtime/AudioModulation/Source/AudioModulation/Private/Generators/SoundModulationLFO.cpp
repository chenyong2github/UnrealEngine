// Copyright Epic Games, Inc. All Rights Reserved.
#include "Generators/SoundModulationLFO.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "Engine/World.h"


namespace AudioModulation
{
#if !UE_BUILD_SHIPPING
	const FString FLFOGenerator::DebugName = TEXT("LFO");

	void FLFOGenerator::GetDebugCategories(TArray<FString>& OutDebugCategories) const
	{
		OutDebugCategories = USoundModulationGeneratorLFO::GetDebugCategories();
	}

	void FLFOGenerator::GetDebugValues(TArray<FString>& OutDebugValues) const
	{
		OutDebugValues.Add(FString::Printf(TEXT("%.4f"), GetValue()));
		OutDebugValues.Add(FString::Printf(TEXT("%.4f"), LFO.GetGain()));
		OutDebugValues.Add(FString::Printf(TEXT("%.4f"), LFO.GetFrequency()));
		OutDebugValues.Add(FString::Printf(TEXT("%.4f"), Params.Offset));

		switch (LFO.GetType())
		{
			case Audio::ELFO::DownSaw:
				OutDebugValues.Add(TEXT("DownSaw"));
				break;

			case Audio::ELFO::Exponential:
				OutDebugValues.Add(TEXT("Exponential"));
				break;

			case Audio::ELFO::RandomSampleHold:
				OutDebugValues.Add(TEXT("Random (Sample & Hold)"));
				break;

			case Audio::ELFO::Sine:
				OutDebugValues.Add(TEXT("Sine"));
				break;

			case Audio::ELFO::Square:
				OutDebugValues.Add(TEXT("Square"));
				break;

			case Audio::ELFO::Triangle:
				OutDebugValues.Add(TEXT("Triangle"));
				break;

			case Audio::ELFO::UpSaw:
				OutDebugValues.Add(TEXT("Up Saw"));
				break;

			default:
				static_assert(static_cast<int32>(Audio::ELFO::NumLFOTypes) == 7, "Missing LFO type case coverage");
				break;
		}
	}

	const FString& FLFOGenerator::GetDebugName() const
	{
		return DebugName;
	}
#endif // !UE_BUILD_SHIPPING
} // namespace AudioModulation

#if !UE_BUILD_SHIPPING
const FString& USoundModulationGeneratorLFO::GetDebugName()
{
	using namespace AudioModulation;
	return FLFOGenerator::DebugName;
}
#endif // !UE_BUILD_SHIPPING