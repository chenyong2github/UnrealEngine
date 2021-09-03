// Copyright Epic Games, Inc. All Rights Reserved.

#include "Generators/SoundModulationEnvelopeFollower.h"

#include "Algo/MaxElement.h"
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
			AudioRenderThreadCommand([this, DeviceId = InDeviceId, NumChannels = Params.AudioBus->GetNumChannels(), BusId = Params.AudioBus->GetUniqueID()]()
			{
				if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
				{
					FAudioDevice* AudioDevice = DeviceManager->GetAudioDeviceRaw(DeviceId);
					if (FMixerDevice* MixerDevice = static_cast<FMixerDevice*>(AudioDevice))
					{
						AudioBusPatch = MixerDevice->AddPatchForAudioBus(BusId, Params.Gain);

						FEnvelopeFollowerInitParams EnvelopeFollowerInitParams;
						EnvelopeFollowerInitParams.SampleRate = MixerDevice->SampleRate;
						EnvelopeFollowerInitParams.NumChannels = NumChannels;
						EnvelopeFollowerInitParams.AttackTimeMsec = Params.AttackTime;
						EnvelopeFollowerInitParams.ReleaseTimeMsec = Params.ReleaseTime;
						EnvelopeFollowerInitParams.Mode = EPeakMode::Peak;

						EnvelopeFollower = FEnvelopeFollower(EnvelopeFollowerInitParams);

						if (Params.bInvert)
						{
							CurrentValue = 1.0f;
						}
						else
						{
							CurrentValue = 0.f;
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
		if (AudioBusPatch.IsValid())
		{
			const int32 NumSamples = AudioBusPatch->GetNumSamplesAvailable();
			const int32 NumFrames = NumSamples / EnvelopeFollower.GetNumChannels();

			if (NumSamples > 0)
			{
				TempBuffer.Reset();
				TempBuffer.AddZeroed(NumSamples);

				AudioBusPatch->PopAudio(TempBuffer.GetData(), NumSamples, true /* bUseLatestAudio */);

				EnvelopeFollower.ProcessAudio(TempBuffer.GetData(), NumFrames);
			}

			float MaxValue = 0.f;
			if (const float* MaxEnvelopePtr = Algo::MaxElement(EnvelopeFollower.GetEnvelopeValues()))
			{
				MaxValue = FMath::Clamp(*MaxEnvelopePtr, 0.f, 1.f);
			}

			if (Params.bInvert)
			{
				CurrentValue = 1.0f - MaxValue;
			}
			else
			{
				CurrentValue = MaxValue;
			}
		}
		else
		{
			CurrentValue = 0.f;
		}

	}

#if !UE_BUILD_SHIPPING
	void FEnvelopeFollowerGenerator::GetDebugValues(TArray<FString>& OutDebugValues) const
	{
		OutDebugValues.Add(FString::Printf(TEXT("%.4f"), GetValue()));
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
