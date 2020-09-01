// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeSynthComponent.h"
#include "AudioThread.h"
#include "TimeSynthModule.h"
#include "Containers/UnrealString.h"

static const float MinAudibleLog = -55;

static int32 DisableTimeSynthCvar = 0;
FAutoConsoleVariableRef CVarDisableTimeSynth(
	TEXT("au.DisableTimeSynth"),
	DisableTimeSynthCvar,
	TEXT("Disables all TimeSynth rendering/processing.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 TimeSynthForceSyncDecodesCvar = 1;
FAutoConsoleVariableRef CVarTimeSynthForceSyncDecodes(
	TEXT("au.TimeSynthForceSyncDecodes"),
	TimeSynthForceSyncDecodesCvar,
	TEXT("Forces decodes of TimeSynth audio to be synchronous.\n")
	TEXT("0: Async decodes, 1: Sync decodes"),
	ECVF_Default);

static int32 TimeSynthCallbackSizeOverrideCvar = -1;
FAutoConsoleVariableRef TimeSynthCallbackSizeOverride(
	TEXT("au.TimeSynthCallbackSizeOverride"),
	TimeSynthCallbackSizeOverrideCvar,
	TEXT("Overrides the default callback size of synth components for Time Synths.\n")
	TEXT(" <= default: Don't override, > default: callback size to use, (must be a multiple of 4)"),
	ECVF_Default);

static int32 TimeSynthActiveClipLimitCvar = 20;
FAutoConsoleVariableRef TimeSynthActiveClipLimit(
	TEXT("au.TimeSynthActiveClipLimit"),
	TimeSynthActiveClipLimitCvar,
	TEXT("Overrides the default callback size of synth components for Time Synths.\n")
	TEXT(" <= default: Don't override, > default: callback size to use, (must be a multiple of 4)"),
	ECVF_Default);
 
static_assert((int32)Audio::EEventQuantization::Count == (int32)ETimeSynthEventQuantization::Count, "These enumerations need to match");

void FTimeSynthEventListener::OnEvent(Audio::EEventQuantization EventQuantizationType, int32 Bars, float Beat)
{
	check(TimeSynth);
	TimeSynth->OnQuantizationEvent(EventQuantizationType, Bars, Beat);
}

UTimeSynthComponent::UTimeSynthComponent(const FObjectInitializer& ObjectInitializer) 
	: Super(ObjectInitializer)
	, MaxPoolSize(TimeSynthActiveClipLimitCvar)
	, TimeSynthEventListener(this)
	, bHasActiveClips(false)
	, bTimeSynthWasDisabled(false)
{
	PrimaryComponentTick.bCanEverTick = true;

	if (TimeSynthCallbackSizeOverrideCvar > 0)
	{
		PreferredBufferLength = FMath::Max(TimeSynthCallbackSizeOverrideCvar, DEFAULT_PROCEDURAL_SOUNDWAVE_BUFFER_SIZE);
	}
}

UTimeSynthComponent::~UTimeSynthComponent()
{
}

void UTimeSynthComponent::PostInitProperties()
{
	Super::PostInitProperties();

	// Copy the settings right away to the audio render thread version
	FilterSettings_AudioRenderThread[(int32)ETimeSynthFilter::FilterA] = FilterASettings;
	FilterSettings_AudioRenderThread[(int32)ETimeSynthFilter::FilterB] = FilterBSettings;
	bIsFilterEnabled_AudioRenderThread[(int32)ETimeSynthFilter::FilterA] = bIsFilterAEnabled;
	bIsFilterEnabled_AudioRenderThread[(int32)ETimeSynthFilter::FilterB] = bIsFilterBEnabled;

	EnvelopeFollowerSettings_AudioRenderThread = EnvelopeFollowerSettings;
	bIsEnvelopeFollowerEnabled_AudioRenderThread = bIsEnvelopeFollowerEnabled;

	SpectrumAnalyzerSettings.FFTSize = GetFFTSize(FFTSize);
	SpectrumAnalyzer.SetSettings(SpectrumAnalyzerSettings);

	// Randomize the seed on post init properties
	RandomStream.GenerateNewSeed();
}

void UTimeSynthComponent::BeginDestroy()
{
	Super::BeginDestroy();
}

void UTimeSynthComponent::OnRegister()
{
	Super::OnRegister();

	SetComponentTickEnabled(true);

	if (!IsRegistered())
	{
		RegisterComponent();
	}
}

void UTimeSynthComponent::OnUnregister()
{
	Super::OnUnregister();

	SetComponentTickEnabled(false);

	if (IsRegistered())
	{
		UnregisterComponent();
	}
}

bool UTimeSynthComponent::IsReadyForFinishDestroy()
{
	return SpectrumAnalysisCounter.GetValue() == 0;
}

void UTimeSynthComponent::AddQuantizationEventDelegate(ETimeSynthEventQuantization QuantizationType, const FOnQuantizationEventBP& OnQuantizationEvent)
{
	// Add a delegate for this event on the game thread data for this event slot
	EventNotificationDelegates_GameThread[(int32)QuantizationType].AddUnique(OnQuantizationEvent);

	// Send over to the audio render thread to tell it that we're listening to this event now
	SynthCommand([this, QuantizationType]
	{
		EventQuantizer.RegisterListenerForEvent(&TimeSynthEventListener, (Audio::EEventQuantization)QuantizationType);
	});
}

void UTimeSynthComponent::SetFilterSettings(ETimeSynthFilter InFilter, const FTimeSynthFilterSettings& InSettings)
{
	if (DisableTimeSynthCvar || bTimeSynthWasDisabled)
	{
		return;
	}

	if (InFilter == ETimeSynthFilter::FilterA)
	{
		FilterASettings = InSettings;
	}
	else
	{
		FilterBSettings = InSettings;
	}

	SynthCommand([this, InFilter, InSettings]
	{
		FilterSettings_AudioRenderThread[(int32)InFilter] = InSettings;
		UpdateFilter((int32)InFilter);
	});
}

void UTimeSynthComponent::SetEnvelopeFollowerSettings(const FTimeSynthEnvelopeFollowerSettings& InSettings)
{
	if (DisableTimeSynthCvar || bTimeSynthWasDisabled)
	{
		return;
	}

	EnvelopeFollowerSettings = InSettings;

	SynthCommand([this, InSettings]
	{
		EnvelopeFollowerSettings_AudioRenderThread = InSettings;
		UpdateEnvelopeFollower();
	});
}

void UTimeSynthComponent::SetFilterEnabled(ETimeSynthFilter InFilter, bool bInIsFilterEnabled)
{
	if (DisableTimeSynthCvar || bTimeSynthWasDisabled)
	{
		return;
	}

	if (InFilter == ETimeSynthFilter::FilterA)
	{
		bIsFilterAEnabled = bInIsFilterEnabled;
	}
	else
	{
		bIsFilterBEnabled = bInIsFilterEnabled;
	}

	SynthCommand([this, InFilter, bInIsFilterEnabled]
	{
		bIsFilterEnabled_AudioRenderThread[(int32)InFilter] = bInIsFilterEnabled;
	});
}

void UTimeSynthComponent::SetEnvelopeFollowerEnabled(bool bInIsEnabled)
{
	if (DisableTimeSynthCvar || bTimeSynthWasDisabled)
	{
		return;
	}

	bIsEnvelopeFollowerEnabled = bInIsEnabled;

	// Set the envelope value to 0.0 immediately if we're disabling the envelope follower
	if (!bInIsEnabled)
	{
		CurrentEnvelopeValue = 0.0f;
	}

	SynthCommand([this, bInIsEnabled]
	{
		bIsEnvelopeFollowerEnabled_AudioRenderThread = bInIsEnabled;
	});
}

Audio::FSpectrumAnalyzerSettings::EFFTSize UTimeSynthComponent::GetFFTSize(ETimeSynthFFTSize InSize) const
{
	switch (InSize)
	{
		case ETimeSynthFFTSize::Min_64: return Audio::FSpectrumAnalyzerSettings::EFFTSize::Min_64;
		case ETimeSynthFFTSize::Small_256: return Audio::FSpectrumAnalyzerSettings::EFFTSize::Small_256;
		case ETimeSynthFFTSize::Medium_512: return Audio::FSpectrumAnalyzerSettings::EFFTSize::Medium_512;
		case ETimeSynthFFTSize::Large_1024: return Audio::FSpectrumAnalyzerSettings::EFFTSize::Large_1024;
		break;
	}
	// return default
	return Audio::FSpectrumAnalyzerSettings::EFFTSize::Medium_512;
}

void UTimeSynthComponent::SetFFTSize(ETimeSynthFFTSize InFFTSize)
{
	if (DisableTimeSynthCvar || bTimeSynthWasDisabled)
	{
		return;
	}

	Audio::FSpectrumAnalyzerSettings::EFFTSize NewFFTSize = GetFFTSize(InFFTSize);

	SynthCommand([this, NewFFTSize]
	{
		SpectrumAnalyzerSettings.FFTSize = NewFFTSize;
		SpectrumAnalyzer.SetSettings(SpectrumAnalyzerSettings);
	});
}

bool UTimeSynthComponent::HasActiveClips()
{
	return bHasActiveClips;
}

void UTimeSynthComponent::OnQuantizationEvent(Audio::EEventQuantization EventQuantizationType, int32 Bars, float Beat)
{
	if (DisableTimeSynthCvar || bTimeSynthWasDisabled)
	{
		return;
	}

	// When this happens, we want to queue up the event data so it can be safely consumed on the game thread
	GameCommand([this, EventQuantizationType, Bars, Beat]()
	{
		EventNotificationDelegates_GameThread[(int32)EventQuantizationType].Broadcast((ETimeSynthEventQuantization)EventQuantizationType, Bars, Beat);
	});	
}

void UTimeSynthComponent::PumpGameCommandQueue()
{
	TFunction<void()> Command;
	while (GameCommandQueue.Dequeue(Command))
	{
		Command();
	}
}

void UTimeSynthComponent::GameCommand(TFunction<void()> Command)
{
	GameCommandQueue.Enqueue(MoveTemp(Command));
}

void UTimeSynthComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// Pump the command queue for any event data that is coming back from the audio render thread/callback
	PumpGameCommandQueue();

	if (DisableTimeSynthCvar || bTimeSynthWasDisabled)
	{
		OnEndGenerate();
		SetActive(false);
		bTimeSynthWasDisabled = static_cast<bool>(DisableTimeSynthCvar);
		return;
	}

	// Broadcast the playback time
	if (OnPlaybackTime.IsBound())
	{
		const float PlaybacktimeSeconds = EventQuantizer.GetPlaybacktimeSeconds();
		OnPlaybackTime.Broadcast(PlaybacktimeSeconds);
	}

	// Perform volume group math to update volume group volume values and then set the volumes on the clips
	for (auto& Entry : VolumeGroupData)
	{
		FVolumeGroupData& VolumeGroup = Entry.Value;
		
		// If we've reached our terminating condition, just set to the target volume 
		if (VolumeGroup.CurrentTime >= VolumeGroup.TargetFadeTime)
		{
			VolumeGroup.CurrentVolumeDb = VolumeGroup.TargetVolumeDb;
		}
		else
		{
			check(VolumeGroup.TargetFadeTime > 0.0f);
			const float FadeFraction = VolumeGroup.CurrentTime / VolumeGroup.TargetFadeTime;

			VolumeGroup.CurrentVolumeDb = VolumeGroup.StartVolumeDb + FadeFraction * (VolumeGroup.TargetVolumeDb - VolumeGroup.StartVolumeDb);
			VolumeGroup.CurrentTime += DeltaTime;
		}

		if (FMath::IsNearlyEqual(VolumeGroup.CurrentVolumeDb, VolumeGroup.LastVolumeDb, KINDA_SMALL_NUMBER))
		{
			continue;
		}

		for (FTimeSynthClipHandle& ClipHandle : VolumeGroup.Clips)
		{
			VolumeGroup.LastVolumeDb = VolumeGroup.CurrentVolumeDb;

			float LinearVolume = Audio::ConvertToLinear(VolumeGroup.CurrentVolumeDb);

			SynthCommand([this, ClipHandle, LinearVolume]
			{
				int32* PlayingClipIndex = ClipIdToClipIndexMap_AudioRenderThread.Find(ClipHandle.ClipId);
				if (PlayingClipIndex)
				{
					FPlayingClipInfo& PlayingClipInfo = PlayingClipsPool_AudioRenderThread[*PlayingClipIndex];
					Audio::FDecodingSoundSourceHandle& DecodingSoundSourceHandle = PlayingClipInfo.DecodingSoundSourceHandle;
					SoundWaveDecoder.SetSourceVolumeScale(DecodingSoundSourceHandle, LinearVolume);
				}
				else
				{
					UE_LOG(LogTimeSynth, Verbose, TEXT("Could not find clip %s "), *(ClipHandle.ClipName.GetPlainNameString()));
				}
			});
		}
	}

	// If the spectrum analyzer is running, grab the desired magnitude spectral data
	if (bEnableSpectralAnalysis)
	{
		SpectralData.Reset();
		SpectrumAnalyzer.LockOutputBuffer();
		for (float Frequency : FrequenciesToAnalyze)
		{
			FTimeSynthSpectralData Data;
			Data.FrequencyHz = Frequency;
			Data.Magnitude = SpectrumAnalyzer.GetMagnitudeForFrequency(Frequency);
			SpectralData.Add(Data);
		}
		SpectrumAnalyzer.UnlockOutputBuffer();
	}

	// Update the synth component on the audio thread
	FAudioThread::RunCommandOnAudioThread([this]()
	{
		SoundWaveDecoder.Update(); 
	});
}

void UTimeSynthComponent::UpdateFilter(int32 FilterIndex)
{
	Filter[FilterIndex].SetFilterType((Audio::EFilter::Type)FilterSettings_AudioRenderThread[FilterIndex].FilterType);
	Filter[FilterIndex].SetFrequency(FilterSettings_AudioRenderThread[FilterIndex].CutoffFrequency);
	Filter[FilterIndex].SetQ(FilterSettings_AudioRenderThread[FilterIndex].FilterQ);
	Filter[FilterIndex].Update();
}

void UTimeSynthComponent::UpdateEnvelopeFollower()
{
	EnvelopeFollower.SetAnalog(EnvelopeFollowerSettings_AudioRenderThread.bIsAnalogMode);
	EnvelopeFollower.SetAttackTime(EnvelopeFollowerSettings_AudioRenderThread.AttackTime);
	EnvelopeFollower.SetReleaseTime(EnvelopeFollowerSettings_AudioRenderThread.ReleaseTime);
	EnvelopeFollower.SetMode((Audio::EPeakMode::Type)EnvelopeFollowerSettings_AudioRenderThread.PeakMode);
}

bool UTimeSynthComponent::Init(int32& InSampleRate)
{
	if (DisableTimeSynthCvar || bTimeSynthWasDisabled)
	{
		SetActive(false);
		OnEndGenerate();
		bTimeSynthWasDisabled = static_cast<bool>(DisableTimeSynthCvar);
		UE_LOG(LogTimeSynth, Verbose, TEXT("Request to initialize TimeSynth received, despite TimeSynth being disabled"));
		return false;
	}

	SampleRate = InSampleRate;
	SoundWaveDecoder.Init(GetAudioDevice(), InSampleRate);
	NumChannels = 2;

	// Initialize the settings for the spectrum analyzer
	SpectrumAnalyzer.Init(InSampleRate);

	// Init and update the filter settings
	for (int32 i = 0; i < 2; ++i)
	{
		Filter[i].Init(InSampleRate, 2);
		UpdateFilter(i);
	}

	DynamicsProcessor.Init(InSampleRate, NumChannels);
	DynamicsProcessor.SetLookaheadMsec(3.0f);
	DynamicsProcessor.SetAttackTime(5.0f);
	DynamicsProcessor.SetReleaseTime(100.0f);
	DynamicsProcessor.SetThreshold(-1.0f);
	DynamicsProcessor.SetRatio(5.0f);
	DynamicsProcessor.SetKneeBandwidth(10.0f);
	DynamicsProcessor.SetInputGain(0.0f);
	DynamicsProcessor.SetOutputGain(0.0f);
	DynamicsProcessor.SetChannelLinkMode(Audio::EDynamicsProcessorChannelLinkMode::Average);
	DynamicsProcessor.SetAnalogMode(true);
	DynamicsProcessor.SetPeakMode(Audio::EPeakMode::Peak);
	DynamicsProcessor.SetProcessingMode(Audio::EDynamicsProcessingMode::Compressor);

	// Init and update the envelope follower settings
	EnvelopeFollower.Init(InSampleRate);
	UpdateEnvelopeFollower();

	// Set the default quantization settings
	SetQuantizationSettings(QuantizationSettings);

	// Create a pool of playing clip runtime infos
	CurrentPoolSize = TimeSynthActiveClipLimitCvar;

	PlayingClipsPool_AudioRenderThread.AddDefaulted(CurrentPoolSize);
	FreePlayingClipIndices_AudioRenderThread.AddDefaulted(CurrentPoolSize);

	for (int32 Index = 0; Index < CurrentPoolSize; ++Index)
	{
		FreePlayingClipIndices_AudioRenderThread[Index] = Index;
	}

	UE_LOG(LogTimeSynth, Verbose, TEXT("TimeSynth initialized"));
	return true; 
}

void UTimeSynthComponent::ShutdownPlayingClips()
{
	SoundWaveDecoder.UpdateRenderThread();

	// Loop through all active loops and render their audio
	for (int32 i = ActivePlayingClipIndices_AudioRenderThread.Num() - 1; i >= 0; --i)
	{
		// Grab the playing clip at the active index
		int32 ClipIndex = ActivePlayingClipIndices_AudioRenderThread[i];
		FPlayingClipInfo& PlayingClip = PlayingClipsPool_AudioRenderThread[ClipIndex];

		// try to wait for the decoder to be initialized
		// if we time out, this is probably in shutdown and the decoder won't ever init.
		if(!SoundWaveDecoder.IsInitialized(PlayingClip.DecodingSoundSourceHandle))
		{
			FPlatformProcess::Sleep(0.5f);
		}

		SoundWaveDecoder.RemoveDecodingSource(PlayingClip.DecodingSoundSourceHandle);
		ActivePlayingClipIndices_AudioRenderThread.RemoveAtSwap(i, 1, false);
		FreePlayingClipIndices_AudioRenderThread.Add(ClipIndex);
		UE_LOG(LogTimeSynth, Verbose, TEXT("Active clip index %i flagged for removal and added to free playing clips"), i);
	}
	ActivePlayingClipIndices_AudioRenderThread.Reset();
	FreePlayingClipIndices_AudioRenderThread.Reset();
	ClipIdToClipIndexMap_AudioRenderThread.Reset();
	DecodingSounds_GameThread.Reset();

	bHasActiveClips.AtomicSet(false);
	UE_LOG(LogTimeSynth, Verbose, TEXT("All playing clips have been shutdown"));
}

void UTimeSynthComponent::OnEndGenerate() 
{
	ShutdownPlayingClips();
}

int32 UTimeSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	if (DisableTimeSynthCvar || bTimeSynthWasDisabled)
	{
		bTimeSynthWasDisabled = static_cast<bool>(DisableTimeSynthCvar);
		UE_LOG(LogTimeSynth, Verbose, TEXT("Received a 'OnGenerateAudio' command when TimeSynth was disabled"));
		return 0;
	}

	// Update the decoder
	SoundWaveDecoder.UpdateRenderThread();

	int32 NumFrames = NumSamples / NumChannels;

	// Perform event quantization notifications
	// This will use the NumFramesPerCallback to evaluate what queued up events need 
	// to begin rendering. THe lambda callback will then enqueue any new rendering clips
	// to the list of active clips. So we only need to loop through active clip indices to render the audio output
	EventQuantizer.NotifyEvents(NumFrames);
	const int32 NumActiveClips = ActivePlayingClipIndices_AudioRenderThread.Num();

	bHasActiveClips.AtomicSet(NumActiveClips > 0);

	// Loop through all active loops and render their audio
	for (int32 i = NumActiveClips - 1; i >= 0; --i)
	{
		// Grab the playing clip at the active index
		int32 ClipIndex = ActivePlayingClipIndices_AudioRenderThread[i];
		FPlayingClipInfo& PlayingClip = PlayingClipsPool_AudioRenderThread[ClipIndex];
		PlayingClip.bHasStartedPlaying = true;

		// Compute the number of frames we need to read
		int32 NumFramesToRead = NumFrames - PlayingClip.StartFrameOffset;
		check(NumFramesToRead > 0 && NumFramesToRead <= NumFrames);

		if (!SoundWaveDecoder.IsInitialized(PlayingClip.DecodingSoundSourceHandle))
		{
			continue;
		}

		AudioScratchBuffer.Reset();
		if (SoundWaveDecoder.GetSourceBuffer(PlayingClip.DecodingSoundSourceHandle, NumFramesToRead, NumChannels, AudioScratchBuffer))
		{
			// Make sure we read the appropriate amount of audio frames
			check(AudioScratchBuffer.Num() == NumFramesToRead * NumChannels);
			// Now mix in the retrieved audio at the appropriate sample index
			float* DecodeSourceAudioPtr = AudioScratchBuffer.GetData();
			float FadeVolume = 1.0f;
			int32 OutputSampleIndex = PlayingClip.StartFrameOffset * NumChannels;
			int32 SourceSampleIndex = 0;
			for (int32 FrameIndex = PlayingClip.StartFrameOffset; FrameIndex < NumFrames; ++FrameIndex)
			{
				// Check the fade in condition
				if (PlayingClip.CurrentFrameCount < PlayingClip.FadeInDurationFrames)
				{
					FadeVolume = (float)PlayingClip.CurrentFrameCount / PlayingClip.FadeInDurationFrames;
				}
				// Check the fade out condition
				else if (PlayingClip.CurrentFrameCount >= PlayingClip.DurationFrames && PlayingClip.FadeOutDurationFrames > 0)
				{
					int32 FadeOutFrameCount = PlayingClip.CurrentFrameCount - PlayingClip.DurationFrames;
					FadeVolume = 1.0f - (float)FadeOutFrameCount / PlayingClip.FadeOutDurationFrames;
				}

				FadeVolume = FMath::Clamp(FadeVolume, 0.0f, 1.0f);
				for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex, ++OutputSampleIndex, ++SourceSampleIndex)
				{
					OutAudio[OutputSampleIndex] += FadeVolume * DecodeSourceAudioPtr[SourceSampleIndex];
				}

				++PlayingClip.CurrentFrameCount;
			}

			// Reset the start frame offset so that when this clip continues playing,
			// it won't start part-way through the audio buffer
			PlayingClip.StartFrameOffset = 0;

			bool bIsClipDurationFinished = PlayingClip.CurrentFrameCount > PlayingClip.DurationFrames + PlayingClip.FadeOutDurationFrames;

			// If the clip finished by artificial clip duration settings or if it naturally finished (file length), remove it from the active list
			if (bIsClipDurationFinished || SoundWaveDecoder.IsFinished(PlayingClip.DecodingSoundSourceHandle))
			{
				if (bIsClipDurationFinished)
				{
					UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s reached end of clip duration"), *(PlayingClip.Handle.ClipName.GetPlainNameString()));
				}
				else
				{
					UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s reached end of wavefile"), *(PlayingClip.Handle.ClipName.GetPlainNameString()));
				}

				SoundWaveDecoder.RemoveDecodingSource(PlayingClip.DecodingSoundSourceHandle);
				ActivePlayingClipIndices_AudioRenderThread.RemoveAtSwap(i, 1, false);
				FreePlayingClipIndices_AudioRenderThread.Add(ClipIndex);
				UE_LOG(LogTimeSynth, Verbose, TEXT("Active clip index %i flagged for removaland added to free playing clips"), ClipIndex);

				// If this clip was playing in a volume group, we need to remove it from the volume group
				if (PlayingClip.VolumeGroupId != INDEX_NONE)
				{
					FTimeSynthClipHandle Handle = PlayingClip.Handle;
					VolumeGroupUniqueId VolumeGroupId = PlayingClip.VolumeGroupId;

					GameCommand([this, Handle, VolumeGroupId]()
					{
						FVolumeGroupData* VolumeGroup = VolumeGroupData.Find(VolumeGroupId);
						if (VolumeGroup)
						{
							VolumeGroup->Clips.Remove(Handle);
							UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s removed from volume group %i"), *(Handle.ClipName.GetPlainNameString()), VolumeGroupId);
						}
						else
						{
							UE_LOG(LogTimeSynth, Warning, TEXT("Volume group %i (referenced by clip %s) could not be found"), VolumeGroupId, *(Handle.ClipName.GetPlainNameString()));
						}
					});
				}
			}
		}
	}

	// Feed audio through filter
	for (int32 i = 0; i < 2; ++i)
	{
		if (bIsFilterEnabled_AudioRenderThread[i])
		{
			Filter[i].ProcessAudio(OutAudio, NumSamples, OutAudio);
		}
	}

	// Feed audio through the envelope follower if it's enabled
	if (bIsEnvelopeFollowerEnabled_AudioRenderThread)
	{
		for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex += NumChannels)
		{
			float InputSample = 0.5f * (OutAudio[SampleIndex] + OutAudio[SampleIndex + 1]);
			CurrentEnvelopeValue = EnvelopeFollower.ProcessAudio(InputSample);
		}
	}

	if (bEnableSpectralAnalysis)
	{
		// If we have stereo audio, sum to mono before sending to analyzer
		if (NumChannels == 2)
		{
			// Use the scratch buffer to sum the audio to mono
			AudioScratchBuffer.Reset();
			AudioScratchBuffer.AddUninitialized(NumFrames);
			float* AudioScratchBufferPtr = AudioScratchBuffer.GetData();
			int32 SampleIndex = 0;
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex, SampleIndex += NumChannels)
			{
				AudioScratchBufferPtr[FrameIndex] = 0.5f * (OutAudio[SampleIndex] + OutAudio[SampleIndex + 1]);
			}
			SpectrumAnalyzer.PushAudio(AudioScratchBufferPtr, NumFrames);
		}
		else
		{
			SpectrumAnalyzer.PushAudio(OutAudio, NumSamples);
		}

		// Launch an analysis task with this audio
		(new FAutoDeleteAsyncTask<FTimeSynthSpectrumAnalysisTask>(&SpectrumAnalyzer, &SpectrumAnalysisCounter))->StartBackgroundTask();
	}

	// Limit the output to prevent clipping
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex += NumChannels)
	{
		DynamicsProcessor.ProcessAudio(&OutAudio[SampleIndex], NumChannels, &OutAudio[SampleIndex]);
	}


 	return NumSamples; 
}

