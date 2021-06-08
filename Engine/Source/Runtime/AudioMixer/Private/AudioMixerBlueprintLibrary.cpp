// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerBlueprintLibrary.h"
#include "Engine/World.h"
#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "CoreMinimal.h"
#include "DSP/ConstantQ.h"
#include "DSP/SpectrumAnalyzer.h"
#include "ContentStreaming.h"
#include "AudioCompressionSettingsUtils.h"
#include "Async/Async.h"
#include "Sound/SoundEffectPreset.h"

// This is our global recording task:
static TUniquePtr<Audio::FAudioRecordingData> RecordingData;


static FAudioDevice* GetAudioDeviceFromWorldContext(const UObject* WorldContextObject)
{
	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!ThisWorld || !ThisWorld->bAllowAudioPlayback || ThisWorld->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	return ThisWorld->GetAudioDevice().GetAudioDevice();
}

static Audio::FMixerDevice* GetAudioMixerDeviceFromWorldContext(const UObject* WorldContextObject)
{
	if (FAudioDevice* AudioDevice = GetAudioDeviceFromWorldContext(WorldContextObject))
	{
		if (!AudioDevice->IsAudioMixerEnabled())
		{
			return nullptr;
		}
		else
		{
			return static_cast<Audio::FMixerDevice*>(AudioDevice);
		}
	}
	return nullptr;
}

void UAudioMixerBlueprintLibrary::AddMasterSubmixEffect(const UObject* WorldContextObject, USoundEffectSubmixPreset* SubmixEffectPreset)
{
	if (!SubmixEffectPreset)
	{
		UE_LOG(LogAudioMixer, Warning, TEXT("AddMasterSubmixEffect was passed invalid submix effect preset"));
		return;
	}

	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		FSoundEffectSubmixInitData InitData;
		InitData.SampleRate = MixerDevice->GetSampleRate();
		InitData.DeviceID = MixerDevice->DeviceID;
		InitData.PresetSettings = nullptr;
		InitData.ParentPresetUniqueId = SubmixEffectPreset->GetUniqueID();

		// Immediately create a new sound effect base here before the object becomes potentially invalidated
		TSoundEffectSubmixPtr SoundEffectSubmix = USoundEffectPreset::CreateInstance<FSoundEffectSubmixInitData, FSoundEffectSubmix>(InitData, *SubmixEffectPreset);
		SoundEffectSubmix->SetEnabled(true);

		MixerDevice->AddMasterSubmixEffect(SoundEffectSubmix);
	}
}

void UAudioMixerBlueprintLibrary::RemoveMasterSubmixEffect(const UObject* WorldContextObject, USoundEffectSubmixPreset* SubmixEffectPreset)
{
	if (!SubmixEffectPreset)
	{
		UE_LOG(LogAudioMixer, Warning, TEXT("RemoveMasterSubmixEffect was passed invalid submix effect preset"));
		return;
	}

	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		// Get the unique id for the preset object on the game thread. Used to refer to the object on audio render thread.
		uint32 SubmixPresetUniqueId = SubmixEffectPreset->GetUniqueID();

		MixerDevice->RemoveMasterSubmixEffect(SubmixPresetUniqueId);
	}
}

void UAudioMixerBlueprintLibrary::ClearMasterSubmixEffects(const UObject* WorldContextObject)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		MixerDevice->ClearMasterSubmixEffects();
	}
}

int32 UAudioMixerBlueprintLibrary::AddSubmixEffect(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix, USoundEffectSubmixPreset* SubmixEffectPreset)
{
	if (!SubmixEffectPreset || !InSoundSubmix)
	{
		return 0;
	}

	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		FSoundEffectSubmixInitData InitData;
		InitData.SampleRate = MixerDevice->GetSampleRate();
		InitData.ParentPresetUniqueId = SubmixEffectPreset->GetUniqueID();

		TSoundEffectSubmixPtr SoundEffectSubmix = USoundEffectPreset::CreateInstance<FSoundEffectSubmixInitData, FSoundEffectSubmix>(InitData, *SubmixEffectPreset);
		SoundEffectSubmix->SetEnabled(true);

		return MixerDevice->AddSubmixEffect(InSoundSubmix, SoundEffectSubmix);
	}

	return 0;
}

