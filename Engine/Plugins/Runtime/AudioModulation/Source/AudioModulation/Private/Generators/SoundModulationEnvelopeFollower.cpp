// Copyright Epic Games, Inc. All Rights Reserved.

#include "Generators/SoundModulationEnvelopeFollower.h"

#include "Algo/MaxElement.h"
#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "AudioModulation.h"


namespace AudioModulation
{
	namespace GeneratorEnvelopeFollowerPrivate
	{
		static const TArray<FString> DebugCategories =
		{
			TEXT("Value"),
			TEXT("Gain"),
			TEXT("Attack"),
			TEXT("Release"),
		};

		static const FString DebugName = TEXT("EnvelopeFollower");
	}

	class AUDIOMODULATION_API FEnvelopeFollowerGenerator : public FGeneratorBase
	{
	public:
		FEnvelopeFollowerGenerator() = default;

		FEnvelopeFollowerGenerator(const FEnvelopeFollowerGeneratorParams& InGeneratorParams, Audio::FDeviceId InDeviceId)
			: FGeneratorBase(InDeviceId)
			, Gain(InGeneratorParams.Gain)
			, bBypass(InGeneratorParams.bBypass ? 1 : 0)
			, bInvert(InGeneratorParams.bInvert ? 1 : 0)
			, bInitialized(0)
		{
			if (UAudioBus* AudioBus = InGeneratorParams.AudioBus)
			{
				BusId = AudioBus->GetUniqueID();
				InitParams.NumChannels = AudioBus->GetNumChannels();
			}

			if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
			{
				FAudioDevice* AudioDevice = DeviceManager->GetAudioDeviceRaw(InDeviceId);
				if (Audio::FMixerDevice* MixerDevice = static_cast<Audio::FMixerDevice*>(AudioDevice))
				{
					InitParams.SampleRate = MixerDevice->SampleRate;
				}
			}

			InitParams.AttackTimeMsec = InGeneratorParams.AttackTime * 1000.0f;
			InitParams.ReleaseTimeMsec = InGeneratorParams.ReleaseTime * 1000.0f;
			InitParams.Mode = Audio::EPeakMode::Peak;

			EnvelopeFollower = Audio::FEnvelopeFollower(InitParams);

			if (bInvert)
			{
				CurrentValue = 1.0f;
			}
		}

		virtual ~FEnvelopeFollowerGenerator()
		{
			AudioBusPatch.Reset();
		}

	#if !UE_BUILD_SHIPPING
		virtual void GetDebugCategories(TArray<FString>& OutDebugCategories) const override
		{
			OutDebugCategories = GeneratorEnvelopeFollowerPrivate::DebugCategories;
		}

		virtual const FString& GetDebugName() const override
		{
			return GeneratorEnvelopeFollowerPrivate::DebugName;
		}

		virtual void GetDebugValues(TArray<FString>& OutDebugValues) const override
		{
			const float AttackTime = EnvelopeFollower.GetAttackTimeMsec() / 1000.f;
			const float ReleaseTime = EnvelopeFollower.GetReleaseTimeMsec() / 1000.f;

			OutDebugValues.Add(FString::Printf(TEXT("%.4f"), GetValue()));
			OutDebugValues.Add(FString::Printf(TEXT("%.4f"), Gain));
			OutDebugValues.Add(FString::Printf(TEXT("%.4f"), AttackTime));
			OutDebugValues.Add(FString::Printf(TEXT("%.4f"), ReleaseTime));
		}

	#endif // !UE_BUILD_SHIPPING

		virtual bool UpdateGenerator(const IGenerator& InGenerator) override
		{
			const FEnvelopeFollowerGenerator& NewGenerator = static_cast<const FEnvelopeFollowerGenerator&>(InGenerator);
			AudioRenderThreadCommand([this, bNewInvert = NewGenerator.bInvert, bNewBypass = NewGenerator.bBypass, NewGain = NewGenerator.Gain, NewInitParams = NewGenerator.InitParams, NewBusId = NewGenerator.BusId]()
			{
				bBypass = bNewBypass;
				bInvert = bNewInvert;

				EnvelopeFollower.SetAnalog(NewInitParams.bIsAnalog);
				EnvelopeFollower.SetAttackTime(NewInitParams.AttackTimeMsec);
				EnvelopeFollower.SetMode(NewInitParams.Mode);
				EnvelopeFollower.SetNumChannels(NewInitParams.NumChannels);
				EnvelopeFollower.SetReleaseTime(NewInitParams.ReleaseTimeMsec);

				if (NewBusId != BusId || !FMath::IsNearlyEqual(Gain, NewGain))
				{
					BusId = NewBusId;
					Gain = NewGain;
					bInitialized = false;
				}
			});

			return true;
		}

		virtual float GetValue() const override
		{
			return CurrentValue;
		}

		void InitBus()
		{
			if (BusId != INDEX_NONE)
			{
				if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
				{
					FAudioDevice* AudioDevice = DeviceManager->GetAudioDeviceRaw(AudioDeviceId);
					if (Audio::FMixerDevice* MixerDevice = static_cast<Audio::FMixerDevice*>(AudioDevice))
					{
						AudioBusPatch = MixerDevice->AddPatchForAudioBus(BusId, Gain);
						bInitialized = true;
					}
				}
			}
		}

		virtual bool IsBypassed() const override
		{
			return bBypass;
		}

		virtual void Update(double InElapsed) override
		{
			if (!bInitialized)
			{
				InitBus();
			}

			if (AudioBusPatch.IsValid() && !AudioBusPatch->IsInputStale())
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

				if (bInvert)
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
				bInitialized = false;
				CurrentValue = 0.f;
			}

		}

	private:
		Audio::FPatchOutputStrongPtr AudioBusPatch;
		Audio::FAlignedFloatBuffer TempBuffer;
		Audio::FEnvelopeFollower EnvelopeFollower;

		Audio::FEnvelopeFollowerInitParams InitParams;

		uint32 BusId = INDEX_NONE;
		float CurrentValue = 0.0f;
		float Gain = 1.0f;
		uint8 bBypass : 1;
		uint8 bInvert : 1;
		uint8 bInitialized : 1;
	};
} // namespace AudioModulation

AudioModulation::FGeneratorPtr USoundModulationGeneratorEnvelopeFollower::CreateInstance(Audio::FDeviceId InDeviceId) const
{
	using namespace AudioModulation;

	auto NewGenerator = MakeShared<FEnvelopeFollowerGenerator, ESPMode::ThreadSafe>(Params, InDeviceId);
	return StaticCastSharedRef<IGenerator>(NewGenerator);
}