void UTimeSynthComponent::SetQuantizationSettings(const FTimeSynthQuantizationSettings& InQuantizationSettings)
{
	if (DisableTimeSynthCvar || bTimeSynthWasDisabled)
	{
		return;
	}

	// Store the quantization on the UObject for BP querying
	QuantizationSettings = InQuantizationSettings;

	// EventQuantizer will handle this case gracefully, but still warn.
 	if (!SampleRate)
 	{
 		UE_LOG(LogTimeSynth, Warning, TEXT("SetQuantizationSettings called with a sample rate of 0.  Did you forget to activate the time synth component?"));
 	}

	// Local store what the global quantization is so we can assign it to clips using global quantization
	GlobalQuantization = (Audio::EEventQuantization)InQuantizationSettings.GlobalQuantization;

	// Translate the TimeSynth version of quantization settings to the non-UObject quantization settings
	Audio::FEventQuantizationSettings Settings;
	Settings.SampleRate = SampleRate;
	Settings.NumChannels = NumChannels;
	Settings.BeatsPerMinute = FMath::Max(1.0f, InQuantizationSettings.BeatsPerMinute);
	Settings.BeatsPerBar = (uint32)FMath::Max(InQuantizationSettings.BeatsPerBar, 1);
	Settings.GlobalQuantization = GlobalQuantization;
	Settings.BeatDivision = FMath::Pow(2, (int32)InQuantizationSettings.BeatDivision);

	SynthCommand([this, Settings]
	{
		EventQuantizer.SetQuantizationSettings(Settings);
	});
}

