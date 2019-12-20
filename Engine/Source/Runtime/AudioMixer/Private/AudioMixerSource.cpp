// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioMixerSource.h"
#include "AudioMixerSourceBuffer.h"
#include "ActiveSound.h"
#include "AudioMixerSourceBuffer.h"
#include "AudioMixerDevice.h"
#include "AudioMixerSourceVoice.h"
#include "ContentStreaming.h"
#include "IAudioExtensionPlugin.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Sound/AudioSettings.h"

// Link to "Audio" profiling category
CSV_DECLARE_CATEGORY_MODULE_EXTERN(AUDIOMIXERCORE_API, Audio);

static int32 UseListenerOverrideForSpreadCVar = 0;
FAutoConsoleVariableRef CVarUseListenerOverrideForSpread(
	TEXT("au.UseListenerOverrideForSpread"),
	UseListenerOverrideForSpreadCVar,
	TEXT("Zero attenuation override distance stereo panning\n")
	TEXT("0: Use actual distance, 1: use listener override"),
	ECVF_Default);


namespace Audio
{
	FMixerSource::FMixerSource(FAudioDevice* InAudioDevice)
		: FSoundSource(InAudioDevice)
		, MixerDevice(static_cast<FMixerDevice*>(InAudioDevice))
		, MixerBuffer(nullptr)
		, MixerSourceVoice(nullptr)
		, PreviousAzimuth(-1.0f)
		, PreviousPlaybackPercent(0.0f)
		, InitializationState(EMixerSourceInitializationState::NotInitialized)
		, bPlayedCachedBuffer(false)
		, bPlaying(false)
		, bLoopCallback(false)
		, bIsDone(false)
		, bIsEffectTailsDone(false)
		, bIsPlayingEffectTails(false)
		, bEditorWarnedChangedSpatialization(false)
		, bUsingHRTFSpatialization(false)
		, bIs3D(false)
		, bDebugMode(false)
		, bIsVorbis(false)
		, bIsStoppingVoicesEnabled(InAudioDevice->IsStoppingVoicesEnabled())
		, bSendingAudioToBuses(false)
	{
	}

	FMixerSource::~FMixerSource()
	{
		FreeResources();
	}

	bool FMixerSource::Init(FWaveInstance* InWaveInstance)
	{
		AUDIO_MIXER_CHECK(MixerBuffer);
		AUDIO_MIXER_CHECK(MixerBuffer->IsRealTimeSourceReady());

		// We've already been passed the wave instance in PrepareForInitialization, make sure we have the same one
		AUDIO_MIXER_CHECK(WaveInstance && WaveInstance == InWaveInstance);

		LLM_SCOPE(ELLMTag::AudioMixer);

		FSoundSource::InitCommon();

		check(WaveInstance->WaveData);

		if (WaveInstance->WaveData->NumChannels == 0)
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Soundwave %s has invalid compressed data."), *(WaveInstance->WaveData->GetName()));
			FreeResources();
			return false;
		}

		// Get the number of frames before creating the buffer
		int32 NumFrames = INDEX_NONE;
		if (WaveInstance->WaveData->DecompressionType != DTYPE_Procedural)
		{
			check(!InWaveInstance->WaveData->RawPCMData || InWaveInstance->WaveData->RawPCMDataSize);
			const int32 NumBytes = WaveInstance->WaveData->RawPCMDataSize;
			if (WaveInstance->WaveData->NumChannels > 0)
			{
				NumFrames = NumBytes / (WaveInstance->WaveData->NumChannels * sizeof(int16));
			}
		}

		// Unfortunately, we need to know if this is a vorbis source since channel maps are different for 5.1 vorbis files
		bIsVorbis = WaveInstance->WaveData->bDecompressedFromOgg;

		bIsStoppingVoicesEnabled = AudioDevice->IsStoppingVoicesEnabled();

		bIsStopping = false;
		bIsEffectTailsDone = true;
		bIsDone = false;