void UAudioMixerBlueprintLibrary::RemoveSubmixEffectPreset(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix, USoundEffectSubmixPreset* InSubmixEffectPreset)
{
	RemoveSubmixEffect(WorldContextObject, InSoundSubmix, InSubmixEffectPreset);
}

void UAudioMixerBlueprintLibrary::RemoveSubmixEffect(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix, USoundEffectSubmixPreset* InSubmixEffectPreset)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		uint32 SubmixPresetUniqueId = InSubmixEffectPreset->GetUniqueID();
		MixerDevice->RemoveSubmixEffect(InSoundSubmix, SubmixPresetUniqueId);
	}
}

void UAudioMixerBlueprintLibrary::RemoveSubmixEffectPresetAtIndex(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix, int32 SubmixChainIndex)
{
	RemoveSubmixEffectAtIndex(WorldContextObject, InSoundSubmix, SubmixChainIndex);
}

void UAudioMixerBlueprintLibrary::RemoveSubmixEffectAtIndex(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix, int32 SubmixChainIndex)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		MixerDevice->RemoveSubmixEffectAtIndex(InSoundSubmix, SubmixChainIndex);
	}
}

void UAudioMixerBlueprintLibrary::ReplaceSoundEffectSubmix(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix, int32 SubmixChainIndex, USoundEffectSubmixPreset* SubmixEffectPreset)
{
	ReplaceSubmixEffect(WorldContextObject, InSoundSubmix, SubmixChainIndex, SubmixEffectPreset);
}

void UAudioMixerBlueprintLibrary::ReplaceSubmixEffect(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix, int32 SubmixChainIndex, USoundEffectSubmixPreset* SubmixEffectPreset)
{
	if (!SubmixEffectPreset || !InSoundSubmix)
	{
		return;
	}

	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		FSoundEffectSubmixInitData InitData;
		InitData.SampleRate = MixerDevice->GetSampleRate();

		TSoundEffectSubmixPtr SoundEffectSubmix = USoundEffectPreset::CreateInstance<FSoundEffectSubmixInitData, FSoundEffectSubmix>(InitData, *SubmixEffectPreset);
		SoundEffectSubmix->SetEnabled(true);

		MixerDevice->ReplaceSoundEffectSubmix(InSoundSubmix, SubmixChainIndex, SoundEffectSubmix);
	}
}

void UAudioMixerBlueprintLibrary::ClearSubmixEffects(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		MixerDevice->ClearSubmixEffects(InSoundSubmix);
	}
}

void UAudioMixerBlueprintLibrary::SetSubmixEffectChainOverride(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix, TArray<USoundEffectSubmixPreset*> InSubmixEffectPresetChain, float InFadeTimeSec)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		TArray<FSoundEffectSubmixPtr> NewSubmixEffectPresetChain;

		for (USoundEffectSubmixPreset* SubmixEffectPreset : InSubmixEffectPresetChain)
		{
			if (SubmixEffectPreset)
			{
				FSoundEffectSubmixInitData InitData;
				InitData.SampleRate = MixerDevice->GetSampleRate();
				InitData.ParentPresetUniqueId = SubmixEffectPreset->GetUniqueID();

				TSoundEffectSubmixPtr SoundEffectSubmix = USoundEffectPreset::CreateInstance<FSoundEffectSubmixInitData, FSoundEffectSubmix>(InitData, *SubmixEffectPreset);
				SoundEffectSubmix->SetEnabled(true);

				NewSubmixEffectPresetChain.Add(SoundEffectSubmix);
			}
		}
		
		if (NewSubmixEffectPresetChain.Num() > 0)
		{
			MixerDevice->SetSubmixEffectChainOverride(InSoundSubmix, NewSubmixEffectPresetChain, InFadeTimeSec);
		}
	}
}

void UAudioMixerBlueprintLibrary::ClearSubmixEffectChainOverride(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix, float InFadeTimeSec)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		MixerDevice->ClearSubmixEffectChainOverride(InSoundSubmix, InFadeTimeSec);
	}
}

void UAudioMixerBlueprintLibrary::StartRecordingOutput(const UObject* WorldContextObject, float ExpectedDuration, USoundSubmix* SubmixToRecord)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		MixerDevice->StartRecording(SubmixToRecord, ExpectedDuration);
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Output recording is an audio mixer only feature."));
	}
}

