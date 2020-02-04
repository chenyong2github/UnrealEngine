// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"

#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "ProfilingDebugging/CsvProfiler.h"

// Link to "Audio" profiling category
CSV_DECLARE_CATEGORY_MODULE_EXTERN(AUDIOMIXERCORE_API, Audio);

namespace
{
	// Use a full multichannel frame as a scratch processing buffer
	static const int32 ProcessorScratchNumChannels = 8;
} // namespace <>

FSubmixEffectDynamicsProcessor::FSubmixEffectDynamicsProcessor()
	: DeviceId(INDEX_NONE)
{
}

FSubmixEffectDynamicsProcessor::~FSubmixEffectDynamicsProcessor()
{
	if (ExternalSubmix.IsValid())
	{
		Audio::FMixerDevice* MixerDevice = nullptr;
		if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
		{
			MixerDevice = static_cast<Audio::FMixerDevice*>(DeviceManager->GetAudioDeviceRaw(DeviceId));
		}

		if (MixerDevice)
		{
			MixerDevice->UnregisterSubmixBufferListener(this, ExternalSubmix.Get());
		}
	}
}

void FSubmixEffectDynamicsProcessor::Init(const FSoundEffectSubmixInitData& InitData)
{
	DynamicsProcessor.Init(InitData.SampleRate, ProcessorScratchNumChannels);

	AudioKeyFrame.Reset();
	AudioKeyFrame.AddZeroed(ProcessorScratchNumChannels);

	AudioInputFrame.Reset();
	AudioInputFrame.AddZeroed(ProcessorScratchNumChannels);

	AudioOutputFrame.Reset();
	AudioOutputFrame.AddZeroed(ProcessorScratchNumChannels);

	DeviceId = InitData.DeviceID;
}

void FSubmixEffectDynamicsProcessor::OnNewSubmixBuffer(
	const USoundSubmix* InOwningSubmix,
	float*				InAudioData,
	int32				InNumSamples,
	int32				InNumChannels,
	const int32			InSampleRate,
	double				InAudioClock)
{
	AudioExternal.Reset();
	AudioExternal.Append(InAudioData, InNumSamples);
}

void FSubmixEffectDynamicsProcessor::OnPresetChanged()
{
	GET_EFFECT_SETTINGS(SubmixEffectDynamicsProcessor);

	switch (Settings.DynamicsProcessorType)
	{
	default:
	case ESubmixEffectDynamicsProcessorType::Compressor:
		DynamicsProcessor.SetProcessingMode(Audio::EDynamicsProcessingMode::Compressor);
		break;

	case ESubmixEffectDynamicsProcessorType::Limiter:
		DynamicsProcessor.SetProcessingMode(Audio::EDynamicsProcessingMode::Limiter);
		break;

	case ESubmixEffectDynamicsProcessorType::Expander:
		DynamicsProcessor.SetProcessingMode(Audio::EDynamicsProcessingMode::Expander);
		break;

	case ESubmixEffectDynamicsProcessorType::Gate:
		DynamicsProcessor.SetProcessingMode(Audio::EDynamicsProcessingMode::Gate);
		break;
	}

	switch (Settings.PeakMode)
	{
	default:
	case ESubmixEffectDynamicsPeakMode::MeanSquared:
		DynamicsProcessor.SetPeakMode(Audio::EPeakMode::MeanSquared);
		break;

	case ESubmixEffectDynamicsPeakMode::RootMeanSquared:
		DynamicsProcessor.SetPeakMode(Audio::EPeakMode::RootMeanSquared);
		break;

	case ESubmixEffectDynamicsPeakMode::Peak:
		DynamicsProcessor.SetPeakMode(Audio::EPeakMode::Peak);
		break;
	}

	DynamicsProcessor.SetLookaheadMsec(Settings.LookAheadMsec);
	DynamicsProcessor.SetAttackTime(Settings.AttackTimeMsec);
	DynamicsProcessor.SetReleaseTime(Settings.ReleaseTimeMsec);
	DynamicsProcessor.SetThreshold(Settings.ThresholdDb);
	DynamicsProcessor.SetRatio(Settings.Ratio);
	DynamicsProcessor.SetKneeBandwidth(Settings.KneeBandwidthDb);
	DynamicsProcessor.SetInputGain(Settings.InputGainDb);
	DynamicsProcessor.SetOutputGain(Settings.OutputGainDb);
	DynamicsProcessor.SetAnalogMode(Settings.bAnalogMode);

	DynamicsProcessor.SetKeyAudition(Settings.bKeyAudition);
	DynamicsProcessor.SetKeyGain(Settings.KeyGainDb);
	DynamicsProcessor.SetKeyHighshelfCutoffFrequency(Settings.KeyHighshelf.Cutoff);
	DynamicsProcessor.SetKeyHighshelfEnabled(Settings.KeyHighshelf.bEnabled);
	DynamicsProcessor.SetKeyHighshelfGain(Settings.KeyHighshelf.GainDb);
	DynamicsProcessor.SetKeyLowshelfCutoffFrequency(Settings.KeyLowshelf.Cutoff);
	DynamicsProcessor.SetKeyLowshelfEnabled(Settings.KeyLowshelf.bEnabled);
	DynamicsProcessor.SetKeyLowshelfGain(Settings.KeyLowshelf.GainDb);

	static_assert(static_cast<int32>(ESubmixEffectDynamicsChannelLinkMode::Count) == static_cast<int32>(Audio::EDynamicsProcessorChannelLinkMode::Count), "Enumerations must match");
	DynamicsProcessor.SetChannelLinkMode(static_cast<Audio::EDynamicsProcessorChannelLinkMode>(Settings.LinkMode));

	Audio::FMixerDevice* MixerDevice = nullptr;
	if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
	{
		MixerDevice = static_cast<Audio::FMixerDevice*>(DeviceManager->GetAudioDeviceRaw(DeviceId));
	}

	if (MixerDevice)
	{
		if (ExternalSubmix.Get() != Settings.ExternalSubmix)
		{
			if (ExternalSubmix.IsValid())
			{
				MixerDevice->UnregisterSubmixBufferListener(this, ExternalSubmix.Get());
			}

			ExternalSubmix = Settings.ExternalSubmix;

			if (ExternalSubmix.IsValid())
			{
				MixerDevice->RegisterSubmixBufferListener(this, ExternalSubmix.Get());
			}
		}
	}
}