		FSoundBuffer* SoundBuffer = static_cast<FSoundBuffer*>(MixerBuffer);
		if (SoundBuffer->NumChannels > 0)
		{
			CSV_SCOPED_TIMING_STAT(Audio, InitSources);

			AUDIO_MIXER_CHECK(MixerDevice);
			MixerSourceVoice = MixerDevice->GetMixerSourceVoice();
			if (!MixerSourceVoice)
			{
				FreeResources();
				UE_LOG(LogAudioMixer, Warning, TEXT("Failed to get a mixer source voice for sound %s."), *InWaveInstance->GetName());
				return false;
			}

			// Initialize the source voice with the necessary format information
			FMixerSourceVoiceInitParams InitParams;
			InitParams.SourceListener = this;
			InitParams.NumInputChannels = WaveInstance->WaveData->NumChannels;
			InitParams.NumInputFrames = NumFrames;
			InitParams.SourceVoice = MixerSourceVoice;
			InitParams.bUseHRTFSpatialization = UseObjectBasedSpatialization();
			InitParams.bIsExternalSend = MixerDevice->bSpatializationIsExternalSend;
			InitParams.bIsAmbisonics = WaveInstance->bIsAmbisonics;

			if (InitParams.bIsAmbisonics)
			{
				checkf(InitParams.NumInputChannels == 4, TEXT("Only allow 4 channel source if file is ambisonics format."));
			}
			InitParams.AudioComponentUserID = WaveInstance->ActiveSound->GetAudioComponentUserID();

			InitParams.AudioComponentID = WaveInstance->ActiveSound->GetAudioComponentID();

			InitParams.EnvelopeFollowerAttackTime = WaveInstance->EnvelopeFollowerAttackTime;
			InitParams.EnvelopeFollowerReleaseTime = WaveInstance->EnvelopeFollowerReleaseTime;

			InitParams.SourceEffectChainId = 0;

			// Source manager needs to know if this is a vorbis source for rebuilding speaker maps
			InitParams.bIsVorbis = bIsVorbis;

			if (InitParams.NumInputChannels <= 2)
			{
				if (WaveInstance->SourceEffectChain)
				{
					InitParams.SourceEffectChainId = WaveInstance->SourceEffectChain->GetUniqueID();

					for (int32 i = 0; i < WaveInstance->SourceEffectChain->Chain.Num(); ++i)
					{
						InitParams.SourceEffectChain.Add(WaveInstance->SourceEffectChain->Chain[i]);
						InitParams.bPlayEffectChainTails = WaveInstance->SourceEffectChain->bPlayEffectChainTails;
					}
				}

				// Only need to care about effect chain tails finishing if we're told to play them
				if (InitParams.bPlayEffectChainTails)
				{
					bIsEffectTailsDone = false;
				}

				// Setup the bus Id if this source is a bus
				if (WaveInstance->WaveData->bIsBus)
				{
					InitParams.BusId = WaveInstance->WaveData->GetUniqueID();
					if (!WaveInstance->WaveData->IsLooping())
					{
						InitParams.BusDuration = WaveInstance->WaveData->GetDuration();
					}
				}

				// Toggle muting the source if sending only to output bus.
				// This can get set even if the source doesn't have bus sends since bus sends can be dynamically enabled.
				InitParams.bOutputToBusOnly = WaveInstance->bOutputToBusOnly;
				DynamicBusSendInfos.Reset();

				// If this source is sending its audio to a bus
				for (int32 BusSendType = 0; BusSendType < (int32)EBusSendType::Count; ++BusSendType)
				{
					// And add all the source bus sends
					for (FSoundSourceBusSendInfo& SendInfo : WaveInstance->SoundSourceBusSends[BusSendType])
					{
						if (SendInfo.SoundSourceBus != nullptr)
						{
							FMixerBusSend BusSend;
							BusSend.BusId = SendInfo.SoundSourceBus->GetUniqueID();
							BusSend.SendLevel = SendInfo.SendLevel;
							InitParams.BusSends[BusSendType].Add(BusSend);

							FDynamicBusSendInfo DynamicBusSendInfo;
							DynamicBusSendInfo.SendLevel = SendInfo.SendLevel;
							DynamicBusSendInfo.BusId = BusSend.BusId;
							DynamicBusSendInfo.BusSendLevelControlMethod = SendInfo.SourceBusSendLevelControlMethod;
							DynamicBusSendInfo.BusSendType = (EBusSendType)BusSendType;
							DynamicBusSendInfo.MinSendLevel = SendInfo.MinSendLevel;
							DynamicBusSendInfo.MaxSendLevel = SendInfo.MaxSendLevel;
							DynamicBusSendInfo.MinSendDistance = SendInfo.MinSendDistance;
							DynamicBusSendInfo.MaxSendDistance = SendInfo.MaxSendDistance;
							DynamicBusSendInfo.CustomSendLevelCurve = SendInfo.CustomSendLevelCurve;

							// Copy the bus SourceBusSendInfo structs to a local copy so we can update it in the update tick
							DynamicBusSendInfos.Add(DynamicBusSendInfo);

							// Flag that we're sending audio to buses so we can check for updates to send levels
							bSendingAudioToBuses = true;
						}
					}
				}
			}

			// Don't set up any submixing if we're set to output to bus only
			if (!InitParams.bOutputToBusOnly)
			{
				// If we're spatializing using HRTF and its an external send, don't need to setup a default/base submix send to master or EQ submix
				// We'll only be using non-default submix sends (e.g. reverb).
				if (!(InitParams.bUseHRTFSpatialization && MixerDevice->bSpatializationIsExternalSend))
				{
					FMixerSubmixWeakPtr SubmixPtr = WaveInstance->SoundSubmix
						? MixerDevice->GetSubmixInstance(WaveInstance->SoundSubmix)
						: MixerDevice->GetMasterSubmix();

					FMixerSourceSubmixSend SubmixSend;
					SubmixSend.Submix = SubmixPtr;
					SubmixSend.SendLevel = 1.0f;
					SubmixSend.bIsMainSend = true;
					InitParams.SubmixSends.Add(SubmixSend);
				}

				// Add submix sends for this source
				for (FSoundSubmixSendInfo& SendInfo : WaveInstance->SoundSubmixSends)
				{
					if (SendInfo.SoundSubmix != nullptr)
					{
						FMixerSourceSubmixSend SubmixSend;
						SubmixSend.Submix = MixerDevice->GetSubmixInstance(SendInfo.SoundSubmix);
						SubmixSend.SendLevel = SendInfo.SendLevel;
						SubmixSend.bIsMainSend = false;
						InitParams.SubmixSends.Add(SubmixSend);
					}
				}
			}

			// Loop through all submix sends to figure out what speaker maps this source is using
			for (FMixerSourceSubmixSend& Send : InitParams.SubmixSends)
			{
				FMixerSubmixPtr SubmixPtr = Send.Submix.Pin();
				if (SubmixPtr.IsValid())
				{
					ESubmixChannelFormat SubmixChannelType = SubmixPtr->GetSubmixChannels();
					ChannelMaps[(int32)SubmixChannelType].bUsed = true;
					ChannelMaps[(int32)SubmixChannelType].ChannelMap.Reset();
				}
			}

			// Check to see if this sound has been flagged to be in debug mode
#if AUDIO_MIXER_ENABLE_DEBUG_MODE
			InitParams.DebugName = WaveInstance->GetName();

			bool bIsDebug = false;
			FString WaveInstanceName = WaveInstance->GetName(); //-V595
			FString TestName = GEngine->GetAudioDeviceManager()->GetDebugger().GetAudioMixerDebugSoundName();
			if (WaveInstanceName.Contains(TestName))
			{
				bDebugMode = true;
				InitParams.bIsDebugMode = bDebugMode;
			}
#endif

			// Whether or not we're 3D
			bIs3D = !UseObjectBasedSpatialization() && WaveInstance->GetUseSpatialization() && SoundBuffer->NumChannels < 3;

			// Grab the source's reverb plugin settings
			InitParams.SpatializationPluginSettings = UseSpatializationPlugin() ? WaveInstance->SpatializationPluginSettings : nullptr;

			// Grab the source's occlusion plugin settings
			InitParams.OcclusionPluginSettings = UseOcclusionPlugin() ? WaveInstance->OcclusionPluginSettings : nullptr;

			// Grab the source's reverb plugin settings
			InitParams.ReverbPluginSettings = UseReverbPlugin() ? WaveInstance->ReverbPluginSettings : nullptr;

			// Grab the source's modulation plugin settings
			InitParams.ModulationPluginSettings = UseModulationPlugin() ? WaveInstance->ModulationPluginSettings : nullptr;

			// We support reverb
			SetReverbApplied(true);

			// Update the buffer sample rate to the wave instance sample rate in case it was serialized incorrectly
			MixerBuffer->InitSampleRate(WaveInstance->WaveData->GetSampleRateForCurrentPlatform());

			// Retrieve the raw pcm buffer data and the precached buffers before initializing so we can avoid having USoundWave ptrs in audio renderer thread
			EBufferType::Type BufferType = MixerBuffer->GetType();
			if (BufferType == EBufferType::PCM || BufferType == EBufferType::PCMPreview)
			{
				FRawPCMDataBuffer RawPCMDataBuffer;
				MixerBuffer->GetPCMData(&RawPCMDataBuffer.Data, &RawPCMDataBuffer.DataSize);
				MixerSourceBuffer->SetPCMData(RawPCMDataBuffer);
			}
#if PLATFORM_NUM_AUDIODECOMPRESSION_PRECACHE_BUFFERS > 0
			else if (BufferType == EBufferType::PCMRealTime || BufferType == EBufferType::Streaming)
			{
				USoundWave* WaveData = WaveInstance->WaveData;
				if (WaveData->CachedRealtimeFirstBuffer)
				{
					const uint32 NumPrecacheSamples = (uint32)(WaveData->NumPrecacheFrames * WaveData->NumChannels);
					const uint32 BufferSize = NumPrecacheSamples * sizeof(int16) * PLATFORM_NUM_AUDIODECOMPRESSION_PRECACHE_BUFFERS;

					TArray<uint8> PrecacheBufferCopy;
					PrecacheBufferCopy.AddUninitialized(BufferSize);

					FMemory::Memcpy(PrecacheBufferCopy.GetData(), WaveData->CachedRealtimeFirstBuffer, BufferSize);

					MixerSourceBuffer->SetCachedRealtimeFirstBuffers(MoveTemp(PrecacheBufferCopy));
				}
			}
#endif

			// Pass the decompression state off to the mixer source buffer if it hasn't already done so
			ICompressedAudioInfo* Decoder = MixerBuffer->GetDecompressionState(true);
			MixerSourceBuffer->SetDecoder(Decoder);

			// Hand off the mixer source buffer decoder
			InitParams.MixerSourceBuffer = MixerSourceBuffer;
			MixerSourceBuffer = nullptr;

			if (MixerSourceVoice->Init(InitParams))
			{
				InitializationState = EMixerSourceInitializationState::Initialized;

				Update();

				return true;
			}
			else
			{
				InitializationState = EMixerSourceInitializationState::NotInitialized;
				UE_LOG(LogAudioMixer, Warning, TEXT("Failed to initialize mixer source voice '%s'."), *InWaveInstance->GetName());
			}
		}
		else
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Num channels was 0 for sound buffer '%s'."), *InWaveInstance->GetName());
		}

		FreeResources();
		return false;
	}

	void FMixerSource::Update()
	{
		CSV_SCOPED_TIMING_STAT(Audio, UpdateSources);

		LLM_SCOPE(ELLMTag::AudioMixer);

		if (!WaveInstance || !MixerSourceVoice || Paused || InitializationState == EMixerSourceInitializationState::NotInitialized)
		{
			return;
		}

		++TickCount;

		UpdateModulation();

		UpdatePitch();

		UpdateVolume();

		UpdateSpatialization();

		UpdateEffects();

		UpdateSourceBusSends();

		UpdateChannelMaps();

#if ENABLE_AUDIO_DEBUG
		FAudioDebugger::DrawDebugInfo(*this);
#endif // ENABLE_AUDIO_DEBUG
	}

	bool FMixerSource::PrepareForInitialization(FWaveInstance* InWaveInstance)
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		// We are currently not supporting playing audio on a controller
		if (InWaveInstance->OutputTarget == EAudioOutputTarget::Controller)
		{
			return false;
		}

		// We are not initialized yet. We won't be until the sound file finishes loading and parsing the header.
		InitializationState = EMixerSourceInitializationState::Initializing;

		//  Reset so next instance will warn if algorithm changes in-flight
		bEditorWarnedChangedSpatialization = false;

		const bool bIsSeeking = InWaveInstance->StartTime > 0.0f;

		check(InWaveInstance);
		check(AudioDevice);

		check(!MixerBuffer);
		MixerBuffer = FMixerBuffer::Init(AudioDevice, InWaveInstance->WaveData, bIsSeeking /* bForceRealtime */);

		if (!MixerBuffer)
		{
			FreeResources(); // APM: maybe need to call this here too? 
			return false;
		}

		// WaveData must be valid beyond this point, otherwise MixerBuffer
		// would have failed to init.
		check(InWaveInstance->WaveData);
		USoundWave& SoundWave = *InWaveInstance->WaveData;

		Buffer = MixerBuffer;
		WaveInstance = InWaveInstance;

		LPFFrequency = MAX_FILTER_FREQUENCY;
		LastLPFFrequency = FLT_MAX;

		HPFFrequency = 0.0f;
		LastHPFFrequency = FLT_MAX;

		bIsDone = false;

		// Not all wave data types have a non-zero duration
		if (SoundWave.Duration > 0.0f)
		{
			if (!SoundWave.bIsBus)
			{
				NumTotalFrames = SoundWave.Duration * SoundWave.GetSampleRateForCurrentPlatform();
				check(NumTotalFrames > 0);
			}
			else if (!SoundWave.IsLooping())
			{
				NumTotalFrames = SoundWave.Duration * AudioDevice->GetSampleRate();
				check(NumTotalFrames > 0);
			}
		}

		check(!MixerSourceBuffer.IsValid());
		MixerSourceBuffer = FMixerSourceBuffer::Create(*MixerBuffer, SoundWave, InWaveInstance->LoopingMode, bIsSeeking);
		
		if (!MixerSourceBuffer.IsValid())
		{
			FreeResources();

			// Guarantee that this wave instance does not try to replay by disabling looping.
			WaveInstance->LoopingMode = LOOP_Never;

			if (ensure(WaveInstance->ActiveSound))
			{
				WaveInstance->ActiveSound->bShouldRemainActiveIfDropped = false;
			}
		}
		
		return MixerSourceBuffer.IsValid();
	}

	bool FMixerSource::IsPreparedToInit()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		if (MixerBuffer && MixerBuffer->IsRealTimeSourceReady())
		{
			check(MixerSourceBuffer.IsValid());

			// Check if we have a realtime audio task already (doing first decode)
			if (MixerSourceBuffer->IsAsyncTaskInProgress())
			{
				// not ready
				return MixerSourceBuffer->IsAsyncTaskDone();
			}
			else if (WaveInstance)
			{
				if (WaveInstance->WaveData->bIsBus)
				{
					// Buses don't need to do anything to play audio
					return true;
				}
				else
				{
					// Now check to see if we need to kick off a decode the first chunk of audio
					const EBufferType::Type BufferType = MixerBuffer->GetType();
					if ((BufferType == EBufferType::PCMRealTime || BufferType == EBufferType::Streaming) && WaveInstance->WaveData)
					{
						// If any of these conditions meet, we need to do an initial async decode before we're ready to start playing the sound
						if (WaveInstance->StartTime > 0.0f || WaveInstance->WaveData->bProcedural || WaveInstance->WaveData->bIsBus || !WaveInstance->WaveData->CachedRealtimeFirstBuffer)
						{
							// Before reading more PCMRT data, we first need to seek the buffer
							if (WaveInstance->IsSeekable())
							{
								MixerBuffer->Seek(WaveInstance->StartTime);
							}

							check(MixerSourceBuffer.IsValid());

							ICompressedAudioInfo* Decoder = MixerBuffer->GetDecompressionState(false);
							if (BufferType == EBufferType::Streaming)
							{
								IStreamingManager::Get().GetAudioStreamingManager().AddDecoder(Decoder);
							}

							MixerSourceBuffer->ReadMoreRealtimeData(Decoder, 0, EBufferReadMode::Asynchronous);

							// not ready
							return false;
						}
					}
				}
			}

			return true;
		}

		return false;
	}

	bool FMixerSource::IsInitialized() const
	{
		return InitializationState == EMixerSourceInitializationState::Initialized;
	}

	void FMixerSource::Play()
	{
		if (!WaveInstance)
		{
			return;
		}

		// Don't restart the sound if it was stopping when we paused, just stop it.
		if (Paused && (bIsStopping || bIsDone))
		{
			StopNow();
			return;
		}

		if (bIsStopping)
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Restarting a source which was stopping. Stopping now."));
			return;
		}

		// It's possible if Pause and Play are called while a sound is async initializing. In this case
		// we'll just not actually play the source here. Instead we'll call play when the sound finishes loading.
		if (MixerSourceVoice && InitializationState == EMixerSourceInitializationState::Initialized)
		{
			if (WaveInstance && WaveInstance->WaveData && WaveInstance->WaveData->bProcedural)
			{
				WaveInstance->WaveData->bPlayingProcedural = true;
			}

			MixerSourceVoice->Play();
		}

		bIsStopping = false;
		Paused = false;
		Playing = true;
		bLoopCallback = false;
		bIsDone = false;
	}

	void FMixerSource::Stop()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		if (InitializationState == EMixerSourceInitializationState::NotInitialized)
		{
			return;
		}

		if (!MixerSourceVoice)
		{
			StopNow();
			return;
		}

		// Always stop procedural sounds immediately.
		if (WaveInstance && WaveInstance->WaveData && WaveInstance->WaveData->bProcedural)
		{
			WaveInstance->WaveData->bPlayingProcedural = false;
			StopNow();
			return;
		}

		if (bIsDone)
		{
			StopNow();
		}
		else if (!bIsStopping)
		{
			// Otherwise, we need to do a quick fade-out of the sound and put the state
			// of the sound into "stopping" mode. This prevents this source from
			// being put into the "free" pool and prevents the source from freeing its resources
			// until the sound has finished naturally (i.e. faded all the way out)

			// StopFade will stop a sound with a very small fade to avoid discontinuities
			if (MixerSourceVoice && Playing)
			{
				if (bIsStoppingVoicesEnabled && !WaveInstance->WaveData->bProcedural)
				{
					// Let the wave instance know it's stopping
					WaveInstance->SetStopping(true);

					// TODO: parameterize the number of fades
					MixerSourceVoice->StopFade(512);
					bIsStopping = true;
				}
				else
				{
					StopNow();
				}
			}
			Paused = false;
		}
	}

	void FMixerSource::StopNow()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		// Immediately stop the sound source

		InitializationState = EMixerSourceInitializationState::NotInitialized;

		IStreamingManager::Get().GetAudioStreamingManager().RemoveStreamingSoundSource(this);

		bIsStopping = false;

		if (WaveInstance)
		{
			if (MixerSourceVoice && Playing)
			{
				MixerSourceVoice->Stop();
			}

			Paused = false;
			Playing = false;

			FreeResources();
		}

		FSoundSource::Stop();
	}

	void FMixerSource::Pause()
	{
		if (!WaveInstance)
		{
			return;
		}

		if (bIsStopping)
		{
			return;
		}

		if (MixerSourceVoice)
		{
			MixerSourceVoice->Pause();
		}

		Paused = true;
	}

	bool FMixerSource::IsFinished()
	{
		// A paused source is not finished.
		if (Paused)
		{
			return false;
		}

		if (InitializationState == EMixerSourceInitializationState::NotInitialized)
		{
			return true;
		}

		if (InitializationState == EMixerSourceInitializationState::Initializing)
		{
			return false;
		}

		if (WaveInstance && MixerSourceVoice)
		{
			if (bIsDone && bIsEffectTailsDone)
			{
				WaveInstance->NotifyFinished();
				bIsStopping = false;
				return true;
			}
			else if (bLoopCallback && WaveInstance->LoopingMode == LOOP_WithNotification)
			{
				WaveInstance->NotifyFinished();
				bLoopCallback = false;
			}

			return false;
		}
		return true;
	}

	float FMixerSource::GetPlaybackPercent() const
	{
		if (InitializationState != EMixerSourceInitializationState::Initialized)
		{
			return PreviousPlaybackPercent;
		}

		if (MixerSourceVoice && NumTotalFrames > 0)
		{
			int64 NumFrames = MixerSourceVoice->GetNumFramesPlayed();
			AUDIO_MIXER_CHECK(NumTotalFrames > 0);
			PreviousPlaybackPercent = (float)NumFrames / NumTotalFrames;
			if (WaveInstance->LoopingMode == LOOP_Never)
			{
				PreviousPlaybackPercent = FMath::Min(PreviousPlaybackPercent, 1.0f);
			}
			return PreviousPlaybackPercent;
		}
		else
		{
			// If we don't have any frames, that means it's a procedural sound wave, which means
			// that we're never going to have a playback percentage.
			return 1.0f;
		}
	}

	float FMixerSource::GetEnvelopeValue() const
	{
		if (MixerSourceVoice)
		{
			return MixerSourceVoice->GetEnvelopeValue();
		}
		return 0.0f;
	}

	void FMixerSource::OnBeginGenerate()
	{
	}

	void FMixerSource::OnDone()
	{
		bIsDone = true;
	}


	void FMixerSource::OnEffectTailsDone()
	{
		bIsEffectTailsDone = true;
	}

	void FMixerSource::FreeResources()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		if (MixerBuffer)
		{
			MixerBuffer->EnsureHeaderParseTaskFinished();
		}

		check(!bIsStopping);
		check(!Playing);

		// Make a new pending release data ptr to pass off release data
		if (MixerSourceVoice)
		{
			// We're now "releasing" so don't recycle this voice until we get notified that the source has finished
			bIsReleasing = true;

			// This will trigger FMixerSource::OnRelease from audio render thread.
			MixerSourceVoice->Release();
			MixerSourceVoice = nullptr;
		}

		MixerSourceBuffer.Reset();
		Buffer = nullptr;
		bLoopCallback = false;
		NumTotalFrames = 0;

		if (MixerBuffer)
		{
			EBufferType::Type BufferType = MixerBuffer->GetType();
			if (BufferType == EBufferType::PCMRealTime || BufferType == EBufferType::Streaming)
			{
				delete MixerBuffer;
			}

			MixerBuffer = nullptr;
		}

		// Reset the source's channel maps
		for (int32 i = 0; i < (int32)ESubmixChannelFormat::Count; ++i)
		{
			ChannelMaps[i].bUsed = false;
			ChannelMaps[i].ChannelMap.Reset();
		}

		InitializationState = EMixerSourceInitializationState::NotInitialized;
	}

	void FMixerSource::UpdateModulation()
	{
		check(AudioDevice);
		check(MixerSourceVoice);
		check(WaveInstance);

		if (AudioDevice->IsModulationPluginEnabled())
		{
			const int32 SourceId = MixerSourceVoice->GetSourceId();
			const bool bUpdatePending = AudioDevice->ModulationInterface->ProcessControls(SourceId, WaveInstance->SoundModulationControls);

			if (bUpdatePending)
			{
				AudioDevice->UpdateModulationControls(SourceId, WaveInstance->SoundModulationControls);
			}
		}
	}

	void FMixerSource::UpdatePitch()
	{
		AUDIO_MIXER_CHECK(MixerBuffer);

		check(WaveInstance);

		Pitch = WaveInstance->GetPitch();

		// Don't apply global pitch scale to UI sounds
		if (!WaveInstance->bIsUISound)
		{
			Pitch *= AudioDevice->GetGlobalPitchScale().GetValue();
		}

		Pitch = AudioDevice->ClampPitch(Pitch);

		// Scale the pitch by the ratio of the audio buffer sample rate and the actual sample rate of the hardware
		if (MixerBuffer)
		{
			const float MixerBufferSampleRate = MixerBuffer->GetSampleRate();
			const float AudioDeviceSampleRate = AudioDevice->GetSampleRate();
			Pitch *= MixerBufferSampleRate / AudioDeviceSampleRate;

			MixerSourceVoice->SetPitch(Pitch);
		}
	}

	void FMixerSource::UpdateVolume()
	{
		MixerSourceVoice->SetDistanceAttenuation(WaveInstance->GetDistanceAttenuation());

		float CurrentVolume = 0.0f;
		if (!AudioDevice->IsAudioDeviceMuted())
		{
			// 1. Apply device gain stage(s)
			CurrentVolume = WaveInstance->ActiveSound->bIsPreviewSound ? 1.0f : AudioDevice->GetMasterVolume();
			CurrentVolume *= AudioDevice->GetPlatformAudioHeadroom();

			// 2. Apply instance gain stage(s)
			CurrentVolume *= WaveInstance->GetVolume();
			CurrentVolume *= WaveInstance->GetDynamicVolume();

			// 3. Apply editor gain stage(s)
			CurrentVolume = FMath::Clamp<float>(GetDebugVolume(CurrentVolume), 0.0f, MAX_VOLUME);
		}
		MixerSourceVoice->SetVolume(CurrentVolume);
	}

	void FMixerSource::UpdateSpatialization()
	{
		SpatializationParams = GetSpatializationParams();
		if (WaveInstance->GetUseSpatialization())
		{
			MixerSourceVoice->SetSpatializationParams(SpatializationParams);
		}
	}

	void FMixerSource::UpdateEffects()
	{
		// Update the default LPF filter frequency
		SetFilterFrequency();

		if (LastLPFFrequency != LPFFrequency)
		{
			MixerSourceVoice->SetLPFFrequency(LPFFrequency);
			LastLPFFrequency = LPFFrequency;
		}

		if (LastHPFFrequency != HPFFrequency)
		{
			MixerSourceVoice->SetHPFFrequency(HPFFrequency);
			LastHPFFrequency = HPFFrequency;
		}

		// If reverb is applied, figure out how of the source to "send" to the reverb.
		if (bReverbApplied)
		{
			float ReverbSendLevel = 0.0f;
			ChannelMaps[(int32)ESubmixChannelFormat::Device].bUsed = true;

			if (WaveInstance->ReverbSendMethod == EReverbSendMethod::Manual)
			{
				ReverbSendLevel = FMath::Clamp(WaveInstance->ManualReverbSendLevel, 0.0f, 1.0f);
			}
			else
			{
				// The alpha value is determined identically between manual and custom curve methods
				const FVector2D& ReverbSendRadialRange = WaveInstance->ReverbSendLevelDistanceRange;
				const float Denom = FMath::Max(ReverbSendRadialRange.Y - ReverbSendRadialRange.X, 1.0f);
				const float Alpha = FMath::Clamp((WaveInstance->ListenerToSoundDistance - ReverbSendRadialRange.X) / Denom, 0.0f, 1.0f);

				if (WaveInstance->ReverbSendMethod == EReverbSendMethod::Linear)
				{
					ReverbSendLevel = FMath::Clamp(FMath::Lerp(WaveInstance->ReverbSendLevelRange.X, WaveInstance->ReverbSendLevelRange.Y, Alpha), 0.0f, 1.0f);
				}
				else
				{
					ReverbSendLevel = FMath::Clamp(WaveInstance->CustomRevebSendCurve.GetRichCurveConst()->Eval(Alpha), 0.0f, 1.0f);
				}
			}

			// Send the source audio to the reverb plugin if enabled
			if (UseReverbPlugin() && AudioDevice->ReverbPluginInterface)
			{
				check(MixerDevice);
				FMixerSubmixPtr ReverbPluginSubmixPtr = MixerDevice->GetSubmixInstance(AudioDevice->ReverbPluginInterface->GetSubmix()).Pin();
				if (ReverbPluginSubmixPtr.IsValid())
				{
					MixerSourceVoice->SetSubmixSendInfo(ReverbPluginSubmixPtr, ReverbSendLevel);
				}
			}

			// Send the source audio to the master reverb
			MixerSourceVoice->SetSubmixSendInfo(MixerDevice->GetMasterReverbSubmix(), ReverbSendLevel);
		}

		// Update submix send levels
		for (FSoundSubmixSendInfo& SendInfo : WaveInstance->SoundSubmixSends)
		{
			if (SendInfo.SoundSubmix != nullptr)
			{
				FMixerSubmixWeakPtr SubmixInstance = MixerDevice->GetSubmixInstance(SendInfo.SoundSubmix);
				float SendLevel = 0.0f;

				// calculate send level based on distance if that method is enabled
				if (SendInfo.SendLevelControlMethod == ESendLevelControlMethod::Manual)
				{
					SendLevel = FMath::Clamp(SendInfo.SendLevel, 0.0f, 1.0f);
				}
				else
				{
					// The alpha value is determined identically between manual and custom curve methods
					const FVector2D SendRadialRange = { SendInfo.MinSendDistance, SendInfo.MaxSendDistance};
					const FVector2D SendLevelRange = { SendInfo.MinSendLevel, SendInfo.MaxSendLevel };
					const float Denom = FMath::Max(SendRadialRange.Y - SendRadialRange.X, 1.0f);
					const float Alpha = FMath::Clamp((WaveInstance->ListenerToSoundDistance - SendRadialRange.X) / Denom, 0.0f, 1.0f);

					if (SendInfo.SendLevelControlMethod == ESendLevelControlMethod::Linear)
					{
						SendLevel = FMath::Clamp(FMath::Lerp(SendLevelRange.X, SendLevelRange.Y, Alpha), 0.0f, 1.0f);
					}
					else // use curve
					{
						SendLevel = FMath::Clamp(SendInfo.CustomSendLevelCurve.GetRichCurveConst()->Eval(Alpha), 0.0f, 1.0f);
					}
				}

				// set the level for this send
				MixerSourceVoice->SetSubmixSendInfo(SubmixInstance, SendLevel);

				// Make sure we flag that we're using this submix sends since these can be dynamically added from BP
				// If we don't flag this then these channel maps won't be generated for this channel format
				ChannelMaps[(int32)SendInfo.SoundSubmix->ChannelFormat].bUsed = true;
			}
		}
 	}

	void FMixerSource::UpdateSourceBusSends()
	{
		// 1) loop through all bus sends
		// 2) check for any bus sends that are set to update non-manually
		// 3) Cache previous send level and only do update if it's changed in any significant amount

		if (!bSendingAudioToBuses)
		{
			return;
		}

		// If this source is sending its audio to a bus, we need to check if it needs to be updated
		for (FDynamicBusSendInfo& DynamicBusSendInfo : DynamicBusSendInfos)
		{
			float SendLevel = 0.0f;

			if (DynamicBusSendInfo.BusSendLevelControlMethod == ESourceBusSendLevelControlMethod::Manual)
			{
				SendLevel = FMath::Clamp(DynamicBusSendInfo.SendLevel, 0.0f, 1.0f);
			}
			else
			{
				// The alpha value is determined identically between linear and custom curve methods
				const FVector2D SendRadialRange = { DynamicBusSendInfo.MinSendDistance, DynamicBusSendInfo.MaxSendDistance};
				const FVector2D SendLevelRange = { DynamicBusSendInfo.MinSendLevel, DynamicBusSendInfo.MaxSendLevel };
				const float Denom = FMath::Max(SendRadialRange.Y - SendRadialRange.X, 1.0f);
				const float Alpha = FMath::Clamp((WaveInstance->ListenerToSoundDistance - SendRadialRange.X) / Denom, 0.0f, 1.0f);

				if (DynamicBusSendInfo.BusSendLevelControlMethod == ESourceBusSendLevelControlMethod::Linear)
				{
					SendLevel = FMath::Clamp(FMath::Lerp(SendLevelRange.X, SendLevelRange.Y, Alpha), 0.0f, 1.0f);
				}
				else // use curve
				{
					SendLevel = FMath::Clamp(DynamicBusSendInfo.CustomSendLevelCurve.GetRichCurveConst()->Eval(Alpha), 0.0f, 1.0f);
				}
			}

			// If the send level changed, then we need to send an update to the audio render thread
			if (!FMath::IsNearlyEqual(SendLevel, DynamicBusSendInfo.SendLevel))
			{
				DynamicBusSendInfo.SendLevel = SendLevel;

				FMixerBusSend BusSend;
				BusSend.BusId = DynamicBusSendInfo.BusId;
				BusSend.SendLevel = SendLevel;

				MixerSourceVoice->SetBusSendInfo(DynamicBusSendInfo.BusSendType, BusSend);
			}
		}
	}

	void FMixerSource::UpdateChannelMaps()
	{
		SetStereoBleed();

		SetLFEBleed();

		int32 NumOutputDeviceChannels = MixerDevice->GetNumDeviceChannels();
		const FAudioPlatformDeviceInfo& DeviceInfo = MixerDevice->GetPlatformDeviceInfo();

		// Compute a new speaker map for each possible output channel mapping for the source
		for (int32 i = 0; i < (int32)ESubmixChannelFormat::Count; ++i)
		{
			FChannelMapInfo& ChannelMapInfo = ChannelMaps[i];
			if (ChannelMapInfo.bUsed)
			{
				ESubmixChannelFormat ChannelType = (ESubmixChannelFormat)i;

				check(Buffer);
				const uint32 NumChannels = Buffer->NumChannels;
				if (ComputeChannelMap(ChannelType, Buffer->NumChannels, ChannelMapInfo.ChannelMap))
				{
					MixerSourceVoice->SetChannelMap(ChannelType, NumChannels, ChannelMapInfo.ChannelMap, bIs3D, WaveInstance->bCenterChannelOnly);
				}
			}
		}
	}

	bool FMixerSource::ComputeMonoChannelMap(const ESubmixChannelFormat SubmixChannelType, Audio::AlignedFloatBuffer& OutChannelMap)
	{
		if (IsUsingObjectBasedSpatialization())
		{
			if (WaveInstance->SpatializationMethod != ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF && !bEditorWarnedChangedSpatialization)
			{
				bEditorWarnedChangedSpatialization = true;
				UE_LOG(LogAudioMixer, Warning, TEXT("Changing the spatialization method on a playing sound is not supported (WaveInstance: %s)"), *WaveInstance->WaveData->GetFullName());
			}

			// Treat the source as if it is a 2D stereo source:
			return ComputeStereoChannelMap(SubmixChannelType, OutChannelMap);
		}
		else if (WaveInstance->GetUseSpatialization() && (!FMath::IsNearlyEqual(WaveInstance->AbsoluteAzimuth, PreviousAzimuth, 0.01f) || MixerSourceVoice->NeedsSpeakerMap()))
		{
			// Don't need to compute the source channel map if the absolute azimuth hasn't changed much
			PreviousAzimuth = WaveInstance->AbsoluteAzimuth;
			OutChannelMap.Reset();
			MixerDevice->Get3DChannelMap(SubmixChannelType, WaveInstance, WaveInstance->AbsoluteAzimuth, SpatializationParams.NormalizedOmniRadius, OutChannelMap);
			return true;
		}
		else if (!OutChannelMap.Num())
		{
			// Only need to compute the 2D channel map once
			MixerDevice->Get2DChannelMap(bIsVorbis, SubmixChannelType, 1, WaveInstance->bCenterChannelOnly, OutChannelMap);
			return true;
		}

		// Return false means the channel map hasn't changed
		return false;
	}

	bool FMixerSource::ComputeStereoChannelMap(const ESubmixChannelFormat InSubmixChannelType, Audio::AlignedFloatBuffer& OutChannelMap)
	{
		// Only recalculate positional data if the source has moved a significant amount:
		if (WaveInstance->GetUseSpatialization() && (!FMath::IsNearlyEqual(WaveInstance->AbsoluteAzimuth, PreviousAzimuth, 0.01f) || MixerSourceVoice->NeedsSpeakerMap()))
		{
			// Make sure our stereo emitter positions are updated relative to the sound emitter position
			if (Buffer->NumChannels == 2)
			{
				UpdateStereoEmitterPositions();
			}

			// Check whether voice is currently using 
			if (!IsUsingObjectBasedSpatialization())
			{
				float AzimuthOffset = 0.0f;

				float LeftAzimuth = 90.0f;
				float RightAzimuth = 270.0f;

				const float DistanceToUse = UseListenerOverrideForSpreadCVar ? WaveInstance->ListenerToSoundDistance : WaveInstance->ListenerToSoundDistanceForPanning;

				if (DistanceToUse > KINDA_SMALL_NUMBER)
				{
					AzimuthOffset = FMath::Atan(0.5f * WaveInstance->StereoSpread / DistanceToUse);
					AzimuthOffset = FMath::RadiansToDegrees(AzimuthOffset);

					LeftAzimuth = WaveInstance->AbsoluteAzimuth - AzimuthOffset;
					if (LeftAzimuth < 0.0f)
					{
						LeftAzimuth += 360.0f;
					}

					RightAzimuth = WaveInstance->AbsoluteAzimuth + AzimuthOffset;
					if (RightAzimuth > 360.0f)
					{
						RightAzimuth -= 360.0f;
					}
				}

				// Reset the channel map, the stereo spatialization channel mapping calls below will append their mappings
				OutChannelMap.Reset();

				MixerDevice->Get3DChannelMap(InSubmixChannelType, WaveInstance, LeftAzimuth, SpatializationParams.NormalizedOmniRadius, OutChannelMap);
				MixerDevice->Get3DChannelMap(InSubmixChannelType, WaveInstance, RightAzimuth, SpatializationParams.NormalizedOmniRadius, OutChannelMap);

				return true;
			}
		}

		if (!OutChannelMap.Num())
		{
			MixerDevice->Get2DChannelMap(bIsVorbis, InSubmixChannelType, 2, WaveInstance->bCenterChannelOnly, OutChannelMap);
			return true;
		}

		return false;
	}

	bool FMixerSource::ComputeChannelMap(const ESubmixChannelFormat InSubmixChannelType, const int32 NumSourceChannels, Audio::AlignedFloatBuffer& OutChannelMap)
	{
		if (NumSourceChannels == 1)
		{
			return ComputeMonoChannelMap(InSubmixChannelType, OutChannelMap);
		}
		else if (NumSourceChannels == 2)
		{
			return ComputeStereoChannelMap(InSubmixChannelType, OutChannelMap);
		}
		else if (!OutChannelMap.Num())
		{
			MixerDevice->Get2DChannelMap(bIsVorbis, InSubmixChannelType, NumSourceChannels, WaveInstance->bCenterChannelOnly, OutChannelMap);
			return true;
		}
		return false;
	}

	bool FMixerSource::UseObjectBasedSpatialization() const
	{
		return (Buffer->NumChannels <= MixerDevice->MaxChannelsSupportedBySpatializationPlugin &&
				AudioDevice->IsSpatializationPluginEnabled() &&
				WaveInstance->SpatializationMethod == ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF);
	}

	bool FMixerSource::IsUsingObjectBasedSpatialization() const
	{
		bool bIsUsingObjectBaseSpatialization = UseObjectBasedSpatialization();

		if (MixerSourceVoice)
		{
			// If it is currently playing, check whether it actively uses HRTF spatializer.
			// HRTF spatialization cannot be altered on currently playing source. So this handles
			// the case where the source was initialized without HRTF spatialization before HRTF
			// spatialization is enabled. 
			bool bDefaultIfNoSourceId = true;
			bIsUsingObjectBaseSpatialization &= MixerSourceVoice->IsUsingHRTFSpatializer(bDefaultIfNoSourceId);
		}
		return bIsUsingObjectBaseSpatialization;
	}

	bool FMixerSource::UseSpatializationPlugin() const
	{
		return (Buffer->NumChannels <= MixerDevice->MaxChannelsSupportedBySpatializationPlugin) &&
			AudioDevice->IsSpatializationPluginEnabled() &&
			WaveInstance->SpatializationPluginSettings != nullptr;
	}

	bool FMixerSource::UseOcclusionPlugin() const
	{
		return (Buffer->NumChannels == 1 || Buffer->NumChannels == 2) &&
			AudioDevice->IsOcclusionPluginEnabled() &&
			WaveInstance->OcclusionPluginSettings != nullptr;
	}

	bool FMixerSource::UseModulationPlugin() const
	{
		return AudioDevice->IsModulationPluginEnabled() &&
			WaveInstance->ModulationPluginSettings != nullptr;
	}

	bool FMixerSource::UseReverbPlugin() const
	{
		return (Buffer->NumChannels == 1 || Buffer->NumChannels == 2) &&
			AudioDevice->IsReverbPluginEnabled() &&
			WaveInstance->ReverbPluginSettings != nullptr;
	}
}
