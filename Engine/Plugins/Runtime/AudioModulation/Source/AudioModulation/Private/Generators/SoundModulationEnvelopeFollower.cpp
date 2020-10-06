// Copyright Epic Games, Inc. All Rights Reserved.

#include "Generators/SoundModulationEnvelopeFollower.h"

#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "AudioModulation.h"


namespace AudioModulation
{
#if !UE_BUILD_SHIPPING
	const FString FEnvelopeFollowerGenerator::DebugName = TEXT("EnvelopeFollower");

	void FEnvelopeFollowerGenerator::GetDebugCategories(TArray<FString>& OutDebugCategories) const
	{
		OutDebugCategories = USoundModulationGeneratorEnvelopeFollower::GetDebugCategories();
	}
#endif // !UE_BUILD_SHIPPING

	FEnvelopeFollowerGenerator::FEnvelopeFollowerGenerator(const FEnvelopeFollowerGeneratorParams& InParams, Audio::FDeviceId InDeviceId)
		: IGenerator(InDeviceId)
		, Params(InParams)
	{
		using namespace Audio;

		if (Params.AudioBus)
		{
			AudioRenderThreadCommand([this, DeviceId = InDeviceId, BusId = Params.AudioBus->GetUniqueID()]()
			{
				if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
				{
					FAudioDevice* AudioDevice = DeviceManager->GetAudioDeviceRaw(DeviceId);
					if (FMixerDevice* MixerDevice = static_cast<FMixerDevice*>(AudioDevice))
					{
						AudioBusPatch = MixerDevice->AddPatchForAudioBus(BusId, Params.Gain);
						EnvelopeFollower = FEnvelopeFollower(MixerDevice->SampleRate, Params.AttackTime, Params.ReleaseTime, Audio::EPeakMode::Peak);
						if (Params.bInvert)
						{
							CurrentValue = 1.0f - EnvelopeFollower.GetCurrentValue();
						}
						else
						{
							CurrentValue = EnvelopeFollower.GetCurrentValue();
						}
					}
				}
			});
		}
	}

	float FEnvelopeFollowerGenerator::GetValue() const
	{
		return CurrentValue;
	}

	bool FEnvelopeFollowerGenerator::IsBypassed() const
	{
		return Params.bBypass;
	}

	void FEnvelopeFollowerGenerator::Update(double InElapsed)
	{
		float Amp = 0.0f;

		if (AudioBusPatch.IsValid())
		{
			int32 NumSamples = AudioBusPatch->GetNumSamplesAvailable();
			if (NumSamples > 0)
			{
				TempBuffer.Reset();
				TempBuffer.AddZeroed(NumSamples);

				AudioBusPatch->PopAudio(TempBuffer.GetData(), NumSamples, true /* bUseLatestAudio */);

				// If NumSamples is below 4 samples, just take first sample
				static const int32 SimdMultiple = 4;
				if (NumSamples % SimdMultiple == 0)
				{
					Amp = Audio::GetAverageAmplitude(TempBuffer.GetData(), NumSamples);
				}
				// If NumSamples is not a multiple of 4, do it the non-SIMD way
				else
				{
					Amp = 0.0f;
					for (int32 i = 0; i < NumSamples; ++i)
					{
						Amp += TempBuffer[i];
					}
					Amp /= NumSamples;
				}
			}
		}

		EnvelopeFollower.ProcessAudio(Amp);
		if (Params.bInvert)
		{
			CurrentValue = 1.0f - EnvelopeFollower.GetCurrentValue();
		}
		else
		{
			CurrentValue = EnvelopeFollower.GetCurrentValue();
		}
	}

#if !UE_BUILD_SHIPPING
	void FEnvelopeFollowerGenerator::GetDebugValues(TArray<FString>& OutDebugValues) const
	{
		OutDebugValues.Add(FString::Printf(TEXT("%.4f"), EnvelopeFollower.GetCurrentValue()));
		OutDebugValues.Add(FString::Printf(TEXT("%.4f"), Params.Gain));
		OutDebugValues.Add(FString::Printf(TEXT("%.4f"), Params.AttackTime));
		OutDebugValues.Add(FString::Printf(TEXT("%.4f"), Params.ReleaseTime));
	}

	const FString& FEnvelopeFollowerGenerator::GetDebugName() const
	{
		return DebugName;
	}
#endif // !UE_BUILD_SHIPPING
} // namespace AudioModulation

#if !UE_BUILD_SHIPPING
const FString& USoundModulationGeneratorEnvelopeFollower::GetDebugName()
{
	using namespace AudioModulation;
	return FEnvelopeFollowerGenerator::DebugName;
}
#endif // !UE_BUILD_SHIPPING