USoundWave* UAudioMixerBlueprintLibrary::StopRecordingOutput(const UObject* WorldContextObject, EAudioRecordingExportType ExportType, const FString& Name, FString Path, USoundSubmix* SubmixToRecord, USoundWave* ExistingSoundWaveToOverwrite)
{
	if (RecordingData.IsValid())
	{
		UE_LOG(LogAudioMixer, Warning, TEXT("Abandoning existing write operation. If you'd like to export multiple submix recordings at the same time, use Start/Finish Recording Submix Output instead."));
	}

	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		float SampleRate;
		float ChannelCount;

		// call the thing here.
		Audio::AlignedFloatBuffer& RecordedBuffer = MixerDevice->StopRecording(SubmixToRecord, ChannelCount, SampleRate);

		if (RecordedBuffer.Num() == 0)
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("No audio data. Did you call Start Recording Output?"));
			return nullptr;
		}

		// Pack output data into a TSampleBuffer and record out:
		RecordingData.Reset(new Audio::FAudioRecordingData());
		RecordingData->InputBuffer = Audio::TSampleBuffer<int16>(RecordedBuffer, ChannelCount, SampleRate);

		switch (ExportType)
		{
		case EAudioRecordingExportType::SoundWave:
		{
			USoundWave* ResultingSoundWave = RecordingData->Writer.SynchronouslyWriteSoundWave(RecordingData->InputBuffer, &Name, &Path);
			RecordingData.Reset();
			return ResultingSoundWave;
			break;
		}
		case EAudioRecordingExportType::WavFile:
		{
			RecordingData->Writer.BeginWriteToWavFile(RecordingData->InputBuffer, Name, Path, [SubmixToRecord]()
			{
				if (SubmixToRecord && SubmixToRecord->OnSubmixRecordedFileDone.IsBound())
				{
					SubmixToRecord->OnSubmixRecordedFileDone.Broadcast(nullptr);
				}

				// I'm gonna try this, but I do not feel great about it.
				RecordingData.Reset();
			});
			break;
		}
		default:
			break;
		}	
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Output recording is an audio mixer only feature."));
	}

	return nullptr;
}

void UAudioMixerBlueprintLibrary::PauseRecordingOutput(const UObject* WorldContextObject, USoundSubmix* SubmixToPause)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		MixerDevice->PauseRecording(SubmixToPause);
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Output recording is an audio mixer only feature."));
	}
}

void UAudioMixerBlueprintLibrary::ResumeRecordingOutput(const UObject* WorldContextObject, USoundSubmix* SubmixToResume)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		MixerDevice->ResumeRecording(SubmixToResume);
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Output recording is an audio mixer only feature."));
	}
}

void UAudioMixerBlueprintLibrary::StartAnalyzingOutput(const UObject* WorldContextObject, USoundSubmix* SubmixToAnalyze, EFFTSize FFTSize, EFFTPeakInterpolationMethod InterpolationMethod, EFFTWindowType WindowType, float HopSize, EAudioSpectrumType AudioSpectrumType)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		FSoundSpectrumAnalyzerSettings Settings = USoundSubmix::GetSpectrumAnalyzerSettings(FFTSize, InterpolationMethod, WindowType, HopSize, AudioSpectrumType);
		MixerDevice->StartSpectrumAnalysis(SubmixToAnalyze, Settings);
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Spectrum Analysis is an audio mixer only feature."));
	}
}

void UAudioMixerBlueprintLibrary::StopAnalyzingOutput(const UObject* WorldContextObject, USoundSubmix* SubmixToStopAnalyzing)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		MixerDevice->StopSpectrumAnalysis(SubmixToStopAnalyzing);
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Spectrum Analysis is an audio mixer only feature."));
	}
}