void UTimeSynthComponent::SetBPM(const float InBeatsPerMinute)
{
	float NewBeatsPerMinute = FMath::Clamp(InBeatsPerMinute, 0.0f, 999.0f);

	if (!FMath::IsNearlyEqual(NewBeatsPerMinute, InBeatsPerMinute))
	{
		UE_LOG(LogTimeSynth, Warning, TEXT("Clapming provided BPM from %f to %f"), InBeatsPerMinute, NewBeatsPerMinute);
	}

	QuantizationSettings.BeatsPerMinute = NewBeatsPerMinute;

	SynthCommand([this, NewBeatsPerMinute]
	{
		EventQuantizer.SetBPM(NewBeatsPerMinute);
	});
}

int32 UTimeSynthComponent::GetBPM() const
{
	return QuantizationSettings.BeatsPerMinute;
}

void UTimeSynthComponent::SetSeed(int32 InSeed)
{
	RandomStream.Initialize(InSeed);
}

void UTimeSynthComponent::ResetSeed()
{
	RandomStream.Reset();
}

FTimeSynthClipHandle UTimeSynthComponent::PlayClip(UTimeSynthClip* InClip, UTimeSynthVolumeGroup* InVolumeGroup)
{
	if (DisableTimeSynthCvar || !GetAudioDevice() || bTimeSynthWasDisabled)
	{
		SetActive(false);
		if (!bTimeSynthWasDisabled)
		{
			OnEndGenerate();
		}
		bTimeSynthWasDisabled = true;
		UE_LOG(LogTimeSynth, Verbose, TEXT("Received a 'Play Clip' command when TimeSynth was disabled"));

		return FTimeSynthClipHandle();
	}

	if (!InClip)
	{
		UE_LOG(LogTimeSynth, Warning, TEXT("Failed to play clip. Null UTimeSynthClip object."));
		return FTimeSynthClipHandle();
	}

	// Validate the clip
	if (InClip->Sounds.Num() == 0)
	{
		UE_LOG(LogTimeSynth, Warning, TEXT("Failed to play clip %s: needs to have sounds to choose from."), *(InClip->GetFName().GetPlainNameString()));
		return FTimeSynthClipHandle();
	}

	const bool bNoFadeIn = InClip->FadeInTime.IsZeroDuration();
	const bool bNoDuration = InClip->ClipDuration.IsZeroDuration();
	const bool bNoFadeOut = !InClip->bApplyFadeOut || InClip->FadeOutTime.IsZeroDuration();
	if (bNoFadeIn && bNoDuration && bNoFadeOut)
	{
		UE_LOG(LogTimeSynth, Warning, TEXT("Failed to play clip %s: no duration or fade in/out set."), *(InClip->GetFName().GetPlainNameString()));
		return FTimeSynthClipHandle();
	}

	if (!IsActive())
	{
		SetActive(true);
	}

	// Get this time synth components transform
	const FTransform& ThisComponentTransform = GetComponentTransform();

	// Get the distance to nearest listener using this transform
	const FAudioDevice* OwningAudioDevice = GetAudioDevice();

	// Validate audio device since it might not be available (i.e. -nosound)
	if (OwningAudioDevice == nullptr)
	{
		static bool bShouldWarn = true;
		UE_CLOG(bShouldWarn, LogTimeSynth, Warning, TEXT("Failed to play clip: no audio device. Running -nosound?"));
		bShouldWarn = false;

		return FTimeSynthClipHandle();
	}

	const float DistanceToListener = OwningAudioDevice->GetDistanceToNearestListener(ThisComponentTransform.GetTranslation());

	TArray<FTimeSynthClipSound> ValidSounds;

	// Make sure at least one of the entries in the sound array has a USoundWave asset ref
	for (const FTimeSynthClipSound& ClipSound : InClip->Sounds)
	{
		if (ClipSound.SoundWave)
		{
			// Now check if this clip sound is in range of the distance to the listener
			if (ClipSound.DistanceRange.X != 0 || ClipSound.DistanceRange.Y != 0)
			{
				float MinDist = FMath::Min(ClipSound.DistanceRange.X, ClipSound.DistanceRange.Y);
				float MaxDist = FMath::Max(ClipSound.DistanceRange.X, ClipSound.DistanceRange.Y);

				if (DistanceToListener >= MinDist && DistanceToListener < MaxDist)
				{
					ValidSounds.Add(ClipSound);
					UE_LOG(LogTimeSynth, Verbose, TEXT("SoundWave %s added to valid sounds for clip %s"), *(ClipSound.SoundWave->GetFName().GetPlainNameString()),
						   *(InClip->GetFName().GetPlainNameString()));
				}
				else
				{
					UE_LOG(LogTimeSynth, Verbose, TEXT("SoundWave %s in clip %s out of range of listener"), *(ClipSound.SoundWave->GetFName().GetPlainNameString()),
						*(InClip->GetFName().GetPlainNameString()));
				}
			}
			else
			{
				ValidSounds.Add(ClipSound);
				UE_LOG(LogTimeSynth, Verbose, TEXT("SoundWave %s added to valid sounds for clip %s"), *(ClipSound.SoundWave->GetFName().GetPlainNameString()),
					*(InClip->GetFName().GetPlainNameString()));
			}
		}
	}

	// We didn't have any valid sounds to play for this clip or component was out of range from listener
	if (!ValidSounds.Num())
	{
		return FTimeSynthClipHandle();
	}

	// Calculate the linear volume
	const float VolumeMin = FMath::Clamp(InClip->VolumeScaleDb.X, -60.0f, 20.0f);
	const float VolumeMax = FMath::Clamp(InClip->VolumeScaleDb.Y, -60.0f, 20.0f);
	const float VolumeDb = RandomStream.FRandRange(VolumeMin, VolumeMax);
	float VolumeScale = Audio::ConvertToLinear(VolumeDb);
	
	// Calculate the pitch scale
	const float PitchMin = FMath::Clamp(InClip->PitchScaleSemitones.X, -24.0f, 24.0f);
	const float PitchMax = FMath::Clamp(InClip->PitchScaleSemitones.Y, -24.0f, 24.0f);
	const float PitchSemitones = RandomStream.FRandRange(PitchMin, PitchMax);
	const float PitchScale = Audio::GetFrequencyMultiplier(PitchSemitones);

	// Only need to find a random-weighted one if there's more than valid sound
	int32 ChosenSoundIndex = 0;
	if (ValidSounds.Num() > 1)
	{
		float SumWeight = 0.0f;
		for (FTimeSynthClipSound& Sound : ValidSounds)
		{
			SumWeight += Sound.RandomWeight;
		}

		const float Choice = RandomStream.FRandRange(0.0f, SumWeight);
		SumWeight = 0.0f;

		for (int32 Index = 0; Index < ValidSounds.Num(); ++Index)
		{
			const FTimeSynthClipSound& Sound = ValidSounds[Index];
			const float NextTotal = SumWeight + Sound.RandomWeight;
			if (Choice >= SumWeight && Choice < NextTotal)
			{
				ChosenSoundIndex = Index;
				break;
			}
			SumWeight = NextTotal;
		}
	}

	const FTimeSynthClipSound& ChosenSound = ValidSounds[ChosenSoundIndex];
	UE_LOG(LogTimeSynth, Verbose, TEXT("SoundWave %s used as sound for clip %s"), *(ValidSounds[ChosenSoundIndex].SoundWave->GetFName().GetPlainNameString()),
		*(InClip->GetFName().GetPlainNameString()));

	// Now have a chosen sound, so we can create a new decoder handle on the game thread
	Audio::FDecodingSoundSourceHandle NewDecoderHandle = SoundWaveDecoder.CreateSourceHandle(ChosenSound.SoundWave);
	DecodingSounds_GameThread.Add(NewDecoderHandle);

	// Generate a new handle for this clip.
	// This handle is used by game thread to control this clip.
	static int32 ClipIds = 0;
	FTimeSynthClipHandle NewHandle;
	NewHandle.ClipName = InClip->GetFName();
	NewHandle.ClipId = ClipIds++;
	UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s given Id %d"), *(InClip->GetFName().GetPlainNameString()), NewHandle.ClipId);

	// New struct for a playing clip handle. This is internal.
	FPlayingClipInfo NewClipInfo;

	// Setup an entry for the playing clip in its volume group if it was set
	if (InVolumeGroup)
	{
		VolumeGroupUniqueId Id = InVolumeGroup->GetUniqueID();
		NewClipInfo.VolumeGroupId = Id;

		FVolumeGroupData* VolumeGroup = VolumeGroupData.Find(Id);
		if (!VolumeGroup)
		{
			FVolumeGroupData NewData(InVolumeGroup->DefaultVolume);
			NewData.Clips.Add(NewHandle);
			VolumeGroup = &VolumeGroupData.Add(Id, NewData);
			UE_LOG(LogTimeSynth, Verbose, TEXT("Volume group %s created with Id number %i"), *(InVolumeGroup->GetName()), Id);
		}
		else
		{
			VolumeGroup->Clips.Add(NewHandle);
		}

		UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s added to volume group %s"), *(InClip->GetFName().GetPlainNameString()), *(InVolumeGroup->GetName()));

		// Get the current volume group value and "scale" it into the volume scale
		VolumeScale *= Audio::ConvertToLinear(VolumeGroup->CurrentVolumeDb);
	}

	Audio::FSourceDecodeInit DecodeInit;
	DecodeInit.Handle = NewDecoderHandle;
	DecodeInit.PitchScale = PitchScale;
	DecodeInit.VolumeScale = VolumeScale;
	DecodeInit.SoundWave = ChosenSound.SoundWave;
	DecodeInit.SeekTime = 0;
	DecodeInit.bForceSyncDecode = (TimeSynthForceSyncDecodesCvar == 1);

	
	UE_CLOG(VolumeScale < MinAudibleLog, LogTimeSynth, Verbose, TEXT("Clip %s playing at inaudibly soft volume"), *(InClip->GetFName().GetPlainNameString()));

	// Update the synth component on the audio thread
	FAudioThread::RunCommandOnAudioThread([this, DecodeInit]()
	{
		SoundWaveDecoder.InitDecodingSource(DecodeInit);
	});


	NewClipInfo.bIsGloballyQuantized = InClip->ClipQuantization == ETimeSynthEventClipQuantization::Global;

	if (NewClipInfo.bIsGloballyQuantized)
	{
		NewClipInfo.ClipQuantization = GlobalQuantization;
	}
	else
	{
		// Our Audio::EEventQuantization enumeration is 1 greater than the ETimeSynthEventClipQuantization to account for
		// the "Global" enumeration slot which is presented to users. We need to special-case it here. 
		int32 ClipQuantizationEnumIndex = (int32)InClip->ClipQuantization;
		check(ClipQuantizationEnumIndex >= 1);
		NewClipInfo.ClipQuantization = (Audio::EEventQuantization)(ClipQuantizationEnumIndex - 1);
	}

	// Pass this off to the clip info. This is going to use this to trigger the follow clip if it exists.
	NewClipInfo.SynthClip = InClip;
	NewClipInfo.VolumeScale = VolumeScale;
	NewClipInfo.PitchScale = PitchScale;
	NewClipInfo.DecodingSoundSourceHandle = NewDecoderHandle;
	NewClipInfo.StartFrameOffset = 0;
	NewClipInfo.CurrentFrameCount = 0;

	// Pass the handle to the clip
	NewClipInfo.Handle = NewHandle;

	FTimeSynthTimeDef ClipDuration = InClip->ClipDuration;
	FTimeSynthTimeDef FadeInTime = InClip->FadeInTime;

	FTimeSynthTimeDef FadeOutTime = InClip->bApplyFadeOut
		? InClip->FadeOutTime
		: FTimeSynthTimeDef(0, 0);


	// Send this new clip over to the audio render thread
	SynthCommand([this, NewClipInfo, ClipDuration, FadeInTime, FadeOutTime]
	{
		// Immediately create a mapping for this clip id to a free clip slot
		// It's possible that the clip might get state changes before it starts playing if
		// we're playing a very long-duration quantization
		int32 FreeClipIndex = -1;
		if (FreePlayingClipIndices_AudioRenderThread.Num() > 0)
		{
			FreeClipIndex = FreePlayingClipIndices_AudioRenderThread.Pop(false);	
			UE_LOG(LogTimeSynth, Verbose, TEXT("Clip index %i removed from free clip pool"), FreeClipIndex);
		}
		else
		{
			UE_LOG(LogTimeSynth, Display, TEXT("Ignoring PlayClip() request since the Playing Clip pool is full, consider initializeng Pool Size to a larger value"));
			return;
		}
		check(FreeClipIndex >= 0);

		// Copy over the clip info to the slot
		PlayingClipsPool_AudioRenderThread[FreeClipIndex] = NewClipInfo;
		PlayingClipsPool_AudioRenderThread[FreeClipIndex].bIsInitialized = true;

		// Add a mapping of the clip handle id to the free index
		// This will allow us to reference the playing clip from BP, etc.
		ClipIdToClipIndexMap_AudioRenderThread.Add(NewClipInfo.Handle.ClipId, FreeClipIndex);

		// Queue an event quantization event up. 
		// The Event quantizer will execute the lambda on the exact frame of the quantization enumeration.
		// It's NumFramesOffset will be the number of frames within the current audio buffer to begin rendering the
		// audio at.
		EventQuantizer.EnqueueEvent(NewClipInfo.ClipQuantization, 

			[this, FreeClipIndex, ClipDuration, FadeInTime, FadeOutTime](uint32 NumFramesOffset)
			{
				FPlayingClipInfo& PlayingClipInfo = PlayingClipsPool_AudioRenderThread[FreeClipIndex];

				// early exit.  Quantized stop event was just processed for this same Quantization step
				if (PlayingClipInfo.bHasBeenStopped)
				{
					UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s did not play, as it was already flagged to stop"), *(PlayingClipInfo.Handle.ClipName.GetPlainNameString()));
					return;
				}

				// Setup the duration of various things using the event quantizer
				PlayingClipInfo.DurationFrames = EventQuantizer.GetDurationInFrames(ClipDuration.NumBars, (float)ClipDuration.NumBeats);
				PlayingClipInfo.FadeInDurationFrames = EventQuantizer.GetDurationInFrames(FadeInTime.NumBars, (float)FadeInTime.NumBeats);
				PlayingClipInfo.FadeOutDurationFrames = EventQuantizer.GetDurationInFrames(FadeOutTime.NumBars, (float)FadeOutTime.NumBeats);
				PlayingClipInfo.StartFrameOffset = NumFramesOffset;

				// Add this clip to the list of active playing clips so it begins rendering
				ActivePlayingClipIndices_AudioRenderThread.Add(FreeClipIndex);
				UE_LOG(LogTimeSynth, Verbose, TEXT("Clip index %i added to active playing clip pool"), FreeClipIndex);
			});
	});
	
	return NewHandle;
}

