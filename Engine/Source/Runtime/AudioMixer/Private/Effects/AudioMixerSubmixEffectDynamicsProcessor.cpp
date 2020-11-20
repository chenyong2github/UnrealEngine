// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"

#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "AudioMixerSubmix.h"
#include "ProfilingDebugging/CsvProfiler.h"

// Link to "Audio" profiling category
CSV_DECLARE_CATEGORY_MODULE_EXTERN(AUDIOMIXERCORE_API, Audio);

DEFINE_STAT(STAT_AudioMixerSubmixDynamics);

static int32 bBypassSubmixDynamicsProcessor = 0;
FAutoConsoleVariableRef CVarBypassDynamicsProcessor(
	TEXT("au.Submix.Effects.DynamicsProcessor.Bypass"),
	bBypassSubmixDynamicsProcessor,
	TEXT("If non-zero, bypasses all submix dynamics processors currently active.\n"),
	ECVF_Default);

FSubmixEffectDynamicsProcessor::FSubmixEffectDynamicsProcessor()
{
}

FSubmixEffectDynamicsProcessor::~FSubmixEffectDynamicsProcessor()
{
	FAudioDeviceManagerDelegates::OnAudioDeviceCreated.Remove(DeviceCreatedHandle);
	ResetKey();
}

Audio::FDeviceId FSubmixEffectDynamicsProcessor::GetDeviceId() const
{
	return DeviceId;
}

void FSubmixEffectDynamicsProcessor::Init(const FSoundEffectSubmixInitData& InitData)
{
	static const int32 ProcessorScratchNumChannels = 8;

	DynamicsProcessor.Init(InitData.SampleRate, ProcessorScratchNumChannels);

	AudioKeyFrame.Reset();
	AudioKeyFrame.AddZeroed(ProcessorScratchNumChannels);

	AudioInputFrame.Reset();
	AudioInputFrame.AddZeroed(ProcessorScratchNumChannels);

	AudioOutputFrame.Reset();
	AudioOutputFrame.AddZeroed(ProcessorScratchNumChannels);

	DeviceId = InitData.DeviceID;

	if (USubmixEffectDynamicsProcessorPreset* ProcPreset = Cast<USubmixEffectDynamicsProcessorPreset>(Preset.Get()))
	{
		switch (ProcPreset->Settings.KeySource)
		{
			case ESubmixEffectDynamicsKeySource::AudioBus:
			{
				if (UAudioBus* AudioBus = ProcPreset->Settings.ExternalAudioBus)
				{
					KeySource.Update(ESubmixEffectDynamicsKeySource::AudioBus, AudioBus->GetUniqueID(), static_cast<int32>(AudioBus->AudioBusChannels) + 1);
				}
			}
			break;

			case ESubmixEffectDynamicsKeySource::Submix:
			{
				if (USoundSubmix* Submix = ProcPreset->Settings.ExternalSubmix)
				{
					KeySource.Update(ESubmixEffectDynamicsKeySource::Submix, Submix->GetUniqueID());
				}
			}
			break;

			default:
			{
				// KeySource is this effect's submix/input, so do nothing
			}
			break;
		}
	}
}

void FSubmixEffectDynamicsProcessor::ResetKey()
{
	KeySource.Reset();
}

void FSubmixEffectDynamicsProcessor::OnPresetChanged()
{
	GET_EFFECT_SETTINGS(SubmixEffectDynamicsProcessor);

	bBypass = Settings.bBypass;

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

	UpdateKeyFromSettings(Settings);
}

Audio::FMixerDevice* FSubmixEffectDynamicsProcessor::GetMixerDevice()
{
	if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
	{
		return static_cast<Audio::FMixerDevice*>(DeviceManager->GetAudioDeviceRaw(DeviceId));
	}

	return nullptr;
}

