// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerDevice.h"

#include "AudioMixerSource.h"
#include "AudioMixerSubmix.h"
#include "AudioMixerSourceVoice.h"
#include "AudioPluginUtilities.h"
#include "AudioMixerEffectsManager.h"
#include "DSP/Noise.h"
#include "DSP/SinOsc.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundSubmixSend.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverbFast.h"
#include "SubmixEffects/AudioMixerSubmixEffectEQ.h"
#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Runtime/HeadMountedDisplay/Public/IHeadMountedDisplayModule.h"
#include "Misc/App.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Async/Async.h"

#if WITH_EDITOR
#include "AudioEditorModule.h"
#endif // WITH_EDITOR


static int32 DisableSubmixEffectEQCvar = 0;
FAutoConsoleVariableRef CVarDisableSubmixEQ(
	TEXT("au.DisableSubmixEffectEQ"),
	DisableSubmixEffectEQCvar,
	TEXT("Disables the eq submix.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

// Link to "Audio" profiling category
CSV_DECLARE_CATEGORY_MODULE_EXTERN(AUDIOMIXERCORE_API, Audio);

namespace Audio
{
	FMixerDevice::FMixerDevice(IAudioMixerPlatformInterface* InAudioMixerPlatform)
		: AudioMixerPlatform(InAudioMixerPlatform)
		, AudioClockDelta(0.0)
		, AudioClock(0.0)
		, PreviousMasterVolume((float)INDEX_NONE)
		, SourceManager(this)
		, GameOrAudioThreadId(INDEX_NONE)
		, AudioPlatformThreadId(INDEX_NONE)
		, bDebugOutputEnabled(false)
		, bSubmixRegistrationDisabled(false)
	{
		// This audio device is the audio mixer
		bAudioMixerModuleLoaded = true;
	}

	FMixerDevice::~FMixerDevice()
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(this);

		if (AudioMixerPlatform != nullptr)
		{
			delete AudioMixerPlatform;
		}
	}

	void FMixerDevice::CheckAudioThread() const
	{
#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		// "Audio Thread" is the game/audio thread ID used above audio rendering thread.
		AUDIO_MIXER_CHECK(IsInAudioThread());
#endif
	}

	void FMixerDevice::OnListenerUpdated(const TArray<FListener>& InListeners)
	{
		ListenerTransforms.Reset(InListeners.Num());

		for (const FListener& Listener : InListeners)
		{
			ListenerTransforms.Add(Listener.Transform);
		}

		SourceManager.SetListenerTransforms(ListenerTransforms);
	}

	void FMixerDevice::ResetAudioRenderingThreadId()
	{
#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		AudioPlatformThreadId = INDEX_NONE;
		CheckAudioRenderingThread();
#endif
	}

	void FMixerDevice::CheckAudioRenderingThread() const
	{
#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		if (AudioPlatformThreadId == INDEX_NONE)
		{
			AudioPlatformThreadId = FPlatformTLS::GetCurrentThreadId();
		}
		int32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
		AUDIO_MIXER_CHECK(CurrentThreadId == AudioPlatformThreadId);
#endif
	}

	bool FMixerDevice::IsAudioRenderingThread() const
	{
		int32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
		return CurrentThreadId == AudioPlatformThreadId;
	}

	void FMixerDevice::UpdateDeviceDeltaTime()
	{
		DeviceDeltaTime = GetGameDeltaTime();
	}

	void FMixerDevice::GetAudioDeviceList(TArray<FString>& OutAudioDeviceNames) const
	{
		if (AudioMixerPlatform && AudioMixerPlatform->IsInitialized())
		{
			uint32 NumOutputDevices;
			if (AudioMixerPlatform->GetNumOutputDevices(NumOutputDevices))
			{
				for (uint32 i = 0; i < NumOutputDevices; ++i)
				{
					FAudioPlatformDeviceInfo DeviceInfo;
					if (AudioMixerPlatform->GetOutputDeviceInfo(i, DeviceInfo))
					{
						OutAudioDeviceNames.Add(DeviceInfo.Name);
					}
				}
			}
		}
	}

	bool FMixerDevice::InitializeHardware()
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(this);
	
		LLM_SCOPE(ELLMTag::AudioMixer);

		// Log that we're inside the audio mixer
		UE_LOG(LogAudioMixer, Display, TEXT("Initializing audio mixer."));

		if (AudioMixerPlatform && AudioMixerPlatform->InitializeHardware())
		{
			const UAudioSettings* AudioSettings = GetDefault<UAudioSettings>();
			MonoChannelUpmixMethod = AudioSettings->MonoChannelUpmixMethod;
			PanningMethod = AudioSettings->PanningMethod;

			// Set whether we're the main audio mixer
			bIsMainAudioMixer = IsMainAudioDevice();

			AUDIO_MIXER_CHECK(SampleRate != 0.0f);

			AudioMixerPlatform->RegisterDeviceChangedListener();

			// Allow platforms to override the platform settings callback buffer frame size (i.e. restrict to particular values, etc)
			PlatformSettings.CallbackBufferFrameSize = AudioMixerPlatform->GetNumFrames(PlatformSettings.CallbackBufferFrameSize);

			OpenStreamParams.NumBuffers = PlatformSettings.NumBuffers;
			OpenStreamParams.NumFrames = PlatformSettings.CallbackBufferFrameSize;
			OpenStreamParams.OutputDeviceIndex = AUDIO_MIXER_DEFAULT_DEVICE_INDEX; // TODO: Support overriding which audio device user wants to open, not necessarily default.
			OpenStreamParams.SampleRate = SampleRate;
			OpenStreamParams.AudioMixer = this;
			OpenStreamParams.MaxSources = GetMaxSources();

			FString DefaultDeviceName = AudioMixerPlatform->GetDefaultDeviceName();

			// Allow HMD to specify audio device, if one was not specified in settings
			if (DefaultDeviceName.IsEmpty() && FAudioDevice::CanUseVRAudioDevice() && IHeadMountedDisplayModule::IsAvailable())
			{
				DefaultDeviceName = IHeadMountedDisplayModule::Get().GetAudioOutputDevice();
			}

			if (!DefaultDeviceName.IsEmpty())
			{
				uint32 NumOutputDevices = 0;
				AudioMixerPlatform->GetNumOutputDevices(NumOutputDevices);

				for (uint32 i = 0; i < NumOutputDevices; ++i)
				{
					FAudioPlatformDeviceInfo DeviceInfo;
					AudioMixerPlatform->GetOutputDeviceInfo(i, DeviceInfo);

					if (DeviceInfo.Name == DefaultDeviceName || DeviceInfo.DeviceId == DefaultDeviceName)
					{
						OpenStreamParams.OutputDeviceIndex = i;

						// If we're intentionally selecting an audio device (and not just using the default device) then 
						// lets try to restore audio to that device if it's removed and then later is restored
						OpenStreamParams.bRestoreIfRemoved = true;
						break;
					}
				}
			}

			if (AudioMixerPlatform->OpenAudioStream(OpenStreamParams))
			{
				// Get the platform device info we're using
				PlatformInfo = AudioMixerPlatform->GetPlatformDeviceInfo();
				UE_LOG(LogAudioMixer, Display, TEXT("Using Audio Device %s"), *PlatformInfo.Name);

				// Initialize some data that depends on speaker configuration, etc.
				InitializeChannelAzimuthMap(PlatformInfo.NumChannels);

				FSourceManagerInitParams SourceManagerInitParams;
				SourceManagerInitParams.NumSources = GetMaxSources();

				// TODO: Migrate this to project settings properly
				SourceManagerInitParams.NumSourceWorkers = 4;

				SourceManager.Init(SourceManagerInitParams);

				AudioClock = 0.0;
				AudioClockDelta = (double)OpenStreamParams.NumFrames / OpenStreamParams.SampleRate;

				FAudioPluginInitializationParams PluginInitializationParams;
				PluginInitializationParams.NumSources = SourceManagerInitParams.NumSources;
				PluginInitializationParams.SampleRate = SampleRate;
				PluginInitializationParams.BufferLength = OpenStreamParams.NumFrames;
				PluginInitializationParams.AudioDevicePtr = this;

				// Initialize any plugins if they exist
				if (SpatializationPluginInterface.IsValid())
				{
					SpatializationPluginInterface->Initialize(PluginInitializationParams);
				}

				// Create a new ambisonics mixer.
				IAudioSpatializationFactory* SpatializationPluginFactory = AudioPluginUtilities::GetDesiredSpatializationPlugin();
				if (SpatializationPluginFactory != nullptr)
				{
					AmbisonicsMixer = SpatializationPluginFactory->CreateNewAmbisonicsMixer(this);
					if (AmbisonicsMixer.IsValid())
					{
						AmbisonicsMixer->Initialize(PluginInitializationParams);
					}
				}

				if (OcclusionInterface.IsValid())
				{
					OcclusionInterface->Initialize(PluginInitializationParams);
				}

				if (ReverbPluginInterface.IsValid())
				{
					ReverbPluginInterface->Initialize(PluginInitializationParams);
				}

				// Need to set these up before we start the audio stream.
				InitSoundSubmixes();

				AudioMixerPlatform->PostInitializeHardware();

				// Initialize the data used for audio thread sub-frame timing.
				AudioThreadTimingData.StartTime = FPlatformTime::Seconds();
				AudioThreadTimingData.AudioThreadTime = 0.0;
				AudioThreadTimingData.AudioRenderThreadTime = 0.0;

				// Start streaming audio
				return AudioMixerPlatform->StartAudioStream();
			}
		}
		return false;
	}

	void FMixerDevice::FadeIn()
	{
		AudioMixerPlatform->FadeIn();
	}

	void FMixerDevice::FadeOut()
	{
		// In editor builds, we aren't going to fade out the main audio device.
#if WITH_EDITOR
		if (!IsMainAudioDevice())
#endif
		{
			AudioMixerPlatform->FadeOut();
		}
	}

	void FMixerDevice::TeardownHardware()
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(this);

		for (TObjectIterator<USoundSubmix> It; It; ++It)
		{
			UnregisterSoundSubmix(*It);
		}
		
		// reset all the sound effect presets loaded
#if WITH_EDITOR
		for (TObjectIterator<USoundEffectPreset> It; It; ++It)
		{
			USoundEffectPreset* SoundEffectPreset = *It;
			SoundEffectPreset->Init();
		}
#endif

		if (AudioMixerPlatform)
		{
			SourceManager.Update();

			AudioMixerPlatform->UnregisterDeviceChangedListener();
			AudioMixerPlatform->StopAudioStream();
			AudioMixerPlatform->CloseAudioStream();
			AudioMixerPlatform->TeardownHardware();
		}

		// Reset existing submixes if they exist
		MasterSubmixInstances.Reset();
		Submixes.Reset();

		if (AmbisonicsMixer.IsValid())
		{
			AmbisonicsMixer->Shutdown();
		}
	}

	void FMixerDevice::UpdateHardwareTiming()
	{
		// Get the relative audio thread time (from start of audio engine)
		// Add some jitter delta to account for any audio thread timing jitter.
		const double AudioThreadJitterDelta = AudioClockDelta;
		AudioThreadTimingData.AudioThreadTime = FPlatformTime::Seconds() - AudioThreadTimingData.StartTime + AudioThreadJitterDelta;
	}

	void FMixerDevice::UpdateGameThread()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);


	}

	void FMixerDevice::UpdateHardware()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		// If we're in editor, re-query these in case they changed. 
		if (GIsEditor)
		{
			const UAudioSettings* AudioSettings = GetDefault<UAudioSettings>();
			MonoChannelUpmixMethod = AudioSettings->MonoChannelUpmixMethod;
			PanningMethod = AudioSettings->PanningMethod;
		}

		SourceManager.Update();

		AudioMixerPlatform->OnHardwareUpdate();

		if (AudioMixerPlatform->CheckAudioDeviceChange())
		{
			// Get the platform device info we're using
			PlatformInfo = AudioMixerPlatform->GetPlatformDeviceInfo();

			// Initialize some data that depends on speaker configuration, etc.
			InitializeChannelAzimuthMap(PlatformInfo.NumChannels);

			// Update the channel device count in case it changed
			SourceManager.UpdateDeviceChannelCount(PlatformInfo.NumChannels);

			// Audio rendering was suspended in CheckAudioDeviceChange if it changed.
			AudioMixerPlatform->ResumePlaybackOnNewDevice();
		}

		// Loop through any envelope-following submixes and perform any broadcasting of envelope data if needed
		TArray<float> SubmixEnvelopeData;
		for (USoundSubmix* SoundSubmix : EnvelopeFollowingSubmixes)
		{
			if (SoundSubmix)
			{
				// Retrieve the submix instance and the envelope data
				Audio::FMixerSubmixWeakPtr SubmixPtr = GetSubmixInstance(SoundSubmix);
				check(SubmixPtr.IsValid());

				// On the audio thread, do the broadcast.
				FAudioThread::RunCommandOnGameThread([this, SubmixPtr]()
				{
					Audio::FMixerSubmixPtr ThisSubmixPtr = SubmixPtr.Pin();
					if (ThisSubmixPtr.IsValid())
					{
						ThisSubmixPtr->BroadcastEnvelope();
					}
				});
			}
		}

		// Check if the background mute changed state and update the submixes which are enabled to do background muting
		const float CurrentMasterVolume = GetMasterVolume();
		if (!FMath::IsNearlyEqual(PreviousMasterVolume, CurrentMasterVolume))
		{
			PreviousMasterVolume = CurrentMasterVolume;
			bool IsMuted = FMath::IsNearlyZero(CurrentMasterVolume);

			for (TObjectIterator<USoundSubmix> It; It; ++It)
			{
				if (*It && It->bMuteWhenBackgrounded)
				{
					FMixerSubmixPtr SubmixInstance = GetSubmixInstance(*It).Pin();
					if (SubmixInstance.IsValid())
					{
						SubmixInstance->SetBackgroundMuted(IsMuted);
					}
				}
			}
		}
	}

	double FMixerDevice::GetAudioTime() const
	{
		return AudioClock;
	}

	FAudioEffectsManager* FMixerDevice::CreateEffectsManager()
	{
		return new FAudioMixerEffectsManager(this);
	}

	FSoundSource* FMixerDevice::CreateSoundSource()
	{
		return new FMixerSource(this);
	}

	FName FMixerDevice::GetRuntimeFormat(USoundWave* InSoundWave)
	{
		check(AudioMixerPlatform);
		return AudioMixerPlatform->GetRuntimeFormat(InSoundWave);
	}

	bool FMixerDevice::HasCompressedAudioInfoClass(USoundWave* InSoundWave)
	{
		check(InSoundWave);
		check(AudioMixerPlatform);
		return AudioMixerPlatform->HasCompressedAudioInfoClass(InSoundWave);
	}

	bool FMixerDevice::SupportsRealtimeDecompression() const
	{
		return AudioMixerPlatform->SupportsRealtimeDecompression();
	}

	bool FMixerDevice::DisablePCMAudioCaching() const
	{
		return AudioMixerPlatform->DisablePCMAudioCaching();
	}

	class ICompressedAudioInfo* FMixerDevice::CreateCompressedAudioInfo(USoundWave* InSoundWave)
	{
		check(InSoundWave);
		check(AudioMixerPlatform);
		return AudioMixerPlatform->CreateCompressedAudioInfo(InSoundWave);
	}

	bool FMixerDevice::ValidateAPICall(const TCHAR* Function, uint32 ErrorCode)
	{
		return false;
	}

	bool FMixerDevice::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
	{
		if (FAudioDevice::Exec(InWorld, Cmd, Ar))
		{
			return true;
		}

		return false;
	}

	void FMixerDevice::CountBytes(FArchive& InArchive)
	{
		FAudioDevice::CountBytes(InArchive);
	}

	bool FMixerDevice::IsExernalBackgroundSoundActive()
	{
		return false;
	}

	void FMixerDevice::ResumeContext()
	{
        AudioMixerPlatform->ResumeContext();
	}

	void FMixerDevice::SuspendContext()
	{
        AudioMixerPlatform->SuspendContext();
	}

	void FMixerDevice::EnableDebugAudioOutput()
	{
		bDebugOutputEnabled = true;
	}

	bool FMixerDevice::OnProcessAudioStream(AlignedFloatBuffer& Output)
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		// This function could be called in a task manager, which means the thread ID may change between calls.
		ResetAudioRenderingThreadId();

		// Update the audio render thread time at the head of the render
		AudioThreadTimingData.AudioRenderThreadTime = FPlatformTime::Seconds() - AudioThreadTimingData.StartTime;

		// Pump the command queue to the audio render thread
		PumpCommandQueue();

		// Compute the next block of audio in the source manager
		SourceManager.ComputeNextBlockOfSamples();

		FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();

		{
			CSV_SCOPED_TIMING_STAT(Audio, Submixes);

			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			if (MasterSubmixPtr.IsValid())
			{
				// Process the audio output from the master submix
				MasterSubmixPtr->ProcessAudio(ESubmixChannelFormat::Device, Output);
			}
		}

		// Reset stopping sounds and clear their state after submixes have been mixed
		SourceManager.ClearStoppingSounds();

		// Do any debug output performing
		if (bDebugOutputEnabled)
		{
			SineOscTest(Output);
		}

		// Update the audio clock
		AudioClock += AudioClockDelta;

		return true;
	}

	void FMixerDevice::OnAudioStreamShutdown()
	{
		// Make sure the source manager pumps any final commands on shutdown. These allow for cleaning up sources, interfacing with plugins, etc.
		// Because we double buffer our command queues, we call this function twice to ensure all commands are successfully pumped.
		SourceManager.PumpCommandQueue();
		SourceManager.PumpCommandQueue();

		// Make sure we force any pending release data to happen on shutdown
		SourceManager.UpdatePendingReleaseData(true);
	}

	void FMixerDevice::LoadMasterSoundSubmix(EMasterSubmixType::Type InType, const FString& InDefaultName, bool bInDefaultMuteWhenBackgrounded, FSoftObjectPath& InObjectPath)
	{
		check(IsInGameThread());

		const int32 MasterSubmixCount = static_cast<int32>(EMasterSubmixType::Type::Count);
		if(MasterSubmixes.Num() < MasterSubmixCount)
		{
			MasterSubmixes.AddZeroed(MasterSubmixCount - MasterSubmixes.Num());
		}

		if (MasterSubmixInstances.Num() < MasterSubmixCount)
		{
			MasterSubmixInstances.AddZeroed(MasterSubmixCount - MasterSubmixInstances.Num());
		}

		const int32 TypeIndex = static_cast<int32>(InType);
		if (USoundSubmix* OldSubmix = MasterSubmixes[TypeIndex])
		{
			// Don't bother swapping if new path is invalid...
			if (!InObjectPath.IsValid())
			{
				return;
			}

			// or is same object already initialized.
			if (InObjectPath.GetAssetPathString() == OldSubmix->GetPathName())
			{
				return;
			}

			OldSubmix->RemoveFromRoot();
			FMixerSubmixPtr OldSubmixPtr = MasterSubmixInstances[TypeIndex];
			if (OldSubmixPtr.IsValid())
			{
				FMixerSubmixPtr ParentSubmixPtr = MasterSubmixInstances[TypeIndex]->GetParentSubmix().Pin();
				if (ParentSubmixPtr.IsValid())
				{
					ParentSubmixPtr->RemoveChildSubmix(MasterSubmixInstances[TypeIndex]);
				}
			}
		}

		// 1. Try loading from Developer Audio Settings
		USoundSubmix* NewSubmix = Cast<USoundSubmix>(InObjectPath.TryLoad());

		// 2. If Unset or not found, fallback to engine asset
		if (!NewSubmix)
		{
			static const FString EngineSubmixDir = TEXT("/Engine/EngineSounds/Submixes");

			InObjectPath = FString::Printf(TEXT("%s/%s.%s"), *EngineSubmixDir, *InDefaultName, *InDefaultName);
			NewSubmix = Cast<USoundSubmix>(InObjectPath.TryLoad());
			UE_LOG(LogAudioMixer, Display, TEXT("Submix unset or invalid in 'AudioSettings': Using engine asset '%s'"),
				*InDefaultName,
				*InObjectPath.GetAssetPathString());
		}

		// 3. If engine version not found, dynamically spawn and post error
		if (!NewSubmix)
		{
			UE_LOG(LogAudioMixer, Error, TEXT("Failed to load submix from engine asset path '%s'. Creating '%s' as a stub."),
				*InObjectPath.GetAssetPathString(),
				*InDefaultName);

			NewSubmix = NewObject<USoundSubmix>(USoundSubmix::StaticClass(), *InDefaultName);
			// Make the master reverb mute when backgrounded
			NewSubmix->bMuteWhenBackgrounded = bInDefaultMuteWhenBackgrounded;
		}

		check(NewSubmix);
		NewSubmix->AddToRoot();
		MasterSubmixes[TypeIndex] = NewSubmix;

		FMixerSubmixPtr NewMixerSubmix = MakeShared<FMixerSubmix, ESPMode::ThreadSafe>(this);
		MasterSubmixInstances[TypeIndex] = NewMixerSubmix;

		NewMixerSubmix->Init(NewSubmix, false /* bAllowReInit */);
	}

	void FMixerDevice::LoadPluginSoundSubmixes()
	{
		check(IsInGameThread());

		if (IsReverbPluginEnabled() && ReverbPluginInterface)
		{
			USoundSubmix* ReverbPluginSubmix = ReverbPluginInterface->GetSubmix();
			check(ReverbPluginSubmix);
			ReverbPluginSubmix->AddToRoot();

			LoadSoundSubmix(*ReverbPluginSubmix);

			// Plugin must provide valid effect to enable reverb
			FSoundEffectSubmixPtr ReverbPluginEffectSubmix = ReverbPluginInterface->GetEffectSubmix();
			if (ReverbPluginEffectSubmix.IsValid())
			{
				if (USoundEffectPreset* Preset = ReverbPluginEffectSubmix->GetPreset())
				{
					FMixerSubmixPtr ReverbPluginMixerSubmixPtr = GetSubmixInstance(ReverbPluginSubmix).Pin();
					check(ReverbPluginMixerSubmixPtr.IsValid());

					const uint32 ReverbPluginId = Preset->GetUniqueID();
					FMixerSubmixWeakPtr ReverbPluginMixerSubmixWeakPtr = ReverbPluginMixerSubmixPtr;
					AudioRenderThreadCommand([ReverbPluginMixerSubmixWeakPtr, ReverbPluginEffectSubmix, ReverbPluginId]()
					{
						FMixerSubmixPtr PluginSubmixPtr = ReverbPluginMixerSubmixWeakPtr.Pin();
						if (PluginSubmixPtr.IsValid())
						{
							PluginSubmixPtr->AddSoundEffectSubmix(ReverbPluginId, ReverbPluginEffectSubmix);
						}
					});
				}
			}
			else
			{
				UE_LOG(LogAudioMixer, Error, TEXT("Reverb plugin failed to provide valid effect submix.  Plugin audio processing disabled."));
			}
		}
	}

	void FMixerDevice::InitSoundSubmixes()
	{
		if (IsInGameThread())
		{
			bSubmixRegistrationDisabled = true;

			UAudioSettings* AudioSettings = GetMutableDefault<UAudioSettings>();
			check(AudioSettings);

			if (MasterSubmixes.Num() > 0)
			{
				UE_LOG(LogAudioMixer, Display, TEXT("Re-initializing Sound Submixes..."));
			}
			else
			{
				UE_LOG(LogAudioMixer, Display, TEXT("Initializing Sound Submixes..."));
			}

			// 1. Load or reload all sound submixes/instances
			LoadMasterSoundSubmix(EMasterSubmixType::Master, TEXT("MasterSubmixDefault"), false /* DefaultMuteWhenBackgrounded */, AudioSettings->MasterSubmix);
			LoadMasterSoundSubmix(EMasterSubmixType::Reverb, TEXT("MasterReverbSubmixDefault"), true /* DefaultMuteWhenBackgrounded */, AudioSettings->ReverbSubmix);

			if (!DisableSubmixEffectEQCvar)
			{
				LoadMasterSoundSubmix(EMasterSubmixType::EQ, TEXT("MasterEQSubmixDefault"), false /* DefaultMuteWhenBackgrounded */, AudioSettings->EQSubmix);
			}

			LoadMasterSoundSubmix(EMasterSubmixType::Ambisonics, TEXT("MasterAmbisonicSubmixDefault"), false /* DefaultMuteWhenBackgrounded */, AudioSettings->AmbisonicSubmix);
			LoadPluginSoundSubmixes();

			for (TObjectIterator<USoundSubmix> It; It; ++It)
			{
				USoundSubmix* SubmixToLoad = *It;
				check(SubmixToLoad);

				if (!IsMasterSubmixType(SubmixToLoad))
				{
					LoadSoundSubmix(*SubmixToLoad);
				}
			}
			bSubmixRegistrationDisabled = false;
		}

		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.InitSoundSubmixes"), STAT_InitSoundSubmixes, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, InitSubmix);
				InitSoundSubmixes();
			}, GET_STATID(STAT_InitSoundSubmixes));
			return;
		}

		for (int32 i = 0; i < static_cast<int32>(EMasterSubmixType::Count); ++i)
		{
			USoundSubmix* SoundSubmix = MasterSubmixes[i];
			check(SoundSubmix);
			FMixerSubmixPtr& MasterSubmixInstance = MasterSubmixInstances[i];

			if (SoundSubmix != MasterSubmixes[static_cast<int32>(EMasterSubmixType::Master)])
			{
				RebuildSubmixLinks(*SoundSubmix, MasterSubmixInstance);
			}
		};

		for (TPair<USoundSubmix*, FMixerSubmixPtr>& Entry : Submixes)
		{
			USoundSubmix* SoundSubmix = Entry.Key;
			check(SoundSubmix);
			FMixerSubmixPtr& SubmixInstance = Entry.Value;
			RebuildSubmixLinks(*SoundSubmix, SubmixInstance);
		}
	}

	void FMixerDevice::RebuildSubmixLinks(USoundSubmix& SoundSubmix, FMixerSubmixPtr& SubmixInstance)
	{
		// Setup up the submix instance's parent and add the submix instance as a child
		FMixerSubmixPtr ParentSubmixInstance = SoundSubmix.ParentSubmix
			? GetSubmixInstance(SoundSubmix.ParentSubmix).Pin()
			: GetMasterSubmix().Pin();

		check(ParentSubmixInstance.IsValid());

		SubmixInstance->SetParentSubmix(ParentSubmixInstance);
		ParentSubmixInstance->AddChildSubmix(SubmixInstance);
	}

 	FAudioPlatformSettings FMixerDevice::GetPlatformSettings() const
 	{
		FAudioPlatformSettings Settings = AudioMixerPlatform->GetPlatformSettings();

		UE_LOG(LogAudioMixer, Display, TEXT("Audio Mixer Platform Settings:"));
		UE_LOG(LogAudioMixer, Display, TEXT("	Sample Rate:						  %d"), Settings.SampleRate);
		UE_LOG(LogAudioMixer, Display, TEXT("	Callback Buffer Frame Size Requested: %d"), Settings.CallbackBufferFrameSize);
		UE_LOG(LogAudioMixer, Display, TEXT("	Callback Buffer Frame Size To Use:	  %d"), AudioMixerPlatform->GetNumFrames(Settings.CallbackBufferFrameSize));
		UE_LOG(LogAudioMixer, Display, TEXT("	Number of buffers to queue:			  %d"), Settings.NumBuffers);
		UE_LOG(LogAudioMixer, Display, TEXT("	Max Channels (voices):				  %d"), Settings.MaxChannels);
		UE_LOG(LogAudioMixer, Display, TEXT("	Number of Async Source Workers:		  %d"), Settings.NumSourceWorkers);

 		return Settings;
 	}

	FMixerSubmixWeakPtr FMixerDevice::GetMasterSubmix()
	{
		return MasterSubmixInstances[EMasterSubmixType::Master];
	}

	FMixerSubmixWeakPtr FMixerDevice::GetMasterReverbSubmix()
	{
		return MasterSubmixInstances[EMasterSubmixType::Reverb];
	}

	FMixerSubmixWeakPtr FMixerDevice::GetMasterEQSubmix()
	{
		return MasterSubmixInstances[EMasterSubmixType::EQ];
	}

	FMixerSubmixWeakPtr FMixerDevice::GetMasterAmbisonicsSubmix()
	{
		return MasterSubmixInstances[EMasterSubmixType::Ambisonics];
	}

	void FMixerDevice::AddMasterSubmixEffect(uint32 SubmixEffectId, FSoundEffectSubmixPtr SoundEffectSubmix)
	{
		AudioRenderThreadCommand([this, SubmixEffectId, SoundEffectSubmix]()
		{
			MasterSubmixInstances[EMasterSubmixType::Master]->AddSoundEffectSubmix(SubmixEffectId, SoundEffectSubmix);
		});
	}

	void FMixerDevice::RemoveMasterSubmixEffect(uint32 SubmixEffectId)
	{
		AudioRenderThreadCommand([this, SubmixEffectId]()
		{
			MasterSubmixInstances[EMasterSubmixType::Master]->RemoveSoundEffectSubmix(SubmixEffectId);
		});
	}

	void FMixerDevice::ClearMasterSubmixEffects()
	{
		AudioRenderThreadCommand([this]()
		{
			MasterSubmixInstances[EMasterSubmixType::Master]->ClearSoundEffectSubmixes();
		});
	}

	void FMixerDevice::UpdateModulationControls(const uint32 InSourceId, const FSoundModulationControls& InControls)
	{
		SourceManager.UpdateModulationControls(InSourceId, InControls);
	}

	void FMixerDevice::UpdateSourceEffectChain(const uint32 SourceEffectChainId, const TArray<FSourceEffectChainEntry>& SourceEffectChain, const bool bPlayEffectChainTails)
	{
		TArray<FSourceEffectChainEntry>* ExistingOverride = SourceEffectChainOverrides.Find(SourceEffectChainId);
		if (ExistingOverride)
		{
			*ExistingOverride = SourceEffectChain;
		}
		else
		{
			SourceEffectChainOverrides.Add(SourceEffectChainId, SourceEffectChain);
		}

		SourceManager.UpdateSourceEffectChain(SourceEffectChainId, SourceEffectChain, bPlayEffectChainTails);
	}

	void FMixerDevice::UpdateSubmixProperties(USoundSubmix* InSoundSubmix)
	{
		check(InSoundSubmix);

#if WITH_EDITOR
		check(IsInAudioThread());

		FMixerSubmixPtr MixerSubmix = GetSubmixInstance(InSoundSubmix).Pin();
		if (MixerSubmix.IsValid())
		{
			const float NewVolume = InSoundSubmix->OutputVolume;
			AudioRenderThreadCommand([MixerSubmix, NewVolume]()
			{
				MixerSubmix->SetOutputVolume(NewVolume);
			});
		}
#endif // WITH_EDITOR
	}

	void FMixerDevice::SetSubmixOutputVolume(USoundSubmix* InSoundSubmix, float NewVolume)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetSubmixOutputVolume"), STAT_AudioSetSubmixOutputVolume, STATGROUP_AudioThreadCommands);

			FMixerDevice* MixerDevice = this;
			FAudioThread::RunCommandOnAudioThread([MixerDevice, InSoundSubmix, NewVolume]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, SetSubmixOutputVolume);
				MixerDevice->SetSubmixOutputVolume(InSoundSubmix, NewVolume);
			}, GET_STATID(STAT_AudioSetSubmixOutputVolume));
			return;
		}

		FMixerSubmixPtr MixerSubmix = GetSubmixInstance(InSoundSubmix).Pin();
		if (MixerSubmix.IsValid())
		{
			AudioRenderThreadCommand([MixerSubmix, NewVolume]()
			{
				MixerSubmix->SetDynamicOutputVolume(NewVolume);
			});
		}
	}

	bool FMixerDevice::GetCurrentSourceEffectChain(const uint32 SourceEffectChainId, TArray<FSourceEffectChainEntry>& OutCurrentSourceEffectChainEntries)
	{
		TArray<FSourceEffectChainEntry>* ExistingOverride = SourceEffectChainOverrides.Find(SourceEffectChainId);
		if (ExistingOverride)
		{
			OutCurrentSourceEffectChainEntries = *ExistingOverride;
			return true;
		}
		return false;
	}

	void FMixerDevice::AudioRenderThreadCommand(TFunction<void()> Command)
	{
		CommandQueue.Enqueue(MoveTemp(Command));
	}

	void FMixerDevice::PumpCommandQueue()
	{
		// Execute the pushed lambda functions
		TFunction<void()> Command;
		while (CommandQueue.Dequeue(Command))
		{
			Command();
		}
	}

	void FMixerDevice::FlushAudioRenderingCommands(bool bPumpSynchronously)
	{
		if (IsInitialized() && (FPlatformProcess::SupportsMultithreading() && !AudioMixerPlatform->IsNonRealtime()))
		{
			SourceManager.FlushCommandQueue(bPumpSynchronously);
		}
		else if (AudioMixerPlatform->IsNonRealtime())
		{
			SourceManager.FlushCommandQueue(true);
		}
		else
		{
			// Pump the audio device's command queue
			PumpCommandQueue();

			// And also directly pump the source manager command queue
			SourceManager.PumpCommandQueue();
			SourceManager.PumpCommandQueue();

			SourceManager.UpdatePendingReleaseData(true);
		}
	}

	bool FMixerDevice::IsMasterSubmixType(USoundSubmix* InSubmix) const
	{
		for (int32 i = 0; i < EMasterSubmixType::Count; ++i)
		{
			if (InSubmix == MasterSubmixes[i])
			{
				return true;
			}
		}
		return false;
	}

	FMixerSubmixPtr FMixerDevice::GetMasterSubmixInstance(USoundSubmix* InSubmix)
	{
		check(MasterSubmixes.Num() == EMasterSubmixType::Count);
		for (int32 i = 0; i < EMasterSubmixType::Count; ++i)
		{
			if (InSubmix == MasterSubmixes[i])
			{
				return MasterSubmixInstances[i];
			}
		}
		return nullptr;
	}

	void FMixerDevice::RegisterSoundSubmix(USoundSubmix* InSoundSubmix, bool bInit)
	{
		if (!InSoundSubmix || bSubmixRegistrationDisabled || IsMasterSubmixType(InSoundSubmix))
		{
			return;
		}

		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.RegisterSoundSubmix"), STAT_AudioRegisterSoundSubmix, STATGROUP_AudioThreadCommands);

			FMixerDevice* MixerDevice = this;
			FAudioThread::RunCommandOnAudioThread([MixerDevice, InSoundSubmix]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, RegisterSubmix);
				MixerDevice->RegisterSoundSubmix(InSoundSubmix);
			}, GET_STATID(STAT_AudioRegisterSoundSubmix));
			return;
		}

		LoadSoundSubmix(*InSoundSubmix);
		FMixerSubmixPtr SubmixPtr = GetSubmixInstance(InSoundSubmix).Pin();
		if (bInit)
		{
			SubmixPtr->Init(InSoundSubmix, true /* BAllowReInit */);
		}

		RebuildSubmixLinks(*InSoundSubmix, SubmixPtr);
	}

	void FMixerDevice::LoadSoundSubmix(USoundSubmix& InSoundSubmix)
	{
		// Ensure submix not already registered by first checking master submixes and then additional mixes.
		FMixerSubmixPtr MixerSubmix = GetSubmixInstance(&InSoundSubmix).Pin();

		// If submix not already found, register it.
		if (!MixerSubmix.IsValid())
		{
			MixerSubmix = MakeShared<FMixerSubmix, ESPMode::ThreadSafe>(this);
			Submixes.Add(&InSoundSubmix, MixerSubmix);
		}

		MixerSubmix->Init(&InSoundSubmix, false /* bAllowReInit */);
	}

	void FMixerDevice::UnregisterSoundSubmix(USoundSubmix* InSoundSubmix)
	{
		if (!InSoundSubmix || bSubmixRegistrationDisabled || IsMasterSubmixType(InSoundSubmix))
		{
			return;
		}

		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.UnregisterSoundSubmix"), STAT_AudioUnregisterSoundSubmix, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this, InSoundSubmix]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, UnregisterSubmix);
				UnregisterSoundSubmix(InSoundSubmix);
			}, GET_STATID(STAT_AudioUnregisterSoundSubmix));
			return;
		}

		UnloadSoundSubmix(*InSoundSubmix);
	}

	void FMixerDevice::UnloadSoundSubmix(USoundSubmix& InSoundSubmix)
	{
		check(IsInAudioThread());

		FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
		FMixerSubmixPtr ParentSubmixInstance = InSoundSubmix.ParentSubmix
			? GetSubmixInstance(InSoundSubmix.ParentSubmix).Pin()
			: MasterSubmix.Pin();

		if (ParentSubmixInstance.IsValid())
		{
			ParentSubmixInstance->RemoveChildSubmix(GetSubmixInstance(&InSoundSubmix));
		}

		for (USoundSubmix* ChildSubmix : InSoundSubmix.ChildSubmixes)
		{
			FMixerSubmixPtr ChildSubmixPtr = GetSubmixInstance(ChildSubmix).Pin();
			if (ChildSubmixPtr.IsValid())
			{
				ChildSubmixPtr->SetParentSubmix(ParentSubmixInstance.IsValid()
					? ParentSubmixInstance
					: MasterSubmix);
			}
		}

		Submixes.Remove(&InSoundSubmix);
	}

	void FMixerDevice::InitSoundEffectPresets()
	{
#if WITH_EDITOR
		IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>("AudioEditor");
		AudioEditorModule->RegisterEffectPresetAssetActions();
#endif
	}

	FMixerSubmixWeakPtr FMixerDevice::GetSubmixInstance(USoundSubmix* InSoundSubmix)
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		FMixerSubmixPtr MixerSubmix = GetMasterSubmixInstance(InSoundSubmix);
		if (MixerSubmix.IsValid())
		{
			return MixerSubmix;
		}

		return Submixes.FindRef(InSoundSubmix);
	}

	FMixerSourceVoice* FMixerDevice::GetMixerSourceVoice()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		FMixerSourceVoice* Voice = nullptr;
		if (!SourceVoices.Dequeue(Voice))
		{
			Voice = new FMixerSourceVoice();
		}

		Voice->Reset(this);
		return Voice;
	}

	void FMixerDevice::ReleaseMixerSourceVoice(FMixerSourceVoice* InSourceVoice)
	{
		SourceVoices.Enqueue(InSourceVoice);
	}

	int32 FMixerDevice::GetNumSources() const
	{
		return Sources.Num();
	}

	int32 FMixerDevice::GetNumActiveSources() const
	{
		return SourceManager.GetNumActiveSources();
	}

	void FMixerDevice::Get3DChannelMap(const ESubmixChannelFormat InSubmixType, const FWaveInstance* InWaveInstance, float EmitterAzimith, float NormalizedOmniRadius, Audio::AlignedFloatBuffer& OutChannelMap)
	{
		// If we're center-channel only, then no need for spatial calculations, but need to build a channel map
		if (InWaveInstance->bCenterChannelOnly)
		{
			int32 NumOutputChannels = GetNumChannelsForSubmixFormat(InSubmixType);
			const TArray<EAudioMixerChannel::Type>& ChannelArray = GetChannelArrayForSubmixChannelFormat(InSubmixType);

			// If we are only spatializing to stereo output
			if (NumOutputChannels == 2)
			{
				// Equal volume in left + right channel with equal power panning
				static const float Pan = 1.0f / FMath::Sqrt(2.0f);
				OutChannelMap.Add(Pan);
				OutChannelMap.Add(Pan);
			}
			else
			{
				for (EAudioMixerChannel::Type Channel : ChannelArray)
				{
					float Pan = (Channel == EAudioMixerChannel::FrontCenter) ? 1.0f : 0.0f;
					OutChannelMap.Add(Pan);
				}
			}

			return;
		}

		float Azimuth = EmitterAzimith;

		const FChannelPositionInfo* PrevChannelInfo = nullptr;
		const FChannelPositionInfo* NextChannelInfo = nullptr;

		const TArray<FChannelPositionInfo>* CurrentChannelAzimuthPositionsPtr = ChannelAzimuthPositions.Find(InSubmixType);
		const TArray<FChannelPositionInfo>& CurrentChannelAzimuthPositions = *CurrentChannelAzimuthPositionsPtr;

		for (int32 i = 0; i < CurrentChannelAzimuthPositions.Num(); ++i)
		{
			const FChannelPositionInfo& ChannelPositionInfo = CurrentChannelAzimuthPositions[i];

			if (Azimuth <= ChannelPositionInfo.Azimuth)
			{
				NextChannelInfo = &CurrentChannelAzimuthPositions[i];

				int32 PrevIndex = i - 1;
				if (PrevIndex < 0)
				{
					PrevIndex = CurrentChannelAzimuthPositions.Num() - 1;
				}

				PrevChannelInfo = &CurrentChannelAzimuthPositions[PrevIndex];
				break;
			}
		}

		// If we didn't find anything, that means our azimuth position is at the top of the mapping
		if (PrevChannelInfo == nullptr)
		{
			PrevChannelInfo = &CurrentChannelAzimuthPositions[CurrentChannelAzimuthPositions.Num() - 1];
			NextChannelInfo = &CurrentChannelAzimuthPositions[0];
			AUDIO_MIXER_CHECK(PrevChannelInfo != NextChannelInfo);
		}

		float NextChannelAzimuth = NextChannelInfo->Azimuth;
		float PrevChannelAzimuth = PrevChannelInfo->Azimuth;

		if (NextChannelAzimuth < PrevChannelAzimuth)
		{
			NextChannelAzimuth += 360.0f;
		}

		if (Azimuth < PrevChannelAzimuth)
		{
			Azimuth += 360.0f;
		}

		AUDIO_MIXER_CHECK(NextChannelAzimuth > PrevChannelAzimuth);
		AUDIO_MIXER_CHECK(Azimuth > PrevChannelAzimuth);
		float Fraction = (Azimuth - PrevChannelAzimuth) / (NextChannelAzimuth - PrevChannelAzimuth);
		AUDIO_MIXER_CHECK(Fraction >= 0.0f && Fraction <= 1.0f);

		// Compute the panning values using equal-power panning law
		float PrevChannelPan; 
		float NextChannelPan;

		if (PanningMethod == EPanningMethod::EqualPower)
		{
			FMath::SinCos(&NextChannelPan, &PrevChannelPan, Fraction * 0.5f * PI);

			// Note that SinCos can return values slightly greater than 1.0 when very close to PI/2
			NextChannelPan = FMath::Clamp(NextChannelPan, 0.0f, 1.0f);
			PrevChannelPan = FMath::Clamp(PrevChannelPan, 0.0f, 1.0f);
		}
		else
		{
			NextChannelPan = Fraction;
			PrevChannelPan = 1.0f - Fraction;
		}

		float NormalizedOmniRadSquared = NormalizedOmniRadius * NormalizedOmniRadius;
		float OmniAmount = 0.0f;

		if (NormalizedOmniRadSquared > 1.0f)
		{
			OmniAmount = 1.0f - 1.0f / NormalizedOmniRadSquared;
		}

		// Build the output channel map based on the current platform device output channel array 

		int32 NumSpatialChannels = CurrentChannelAzimuthPositions.Num();
		if (CurrentChannelAzimuthPositions.Num() > 4)
		{
			NumSpatialChannels--;
		}
		float OmniPanFactor = 1.0f / NumSpatialChannels;

		float DefaultEffectivePan = !OmniAmount ? 0.0f : FMath::Lerp(0.0f, OmniPanFactor, OmniAmount);
		const TArray<EAudioMixerChannel::Type>& ChannelArray = GetChannelArrayForSubmixChannelFormat(InSubmixType);

		for (EAudioMixerChannel::Type Channel : ChannelArray)
		{
			float EffectivePan = DefaultEffectivePan;

			// Check for manual channel mapping parameters (LFE and Front Center)
			if (Channel == EAudioMixerChannel::LowFrequency)
			{
				EffectivePan = InWaveInstance->LFEBleed;
			}
			else if (Channel == PrevChannelInfo->Channel)
			{
				EffectivePan = !OmniAmount ? PrevChannelPan : FMath::Lerp(PrevChannelPan, OmniPanFactor, OmniAmount);
			}
			else if (Channel == NextChannelInfo->Channel)
			{
				EffectivePan = !OmniAmount ? NextChannelPan : FMath::Lerp(NextChannelPan, OmniPanFactor, OmniAmount);
			}

			if (Channel == EAudioMixerChannel::FrontCenter)
			{
				EffectivePan = FMath::Max(InWaveInstance->VoiceCenterChannelVolume, EffectivePan);
			}

			AUDIO_MIXER_CHECK(EffectivePan >= 0.0f && EffectivePan <= 1.0f);
			OutChannelMap.Add(EffectivePan);
		}
	}

	uint32 FMixerDevice::GetNewUniqueAmbisonicsStreamID()
	{
		static uint32 AmbisonicsStreamIDCounter = 0;
		return ++AmbisonicsStreamIDCounter;
	}

	const TArray<FTransform>* FMixerDevice::GetListenerTransforms()
	{
		return SourceManager.GetListenerTransforms();
	}

	void FMixerDevice::StartRecording(USoundSubmix* InSubmix, float ExpectedRecordingDuration)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.PauseRecording"), STAT_StartRecording, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this, InSubmix, ExpectedRecordingDuration]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, StartRecording);
				StartRecording(InSubmix, ExpectedRecordingDuration);
			}, GET_STATID(STAT_StartRecording));
			return;
		}

		// if we can find the submix here, record that submix. Otherwise, just record the master submix.
		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			FoundSubmix->OnStartRecordingOutput(ExpectedRecordingDuration);
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			MasterSubmixPtr->OnStartRecordingOutput(ExpectedRecordingDuration);
		}
	}

	Audio::AlignedFloatBuffer& FMixerDevice::StopRecording(USoundSubmix* InSubmix, float& OutNumChannels, float& OutSampleRate)
	{
		// if we can find the submix here, record that submix. Otherwise, just record the master submix.
		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			return FoundSubmix->OnStopRecordingOutput(OutNumChannels, OutSampleRate);
		}
		else
		{
			FMixerSubmixPtr MasterSubmixPtr = GetMasterSubmix().Pin();
			check(MasterSubmixPtr.IsValid());

			return MasterSubmixPtr->OnStopRecordingOutput(OutNumChannels, OutSampleRate);
		}
	}

	void FMixerDevice::PauseRecording(USoundSubmix* InSubmix)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.PauseRecording"), STAT_PauseRecording, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this, InSubmix]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, PauseRecording);
				PauseRecording(InSubmix);
			}, GET_STATID(STAT_PauseRecording));
			return;
		}

		// if we can find the submix here, pause that submix. Otherwise, just pause the master submix.
		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			FoundSubmix->PauseRecordingOutput();
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			MasterSubmixPtr->PauseRecordingOutput();
		}
	}

	void FMixerDevice::ResumeRecording(USoundSubmix* InSubmix)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.ResumeRecording"), STAT_ResumeRecording, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this, InSubmix]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, ResumeRecording);
				ResumeRecording(InSubmix);
			}, GET_STATID(STAT_ResumeRecording));
			return;
		}

		// if we can find the submix here, resume that submix. Otherwise, just resume the master submix.
		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			FoundSubmix->ResumeRecordingOutput();
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			MasterSubmixPtr->ResumeRecordingOutput();
		}
	}

	void FMixerDevice::StartEnvelopeFollowing(USoundSubmix* InSubmix)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.StartEnvelopeFollowing"), STAT_StartEnvelopeFollowing, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this, InSubmix]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, StartEnvelopeFollowing);
				StartEnvelopeFollowing(InSubmix);
			}, GET_STATID(STAT_StartEnvelopeFollowing));
			return;
		}

		// if we can find the submix here, record that submix. Otherwise, just record the master submix.
		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			FoundSubmix->StartEnvelopeFollowing(InSubmix->EnvelopeFollowerAttackTime, InSubmix->EnvelopeFollowerReleaseTime);
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			MasterSubmixPtr->StartEnvelopeFollowing(InSubmix->EnvelopeFollowerAttackTime, InSubmix->EnvelopeFollowerReleaseTime);
		}

		EnvelopeFollowingSubmixes.AddUnique(InSubmix);
	}

	void FMixerDevice::StopEnvelopeFollowing(USoundSubmix* InSubmix)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.StopEnvelopeFollowing"), STAT_StopEnvelopeFollowing, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this, InSubmix]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, StopEnvelopeFollowing);
				StopEnvelopeFollowing(InSubmix);
			}, GET_STATID(STAT_StopEnvelopeFollowing));
			return;
		}

		// if we can find the submix here, record that submix. Otherwise, just record the master submix.
		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			FoundSubmix->StopEnvelopeFollowing();
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			MasterSubmixPtr->StopEnvelopeFollowing();
		}

		EnvelopeFollowingSubmixes.RemoveSingleSwap(InSubmix);
	}

	void FMixerDevice::AddEnvelopeFollowerDelegate(USoundSubmix* InSubmix, const FOnSubmixEnvelopeBP& OnSubmixEnvelopeBP)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.AddEnvelopeFollowerDelegate"), STAT_AddEnvelopeFollowerDelegate, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this, InSubmix, OnSubmixEnvelopeBP]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, AddEnvelopeFollowerDelegate);
				AddEnvelopeFollowerDelegate(InSubmix, OnSubmixEnvelopeBP);
			}, GET_STATID(STAT_AddEnvelopeFollowerDelegate));
			return;
		}

		// if we can find the submix here, record that submix. Otherwise, just record the master submix.
		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			FoundSubmix->AddEnvelopeFollowerDelegate(OnSubmixEnvelopeBP);
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			MasterSubmixPtr->AddEnvelopeFollowerDelegate(OnSubmixEnvelopeBP);
		}
	}


	void FMixerDevice::StartSpectrumAnalysis(USoundSubmix* InSubmix, const Audio::FSpectrumAnalyzerSettings& InSettings)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.StartSpectrumAnalysis"), STAT_StartSpectrumAnalysis, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this, InSubmix, InSettings]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, StartSpectrumAnalysis);
				StartSpectrumAnalysis(InSubmix, InSettings);
			}, GET_STATID(STAT_StartSpectrumAnalysis));
			return;
		}

		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			FoundSubmix->StartSpectrumAnalysis(InSettings);
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			MasterSubmixPtr->StartSpectrumAnalysis(InSettings);
		}
	}

	void FMixerDevice::StopSpectrumAnalysis(USoundSubmix* InSubmix)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.StopSpectrumAnalysis"), STAT_StopSpectrumAnalysis, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this, InSubmix]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, StopSpectrumAnalysis);
				StopSpectrumAnalysis(InSubmix);
			}, GET_STATID(STAT_StopSpectrumAnalysis));
			return;
		}

		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			FoundSubmix->StopSpectrumAnalysis();
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			MasterSubmixPtr->StopSpectrumAnalysis();
		}
	}

	void FMixerDevice::GetMagnitudesForFrequencies(USoundSubmix* InSubmix, const TArray<float>& InFrequencies, TArray<float>& OutMagnitudes)
	{
		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			FoundSubmix->GetMagnitudeForFrequencies(InFrequencies, OutMagnitudes);
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			MasterSubmixPtr->GetMagnitudeForFrequencies(InFrequencies, OutMagnitudes);
		}
	}

	void FMixerDevice::GetPhasesForFrequencies(USoundSubmix* InSubmix, const TArray<float>& InFrequencies, TArray<float>& OutPhases)
	{
		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			FoundSubmix->GetPhaseForFrequencies(InFrequencies, OutPhases);
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			MasterSubmixPtr->GetPhaseForFrequencies(InFrequencies, OutPhases);
		}
	}

	void FMixerDevice::RegisterSubmixBufferListener(ISubmixBufferListener* InSubmixBufferListener, USoundSubmix* InSubmix)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.RegisterSubmixBufferListener"), STAT_RegisterSubmixBufferListener, STATGROUP_AudioThreadCommands);
			auto AudioThreadCommand = [this, InSubmixBufferListener, InSubmix]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, StopSpectrumAnalysis);
				RegisterSubmixBufferListener(InSubmixBufferListener, InSubmix);
			};

			if (IsInGameThread())
			{
				FAudioThread::RunCommandOnAudioThread(AudioThreadCommand, GET_STATID(STAT_RegisterSubmixBufferListener));
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, [AudioThreadCommand]() {
					FAudioThread::RunCommandOnAudioThread(AudioThreadCommand, GET_STATID(STAT_RegisterSubmixBufferListener));
				});
			}
			return;
		}

		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			return FoundSubmix->RegisterBufferListener(InSubmixBufferListener);
		}
		else
		{
			FMixerSubmixWeakPtr MasterSubmix = GetMasterSubmix();
			FMixerSubmixPtr MasterSubmixPtr = MasterSubmix.Pin();
			check(MasterSubmixPtr.IsValid());

			return MasterSubmixPtr->RegisterBufferListener(InSubmixBufferListener);
		}
	}

	void FMixerDevice::UnregisterSubmixBufferListener(ISubmixBufferListener* InSubmixBufferListener, USoundSubmix* InSubmix)
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.UnregisterSubmixBufferListener"), STAT_UnregisterSubmixBufferListener, STATGROUP_AudioThreadCommands);

			auto AudioThreadCommand = [this, InSubmixBufferListener, InSubmix]()
			{
				CSV_SCOPED_TIMING_STAT(Audio, UnregisterSubmixBufferListener);
				UnregisterSubmixBufferListener(InSubmixBufferListener, InSubmix);
			};

			if (IsInGameThread())
			{
				FAudioThread::RunCommandOnAudioThread(AudioThreadCommand, GET_STATID(STAT_UnregisterSubmixBufferListener));
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, [AudioThreadCommand]() {
					FAudioThread::RunCommandOnAudioThread(AudioThreadCommand, GET_STATID(STAT_UnregisterSubmixBufferListener));
				});
			}

			return;
		}

		FMixerSubmixPtr FoundSubmix = GetSubmixInstance(InSubmix).Pin();
		if (FoundSubmix.IsValid())
		{
			return FoundSubmix->UnregisterBufferListener(InSubmixBufferListener);
		}
		else
		{
			FMixerSubmixPtr MasterSubmixPtr = GetMasterSubmix().Pin();
			check(MasterSubmixPtr.IsValid());

			return MasterSubmixPtr->UnregisterBufferListener(InSubmixBufferListener);
		}
	}

	int32 FMixerDevice::GetDeviceSampleRate() const
	{
		return SampleRate;
	}

	int32 FMixerDevice::GetDeviceOutputChannels() const
	{
		return PlatformInfo.NumChannels;
	}

	FMixerSourceManager* FMixerDevice::GetSourceManager()
	{
		return &SourceManager;
	}

	bool FMixerDevice::IsMainAudioDevice() const
	{
		bool bIsMain = (this == GEngine->GetMainAudioDevice());
		return bIsMain;
	}

	void FMixerDevice::WhiteNoiseTest(AlignedFloatBuffer& Output)
	{
		const int32 NumFrames = OpenStreamParams.NumFrames;
		const int32 NumChannels = PlatformInfo.NumChannels;

		static FWhiteNoise WhiteNoise(0.2f);

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
			{
				int32 Index = FrameIndex * NumChannels + ChannelIndex;
				Output[Index] += WhiteNoise.Generate();
			}
		}
	}

	void FMixerDevice::SineOscTest(AlignedFloatBuffer& Output)
	{
		const int32 NumFrames = OpenStreamParams.NumFrames;
		const int32 NumChannels = PlatformInfo.NumChannels;

		check(NumChannels > 0);

		static FSineOsc SineOscLeft(PlatformInfo.SampleRate, 440.0f, 0.2f);
		static FSineOsc SineOscRight(PlatformInfo.SampleRate, 220.0f, 0.2f);

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			int32 Index = FrameIndex * NumChannels;

			Output[Index] += SineOscLeft.ProcessAudio();

			if (NumChannels > 1)
			{
				Output[Index + 1] += SineOscRight.ProcessAudio();
			}
		}
	}

}