void UTimeSynthComponent::StopClip(FTimeSynthClipHandle InClipHandle, ETimeSynthEventClipQuantization EventQuantization)
{
	Audio::EEventQuantization StopQuantization = GlobalQuantization;
	if (EventQuantization != ETimeSynthEventClipQuantization::Global)
	{
		int32 ClipQuantizationEnumIndex = (int32)EventQuantization;
		check(ClipQuantizationEnumIndex >= 1);
		StopQuantization = (Audio::EEventQuantization)(ClipQuantizationEnumIndex - 1);
	}

	SynthCommand([this, InClipHandle, StopQuantization]
	{
		EventQuantizer.EnqueueEvent(StopQuantization,

			[this, InClipHandle](uint32 NumFramesOffset)
			{
				int32* PlayingClipIndexPtr = ClipIdToClipIndexMap_AudioRenderThread.Find(InClipHandle.ClipId);
				if (PlayingClipIndexPtr)
				{
					const int32 PlayingClipIndex = *PlayingClipIndexPtr;
					// Grab the clip info
					FPlayingClipInfo& PlayingClipInfo = PlayingClipsPool_AudioRenderThread[PlayingClipIndex];

					if (!PlayingClipInfo.bHasStartedPlaying)
					{
						UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s stopped before it began playing"), *(PlayingClipInfo.Handle.ClipName.GetPlainNameString()));

						// add index back to free pool
						FreePlayingClipIndices_AudioRenderThread.Add(PlayingClipIndex);
						UE_LOG(LogTimeSynth, Verbose, TEXT("Clip index %i added to free clip pool"), PlayingClipIndex);

						// remove map entry
						ClipIdToClipIndexMap_AudioRenderThread.Remove(InClipHandle.ClipId);

						// Clip may have been staged to play, so we can search and remove it manually
						bool bFoundClip = false;
						int32 NumActivePlayingClipIndices = ActivePlayingClipIndices_AudioRenderThread.Num();

						for (int i = 0; i < NumActivePlayingClipIndices; ++i)
						{
							if (ActivePlayingClipIndices_AudioRenderThread[i] == PlayingClipIndex)
							{
								ActivePlayingClipIndices_AudioRenderThread.RemoveAtSwap(i, 1, false);
								UE_LOG(LogTimeSynth, Verbose, TEXT("Instance of clip %s removed from render thread"), *(PlayingClipInfo.Handle.ClipName.GetPlainNameString()));
								bFoundClip = true;
								--NumActivePlayingClipIndices;
							}
						}

						// clip wasn't staged to play yet. Raise the flag for the Notify
						if (!bFoundClip)
						{
							PlayingClipInfo.bHasBeenStopped = true;
						}
					}
					// Already playing the clip, so force the clip to start fading if its already playing
					else if (PlayingClipInfo.CurrentFrameCount < PlayingClipInfo.DurationFrames)
					{
						UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s forced to start fade out"), *(PlayingClipInfo.Handle.ClipName.GetPlainNameString()));

						// Adjust the duration of the clip to "spoof" it's code which triggers a fade this render callback block.
						PlayingClipInfo.DurationFrames = PlayingClipInfo.CurrentFrameCount + NumFramesOffset;
					}
				}
				else
				{
					UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s was given 'Stop' command, but could not be found"), *(InClipHandle.ClipName.GetPlainNameString()));
				}
			});
	});
}