bool FSubmixEffectDynamicsProcessor::UpdateKeySourcePatch()
{
	if (KeySource.Patch.IsValid())
	{
		return true;
	}

	if (Audio::FMixerDevice* MixerDevice = GetMixerDevice())
	{
		switch (KeySource.Type)
		{
			case ESubmixEffectDynamicsKeySource::AudioBus:
			{
				KeySource.Patch = MixerDevice->AddPatchForAudioBus(KeySource.ObjectId, 1.0f /* PatchGain */);
				if (KeySource.Patch.IsValid())
				{
					DynamicsProcessor.SetKeyNumChannels(KeySource.GetNumChannels());
					return true;
				}
			}
			break;

			case ESubmixEffectDynamicsKeySource::Submix:
			{
				KeySource.Patch = MixerDevice->AddPatchForSubmix(KeySource.ObjectId, 1.0f /* PatchGain */);
				if (KeySource.Patch.IsValid())
				{
					Audio::FMixerSubmixPtr SubmixPtr = MixerDevice->FindSubmixInstanceByObjectId(KeySource.ObjectId);
					if (SubmixPtr.IsValid())
					{
						const int32 SubmixNumChannels = SubmixPtr->GetNumOutputChannels();
						KeySource.SetNumChannels(SubmixNumChannels);
						DynamicsProcessor.SetKeyNumChannels(SubmixNumChannels);
						return true;
					}
				}
			}
			break;

			case ESubmixEffectDynamicsKeySource::Default:
			default:
			{
			}
			break;
		}
	}

	return false;
}

void FSubmixEffectDynamicsProcessor::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
	CSV_SCOPED_TIMING_STAT(Audio, SubmixDynamics);
	SCOPE_CYCLE_COUNTER(STAT_AudioMixerSubmixDynamics);

	ensure(InData.NumChannels == OutData.NumChannels);

	const Audio::AlignedFloatBuffer& InBuffer = *InData.AudioBuffer;
	Audio::AlignedFloatBuffer& OutBuffer = *OutData.AudioBuffer;

	const bool bBypassDueToInvalidChannelCount = !ensure(InData.NumChannels <= AudioInputFrame.Num());
	if (bBypassDueToInvalidChannelCount || bBypassSubmixDynamicsProcessor || bBypass)
	{
		FMemory::Memcpy(OutBuffer.GetData(), InBuffer.GetData(), sizeof(float) * InBuffer.Num());
		return;
	}

	int32 NumKeyChannels = DynamicsProcessor.GetKeyNumChannels();
	AudioExternal.Reset();
	if (KeySource.ObjectId != INDEX_NONE)
	{
		if (UpdateKeySourcePatch())
		{
			// Refresh num channels in case it changed when updating patch
			NumKeyChannels = DynamicsProcessor.GetKeyNumChannels();

			const int32 NumSamples = InData.NumFrames * NumKeyChannels;

			AudioExternal.AddZeroed(NumSamples);
			KeySource.Patch->PopAudio(AudioExternal.GetData(), NumSamples, true /* bUseLatestAudio */);
		}
	}

	const int32 NumChannels = DynamicsProcessor.GetNumChannels();
	if (InData.NumChannels != NumChannels)
	{
		DynamicsProcessor.SetNumChannels(InData.NumChannels);
	}

	for (int32 Frame = 0; Frame < InData.NumFrames; ++Frame)
	{
		// Copy the data to the frame input
		const int32 SampleIndexOfInputFrame = Frame * InData.NumChannels;
		for (int32 Channel = 0; Channel < InData.NumChannels; ++Channel)
		{
			const int32 SampleIndex = SampleIndexOfInputFrame + Channel;
			AudioInputFrame[Channel] = InBuffer[SampleIndex];
		}

		// Copy buffer data to key if key is external source
		if (AudioExternal.Num() > 0)
		{
			// Copy the data to the frame input
			const int32 SampleIndexOfKeyFrame = Frame * NumKeyChannels;
			for (int32 Channel = 0; Channel < NumKeyChannels; ++Channel)
			{
				const int32 SampleIndex = SampleIndexOfKeyFrame + Channel;
				AudioKeyFrame[Channel] = AudioExternal[SampleIndex];
			}

			DynamicsProcessor.ProcessAudio(AudioInputFrame.GetData(), InData.NumChannels, AudioOutputFrame.GetData(), AudioKeyFrame.GetData());
		}
		else
		{
			DynamicsProcessor.ProcessAudio(AudioInputFrame.GetData(), InData.NumChannels, AudioOutputFrame.GetData());
		}

		// Copy the data to the frame output
		for (int32 Channel = 0; Channel < InData.NumChannels; ++Channel)
		{
			const int32 SampleIndex = SampleIndexOfInputFrame + Channel;
			OutBuffer[SampleIndex] = AudioOutputFrame[Channel];
		}
	}

	AudioExternal.Reset();
}