TArray<FSoundSubmixSpectralAnalysisBandSettings> UAudioMixerBlueprintLibrary::MakeMusicalSpectralAnalysisBandSettings(int32 InNumNotes, EMusicalNoteName InStartingMusicalNote, int32 InStartingOctave, int32 InAttackTimeMsec, int32 InReleaseTimeMsec)
{
	// Make values sane.
	InNumNotes = FMath::Clamp(InNumNotes, 0, 10000);
	InStartingOctave = FMath::Clamp(InStartingOctave, -1, 10);
	InAttackTimeMsec = FMath::Clamp(InAttackTimeMsec, 0, 10000);
	InReleaseTimeMsec = FMath::Clamp(InReleaseTimeMsec, 0, 10000);

	// Some assumptions here on what constitutes "music". 12 notes, equal temperament.
	const float BandsPerOctave = 12.f;
	// This QFactor makes the bandwidth equal to the difference in frequency between adjacent notes.
	const float QFactor = 1.f / (FMath::Pow(2.f, 1.f / BandsPerOctave) - 1.f);

	// Base note index off of A4 which we know to be 440 hz.
	// Make note relative to A
	int32 NoteIndex = static_cast<int32>(InStartingMusicalNote) - static_cast<int32>(EMusicalNoteName::A);
	// Make relative to 4th octave of A
	NoteIndex += 12 * (InStartingOctave - 4);

	const float StartingFrequency = 440.f * FMath::Pow(2.f, static_cast<float>(NoteIndex) / 12.f);


	TArray<FSoundSubmixSpectralAnalysisBandSettings> BandSettingsArray;
	for (int32 i = 0; i < InNumNotes; i++)
	{
		FSoundSubmixSpectralAnalysisBandSettings BandSettings;

		BandSettings.BandFrequency = Audio::FPseudoConstantQ::GetConstantQCenterFrequency(i, StartingFrequency, BandsPerOctave);

		BandSettings.QFactor = QFactor;
		BandSettings.AttackTimeMsec = InAttackTimeMsec;
		BandSettings.ReleaseTimeMsec = InReleaseTimeMsec;

		BandSettingsArray.Add(BandSettings);
	}

	return BandSettingsArray;
}

TArray<FSoundSubmixSpectralAnalysisBandSettings> UAudioMixerBlueprintLibrary::MakeFullSpectrumSpectralAnalysisBandSettings(int32 InNumBands, float InMinimumFrequency, float InMaximumFrequency, int32 InAttackTimeMsec, int32 InReleaseTimeMsec)
{
	// Make inputs sane.
	InNumBands = FMath::Clamp(InNumBands, 0, 10000);
	InMinimumFrequency = FMath::Clamp(InMinimumFrequency, 20.0f, 20000.0f);
	InMaximumFrequency = FMath::Clamp(InMaximumFrequency, InMinimumFrequency, 20000.0f);
	InAttackTimeMsec = FMath::Clamp(InAttackTimeMsec, 0, 10000);
	InReleaseTimeMsec = FMath::Clamp(InReleaseTimeMsec, 0, 10000);

	// Calculate CQT settings needed to space bands.
	const float NumOctaves = FMath::Loge(InMaximumFrequency / InMinimumFrequency) / FMath::Loge(2.f);
	const float BandsPerOctave = static_cast<float>(InNumBands) / FMath::Max(NumOctaves, 0.01f);
	const float QFactor = 1.f / (FMath::Pow(2.f, 1.f / FMath::Max(BandsPerOctave, 0.01f)) - 1.f);

	TArray<FSoundSubmixSpectralAnalysisBandSettings> BandSettingsArray;
	for (int32 i = 0; i < InNumBands; i++)
	{
		FSoundSubmixSpectralAnalysisBandSettings BandSettings;

		BandSettings.BandFrequency = Audio::FPseudoConstantQ::GetConstantQCenterFrequency(i, InMinimumFrequency, BandsPerOctave);

		BandSettings.QFactor = QFactor;
		BandSettings.AttackTimeMsec = InAttackTimeMsec;
		BandSettings.ReleaseTimeMsec = InReleaseTimeMsec;

		BandSettingsArray.Add(BandSettings);
	}

	return BandSettingsArray;
}