void FSubmixEffectDynamicsProcessor::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
	CSV_SCOPED_TIMING_STAT(Audio, SubmixDynamics);

	const Audio::AlignedFloatBuffer& InBuffer = *InData.AudioBuffer;
	Audio::AlignedFloatBuffer& OutBuffer = *OutData.AudioBuffer;

	for (int32 Frame = 0; Frame < InData.NumFrames; ++Frame)
	{
		// Copy the data to the frame input
		const int32 SampleIndexOfFrame = Frame * InData.NumChannels;
		for (int32 Channel = 0; Channel < InData.NumChannels; ++Channel)
		{
			const int32 SampleIndex = SampleIndexOfFrame + Channel;
			AudioInputFrame[Channel] = InBuffer[SampleIndex];
			if (ExternalSubmix.IsValid())
			{
				AudioKeyFrame[Channel] = AudioExternal.Num() > SampleIndex ? AudioExternal[SampleIndex] : 0;
			}
			else
			{
				AudioKeyFrame[Channel] = InBuffer[SampleIndex];
			}
		}

		// Process
		DynamicsProcessor.ProcessAudio(AudioInputFrame.GetData(), InData.NumChannels, AudioOutputFrame.GetData(), AudioKeyFrame.GetData());

		// Copy the data to the frame output
		for (int32 Channel = 0; Channel < InData.NumChannels; ++Channel)
		{
			const int32 SampleIndex = SampleIndexOfFrame + Channel;
			OutBuffer[SampleIndex] = AudioOutputFrame[Channel];
		}
	}
}

void USubmixEffectDynamicsProcessorPreset::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& UnderlyingArchive = Record.GetUnderlyingArchive();
	if (UnderlyingArchive.IsLoading())
	{
		if (Settings.bChannelLinked_DEPRECATED)
		{
			Settings.LinkMode = ESubmixEffectDynamicsChannelLinkMode::Average;
			Settings.bChannelLinked_DEPRECATED = 0;
		}
	}

	Super::Serialize(Record);
}

void USubmixEffectDynamicsProcessorPreset::SetSettings(const FSubmixEffectDynamicsProcessorSettings& InSettings)
{
	UpdateSettings(InSettings);
}