void FSubmixEffectDynamicsProcessor::UpdateKeyFromSettings(const FSubmixEffectDynamicsProcessorSettings& InSettings)
{
	uint32 ObjectId = INDEX_NONE;
	int32 SourceNumChannels = 0;
	switch (InSettings.KeySource)
	{
		case ESubmixEffectDynamicsKeySource::AudioBus:
		{
			if (InSettings.ExternalAudioBus)
			{
				ObjectId = InSettings.ExternalAudioBus->GetUniqueID();
				SourceNumChannels = static_cast<int32>(InSettings.ExternalAudioBus->AudioBusChannels) + 1;
			}
		}
		break;

		case ESubmixEffectDynamicsKeySource::Submix:
		{
			if (InSettings.ExternalSubmix)
			{
				ObjectId = InSettings.ExternalSubmix->GetUniqueID();
			}
		}
		break;

		default:
		{
		}
		break;
	}

	KeySource.Update(InSettings.KeySource, ObjectId, SourceNumChannels);
}

void FSubmixEffectDynamicsProcessor::OnNewDeviceCreated(Audio::FDeviceId InDeviceId)
{
	if (InDeviceId == DeviceId)
	{
		GET_EFFECT_SETTINGS(SubmixEffectDynamicsProcessor);
		UpdateKeyFromSettings(Settings);

		FAudioDeviceManagerDelegates::OnAudioDeviceCreated.Remove(DeviceCreatedHandle);
	}
}

void USubmixEffectDynamicsProcessorPreset::OnInit()
{
	switch (Settings.KeySource)
	{
		case ESubmixEffectDynamicsKeySource::AudioBus:
		{
			SetAudioBus(Settings.ExternalAudioBus);
		}
		break;

		case ESubmixEffectDynamicsKeySource::Submix:
		{
			SetExternalSubmix(Settings.ExternalSubmix);
		}
		break;

		default:
		{
		}
		break;
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

void USubmixEffectDynamicsProcessorPreset::ResetKey()
{
	EffectCommand<FSubmixEffectDynamicsProcessor>([](FSubmixEffectDynamicsProcessor& Instance)
	{
		Instance.ResetKey();
	});
}

void USubmixEffectDynamicsProcessorPreset::SetAudioBus(UAudioBus* InAudioBus)
{
	const int32 BusChannels = static_cast<int32>(InAudioBus->AudioBusChannels) + 1;
	SetKey(ESubmixEffectDynamicsKeySource::AudioBus, InAudioBus, BusChannels);
}

void USubmixEffectDynamicsProcessorPreset::SetExternalSubmix(USoundSubmix* InSubmix)
{
	SetKey(ESubmixEffectDynamicsKeySource::Submix, InSubmix);
}

void USubmixEffectDynamicsProcessorPreset::SetKey(ESubmixEffectDynamicsKeySource InKeySource, UObject* InObject, int32 InNumChannels)
{
	if (InObject)
	{
		EffectCommand<FSubmixEffectDynamicsProcessor>([this, ObjectId = InObject->GetUniqueID(), InKeySource, InNumChannels](FSubmixEffectDynamicsProcessor& Instance)
		{
			Instance.KeySource.Update(InKeySource, ObjectId, InNumChannels);
		});
	}
}

void USubmixEffectDynamicsProcessorPreset::SetSettings(const FSubmixEffectDynamicsProcessorSettings& InSettings)
{
	UpdateSettings(InSettings);

	IterateEffects<FSubmixEffectDynamicsProcessor>([&](FSubmixEffectDynamicsProcessor& Instance)
	{
		Instance.UpdateKeyFromSettings(InSettings);
	});
}