TArray<FSoundSubmixSpectralAnalysisBandSettings> UAudioMixerBlueprintLibrary::MakePresetSpectralAnalysisBandSettings(EAudioSpectrumBandPresetType InBandPresetType, int32 InNumBands, int32 InAttackTimeMsec, int32 InReleaseTimeMsec)
{
	float MinimumFrequency = 20.f;
	float MaximumFrequency = 20000.f;

	// Likely all these are debatable. What we are shooting for is the most active frequency
	// ranges, so when an instrument plays a significant amount of spectral energy from that
	// instrument will show up in the frequency range. 
	switch (InBandPresetType)
	{
		case EAudioSpectrumBandPresetType::KickDrum:
			MinimumFrequency = 40.f;
			MaximumFrequency = 100.f;
			break;

		case EAudioSpectrumBandPresetType::SnareDrum:
			MinimumFrequency = 150.f;
			MaximumFrequency = 4500.f;
			break;

		case EAudioSpectrumBandPresetType::Voice:
			MinimumFrequency = 300.f;
			MaximumFrequency = 3000.f;
			break;

		case EAudioSpectrumBandPresetType::Cymbals:
			MinimumFrequency = 6000.f;
			MaximumFrequency = 16000.f;
			break;

		// More presets can be added. The possibilities are endless.
	}

	return MakeFullSpectrumSpectralAnalysisBandSettings(InNumBands, MinimumFrequency, MaximumFrequency, InAttackTimeMsec, InReleaseTimeMsec);
}

void UAudioMixerBlueprintLibrary::GetMagnitudeForFrequencies(const UObject* WorldContextObject, const TArray<float>& Frequencies, TArray<float>& Magnitudes, USoundSubmix* SubmixToAnalyze /*= nullptr*/)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		MixerDevice->GetMagnitudesForFrequencies(SubmixToAnalyze, Frequencies, Magnitudes);
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Getting magnitude for frequencies is an audio mixer only feature."));
	}
}

void UAudioMixerBlueprintLibrary::GetPhaseForFrequencies(const UObject* WorldContextObject, const TArray<float>& Frequencies, TArray<float>& Phases, USoundSubmix* SubmixToAnalyze /*= nullptr*/)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		MixerDevice->GetPhasesForFrequencies(SubmixToAnalyze, Frequencies, Phases);
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Output recording is an audio mixer only feature."));
	}
}

void UAudioMixerBlueprintLibrary::AddSourceEffectToPresetChain(const UObject* WorldContextObject, USoundEffectSourcePresetChain* PresetChain, FSourceEffectChainEntry Entry)
{
	if (!PresetChain)
	{
		UE_LOG(LogAudioMixer, Warning, TEXT("AddSourceEffectToPresetChain was passed invalid preset chain"));
		return;
	}

	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		TArray<FSourceEffectChainEntry> Chain;

		uint32 PresetChainId = PresetChain->GetUniqueID();

		if (!MixerDevice->GetCurrentSourceEffectChain(PresetChainId, Chain))
		{
			Chain = PresetChain->Chain;
		}

		Chain.Add(Entry);
		MixerDevice->UpdateSourceEffectChain(PresetChainId, Chain, PresetChain->bPlayEffectChainTails);
	}
}

void UAudioMixerBlueprintLibrary::RemoveSourceEffectFromPresetChain(const UObject* WorldContextObject, USoundEffectSourcePresetChain* PresetChain, int32 EntryIndex)
{
	if (!PresetChain)
	{
		UE_LOG(LogAudioMixer, Warning, TEXT("RemoveSourceEffectFromPresetChain was passed invalid preset chain"));
		return;
	}

	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		TArray<FSourceEffectChainEntry> Chain;

		uint32 PresetChainId = PresetChain->GetUniqueID();

		if (!MixerDevice->GetCurrentSourceEffectChain(PresetChainId, Chain))
		{
			Chain = PresetChain->Chain;
		}

		if (EntryIndex < Chain.Num())
		{
			Chain.RemoveAt(EntryIndex);
		}

		MixerDevice->UpdateSourceEffectChain(PresetChainId, Chain, PresetChain->bPlayEffectChainTails);
	}

}

void UAudioMixerBlueprintLibrary::SetBypassSourceEffectChainEntry(const UObject* WorldContextObject, USoundEffectSourcePresetChain* PresetChain, int32 EntryIndex, bool bBypassed)
{
	if (!PresetChain)
	{
		UE_LOG(LogAudioMixer, Warning, TEXT("SetBypassSourceEffectChainEntry was passed invalid preset chain"));
		return;
	}

	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		TArray<FSourceEffectChainEntry> Chain;

		uint32 PresetChainId = PresetChain->GetUniqueID();

		if (!MixerDevice->GetCurrentSourceEffectChain(PresetChainId, Chain))
		{
			Chain = PresetChain->Chain;
		}

		if (EntryIndex < Chain.Num())
		{
			Chain[EntryIndex].bBypass = bBypassed;
		}

		MixerDevice->UpdateSourceEffectChain(PresetChainId, Chain, PresetChain->bPlayEffectChainTails);
	}
}