void UTimeSynthComponent::StopClipWithFadeOverride(FTimeSynthClipHandle InClipHandle, ETimeSynthEventClipQuantization EventQuantization, const FTimeSynthTimeDef& FadeTime)
{
	Audio::EEventQuantization StopQuantization = GlobalQuantization;
	if (EventQuantization != ETimeSynthEventClipQuantization::Global)
	{
		int32 ClipQuantizationEnumIndex = (int32)EventQuantization;
		check(ClipQuantizationEnumIndex >= 1);
		StopQuantization = (Audio::EEventQuantization)(ClipQuantizationEnumIndex - 1);
	}

	SynthCommand([this, InClipHandle, StopQuantization, FadeTime]
	{
		EventQuantizer.EnqueueEvent(StopQuantization,

			[this, InClipHandle, FadeTime](uint32 NumFramesOffset)
			{
				int32* PlayingClipIndex = ClipIdToClipIndexMap_AudioRenderThread.Find(InClipHandle.ClipId);
				if (PlayingClipIndex)
				{
					// Grab the clip info
					FPlayingClipInfo& PlayingClipInfo = PlayingClipsPool_AudioRenderThread[*PlayingClipIndex];

					// Only do anything if the clip is not yet already fading
					if (PlayingClipInfo.CurrentFrameCount < PlayingClipInfo.DurationFrames)
					{
						// Adjust the duration of the clip to "spoof" it's code which triggers a fade this render callback block.
						PlayingClipInfo.DurationFrames = PlayingClipInfo.CurrentFrameCount + NumFramesOffset;

						// Override the clip's fade out duration (but prevent pops so we can do a brief fade out at least)
						PlayingClipInfo.FadeOutDurationFrames = FMath::Max(EventQuantizer.GetDurationInFrames(FadeTime.NumBars, (float)FadeTime.NumBeats), 100u);

						UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s was stopped with fade override"), *(InClipHandle.ClipName.GetPlainNameString()));
					}
					else
					{
						UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s was given 'Stop with Fade Override' command, but was already fading out"), *(InClipHandle.ClipName.GetPlainNameString()));
					}
				}
				else
				{
					UE_LOG(LogTimeSynth, Verbose, TEXT("Clip %s was given 'Stop with Fade Override' command, but could not be found"), *(InClipHandle.ClipName.GetPlainNameString()));
				}
			});
	});
}