int32 UAudioMixerBlueprintLibrary::GetNumberOfEntriesInSourceEffectChain(const UObject* WorldContextObject, USoundEffectSourcePresetChain* PresetChain)
{
	if (!PresetChain)
	{
		UE_LOG(LogAudioMixer, Warning, TEXT("GetNumberOfEntriesInSourceEffectChain was passed invalid preset chain"));
		return 0;
	}

	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		TArray<FSourceEffectChainEntry> Chain;

		uint32 PresetChainId = PresetChain->GetUniqueID();

		if (!MixerDevice->GetCurrentSourceEffectChain(PresetChainId, Chain))
		{
			return PresetChain->Chain.Num();
		}

		return Chain.Num();
	}

	return 0;
}

void UAudioMixerBlueprintLibrary::PrimeSoundForPlayback(USoundWave* SoundWave, const FOnSoundLoadComplete OnLoadCompletion)
{
	if (!SoundWave)
	{
		UE_LOG(LogAudioMixer, Warning, TEXT("Prime Sound For Playback called with a null SoundWave pointer."));
	}
	else if (!FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching())
	{
		UE_LOG(LogAudioMixer, Warning, TEXT("Prime Sound For Playback doesn't do anything unless Audio Load On Demand is enabled."));
		
		OnLoadCompletion.ExecuteIfBound(SoundWave, false);
	}
	else
	{
		IStreamingManager::Get().GetAudioStreamingManager().RequestChunk(SoundWave, 1, [OnLoadCompletion, SoundWave](EAudioChunkLoadResult InResult) 
		{
			AsyncTask(ENamedThreads::GameThread, [OnLoadCompletion, SoundWave, InResult]() {
				if (InResult == EAudioChunkLoadResult::Completed || InResult == EAudioChunkLoadResult::AlreadyLoaded)
				{
					OnLoadCompletion.ExecuteIfBound(SoundWave, false);
				}
				else
				{
					OnLoadCompletion.ExecuteIfBound(SoundWave, true);
				}
			});
		});
	}
}

void UAudioMixerBlueprintLibrary::PrimeSoundCueForPlayback(USoundCue* SoundCue)
{
	if (SoundCue)
	{
		SoundCue->PrimeSoundCue();
	}
}

float UAudioMixerBlueprintLibrary::TrimAudioCache(float InMegabytesToFree)
{
	uint64 NumBytesToFree = (uint64) (((double)InMegabytesToFree) * 1024.0 * 1024.0);
	uint64 NumBytesFreed = IStreamingManager::Get().GetAudioStreamingManager().TrimMemory(NumBytesToFree);
	return (float)(((double) NumBytesFreed / 1024) / 1024.0);
}

void UAudioMixerBlueprintLibrary::StartAudioBus(const UObject* WorldContextObject, UAudioBus* AudioBus)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		uint32 AudioBusId = AudioBus->GetUniqueID();
		int32 NumChannels = (int32)AudioBus->AudioBusChannels + 1;
		MixerDevice->StartAudioBus(AudioBusId, NumChannels, false);
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Audio buses are an audio mixer only feature. Please run the game with audio mixer enabled for this feature."));
	}
}

void UAudioMixerBlueprintLibrary::StopAudioBus(const UObject* WorldContextObject, UAudioBus* AudioBus)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		uint32 AudioBusId = AudioBus->GetUniqueID();
		MixerDevice->StopAudioBus(AudioBusId);
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Audio buses are an audio mixer only feature. Please run the game with audio mixer enabled for this feature."));
	}
}

bool UAudioMixerBlueprintLibrary::IsAudioBusActive(const UObject* WorldContextObject, UAudioBus* AudioBus)
{
	if (Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		uint32 AudioBusId = AudioBus->GetUniqueID();
		return MixerDevice->IsAudioBusActive(AudioBusId);
	}
	else
	{
		UE_LOG(LogAudioMixer, Error, TEXT("Audio buses are an audio mixer only feature. Please run the game with audio mixer enabled for this feature."));
		return false;
	}
}