void UTimeSynthComponent::SetVolumeGroupInternal(FVolumeGroupData& InData, float VolumeDb, float FadeTimeSec)
{
	if (FadeTimeSec == 0.0f)
	{
		InData.CurrentVolumeDb = VolumeDb;
		InData.StartVolumeDb = VolumeDb;
	}
	else
	{
		InData.StartVolumeDb = InData.CurrentVolumeDb;
	}
	InData.TargetVolumeDb = VolumeDb;

	InData.CurrentTime = 0.0f;
	InData.TargetFadeTime = FadeTimeSec;
}

void UTimeSynthComponent::SetVolumeGroup(UTimeSynthVolumeGroup* InVolumeGroup, float VolumeDb, float FadeTimeSec)
{
	if (!InVolumeGroup)
	{
		UE_LOG(LogTimeSynth, Warning, TEXT("Failed to set volume group. Null UTimeSynthVolumeGroup object."));
		return;
	}

	VolumeGroupUniqueId Id = InVolumeGroup->GetUniqueID();
	FVolumeGroupData* VolumeGroup = VolumeGroupData.Find(Id);

	// If no volume group exists, there are no clips playing on that volume group, just create a slot for it.
	// New clips that are playing on this group will just get the volume set here.
	if (!VolumeGroup)
	{
		FVolumeGroupData NewData;
		SetVolumeGroupInternal(NewData, VolumeDb, FadeTimeSec);
		VolumeGroupData.Add(Id, NewData);

		UE_LOG(LogTimeSynth, Verbose, TEXT("New volume group %s created with Id %i"), (*InVolumeGroup->GetName()), Id);
	}
	else
	{
		SetVolumeGroupInternal(*VolumeGroup, VolumeDb, FadeTimeSec);
	}

	UE_LOG(LogTimeSynth, Verbose, TEXT("Volume group %s's volume changed to %f dB"), (*InVolumeGroup->GetName()), VolumeDb);

	UE_CLOG(VolumeDb < MinAudibleLog, LogTimeSynth, Verbose, TEXT("Volume group %s set to inaudibly low volume"), (*InVolumeGroup->GetName()));
}

void UTimeSynthComponent::StopSoundsOnVolumeGroup(UTimeSynthVolumeGroup* InVolumeGroup, ETimeSynthEventClipQuantization EventQuantization)
{
	VolumeGroupUniqueId Id = InVolumeGroup->GetUniqueID();
	FVolumeGroupData* VolumeGroupEntry = VolumeGroupData.Find(Id);

	if (VolumeGroupEntry)
	{
		for (FTimeSynthClipHandle& ClipHandle : VolumeGroupEntry->Clips)
		{
			StopClip(ClipHandle, EventQuantization);
		}
		UE_LOG(LogTimeSynth, Verbose, TEXT("Stopped all clips in volume group %s"), *(InVolumeGroup->GetName()));
	}
	else
	{
		UE_LOG(LogTimeSynth, Verbose, TEXT("Attempted to stop all clips on volume group %s, but the volume group could not be found"), *(InVolumeGroup->GetName()));
	}
}

void UTimeSynthComponent::StopSoundsOnVolumeGroupWithFadeOverride(UTimeSynthVolumeGroup* InVolumeGroup, ETimeSynthEventClipQuantization EventQuantization, const FTimeSynthTimeDef& FadeTime)
{
	VolumeGroupUniqueId Id = InVolumeGroup->GetUniqueID();
	FVolumeGroupData* VolumeGroupEntry = VolumeGroupData.Find(Id);

	if (VolumeGroupEntry)
	{
		for (FTimeSynthClipHandle& ClipHandle : VolumeGroupEntry->Clips)
		{
			StopClipWithFadeOverride(ClipHandle, EventQuantization, FadeTime);
		}

		UE_LOG(LogTimeSynth, Verbose, TEXT("Stopped all clips, with fade override, in volume group %s"), *(InVolumeGroup->GetName()));
	}
	else
	{
		UE_LOG(LogTimeSynth, Verbose, TEXT("Attempted to stop all clips, with fade override, on volume group %s, but the volume group could not be found"), *(InVolumeGroup->GetName()));
	}
}

TArray<FTimeSynthSpectralData> UTimeSynthComponent::GetSpectralData() const
{
	if (bEnableSpectralAnalysis)
	{
		return SpectralData;
	}
	// Return empty array if not analyzing spectra
	return TArray<FTimeSynthSpectralData>();
}

int32 UTimeSynthComponent::GetMaxActiveClipLimit() const
{
	return CurrentPoolSize;
}

