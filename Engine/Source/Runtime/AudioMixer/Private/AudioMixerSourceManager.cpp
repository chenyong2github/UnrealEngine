// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerSourceManager.h"
#include "AudioMixerSourceBuffer.h"
#include "AudioMixerSource.h"
#include "AudioMixerDevice.h"
#include "AudioMixerSourceVoice.h"
#include "AudioMixerSubmix.h"
#include "AudioThread.h"
#include "IAudioExtensionPlugin.h"
#include "AudioMixer.h"
#include "Sound/SoundModulationDestination.h"
#include "SoundFieldRendering.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Async/Async.h"
#include "Stats/Stats.h"

// Link to "Audio" profiling category
CSV_DECLARE_CATEGORY_MODULE_EXTERN(AUDIOMIXERCORE_API, Audio);
static int32 DisableParallelSourceProcessingCvar = 1;
FAutoConsoleVariableRef CVarDisableParallelSourceProcessing(
	TEXT("au.DisableParallelSourceProcessing"),
	DisableParallelSourceProcessingCvar,
	TEXT("Disables using async tasks for processing sources.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableFilteringCvar = 0;
FAutoConsoleVariableRef CVarDisableFiltering(
	TEXT("au.DisableFiltering"),
	DisableFilteringCvar,
	TEXT("Disables using the per-source lowpass and highpass filter.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableHPFilteringCvar = 0;
FAutoConsoleVariableRef CVarDisableHPFiltering(
	TEXT("au.DisableHPFiltering"),
	DisableHPFilteringCvar,
	TEXT("Disables using the per-source highpass filter.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableEnvelopeFollowingCvar = 0;
FAutoConsoleVariableRef CVarDisableEnvelopeFollowing(
	TEXT("au.DisableEnvelopeFollowing"),
	DisableEnvelopeFollowingCvar,
	TEXT("Disables using the envlope follower for source envelope tracking.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableSourceEffectsCvar = 0;
FAutoConsoleVariableRef CVarDisableSourceEffects(
	TEXT("au.DisableSourceEffects"),
	DisableSourceEffectsCvar,
	TEXT("Disables using any source effects.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableDistanceAttenuationCvar = 0;
FAutoConsoleVariableRef CVarDisableDistanceAttenuation(
	TEXT("au.DisableDistanceAttenuation"),
	DisableDistanceAttenuationCvar,
	TEXT("Disables using any Distance Attenuation.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 BypassAudioPluginsCvar = 0;
FAutoConsoleVariableRef CVarBypassAudioPlugins(
	TEXT("au.BypassAudioPlugins"),
	BypassAudioPluginsCvar,
	TEXT("Bypasses any audio plugin processing.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 FlushCommandBufferOnTimeoutCvar = 0;
FAutoConsoleVariableRef CVarFlushCommandBufferOnTimeout(
	TEXT("au.FlushCommandBufferOnTimeout"),
	FlushCommandBufferOnTimeoutCvar,
	TEXT("When set to 1, flushes audio render thread synchronously when our fence has timed out.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 CommandBufferFlushWaitTimeMsCvar = 1000;
FAutoConsoleVariableRef CVarCommandBufferFlushWaitTimeMs(
	TEXT("au.CommandBufferFlushWaitTimeMs"),
	CommandBufferFlushWaitTimeMsCvar,
	TEXT("How long to wait for the command buffer flush to complete.\n"),
	ECVF_Default);

// +/- 4 Octaves (default)
static float MaxModulationPitchRangeFreqCVar = 16.0f;
static float MinModulationPitchRangeFreqCVar = 0.0625f;
static FAutoConsoleCommand GModulationSetMaxPitchRange(
	TEXT("au.Modulation.SetPitchRange"),
	TEXT("Sets max final modulation range of pitch (in semitones). Default: 96 semitones (+/- 4 octaves)"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogAudioMixer, Error, TEXT("Failed to set max modulation pitch range: Range not provided"));
				return;
			}

			const float Range = FCString::Atof(*Args[0]);
			MaxModulationPitchRangeFreqCVar = Audio::GetFrequencyMultiplier(Range * 0.5f);
			MaxModulationPitchRangeFreqCVar = Audio::GetFrequencyMultiplier(Range * -0.5f);
		}
	)
);

#define ENVELOPE_TAIL_THRESHOLD (1.58489e-5f) // -96 dB

#define VALIDATE_SOURCE_MIXER_STATE 1

#if AUDIO_MIXER_ENABLE_DEBUG_MODE

// Macro which checks if the source id is in debug mode, avoids having a bunch of #ifdefs in code
#define AUDIO_MIXER_DEBUG_LOG(SourceId, Format, ...)																							\
	if (SourceInfos[SourceId].bIsDebugMode)																													\
	{																																			\
		FString CustomMessage = FString::Printf(Format, ##__VA_ARGS__);																			\
		FString LogMessage = FString::Printf(TEXT("<Debug Sound Log> [Id=%d][Name=%s]: %s"), SourceId, *SourceInfos[SourceId].DebugName, *CustomMessage);	\
		UE_LOG(LogAudioMixer, Log, TEXT("%s"), *LogMessage);																								\
	}

#else

#define AUDIO_MIXER_DEBUG_LOG(SourceId, Message)

#endif

// Disable subframe timing logic
#define AUDIO_SUBFRAME_ENABLED 0

// Define profiling for source manager. 
DEFINE_STAT(STAT_AudioMixerHRTF);
DEFINE_STAT(STAT_AudioMixerSourceBuffers);
DEFINE_STAT(STAT_AudioMixerSourceEffectBuffers);
DEFINE_STAT(STAT_AudioMixerSourceManagerUpdate);
DEFINE_STAT(STAT_AudioMixerSourceOutputBuffers);

namespace Audio
{
	/*************************************************************************
	* FMixerSourceManager
	**************************************************************************/

	FMixerSourceManager::FMixerSourceManager(FMixerDevice* InMixerDevice)
		: MixerDevice(InMixerDevice)
		, NumActiveSources(0)
		, NumTotalSources(0)
		, NumOutputFrames(0)
		, NumOutputSamples(0)
		, NumSourceWorkers(4)
		, bInitialized(false)
		, bUsingSpatializationPlugin(false)
		, MaxChannelsSupportedBySpatializationPlugin(1)
	{
		// Get a manual resetable event
		const bool bIsManualReset = true;
		CommandsProcessedEvent = FPlatformProcess::GetSynchEventFromPool(bIsManualReset);
		check(CommandsProcessedEvent != nullptr);

		// Immediately trigger the command processed in case a flush happens before the audio thread swaps command buffers
		CommandsProcessedEvent->Trigger();
	}

	FMixerSourceManager::~FMixerSourceManager()
	{
		if (SourceWorkers.Num() > 0)
		{
			for (int32 i = 0; i < SourceWorkers.Num(); ++i)
			{
				delete SourceWorkers[i];
				SourceWorkers[i] = nullptr;
			}

			SourceWorkers.Reset();
		}

		FPlatformProcess::ReturnSynchEventToPool(CommandsProcessedEvent);
	}

	void FMixerSourceManager::Init(const FSourceManagerInitParams& InitParams)
	{
		AUDIO_MIXER_CHECK(InitParams.NumSources > 0);

		if (bInitialized || !MixerDevice)
		{
			return;
		}

		AUDIO_MIXER_CHECK(MixerDevice->GetSampleRate() > 0);

		NumTotalSources = InitParams.NumSources;

		NumOutputFrames = MixerDevice->PlatformSettings.CallbackBufferFrameSize;
		NumOutputSamples = NumOutputFrames * MixerDevice->GetNumDeviceChannels();

		MixerSources.Init(nullptr, NumTotalSources);

		// Populate output sources array with default data
		SourceSubmixOutputBuffers.Reset();
		for (int32 Index = 0; Index < NumTotalSources; Index++)
		{
			SourceSubmixOutputBuffers.Emplace(MixerDevice, 2, MixerDevice->GetNumDeviceChannels(), NumOutputFrames);
		}

		SourceInfos.AddDefaulted(NumTotalSources);

		for (int32 i = 0; i < NumTotalSources; ++i)
		{
			FSourceInfo& SourceInfo = SourceInfos[i];

			SourceInfo.MixerSourceBuffer = nullptr;

			SourceInfo.VolumeSourceStart = -1.0f;
			SourceInfo.VolumeSourceDestination = -1.0f;
			SourceInfo.VolumeFadeSlope = 0.0f;
			SourceInfo.VolumeFadeStart = 0.0f;
			SourceInfo.VolumeFadeFramePosition = 0;
			SourceInfo.VolumeFadeNumFrames = 0;

			SourceInfo.DistanceAttenuationSourceStart = -1.0f;
			SourceInfo.DistanceAttenuationSourceDestination = -1.0f;

			SourceInfo.LowPassFreq = MAX_FILTER_FREQUENCY;
			SourceInfo.HighPassFreq = MIN_FILTER_FREQUENCY;

			SourceInfo.SourceListener = nullptr;
			SourceInfo.CurrentPCMBuffer = nullptr;	
			SourceInfo.CurrentAudioChunkNumFrames = 0;
			SourceInfo.CurrentFrameAlpha = 0.0f;
			SourceInfo.CurrentFrameIndex = 0;
			SourceInfo.NumFramesPlayed = 0;
			SourceInfo.StartTime = 0.0;
			SourceInfo.SubmixSends.Reset();
			SourceInfo.AudioBusId = INDEX_NONE;
			SourceInfo.SourceBusDurationFrames = INDEX_NONE;
		
			SourceInfo.AudioBusSends[(int32)EBusSendType::PreEffect].Reset();
			SourceInfo.AudioBusSends[(int32)EBusSendType::PostEffect].Reset();

			SourceInfo.SourceEffectChainId = INDEX_NONE;

			SourceInfo.SourceEnvelopeFollower = Audio::FEnvelopeFollower(MixerDevice->SampleRate, 10, 100, Audio::EPeakMode::Peak);
			SourceInfo.SourceEnvelopeValue = 0.0f;
			SourceInfo.bEffectTailsDone = false;
		
			SourceInfo.ResetModulators(MixerDevice->DeviceID);

			SourceInfo.bIs3D = false;
			SourceInfo.bIsCenterChannelOnly = false;
			SourceInfo.bIsActive = false;
			SourceInfo.bIsPlaying = false;
			SourceInfo.bIsPaused = false;
			SourceInfo.bIsPausedForQuantization = false;
			SourceInfo.bDelayLineSet = false;
			SourceInfo.bIsStopping = false;
			SourceInfo.bIsDone = false;
			SourceInfo.bIsLastBuffer = false;
			SourceInfo.bIsBusy = false;
			SourceInfo.bUseHRTFSpatializer = false;
			SourceInfo.bUseOcclusionPlugin = false;
			SourceInfo.bUseReverbPlugin = false;
			SourceInfo.bHasStarted = false;
			SourceInfo.bEnableBusSends = false;
			SourceInfo.bEnableBaseSubmix = false;
			SourceInfo.bEnableSubmixSends = false;
			SourceInfo.bIsVorbis = false;
			SourceInfo.bIsBypassingLPF = false;
			SourceInfo.bIsBypassingHPF = false;
			SourceInfo.bHasPreDistanceAttenuationSend = false;
			SourceInfo.bModFiltersUpdated = false;

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
			SourceInfo.bIsDebugMode = false;
#endif // AUDIO_MIXER_ENABLE_DEBUG_MODE

			SourceInfo.NumInputChannels = 0;
			SourceInfo.NumPostEffectChannels = 0;
			SourceInfo.NumInputFrames = 0;
		}
		
		GameThreadInfo.bIsBusy.AddDefaulted(NumTotalSources);
		GameThreadInfo.bNeedsSpeakerMap.AddDefaulted(NumTotalSources);
		GameThreadInfo.bIsDebugMode.AddDefaulted(NumTotalSources);
		GameThreadInfo.bIsUsingHRTFSpatializer.AddDefaulted(NumTotalSources);
		GameThreadInfo.FreeSourceIndices.Reset(NumTotalSources);
		for (int32 i = NumTotalSources - 1; i >= 0; --i)
		{
			GameThreadInfo.FreeSourceIndices.Add(i);
		}

		// Initialize the source buffer memory usage to max source scratch buffers (num frames times max source channels)
		for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			SourceInfo.SourceBuffer.Reset(NumOutputFrames * 8);
			SourceInfo.PreDistanceAttenuationBuffer.Reset(NumOutputFrames * 8);
			SourceInfo.SourceEffectScratchBuffer.Reset(NumOutputFrames * 8);
			SourceInfo.AudioPluginOutputData.AudioBuffer.Reset(NumOutputFrames * 2);
		}

		// Setup the source workers
		SourceWorkers.Reset();
		if (NumSourceWorkers > 0)
		{
			const int32 NumSourcesPerWorker = FMath::Max(NumTotalSources / NumSourceWorkers, 1);
			int32 StartId = 0;
			int32 EndId = 0;
			while (EndId < NumTotalSources)
			{
				EndId = FMath::Min(StartId + NumSourcesPerWorker, NumTotalSources);
				SourceWorkers.Add(new FAsyncTask<FAudioMixerSourceWorker>(this, StartId, EndId));
				StartId = EndId;
			}
		}
		NumSourceWorkers = SourceWorkers.Num();

		// Cache the spatialization plugin
		SpatializationPlugin = MixerDevice->SpatializationPluginInterface;
		if (SpatializationPlugin.IsValid())
		{
			bUsingSpatializationPlugin = true;
			MaxChannelsSupportedBySpatializationPlugin = MixerDevice->MaxChannelsSupportedBySpatializationPlugin;
		}

		// Spam command queue with nops.
		static FAutoConsoleCommand SpamNopsCmd(
			TEXT("au.SpamCommandQueue"),
			TEXT(""),
			FConsoleCommandDelegate::CreateLambda([this]() 
			{				
				struct FSpamPayload
				{
					uint8 JunkBytes[1024];
				} Payload;
				for (int32 i = 0; i < 65536; ++i)
				{
					AudioMixerThreadCommand([Payload] {});
				}
			})
		);

		bInitialized = true;
		bPumpQueue = false;
	}

	void FMixerSourceManager::Update(bool bTimedOut)
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

#if VALIDATE_SOURCE_MIXER_STATE
		for (int32 i = 0; i < NumTotalSources; ++i)
		{
			if (!GameThreadInfo.bIsBusy[i])
			{
				// Make sure that our bIsFree and FreeSourceIndices are correct
				AUDIO_MIXER_CHECK(GameThreadInfo.FreeSourceIndices.Contains(i) == true);
			}
		}
#endif

		if (FPlatformProcess::SupportsMultithreading())
		{
			// If the command was triggered, then we want to do a swap of command buffers
			if (CommandsProcessedEvent->Wait(0))
			{
				int32 CurrentGameIndex = !RenderThreadCommandBufferIndex.GetValue();

				// This flags the audio render thread to be able to pump the next batch of commands
				// And will allow the audio thread to write to a new command slot
				const int32 NextIndex = (CurrentGameIndex + 1) & 1;

				FCommands& NextCommandBuffer = CommandBuffers[NextIndex];

				// Make sure we've actually emptied the command queue from the render thread before writing to it
				if (FlushCommandBufferOnTimeoutCvar && NextCommandBuffer.SourceCommandQueue.Num() != 0)
				{
					UE_LOG(LogAudioMixer, Warning, TEXT("Audio render callback stopped. Flushing %d commands."), NextCommandBuffer.SourceCommandQueue.Num());

					// Pop and execute all the commands that came since last update tick
					for (int32 Id = 0; Id < NextCommandBuffer.SourceCommandQueue.Num(); ++Id)
					{
						TFunction<void()>& CommandFunction = NextCommandBuffer.SourceCommandQueue[Id];
						CommandFunction();
						NumCommands.Decrement();
					}

					NextCommandBuffer.SourceCommandQueue.Reset();
				}

				// Here we ensure that we block for any pending calls to AudioMixerThreadCommand.
				FScopeLock ScopeLock(&CommandBufferIndexCriticalSection);
				RenderThreadCommandBufferIndex.Set(CurrentGameIndex);

				CommandsProcessedEvent->Reset();
			}
		}
		else
		{
			int32 CurrentRenderIndex = RenderThreadCommandBufferIndex.GetValue();
			int32 CurrentGameIndex = !RenderThreadCommandBufferIndex.GetValue();
			check(CurrentGameIndex == 0 || CurrentGameIndex == 1);
			check(CurrentRenderIndex == 0 || CurrentRenderIndex == 1);

			// If these values are the same, that means the audio render thread has finished the last buffer queue so is ready for the next block
			if (CurrentRenderIndex == CurrentGameIndex)
			{
				// This flags the audio render thread to be able to pump the next batch of commands
				// And will allow the audio thread to write to a new command slot
				const int32 NextIndex = !CurrentGameIndex;

				// Make sure we've actually emptied the command queue from the render thread before writing to it
				if (CommandBuffers[NextIndex].SourceCommandQueue.Num() != 0)
				{
					UE_LOG(LogAudioMixer, Warning, TEXT("Source command queue not empty: %d"), CommandBuffers[NextIndex].SourceCommandQueue.Num());
				}
				bPumpQueue = true;
			}
		}

	}

	void FMixerSourceManager::ReleaseSource(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(bInitialized);

		if (MixerSources[SourceId] == nullptr)
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Ignoring double release of SourceId: %i"), SourceId);
			return;
		}

		AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Is releasing"));
		
		FSourceInfo& SourceInfo = SourceInfos[SourceId];

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		if (SourceInfo.bIsDebugMode)
		{
			DebugSoloSources.Remove(SourceId);
		}
#endif
		// Remove from list of active bus or source ids depending on what type of source this is
		if (SourceInfo.AudioBusId != INDEX_NONE)
		{
			// Remove this bus from the registry of bus instances
			TSharedPtr<FMixerAudioBus> AudioBusPtr = AudioBuses.FindRef(SourceInfo.AudioBusId);
			if (AudioBusPtr.IsValid())
			{
				// If this audio bus was automatically created via source bus playback, this this audio bus can be removed
				if (AudioBusPtr->RemoveInstanceId(SourceId))
				{
					// Only automatic buses will be getting removed here. Otherwise they need to be manually removed from the source manager.
					ensure(AudioBusPtr->IsAutomatic());
					AudioBuses.Remove(SourceInfo.AudioBusId);
				}
			}
		}

		// Remove this source's send list from the bus data registry
		for (int32 AudioBusSendType = 0; AudioBusSendType < (int32)EBusSendType::Count; ++AudioBusSendType)
		{
			for (uint32 AudioBusId : SourceInfo.AudioBusSends[AudioBusSendType])
			{
				// we should have a bus registration entry still since the send hasn't been cleaned up yet
				TSharedPtr<FMixerAudioBus> AudioBusPtr = AudioBuses.FindRef(AudioBusId);
				if (AudioBusPtr.IsValid())
				{
					if (AudioBusPtr->RemoveSend((EBusSendType)AudioBusSendType, SourceId))
					{
						ensure(AudioBusPtr->IsAutomatic());
						AudioBuses.Remove(AudioBusId);
					}
				}
			}

			SourceInfo.AudioBusSends[AudioBusSendType].Reset();
		}

		SourceInfo.AudioBusId = INDEX_NONE;
		SourceInfo.SourceBusDurationFrames = INDEX_NONE;

		// Free the mixer source buffer data
		if (SourceInfo.MixerSourceBuffer.IsValid())
		{
			PendingSourceBuffers.Add(SourceInfo.MixerSourceBuffer);
			SourceInfo.MixerSourceBuffer = nullptr;
		}

		SourceInfo.SourceListener = nullptr;

		// Remove the mixer source from its submix sends
		for (FMixerSourceSubmixSend& SubmixSendItem : SourceInfo.SubmixSends)
		{
			FMixerSubmixPtr SubmixPtr = SubmixSendItem.Submix.Pin();
			if (SubmixPtr.IsValid())
			{
				SubmixPtr->RemoveSourceVoice(MixerSources[SourceId]);
			}
		}
		SourceInfo.SubmixSends.Reset();

		// Notify plugin effects
		if (SourceInfo.bUseHRTFSpatializer)
		{
			AUDIO_MIXER_CHECK(bUsingSpatializationPlugin);
			LLM_SCOPE(ELLMTag::AudioMixerPlugins);
			SpatializationPlugin->OnReleaseSource(SourceId);
		}

		if (SourceInfo.bUseOcclusionPlugin)
		{
			MixerDevice->OcclusionInterface->OnReleaseSource(SourceId);
		}

		if (SourceInfo.bUseReverbPlugin)
		{
			MixerDevice->ReverbPluginInterface->OnReleaseSource(SourceId);
		}

		// Delete the source effects
		SourceInfo.SourceEffectChainId = INDEX_NONE;
		ResetSourceEffectChain(SourceId);

		SourceInfo.SourceEnvelopeFollower.Reset();
		SourceInfo.bEffectTailsDone = true;

		// Release the source voice back to the mixer device. This is pooled.
		MixerDevice->ReleaseMixerSourceVoice(MixerSources[SourceId]);
		MixerSources[SourceId] = nullptr;

		// Reset all state and data
		SourceInfo.PitchSourceParam.Init();
		SourceInfo.VolumeSourceStart = -1.0f;
		SourceInfo.VolumeSourceDestination = -1.0f;
		SourceInfo.VolumeFadeSlope = 0.0f;
		SourceInfo.VolumeFadeStart = 0.0f;
		SourceInfo.VolumeFadeFramePosition = 0;
		SourceInfo.VolumeFadeNumFrames = 0;

		SourceInfo.DistanceAttenuationSourceStart = -1.0f;
		SourceInfo.DistanceAttenuationSourceDestination = -1.0f;

		SourceInfo.LowPassFreq = MAX_FILTER_FREQUENCY;
		SourceInfo.HighPassFreq = MIN_FILTER_FREQUENCY;

		SourceInfo.ResetModulators(MixerDevice->DeviceID);

		SourceInfo.LowPassFilter.Reset();
		SourceInfo.HighPassFilter.Reset();
		SourceInfo.CurrentPCMBuffer = nullptr;
		SourceInfo.CurrentAudioChunkNumFrames = 0;
		SourceInfo.SourceBuffer.Reset();
		SourceInfo.PreDistanceAttenuationBuffer.Reset();
		SourceInfo.SourceEffectScratchBuffer.Reset();
		SourceInfo.AudioPluginOutputData.AudioBuffer.Reset();
		SourceInfo.CurrentFrameValues.Reset();
		SourceInfo.NextFrameValues.Reset();
		SourceInfo.CurrentFrameAlpha = 0.0f;
		SourceInfo.CurrentFrameIndex = 0;
		SourceInfo.NumFramesPlayed = 0;
		SourceInfo.StartTime = 0.0;
		SourceInfo.bIs3D = false;
		SourceInfo.bIsCenterChannelOnly = false;
		SourceInfo.bIsActive = false;
		SourceInfo.bIsPlaying = false;
		SourceInfo.bIsDone = true;
		SourceInfo.bIsLastBuffer = false;
		SourceInfo.bIsPaused = false;
		SourceInfo.bIsPausedForQuantization = false;
		SourceInfo.bDelayLineSet = false;
		SourceInfo.bIsStopping = false;
		SourceInfo.bIsBusy = false;
		SourceInfo.bUseHRTFSpatializer = false;
		SourceInfo.bIsExternalSend = false;
		SourceInfo.bUseOcclusionPlugin = false;
		SourceInfo.bUseReverbPlugin = false;
		SourceInfo.bHasStarted = false;
		SourceInfo.bEnableBusSends = false;
		SourceInfo.bEnableBaseSubmix = false;
		SourceInfo.bEnableSubmixSends = false;
		SourceInfo.bIsBypassingLPF = false;
		SourceInfo.bIsBypassingHPF = false;
		SourceInfo.bHasPreDistanceAttenuationSend = false;
		SourceInfo.bModFiltersUpdated = false;

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		SourceInfo.bIsDebugMode = false;
		SourceInfo.DebugName = FString();
#endif //AUDIO_MIXER_ENABLE_DEBUG_MODE

		SourceInfo.NumInputChannels = 0;
		SourceInfo.NumPostEffectChannels = 0;

		GameThreadInfo.bNeedsSpeakerMap[SourceId] = false;
	}

	void FMixerSourceManager::BuildSourceEffectChain(const int32 SourceId, FSoundEffectSourceInitData& InitData, const TArray<FSourceEffectChainEntry>& InSourceEffectChain, TArray<TSoundEffectSourcePtr>& OutSourceEffects)
	{
		// Create new source effects. The memory will be owned by the source manager.
		FScopeLock ScopeLock(&EffectChainMutationCriticalSection);
		for (const FSourceEffectChainEntry& ChainEntry : InSourceEffectChain)
		{
			// Presets can have null entries
			if (!ChainEntry.Preset)
			{
				continue;
			}

			// Get this source effect presets unique id so instances can identify their originating preset object
			const uint32 PresetUniqueId = ChainEntry.Preset->GetUniqueID();
			InitData.ParentPresetUniqueId = PresetUniqueId;

			TSoundEffectSourcePtr NewEffect = USoundEffectPreset::CreateInstance<FSoundEffectSourceInitData, FSoundEffectSource>(InitData, *ChainEntry.Preset);
			NewEffect->SetEnabled(!ChainEntry.bBypass);

			OutSourceEffects.Add(NewEffect);
		}
	}

	void FMixerSourceManager::ResetSourceEffectChain(const int32 SourceId)
	{
		FScopeLock ScopeLock(&EffectChainMutationCriticalSection);
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Unregister these source effect instances from their owning USoundEffectInstance on the next audio thread tick.
 			const ENamedThreads::Type UnregistrationThread = IsAudioThreadRunning() ? ENamedThreads::AudioThread: ENamedThreads::GameThread;
			AsyncTask(UnregistrationThread, [SourceEffects = MoveTemp(SourceInfo.SourceEffects)]() mutable
			{
				for (int32 i = 0; i < SourceEffects.Num(); ++i)
				{
					USoundEffectPreset::UnregisterInstance(SourceEffects[i]);
				}
			});

			SourceInfo.SourceEffects.Reset();

			for (int32 i = 0; i < SourceInfo.SourceEffectPresets.Num(); ++i)
			{
				SourceInfo.SourceEffectPresets[i] = nullptr;
			}
			SourceInfo.SourceEffectPresets.Reset();
		}
	}

	bool FMixerSourceManager::GetFreeSourceId(int32& OutSourceId)
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		if (GameThreadInfo.FreeSourceIndices.Num())
		{
			OutSourceId = GameThreadInfo.FreeSourceIndices.Pop();

			AUDIO_MIXER_CHECK(OutSourceId < NumTotalSources);
			AUDIO_MIXER_CHECK(!GameThreadInfo.bIsBusy[OutSourceId]);

			AUDIO_MIXER_CHECK(!GameThreadInfo.bIsDebugMode[OutSourceId]);
			AUDIO_MIXER_CHECK(NumActiveSources < NumTotalSources);
			++NumActiveSources;

			GameThreadInfo.bIsBusy[OutSourceId] = true;
			return true;
		}
		AUDIO_MIXER_CHECK(false);
		return false;
	}

	int32 FMixerSourceManager::GetNumActiveSources() const
	{
		return NumActiveSources;
	}

	int32 FMixerSourceManager::GetNumActiveAudioBuses() const
	{
		return AudioBuses.Num();
	}

	void FMixerSourceManager::InitSource(const int32 SourceId, const FMixerSourceVoiceInitParams& InitParams)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK(!GameThreadInfo.bIsDebugMode[SourceId]);
		AUDIO_MIXER_CHECK(InitParams.SourceListener != nullptr);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		GameThreadInfo.bIsDebugMode[SourceId] = InitParams.bIsDebugMode;
#endif 

		// Make sure we flag that this source needs a speaker map to at least get one
		GameThreadInfo.bNeedsSpeakerMap[SourceId] = true;

		GameThreadInfo.bIsUsingHRTFSpatializer[SourceId] = InitParams.bUseHRTFSpatialization;

		// Need to build source effect instances on the audio thread
		FSoundEffectSourceInitData InitData;
		InitData.SampleRate = MixerDevice->SampleRate;
		InitData.NumSourceChannels = InitParams.NumInputChannels;
		InitData.AudioClock = MixerDevice->GetAudioTime();
		InitData.AudioDeviceId = MixerDevice->DeviceID;

		TArray<TSoundEffectSourcePtr> SourceEffectChain;
		BuildSourceEffectChain(SourceId, InitData, InitParams.SourceEffectChain, SourceEffectChain);

		FModulationDestination VolumeModulation;
		VolumeModulation.Init(MixerDevice->DeviceID, FName("Volume"), false /* bInIsBuffered */, true /* bInValueLinear */);
		VolumeModulation.UpdateModulator(InitParams.ModulationSettings.VolumeModulationDestination.Modulator);

		FModulationDestination PitchModulation;
		PitchModulation.Init(MixerDevice->DeviceID, FName("Pitch"), false /* bInIsBuffered */);
		PitchModulation.UpdateModulator(InitParams.ModulationSettings.PitchModulationDestination.Modulator);

		FModulationDestination HighpassModulation;
		HighpassModulation.Init(MixerDevice->DeviceID, FName("HPFCutoffFrequency"), false /* bInIsBuffered */);
		HighpassModulation.UpdateModulator(InitParams.ModulationSettings.HighpassModulationDestination.Modulator);

		FModulationDestination LowpassModulation;
		LowpassModulation.Init(MixerDevice->DeviceID, FName("LPFCutoffFrequency"), false /* bInIsBuffered */);
		LowpassModulation.UpdateModulator(InitParams.ModulationSettings.LowpassModulationDestination.Modulator);

		AudioMixerThreadCommand([this, SourceId, InitParams, VolumeModulation, HighpassModulation, LowpassModulation, PitchModulation, SourceEffectChain]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
			AUDIO_MIXER_CHECK(InitParams.SourceVoice != nullptr);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Initialize the mixer source buffer decoder with the given mixer buffer
			SourceInfo.MixerSourceBuffer = InitParams.MixerSourceBuffer;
			AUDIO_MIXER_CHECK(SourceInfo.MixerSourceBuffer.IsValid());
			SourceInfo.MixerSourceBuffer->Init();
			SourceInfo.MixerSourceBuffer->OnBeginGenerate();

			SourceInfo.bIs3D = InitParams.bIs3D;
			SourceInfo.bIsPlaying = false;
			SourceInfo.bIsPaused = false;
			SourceInfo.bIsPausedForQuantization = false;
			SourceInfo.bDelayLineSet = false;
			SourceInfo.bIsStopping = false;
			SourceInfo.bIsActive = true;
			SourceInfo.bIsBusy = true;
			SourceInfo.bIsDone = false;
			SourceInfo.bIsLastBuffer = false;
			SourceInfo.bUseHRTFSpatializer = InitParams.bUseHRTFSpatialization;
			SourceInfo.bIsExternalSend = InitParams.bIsExternalSend;
			SourceInfo.bIsVorbis = InitParams.bIsVorbis;
			SourceInfo.AudioComponentID = InitParams.AudioComponentID;
			SourceInfo.bIsSoundfield = InitParams.bIsSoundfield;

			// Call initialization from the render thread so anything wanting to do any initialization here can do so (e.g. procedural sound waves)
			SourceInfo.SourceListener = InitParams.SourceListener;
			SourceInfo.SourceListener->OnBeginGenerate();

			SourceInfo.NumInputChannels = InitParams.NumInputChannels;
			SourceInfo.NumInputFrames = InitParams.NumInputFrames;

			// Initialize the number of per-source LPF filters based on input channels
			SourceInfo.LowPassFilter.Init(MixerDevice->SampleRate, InitParams.NumInputChannels);
			SourceInfo.HighPassFilter.Init(MixerDevice->SampleRate, InitParams.NumInputChannels);

			SourceInfo.SourceEnvelopeFollower = Audio::FEnvelopeFollower(MixerDevice->SampleRate / NumOutputFrames, (float)InitParams.EnvelopeFollowerAttackTime, (float)InitParams.EnvelopeFollowerReleaseTime, Audio::EPeakMode::Peak);

			SourceInfo.VolumeModulation = VolumeModulation;
			SourceInfo.PitchModulation = PitchModulation;
			SourceInfo.LowpassModulation = LowpassModulation;
			SourceInfo.HighpassModulation = HighpassModulation;

			// Pass required info to clock manager
			const FQuartzQuantizedRequestData& QuantData = InitParams.QuantizedRequestData;
			if (QuantData.QuantizedCommandPtr)
			{
				if (false == MixerDevice->QuantizedEventClockManager.DoesClockExist(QuantData.ClockName))
				{
					UE_LOG(LogAudioMixer, Warning, TEXT("Quantization Clock: '%s' Does not exist."), *QuantData.ClockName.ToString());
					QuantData.QuantizedCommandPtr->Cancel();
				}
				else
				{
					FQuartzQuantizedCommandInitInfo QuantCommandInitInfo(QuantData, SourceId);
					SourceInfo.QuantizedCommandHandle = MixerDevice->QuantizedEventClockManager.AddCommandToClock(QuantCommandInitInfo);
				}
			}


			// Create the spatialization plugin source effect
			if (InitParams.bUseHRTFSpatialization)
			{
				AUDIO_MIXER_CHECK(bUsingSpatializationPlugin);
				LLM_SCOPE(ELLMTag::AudioMixerPlugins);
				SpatializationPlugin->OnInitSource(SourceId, InitParams.AudioComponentUserID, InitParams.SpatializationPluginSettings);
			}

			// Create the occlusion plugin source effect
			if (InitParams.OcclusionPluginSettings != nullptr)
			{
				MixerDevice->OcclusionInterface->OnInitSource(SourceId, InitParams.AudioComponentUserID, InitParams.NumInputChannels, InitParams.OcclusionPluginSettings);
				SourceInfo.bUseOcclusionPlugin = true;
			}

			// Create the reverb plugin source effect
			if (InitParams.ReverbPluginSettings != nullptr)
			{
				MixerDevice->ReverbPluginInterface->OnInitSource(SourceId, InitParams.AudioComponentUserID, InitParams.NumInputChannels, InitParams.ReverbPluginSettings);
				SourceInfo.bUseReverbPlugin = true;
			}

			// Default all sounds to not consider effect chain tails when playing
			SourceInfo.bEffectTailsDone = true;

			// Which forms of routing to enable
			SourceInfo.bEnableBusSends = InitParams.bEnableBusSends;
			SourceInfo.bEnableBaseSubmix = InitParams.bEnableBaseSubmix;
			SourceInfo.bEnableSubmixSends = InitParams.bEnableSubmixSends;

			// Copy the source effect chain if the channel count is 1 or 2
			if (InitParams.NumInputChannels <= 2)
			{
				// If we're told to care about effect chain tails, then we're not allowed
				// to stop playing until the effect chain tails are finished
				SourceInfo.bEffectTailsDone = !InitParams.bPlayEffectChainTails;
				SourceInfo.SourceEffectChainId = InitParams.SourceEffectChainId;
				
				// Add the effect chain instances 
				SourceInfo.SourceEffects = SourceEffectChain;
				
				// Add a slot entry for the preset so it can change while running. This will get sent to the running effect instance if the preset changes.
				SourceInfo.SourceEffectPresets.Add(nullptr);
				// If this is going to be a source bus, add this source id to the list of active bus ids
				if (InitParams.AudioBusId != INDEX_NONE)
				{
					// Setting this BusId will flag this source as a bus. It doesn't try to generate 
					// audio in the normal way but instead will render in a second stage, after normal source rendering.
					SourceInfo.AudioBusId = InitParams.AudioBusId;

					// Source bus duration allows us to stop a bus after a given time
					if (InitParams.SourceBusDuration != 0.0f)
					{
						SourceInfo.SourceBusDurationFrames = InitParams.SourceBusDuration * MixerDevice->GetSampleRate();
					}

					// Register this bus as an instance
					TSharedPtr<FMixerAudioBus> AudioBusPtr = AudioBuses.FindRef(SourceInfo.AudioBusId);
					if (AudioBusPtr.IsValid())
					{
						// If this bus is already registered, add this as a source id
						AudioBusPtr->AddInstanceId(SourceId, InitParams.NumInputChannels);
					}
					else
					{
						// If the bus is not registered, make a new entry. This will default to an automatic audio bus until explicitly made manual later.
						TSharedPtr<FMixerAudioBus> NewAudioBus = TSharedPtr<FMixerAudioBus>(new FMixerAudioBus(this, true, InitParams.NumInputChannels));
						NewAudioBus->AddInstanceId(SourceId, InitParams.NumInputChannels);

						AudioBuses.Add(InitParams.AudioBusId, NewAudioBus);
					}
				}

			}

			// Iterate through source's bus sends and add this source to the bus send list
			// Note: buses can also send their audio to other buses.
			for (int32 BusSendType = 0; BusSendType < (int32)EBusSendType::Count; ++BusSendType)
			{
				for (const FInitAudioBusSend& AudioBusSend : InitParams.AudioBusSends[BusSendType])
				{
					// New struct to map which source (SourceId) is sending to the bus
					FAudioBusSend NewAudioBusSend;
					NewAudioBusSend.SourceId = SourceId;
					NewAudioBusSend.SendLevel = AudioBusSend.SendLevel;

					// Get existing BusId and add the send, or create new bus registration
					TSharedPtr<FMixerAudioBus> AudioBusPtr = AudioBuses.FindRef(AudioBusSend.AudioBusId);
					if (AudioBusPtr.IsValid())
					{
						AudioBusPtr->AddSend((EBusSendType)BusSendType, NewAudioBusSend);
					}
					else
					{
						// If the bus is not registered, make a new entry. This will default to an automatic audio bus until explicitly made manual later.
						TSharedPtr<FMixerAudioBus> NewAudioBus(new FMixerAudioBus(this, true, FMath::Min(2, InitParams.NumInputChannels)));

						// Add a send to it. This will not have a bus instance id (i.e. won't output audio), but 
						// we register the send anyway in the event that this bus does play, we'll know to send this
						// source's audio to it.
						NewAudioBus->AddSend((EBusSendType)BusSendType, NewAudioBusSend);

						AudioBuses.Add(AudioBusSend.AudioBusId, NewAudioBus);
					}

					// Store on this source, which buses its sending its audio to
					SourceInfo.AudioBusSends[BusSendType].Add(AudioBusSend.AudioBusId);
				}
			}

			SourceInfo.CurrentFrameValues.Init(0.0f, InitParams.NumInputChannels);
			SourceInfo.NextFrameValues.Init(0.0f, InitParams.NumInputChannels);

			AUDIO_MIXER_CHECK(MixerSources[SourceId] == nullptr);
			MixerSources[SourceId] = InitParams.SourceVoice;

			// Loop through the source's sends and add this source to those submixes with the send info

			AUDIO_MIXER_CHECK(SourceInfo.SubmixSends.Num() == 0);

			// Initialize a new downmix data:
			check(SourceId < SourceInfos.Num());
			const int32 SourceInputChannels = (SourceInfo.bUseHRTFSpatializer && !SourceInfo.bIsExternalSend) ? 2 : SourceInfo.NumInputChannels;

			// Collect the soundfield encoding keys we need to initialize with our output buffers
			TArray<FMixerSubmixPtr> SoundfieldSubmixSends;

			for (int32 i = 0; i < InitParams.SubmixSends.Num(); ++i)
			{
				const FMixerSourceSubmixSend& MixerSubmixSend = InitParams.SubmixSends[i];

				FMixerSubmixPtr SubmixPtr = MixerSubmixSend.Submix.Pin();
				if (SubmixPtr.IsValid())
				{
					SourceInfo.SubmixSends.Add(MixerSubmixSend);

					if (MixerSubmixSend.SubmixSendStage == EMixerSourceSubmixSendStage::PreDistanceAttenuation)
					{
						SourceInfo.bHasPreDistanceAttenuationSend = true;
					}

					SubmixPtr->AddOrSetSourceVoice(InitParams.SourceVoice, MixerSubmixSend.SendLevel, MixerSubmixSend.SubmixSendStage);
					
					if (SubmixPtr->IsSoundfieldSubmix())
					{
						SoundfieldSubmixSends.Add(SubmixPtr);
					}
				}
			}

			// Initialize the submix output source for this source id
			FMixerSourceSubmixOutputBuffer& SourceSubmixOutputBuffer = SourceSubmixOutputBuffers[SourceId];

			FMixerSourceSubmixOutputBufferSettings SourceSubmixOutputResetSettings;
			SourceSubmixOutputResetSettings.NumOutputChannels = MixerDevice->GetDeviceOutputChannels();
			SourceSubmixOutputResetSettings.NumSourceChannels = SourceInputChannels;
			SourceSubmixOutputResetSettings.SoundfieldSubmixSends = SoundfieldSubmixSends;
			SourceSubmixOutputResetSettings.bIs3D = SourceInfo.bIs3D;
			SourceSubmixOutputResetSettings.bIsSoundfield = SourceInfo.bIsSoundfield;

			SourceSubmixOutputBuffer.Reset(SourceSubmixOutputResetSettings);

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
			AUDIO_MIXER_CHECK(!SourceInfo.bIsDebugMode);
			SourceInfo.bIsDebugMode = InitParams.bIsDebugMode;

			AUDIO_MIXER_CHECK(SourceInfo.DebugName.IsEmpty());
			SourceInfo.DebugName = InitParams.DebugName;
#endif 

			AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Is initializing"));
		});
	}

	void FMixerSourceManager::ReleaseSourceId(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AUDIO_MIXER_CHECK(NumActiveSources > 0);
		--NumActiveSources;

		GameThreadInfo.bIsBusy[SourceId] = false;

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		GameThreadInfo.bIsDebugMode[SourceId] = false;
#endif

		GameThreadInfo.FreeSourceIndices.Push(SourceId);

		AUDIO_MIXER_CHECK(GameThreadInfo.FreeSourceIndices.Contains(SourceId));

		AudioMixerThreadCommand([this, SourceId]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			ReleaseSource(SourceId);
		});
	}

	void FMixerSourceManager::StartAudioBus(uint32 InAudioBusId, int32 InNumChannels, bool bInIsAutomatic)
	{
		if (AudioBusIds_AudioThread.Contains(InAudioBusId))
		{
			return;
		}

		AudioBusIds_AudioThread.Add(InAudioBusId);

		AudioMixerThreadCommand([this, InAudioBusId, InNumChannels, bInIsAutomatic]()
		{
			// If this audio bus id already exists, set it to not be automatic and return it
			TSharedPtr<FMixerAudioBus> AudioBusPtr = AudioBuses.FindRef(InAudioBusId);
			if (AudioBusPtr.IsValid())
			{
				// If this audio bus already existed, make sure the num channels lines up
				ensure(AudioBusPtr->GetNumChannels() == InNumChannels);
				AudioBusPtr->SetAutomatic(bInIsAutomatic);
			}
			else
			{
				// If the bus is not registered, make a new entry.
				TSharedPtr<FMixerAudioBus> NewBusData(new FMixerAudioBus(this, bInIsAutomatic, InNumChannels));

				AudioBuses.Add(InAudioBusId, NewBusData);
			}

			//  Now add any existing playing sources to this audio bus as sends if they exist
			for (FSourceInfo& SourceInfo : SourceInfos)
			{
				if (SourceInfo.AudioBusId == InAudioBusId)
				{
					SourceInfo.bIsPlaying = false;
					SourceInfo.bIsPaused = false;
					SourceInfo.bIsActive = false;
					SourceInfo.bIsStopping = false;
				}
			}
		});
	}

	void FMixerSourceManager::StopAudioBus(uint32 InAudioBusId)
	{
		if (!AudioBusIds_AudioThread.Contains(InAudioBusId))
		{
			return;
		}

		AudioBusIds_AudioThread.Remove(InAudioBusId);

		AudioMixerThreadCommand([this, InAudioBusId]()
		{
			TSharedPtr<FMixerAudioBus>* AudioBusPtr = AudioBuses.Find(InAudioBusId);
			if (AudioBusPtr)
			{
				if (!(*AudioBusPtr)->IsAutomatic())
				{
					// Immediately stop all sources which were source buses
					for (FSourceInfo& SourceInfo : SourceInfos)
					{
						if (SourceInfo.AudioBusId == InAudioBusId)
						{
							SourceInfo.bIsPlaying = false;
							SourceInfo.bIsPaused = false;
							SourceInfo.bIsActive = false;
							SourceInfo.bIsStopping = false;
						}
					}
					AudioBuses.Remove(InAudioBusId);
				}
			}
		});
	}

	bool FMixerSourceManager::IsAudioBusActive(uint32 InAudioBusId)
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);
		return AudioBusIds_AudioThread.Contains(InAudioBusId);
	}

	FPatchOutputStrongPtr FMixerSourceManager::AddPatchForAudioBus(uint32 InAudioBusId, float PatchGain)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
		TSharedPtr<FMixerAudioBus> AudioBusPtr = AudioBuses.FindRef(InAudioBusId);
		if (AudioBusPtr.IsValid())
		{
			return AudioBusPtr->AddNewPatch(NumOutputFrames * AudioBusPtr->GetNumChannels(), PatchGain);
		}
		return nullptr;
	}

	void FMixerSourceManager::Play(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		// Compute the frame within which to start the sound based on the current "thread faction" on the audio thread
		double StartTime = MixerDevice->GetAudioThreadTime();

		AudioMixerThreadCommand([this, SourceId, StartTime]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];
			
			SourceInfo.bIsPlaying = true;
			SourceInfo.bIsPaused = false;
			SourceInfo.bIsActive = true;

			SourceInfo.StartTime = StartTime;

			AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Is playing"));
		});
	}

	void FMixerSourceManager::Stop(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId]()
		{
			StopInternal(SourceId);
		});
	}

	void FMixerSourceManager::StopInternal(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		FSourceInfo& SourceInfo = SourceInfos[SourceId];

		SourceInfo.bIsPlaying = false;
		SourceInfo.bIsPaused = false;
		SourceInfo.bIsActive = false;
		SourceInfo.bIsStopping = false;

		if (SourceInfo.bIsPausedForQuantization)
		{
			UE_LOG(LogAudioMixer, Display, TEXT("StopInternal() cancelling command [%s]"), *SourceInfo.QuantizedCommandHandle.CommandPtr->GetCommandName().ToString());
			SourceInfo.QuantizedCommandHandle.Cancel();
			SourceInfo.bIsPausedForQuantization = false;
		}

		AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Is immediately stopping"));
	}

	void FMixerSourceManager::StopFade(const int32 SourceId, const int32 NumFrames)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK(NumFrames > 0);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);


		AudioMixerThreadCommand([this, SourceId, NumFrames]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			SourceInfo.bIsPaused = false;
			SourceInfo.bIsStopping = true;

			if (SourceInfo.bIsPausedForQuantization)
			{
				// no need to fade, we haven't actually started playing
				StopInternal(SourceId);
				return;
			}
			
			// Only allow multiple of 4 fade frames and positive
			int32 NumFadeFrames = AlignArbitrary(NumFrames, 4);
			if (NumFadeFrames <= 0)
			{
				// Stop immediately if we've been given no fade frames
				SourceInfo.bIsPlaying = false;
				SourceInfo.bIsPaused = false;
				SourceInfo.bIsActive = false;
				SourceInfo.bIsStopping = false;
			}
			else
			{
				// compute the fade slope
				SourceInfo.VolumeFadeStart = SourceInfo.VolumeSourceStart;
				SourceInfo.VolumeFadeNumFrames = NumFadeFrames;
				SourceInfo.VolumeFadeSlope = -SourceInfo.VolumeSourceStart / SourceInfo.VolumeFadeNumFrames;
				SourceInfo.VolumeFadeFramePosition = 0;
			}

			AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Is stopping with fade"));
		});
	}


	void FMixerSourceManager::Pause(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			SourceInfo.bIsPaused = true;
			SourceInfo.bIsActive = false;
		});
	}

	void FMixerSourceManager::SetPitch(const int32 SourceId, const float Pitch)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);

		AudioMixerThreadCommand([this, SourceId, Pitch]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
			check(NumOutputFrames > 0);

			SourceInfos[SourceId].PitchSourceParam.SetValue(Pitch, NumOutputFrames);
		});
	}

	void FMixerSourceManager::SetVolume(const int32 SourceId, const float Volume)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, Volume]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
			check(NumOutputFrames > 0);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Only set the volume if we're not stopping. Stopping sources are setting their volume to 0.0.
			if (!SourceInfo.bIsStopping)
			{
				// If we've not yet set a volume, we need to immediately set the start and destination to be the same value (to avoid an initial fade in)
				if (SourceInfos[SourceId].VolumeSourceDestination < 0.0f)
				{
					SourceInfos[SourceId].VolumeSourceStart = Volume;
				}

				SourceInfos[SourceId].VolumeSourceDestination = Volume;
			}
		});
	}

	void FMixerSourceManager::SetDistanceAttenuation(const int32 SourceId, const float DistanceAttenuation)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, DistanceAttenuation]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
			check(NumOutputFrames > 0);

			// If we've not yet set a distance attenuation, we need to immediately set the start and destination to be the same value (to avoid an initial fade in)
			if (SourceInfos[SourceId].DistanceAttenuationSourceDestination < 0.0f)
			{
				SourceInfos[SourceId].DistanceAttenuationSourceStart = DistanceAttenuation;
			}

			SourceInfos[SourceId].DistanceAttenuationSourceDestination = DistanceAttenuation;
		});
	}

	void FMixerSourceManager::SetSpatializationParams(const int32 SourceId, const FSpatializationParams& InParams)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InParams]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			SourceInfos[SourceId].SpatParams = InParams;
		});
	}

	void FMixerSourceManager::SetChannelMap(const int32 SourceId, const uint32 NumInputChannels, const Audio::AlignedFloatBuffer& ChannelMap, const bool bInIs3D, const bool bInIsCenterChannelOnly)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, NumInputChannels, ChannelMap, bInIs3D, bInIsCenterChannelOnly]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			check(NumOutputFrames > 0);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];
			FMixerSourceSubmixOutputBuffer& SourceSubmixOutput = SourceSubmixOutputBuffers[SourceId];

			if (SourceSubmixOutput.GetNumSourceChannels() != NumInputChannels && !SourceInfo.bUseHRTFSpatializer)
			{
				// This means that this source has been reinitialized as a different source while this command was in flight,
				// In which case it is of no use to us. Exit.
				return;
			}

			// Set whether or not this is a 3d channel map and if its center channel only. Used for reseting channel maps on device change.
			SourceInfo.bIs3D = bInIs3D;
			SourceInfo.bIsCenterChannelOnly = bInIsCenterChannelOnly;

			bool bNeedsSpeakerMap = SourceSubmixOutput.SetChannelMap(ChannelMap, bInIsCenterChannelOnly);
			GameThreadInfo.bNeedsSpeakerMap[SourceId] = bNeedsSpeakerMap;
		});
	}

	void FMixerSourceManager::SetLPFFrequency(const int32 SourceId, const float InLPFFrequency)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InLPFFrequency]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// LowPassFreq is cached off as the version set by this setter as well as that internal to the LPF.
			// There is a second cutoff frequency cached in SourceInfo.LowpassModulation updated per buffer callback.
			// On callback, the client version may be overridden with the modulation LPF value depending on which is more aggressive.  
			SourceInfo.LowPassFreq = InLPFFrequency;
			SourceInfo.LowPassFilter.StartFrequencyInterpolation(InLPFFrequency, NumOutputFrames);
		});
	}

	void FMixerSourceManager::SetHPFFrequency(const int32 SourceId, const float InHPFFrequency)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InHPFFrequency]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// HighPassFreq is cached off as the version set by this setter as well as that internal to the HPF.
			// There is a second cutoff frequency cached in SourceInfo.HighpassModulation updated per buffer callback.
			// On callback, the client version may be overridden with the modulation HPF value depending on which is more aggressive.  
			SourceInfo.HighPassFreq = InHPFFrequency;
			SourceInfo.HighPassFilter.StartFrequencyInterpolation(InHPFFrequency, NumOutputFrames);
		});
	}

	void FMixerSourceManager::SetModLPFFrequency(const int32 SourceId, const float InLPFFrequency)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InLPFFrequency]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];
			SourceInfo.LowpassModulationBase = InLPFFrequency;
			SourceInfo.bModFiltersUpdated = true;
		});
	}

	void FMixerSourceManager::SetModHPFFrequency(const int32 SourceId, const float InHPFFrequency)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InHPFFrequency]()
			{
				AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

				FSourceInfo& SourceInfo = SourceInfos[SourceId];
				SourceInfo.HighpassModulationBase = InHPFFrequency;
				SourceInfo.bModFiltersUpdated = true;
			});
	}

	void FMixerSourceManager::SetModVolume(const int32 SourceId, const float InModVolume)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InModVolume]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];
			SourceInfo.VolumeModulationBase = InModVolume;
		});
	}

	void FMixerSourceManager::SetModPitch(const int32 SourceId, const float InModPitch)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InModPitch]()
			{
				AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

				FSourceInfo& SourceInfo = SourceInfos[SourceId];
				SourceInfo.PitchModulationBase = InModPitch;
			});
	}

	void FMixerSourceManager::SetSubmixSendInfo(const int32 SourceId, const FMixerSourceSubmixSend& InSubmixSend)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InSubmixSend]()
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			FMixerSubmixPtr InSubmixPtr = InSubmixSend.Submix.Pin();
			if (InSubmixPtr.IsValid())
			{
				bool bIsNew = true;
				
				SourceInfo.bHasPreDistanceAttenuationSend = false;
				for (FMixerSourceSubmixSend& SubmixSend : SourceInfo.SubmixSends)
				{
					FMixerSubmixPtr SubmixPtr = SubmixSend.Submix.Pin();
					if (SubmixPtr.IsValid())
					{
						if (SubmixSend.SubmixSendStage == EMixerSourceSubmixSendStage::PreDistanceAttenuation)
						{
							SourceInfo.bHasPreDistanceAttenuationSend = true;
						}
					
						if (SubmixPtr->GetId() == InSubmixPtr->GetId())
						{
							SubmixSend.SendLevel = InSubmixSend.SendLevel;
							SubmixSend.SubmixSendStage = InSubmixSend.SubmixSendStage;
							bIsNew = false;
							if (SourceInfo.bHasPreDistanceAttenuationSend)
							{
								break;
							}
						}
					}
				}

				if (bIsNew)
				{
					SourceInfo.SubmixSends.Add(InSubmixSend);
				}
				
				// If we don't have a pre-distance attenuation send, lets zero out the buffer so the output buffer stops doing math with it.
				if (!SourceInfo.bHasPreDistanceAttenuationSend)
				{
					SourceSubmixOutputBuffers[SourceId].SetPreAttenuationSourceBuffer(nullptr);
				}

				InSubmixPtr->AddOrSetSourceVoice(MixerSources[SourceId], InSubmixSend.SendLevel, InSubmixSend.SubmixSendStage);
			}
		});
	}

	void FMixerSourceManager::ClearSubmixSendInfo(const int32 SourceId, const FMixerSourceSubmixSend& InSubmixSend)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InSubmixSend]()
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			FMixerSubmixPtr InSubmixPtr = InSubmixSend.Submix.Pin();
			if (InSubmixPtr.IsValid())
			{
				for (int32 i = SourceInfo.SubmixSends.Num() - 1; i >= 0; --i)
				{
					if (SourceInfo.SubmixSends[i].Submix == InSubmixSend.Submix)
					{
						SourceInfo.SubmixSends.RemoveAtSwap(i, 1, false);
					}
				}

				// Update the has predist attenuation send state
				SourceInfo.bHasPreDistanceAttenuationSend = false;
				for (FMixerSourceSubmixSend& SubmixSend : SourceInfo.SubmixSends)
				{
					FMixerSubmixPtr SubmixPtr = SubmixSend.Submix.Pin();
					if (SubmixPtr.IsValid())
					{
						if (SubmixSend.SubmixSendStage == EMixerSourceSubmixSendStage::PreDistanceAttenuation)
						{
							SourceInfo.bHasPreDistanceAttenuationSend = true;
							break;
						}
					}
				}

				// If we don't have a pre-distance attenuation send, lets zero out the buffer so the output buffer stops doing math with it.
				if (!SourceInfo.bHasPreDistanceAttenuationSend)
				{
					SourceSubmixOutputBuffers[SourceId].SetPreAttenuationSourceBuffer(nullptr);
				}

				// Now remove the source voice from the submix send list
				InSubmixPtr->RemoveSourceVoice(MixerSources[SourceId]);
			}
		});
	}

	void FMixerSourceManager::SetBusSendInfo(const int32 SourceId, EBusSendType InAudioBusSendType, uint32 AudioBusId, float BusSendLevel)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InAudioBusSendType, AudioBusId, BusSendLevel]()
		{
			// Create mapping of source id to bus send level
			FAudioBusSend BusSend;
			BusSend.SourceId = SourceId;
			BusSend.SendLevel = BusSendLevel;

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Retrieve the bus we want to send audio to
			TSharedPtr<FMixerAudioBus>* AudioBusPtr = AudioBuses.Find(AudioBusId);

			// If we already have a bus, we update the amount of audio we want to send to it
			if (AudioBusPtr)
			{
				(*AudioBusPtr)->AddSend(InAudioBusSendType, BusSend);
			}
			else
			{
				// If the bus is not registered, make a new entry on the send
				TSharedPtr<FMixerAudioBus> NewBusData(new FMixerAudioBus(this, true, SourceInfo.NumInputChannels));

				// Add a send to it. This will not have a bus instance id (i.e. won't output audio), but 
				// we register the send anyway in the event that this bus does play, we'll know to send this
				// source's audio to it.
				NewBusData->AddSend(InAudioBusSendType, BusSend);

				AudioBuses.Add(AudioBusId, NewBusData);
			}

			// Check to see if we need to create new bus data. If we are not playing a bus with this id, then we
			// need to create a slot for it such that when a bus does play, it'll start rendering audio from this source
			bool bExisted = false;
			for (uint32 BusId : SourceInfo.AudioBusSends[(int32)InAudioBusSendType])
			{
				if (BusId == AudioBusId)
				{
					bExisted = true;
					break;
				}
			}

			if (!bExisted)
			{
				SourceInfo.AudioBusSends[(int32)InAudioBusSendType].Add(AudioBusId);
			}
		});
	}

	void FMixerSourceManager::SetListenerTransforms(const TArray<FTransform>& InListenerTransforms)
	{
		AudioMixerThreadCommand([this, InListenerTransforms]()
		{
			ListenerTransforms = InListenerTransforms;
		});
	}

	const TArray<FTransform>* FMixerSourceManager::GetListenerTransforms() const
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
		return &ListenerTransforms;
	}

	int64 FMixerSourceManager::GetNumFramesPlayed(const int32 SourceId) const
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);
		return SourceInfos[SourceId].NumFramesPlayed;
	}

	float FMixerSourceManager::GetEnvelopeValue(const int32 SourceId) const
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);
		return SourceInfos[SourceId].SourceEnvelopeValue;
	}

	bool FMixerSourceManager::IsUsingHRTFSpatializer(const int32 SourceId) const
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);
		return GameThreadInfo.bIsUsingHRTFSpatializer[SourceId];
	}

	bool FMixerSourceManager::NeedsSpeakerMap(const int32 SourceId) const
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);
		return GameThreadInfo.bNeedsSpeakerMap[SourceId];
	}

	void FMixerSourceManager::ReadSourceFrame(const int32 SourceId)
	{
		FSourceInfo& SourceInfo = SourceInfos[SourceId];

		const int32 NumChannels = SourceInfo.NumInputChannels;

		// Check if the next frame index is out of range of the total number of frames we have in our current audio buffer
		bool bNextFrameOutOfRange = (SourceInfo.CurrentFrameIndex + 1) >= SourceInfo.CurrentAudioChunkNumFrames;
		bool bCurrentFrameOutOfRange = SourceInfo.CurrentFrameIndex >= SourceInfo.CurrentAudioChunkNumFrames;

		bool bReadCurrentFrame = true;

		// Check the boolean conditions that determine if we need to pop buffers from our queue (in PCMRT case) *OR* loop back (looping PCM data)
		while (bNextFrameOutOfRange || bCurrentFrameOutOfRange)
		{
			// If our current frame is in range, but next frame isn't, read the current frame now to avoid pops when transitioning between buffers
			if (bNextFrameOutOfRange && !bCurrentFrameOutOfRange)
			{
				// Don't need to read the current frame audio after reading new audio chunk
				bReadCurrentFrame = false;

				AUDIO_MIXER_CHECK(SourceInfo.CurrentPCMBuffer.IsValid());
				const float* AudioData = SourceInfo.CurrentPCMBuffer->AudioData.GetData();
				const int32 CurrentSampleIndex = SourceInfo.CurrentFrameIndex * NumChannels;

				for (int32 Channel = 0; Channel < NumChannels; ++Channel)
				{
					SourceInfo.CurrentFrameValues[Channel] = AudioData[CurrentSampleIndex + Channel];
				}
			}

			// If this is our first PCM buffer, we don't need to do a callback to get more audio
			if (SourceInfo.CurrentPCMBuffer.IsValid())
			{
				if (SourceInfo.CurrentPCMBuffer->LoopCount == Audio::LOOP_FOREVER && !SourceInfo.CurrentPCMBuffer->bRealTimeBuffer)
				{
					AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Hit Loop boundary, looping."));

					SourceInfo.CurrentFrameIndex = FMath::Max(SourceInfo.CurrentFrameIndex - SourceInfo.CurrentAudioChunkNumFrames, 0);
					break;
				}

				if (ensure(SourceInfo.MixerSourceBuffer.IsValid()))
				{
					SourceInfo.MixerSourceBuffer->OnBufferEnd();
				}
			}

			// If we have audio in our queue, we're still playing
			if (ensure(SourceInfo.MixerSourceBuffer.IsValid()) && SourceInfo.MixerSourceBuffer->GetNumBuffersQueued() > 0 && NumChannels > 0)
			{
				SourceInfo.CurrentPCMBuffer = SourceInfo.MixerSourceBuffer->GetNextBuffer();
				SourceInfo.CurrentAudioChunkNumFrames = SourceInfo.CurrentPCMBuffer->AudioData.Num() / NumChannels;

				// Subtract the number of frames in the current buffer from our frame index.
				// Note: if this is the first time we're playing, CurrentFrameIndex will be 0
				if (bReadCurrentFrame)
				{
					SourceInfo.CurrentFrameIndex = FMath::Max(SourceInfo.CurrentFrameIndex - SourceInfo.CurrentAudioChunkNumFrames, 0);
				}
				else
				{
					// Since we're not reading the current frame, we allow the current frame index to be negative (NextFrameIndex will then be 0)
					// This prevents dropping a frame of audio on the buffer boundary
					SourceInfo.CurrentFrameIndex = -1;
				}
			}
			else
			{
				SourceInfo.bIsLastBuffer = !SourceInfo.SubCallbackDelayLengthInFrames;
				SourceInfo.SubCallbackDelayLengthInFrames = 0;
				return;
			}

			bNextFrameOutOfRange = (SourceInfo.CurrentFrameIndex + 1) >= SourceInfo.CurrentAudioChunkNumFrames;
			bCurrentFrameOutOfRange = SourceInfo.CurrentFrameIndex >= SourceInfo.CurrentAudioChunkNumFrames;
		}

		if (SourceInfo.CurrentPCMBuffer.IsValid())
		{
			// Grab the float PCM audio data (which could be a new audio chunk from previous ReadSourceFrame call)
			const float* AudioData = SourceInfo.CurrentPCMBuffer->AudioData.GetData();
			const int32 NextSampleIndex = (SourceInfo.CurrentFrameIndex + 1)  * NumChannels;

			if (bReadCurrentFrame)
			{
				const int32 CurrentSampleIndex = SourceInfo.CurrentFrameIndex * NumChannels;
				for (int32 Channel = 0; Channel < NumChannels; ++Channel)
				{
					SourceInfo.CurrentFrameValues[Channel] = AudioData[CurrentSampleIndex + Channel];
					SourceInfo.NextFrameValues[Channel] = AudioData[NextSampleIndex + Channel];
				}
			}
			else
			{
				for (int32 Channel = 0; Channel < NumChannels; ++Channel)
				{
					SourceInfo.NextFrameValues[Channel] = AudioData[NextSampleIndex + Channel];
				}
			}
		}
	}

	void FMixerSourceManager::ComputeSourceBuffersForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd)
	{
		CSV_SCOPED_TIMING_STAT(Audio, SourceBuffers);
		SCOPE_CYCLE_COUNTER(STAT_AudioMixerSourceBuffers);

		const double AudioRenderThreadTime = MixerDevice->GetAudioRenderThreadTime();
		const double AudioClockDelta = MixerDevice->GetAudioClockDelta();

		for (int32 SourceId = SourceIdStart; SourceId < SourceIdEnd; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			if (!SourceInfo.bIsBusy || !SourceInfo.bIsPlaying || SourceInfo.bIsPaused || SourceInfo.bIsPausedForQuantization)
			{
				continue;
			}

			// If this source is still playing at this point but technically done, zero the buffers. We haven't yet been removed by the FMixerSource owner.
			// This should be rare but could happen due to thread timing since done-ness is queried on audio thread.
			if (SourceInfo.bIsDone)
			{
				const int32 NumSamples = NumOutputFrames * SourceInfo.NumInputChannels;

				SourceInfo.PreDistanceAttenuationBuffer.Reset();
				SourceInfo.PreDistanceAttenuationBuffer.AddZeroed(NumSamples);

				SourceInfo.SourceBuffer.Reset();
				SourceInfo.SourceBuffer.AddZeroed(NumSamples);

				continue;
			}

			const bool bIsSourceBus = SourceInfo.AudioBusId != INDEX_NONE;
			if ((bGenerateBuses && !bIsSourceBus) || (!bGenerateBuses && bIsSourceBus))
			{
				continue;
			}

			// Fill array with elements all at once to avoid sequential Add() operation overhead.
			const int32 NumSamples = NumOutputFrames * SourceInfo.NumInputChannels;
			
			// Initialize both the pre-distance attenuation buffer and the source buffer
			SourceInfo.PreDistanceAttenuationBuffer.Reset();
			SourceInfo.PreDistanceAttenuationBuffer.AddZeroed(NumSamples);


			SourceInfo.SourceEffectScratchBuffer.Reset();
			SourceInfo.SourceEffectScratchBuffer.AddZeroed(NumSamples);

			SourceInfo.SourceBuffer.Reset();
			SourceInfo.SourceBuffer.AddZeroed(NumSamples);

			if (SourceInfo.SubCallbackDelayLengthInFrames && !SourceInfo.bDelayLineSet)
			{
				SourceInfo.SourceBufferDelayLine.SetCapacity(SourceInfo.SubCallbackDelayLengthInFrames + 1);
				SourceInfo.SourceBufferDelayLine.PushZeros(SourceInfo.SubCallbackDelayLengthInFrames * SourceInfo.NumInputChannels);
				SourceInfo.bDelayLineSet = true;
			}

			float* PreDistanceAttenBufferPtr = SourceInfo.PreDistanceAttenuationBuffer.GetData();

			// if this is a bus, we just want to copy the bus audio to this source's output audio
			// Note we need to copy this since bus instances may have different audio via dynamic source effects, etc.
			if (bIsSourceBus)
			{
				// Get the source's rendered and mixed audio bus data
				const TSharedPtr<FMixerAudioBus> AudioBusPtr = AudioBuses.FindRef(SourceInfo.AudioBusId);
				if (AudioBusPtr.IsValid())
				{
					int32 NumFramesPlayed = NumOutputFrames;
					if (SourceInfo.SourceBusDurationFrames != INDEX_NONE)
					{
						// If we're now finishing, only copy over the real data
						if ((SourceInfo.NumFramesPlayed + NumOutputFrames) >= SourceInfo.SourceBusDurationFrames)
						{
							NumFramesPlayed = SourceInfo.SourceBusDurationFrames - SourceInfo.NumFramesPlayed;
							SourceInfo.bIsLastBuffer = true;
						}
					}

					SourceInfo.NumFramesPlayed += NumFramesPlayed;
					AudioBusPtr->CopyCurrentBuffer(SourceInfo.PreDistanceAttenuationBuffer, NumFramesPlayed, SourceInfo.NumInputChannels);
				}
			}
			else
			{

#if AUDIO_SUBFRAME_ENABLED
				// If we're not going to start yet, just continue
				double StartFraction = (SourceInfo.StartTime - AudioRenderThreadTime) / AudioClockDelta;
				if (StartFraction >= 1.0)
				{
					// note this is already zero'd so no need to write zeroes
					SourceInfo.PitchSourceParam.Reset();
					continue;
				}

				// Init the frame index iterator to 0 (i.e. render whole buffer)
				int32 StartFrame = 0;

				// If the start fraction is greater than 0.0 (and is less than 1.0), we are starting on a sub-frame
				// Otherwise, just start playing it right away
				if (StartFraction > 0.0)
				{
					StartFrame = NumOutputFrames * StartFraction;
				}

				// Update sample index to the frame we're starting on, accounting for source channels
				int32 SampleIndex = StartFrame * SourceInfo.NumInputChannels;
				bool bWriteOutZeros = true;
#else
				int32 SampleIndex = 0;
				int32 StartFrame = 0;
#endif

				// Modulate parameter target should modulation be active
				// Due to managing two separate pitch values that are updated at different rates
				// (game thread rate and copy set by SetPitch and buffer callback rate set by Modulation System),
				// the PitchSourceParam's target is marshaled before processing by mult'ing in the modulation pitch,
				// processing the buffer, and then resetting it back if modulation is active. 

				const bool bModActive = MixerDevice->IsModulationPluginEnabled() && MixerDevice->ModulationInterface.IsValid();
				if (bModActive)
				{
					SourceInfo.PitchModulation.ProcessControl(SourceInfo.PitchModulationBase);
				}

				const float TargetPitch = SourceInfo.PitchSourceParam.GetTarget();
				// Convert from semitones to frequency multiplier
				const float ModPitch = bModActive
					? Audio::GetFrequencyMultiplier(SourceInfo.PitchModulation.GetValue())
					: 1.0f;
				const float FinalPitch = FMath::Clamp(TargetPitch * ModPitch, MinModulationPitchRangeFreqCVar, MaxModulationPitchRangeFreqCVar);
				SourceInfo.PitchSourceParam.SetValue(FinalPitch, NumOutputFrames);

				for (int32 Frame = StartFrame; Frame < NumOutputFrames; ++Frame)
				{
					// If we've read our last buffer, we're done
					if (SourceInfo.bIsLastBuffer)
					{
						break;
					}

					// Whether or not we need to read another sample from the source buffers
					// If we haven't yet played any frames, then we will need to read the first source samples no matter what
					bool bReadNextSample = !SourceInfo.bHasStarted;

					// Reset that we've started generating audio
					SourceInfo.bHasStarted = true;

					// Update the PrevFrameIndex value for the source based on alpha value
					while (SourceInfo.CurrentFrameAlpha >= 1.0f)
					{
						// Our inter-frame alpha lerping value is causing us to read new source frames
						bReadNextSample = true;

						// Bump up the current frame index
						SourceInfo.CurrentFrameIndex++;

						// Bump up the frames played -- this is tracking the total frames in source file played
						// CurrentFrameIndex can wrap for looping sounds so won't be accurate in that case
						SourceInfo.NumFramesPlayed++;

						SourceInfo.CurrentFrameAlpha -= 1.0f;
					}

					// If our alpha parameter caused us to jump to a new source frame, we need
					// read new samples into our prev and next frame sample data
					if (bReadNextSample)
					{
						ReadSourceFrame(SourceId);
					}

					// perform linear SRC to get the next sample value from the decoded buffer
					if (SourceInfo.SubCallbackDelayLengthInFrames == 0)
					{
						for (int32 Channel = 0; Channel < SourceInfo.NumInputChannels; ++Channel)
						{
							const float CurrFrameValue = SourceInfo.CurrentFrameValues[Channel];
							const float NextFrameValue = SourceInfo.NextFrameValues[Channel];
							const float CurrentAlpha = SourceInfo.CurrentFrameAlpha;
							PreDistanceAttenBufferPtr[SampleIndex++] = FMath::Lerp(CurrFrameValue, NextFrameValue, CurrentAlpha);
						}
					}
					else
					{
						for (int32 Channel = 0; Channel < SourceInfo.NumInputChannels; ++Channel)
						{
							const float CurrFrameValue = SourceInfo.CurrentFrameValues[Channel];
							const float NextFrameValue = SourceInfo.NextFrameValues[Channel];
							const float CurrentAlpha = SourceInfo.CurrentFrameAlpha;

							const float CurrentSample = FMath::Lerp(CurrFrameValue, NextFrameValue, CurrentAlpha);

							SourceInfo.SourceBufferDelayLine.Push(&CurrentSample, 1);
							SourceInfo.SourceBufferDelayLine.Pop(&PreDistanceAttenBufferPtr[SampleIndex++], 1);
						}
					}

					const float CurrentPitchScale = SourceInfo.PitchSourceParam.Update();
					SourceInfo.CurrentFrameAlpha += CurrentPitchScale;
				}

				// After processing the frames, reset the pitch param
				SourceInfo.PitchSourceParam.Reset();

				// Reset target value as modulation may have modified prior to processing
				// And source param should not store modulation value internally as its
				// processed by the modulation plugin independently.
				SourceInfo.PitchSourceParam.SetValue(TargetPitch, NumOutputFrames);
			}
		}
	}

	void FMixerSourceManager::ComputeBuses()
	{
		// Loop through the bus registry and mix source audio
		for (auto& Entry : AudioBuses)
		{
			TSharedPtr<FMixerAudioBus>& AudioBus = Entry.Value;
			AudioBus->MixBuffer();
		}
	}

	void FMixerSourceManager::UpdateBuses()
	{
		// Update the bus states post mixing. This flips the current/previous buffer indices.
		for (auto& Entry : AudioBuses)
		{
			TSharedPtr<FMixerAudioBus>& AudioBus = Entry.Value;
			AudioBus->Update();
		}
	}

	void FMixerSourceManager::ApplyDistanceAttenuation(FSourceInfo& SourceInfo, int32 NumSamples)
	{
		if (DisableDistanceAttenuationCvar)
		{
			return;
		}

		float* PostDistanceAttenBufferPtr = SourceInfo.SourceBuffer.GetData();
		Audio::FadeBufferFast(PostDistanceAttenBufferPtr, SourceInfo.SourceBuffer.Num(), SourceInfo.DistanceAttenuationSourceStart, SourceInfo.DistanceAttenuationSourceDestination);
		SourceInfo.DistanceAttenuationSourceStart = SourceInfo.DistanceAttenuationSourceDestination;
	}

	void FMixerSourceManager::ComputePluginAudio(FSourceInfo& SourceInfo, FMixerSourceSubmixOutputBuffer& InSourceSubmixOutputBuffer, int32 SourceId, int32 NumSamples)
	{
		if (BypassAudioPluginsCvar)
		{
			// If we're bypassing audio plugins, our pre- and post-effect channels are the same as the input channels
			SourceInfo.NumPostEffectChannels = SourceInfo.NumInputChannels;

			// Set the ptr to use for post-effect buffers:
			InSourceSubmixOutputBuffer.SetPostAttenuationSourceBuffer(&SourceInfo.SourceBuffer);

			if (SourceInfo.bHasPreDistanceAttenuationSend)
			{
				InSourceSubmixOutputBuffer.SetPreAttenuationSourceBuffer(&SourceInfo.PreDistanceAttenuationBuffer);
			}
			return;
		}

		float* PostDistanceAttenBufferPtr = SourceInfo.SourceBuffer.GetData();

		bool bShouldMixInReverb = false;
		if (SourceInfo.bUseReverbPlugin)
		{
			const FSpatializationParams* SourceSpatParams = &SourceInfo.SpatParams;

			// Move the audio buffer to the reverb plugin buffer
			FAudioPluginSourceInputData AudioPluginInputData;
			AudioPluginInputData.SourceId = SourceId;
			AudioPluginInputData.AudioBuffer = &SourceInfo.SourceBuffer;
			AudioPluginInputData.SpatializationParams = SourceSpatParams;
			AudioPluginInputData.NumChannels = SourceInfo.NumInputChannels;
			AudioPluginInputData.AudioComponentId = SourceInfo.AudioComponentID;
			SourceInfo.AudioPluginOutputData.AudioBuffer.Reset();
			SourceInfo.AudioPluginOutputData.AudioBuffer.AddZeroed(AudioPluginInputData.AudioBuffer->Num());

			MixerDevice->ReverbPluginInterface->ProcessSourceAudio(AudioPluginInputData, SourceInfo.AudioPluginOutputData);

			// Make sure the buffer counts didn't change and are still the same size
			AUDIO_MIXER_CHECK(SourceInfo.AudioPluginOutputData.AudioBuffer.Num() == NumSamples);

			//If the reverb effect doesn't send it's audio to an external device, mix the output data back in.
			if (!MixerDevice->bReverbIsExternalSend)
			{
				// Copy the reverb-processed data back to the source buffer
				InSourceSubmixOutputBuffer.CopyReverbPluginOutputData(SourceInfo.AudioPluginOutputData.AudioBuffer);
				bShouldMixInReverb = true;
			}
		}

		if (SourceInfo.bUseOcclusionPlugin)
		{
			const FSpatializationParams* SourceSpatParams = &SourceInfo.SpatParams;

			// Move the audio buffer to the occlusion plugin buffer
			FAudioPluginSourceInputData AudioPluginInputData;
			AudioPluginInputData.SourceId = SourceId;
			AudioPluginInputData.AudioBuffer = &SourceInfo.SourceBuffer;
			AudioPluginInputData.SpatializationParams = SourceSpatParams;
			AudioPluginInputData.NumChannels = SourceInfo.NumInputChannels;
			AudioPluginInputData.AudioComponentId = SourceInfo.AudioComponentID;

			SourceInfo.AudioPluginOutputData.AudioBuffer.Reset();
			SourceInfo.AudioPluginOutputData.AudioBuffer.AddZeroed(AudioPluginInputData.AudioBuffer->Num());

			MixerDevice->OcclusionInterface->ProcessAudio(AudioPluginInputData, SourceInfo.AudioPluginOutputData);

			// Make sure the buffer counts didn't change and are still the same size
			AUDIO_MIXER_CHECK(SourceInfo.AudioPluginOutputData.AudioBuffer.Num() == NumSamples);

			// Copy the occlusion-processed data back to the source buffer and mix with the reverb plugin output buffer
			if (bShouldMixInReverb)
			{
				const float* ReverbPluginOutputBufferPtr = InSourceSubmixOutputBuffer.GetReverbPluginOutputData();
				const float* AudioPluginOutputDataPtr = SourceInfo.AudioPluginOutputData.AudioBuffer.GetData();

				Audio::SumBuffers(ReverbPluginOutputBufferPtr, AudioPluginOutputDataPtr, PostDistanceAttenBufferPtr, NumSamples);
			}
			else
			{
				FMemory::Memcpy(PostDistanceAttenBufferPtr, SourceInfo.AudioPluginOutputData.AudioBuffer.GetData(), sizeof(float) * NumSamples);
			}
		}
		else if (bShouldMixInReverb)
		{
			const float* ReverbPluginOutputBufferPtr = InSourceSubmixOutputBuffer.GetReverbPluginOutputData();
			Audio::MixInBufferFast(ReverbPluginOutputBufferPtr, PostDistanceAttenBufferPtr, NumSamples);
		}

		// If the source has HRTF processing enabled, run it through the spatializer
		if (SourceInfo.bUseHRTFSpatializer)
		{
			CSV_SCOPED_TIMING_STAT(Audio, HRTF);
			SCOPE_CYCLE_COUNTER(STAT_AudioMixerHRTF);

			AUDIO_MIXER_CHECK(SpatializationPlugin.IsValid());
			AUDIO_MIXER_CHECK(SourceInfo.NumInputChannels <= MaxChannelsSupportedBySpatializationPlugin);

			FAudioPluginSourceInputData AudioPluginInputData;
			AudioPluginInputData.AudioBuffer = &SourceInfo.SourceBuffer;
			AudioPluginInputData.NumChannels = SourceInfo.NumInputChannels;
			AudioPluginInputData.SourceId = SourceId;
			AudioPluginInputData.SpatializationParams = &SourceInfo.SpatParams;

			if (!MixerDevice->bSpatializationIsExternalSend)
			{
				SourceInfo.AudioPluginOutputData.AudioBuffer.Reset();
				SourceInfo.AudioPluginOutputData.AudioBuffer.AddZeroed(2 * NumOutputFrames);
			}

			{
				LLM_SCOPE(ELLMTag::AudioMixerPlugins);
				SpatializationPlugin->ProcessAudio(AudioPluginInputData, SourceInfo.AudioPluginOutputData);
			}

			// If this is an external send, we treat this source audio as if it was still a mono source
			// This will allow it to traditionally pan in the ComputeOutputBuffers function and be
			// sent to submixes (e.g. reverb) panned and mixed down. Certain submixes will want this spatial 
			// information in addition to the external send. We've already bypassed adding this source
			// to a base submix (e.g. master/eq, etc)
			if (MixerDevice->bSpatializationIsExternalSend)
			{
				// Otherwise our pre- and post-effect channels are the same as the input channels
				SourceInfo.NumPostEffectChannels = SourceInfo.NumInputChannels;

				// Set the ptr to use for post-effect buffers rather than the plugin output data (since the plugin won't have output audio data)
				InSourceSubmixOutputBuffer.SetPostAttenuationSourceBuffer(&SourceInfo.SourceBuffer);

				if (SourceInfo.bHasPreDistanceAttenuationSend)
				{
					InSourceSubmixOutputBuffer.SetPreAttenuationSourceBuffer(&SourceInfo.PreDistanceAttenuationBuffer);
				}
			}
			else
			{
				// Otherwise, we are now a 2-channel file and should not be spatialized using normal 3d spatialization
				SourceInfo.NumPostEffectChannels = 2;

				// Set the ptr to use for post-effect buffers rather than the plugin output data (since the plugin won't have output audio data)
				InSourceSubmixOutputBuffer.SetPostAttenuationSourceBuffer(&SourceInfo.AudioPluginOutputData.AudioBuffer);

				if (SourceInfo.bHasPreDistanceAttenuationSend)
				{
					InSourceSubmixOutputBuffer.SetPreAttenuationSourceBuffer(&SourceInfo.PreDistanceAttenuationBuffer);
				}
			}
		}
		else
		{
			// Otherwise our pre- and post-effect channels are the same as the input channels
			SourceInfo.NumPostEffectChannels = SourceInfo.NumInputChannels;

			InSourceSubmixOutputBuffer.SetPostAttenuationSourceBuffer(&SourceInfo.SourceBuffer);

			if (SourceInfo.bHasPreDistanceAttenuationSend)
			{
				InSourceSubmixOutputBuffer.SetPreAttenuationSourceBuffer(&SourceInfo.PreDistanceAttenuationBuffer);
			}
		}
	}

	void FMixerSourceManager::ComputePostSourceEffectBufferForIdRange(bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd)
	{
		CSV_SCOPED_TIMING_STAT(Audio, SourceEffectsBuffers);
		SCOPE_CYCLE_COUNTER(STAT_AudioMixerSourceEffectBuffers);

		const bool bIsDebugModeEnabled = DebugSoloSources.Num() > 0;

		for (int32 SourceId = SourceIdStart; SourceId < SourceIdEnd; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			if (!SourceInfo.bIsBusy || !SourceInfo.bIsPlaying || SourceInfo.bIsPaused || SourceInfo.bIsPausedForQuantization || (SourceInfo.bIsDone && SourceInfo.bEffectTailsDone))
			{
				continue;
			}

			const bool bIsSourceBus = SourceInfo.AudioBusId != INDEX_NONE;
			if ((bGenerateBuses && !bIsSourceBus) || (!bGenerateBuses && bIsSourceBus))
			{
				continue;
			}

			// Copy and store the current state of the pre-distance attenuation buffer before we feed it through our source effects
			// This is used by pre-effect sends
			if (SourceInfo.AudioBusSends[(int32)EBusSendType::PreEffect].Num() > 0)
			{
				SourceInfo.PreEffectBuffer.Reset();
				SourceInfo.PreEffectBuffer.Reserve(SourceInfo.PreDistanceAttenuationBuffer.Num());

				FMemory::Memcpy(SourceInfo.PreEffectBuffer.GetData(), SourceInfo.PreDistanceAttenuationBuffer.GetData(), sizeof(float)*SourceInfo.PreDistanceAttenuationBuffer.Num());
			}

			float* PreDistanceAttenBufferPtr = SourceInfo.PreDistanceAttenuationBuffer.GetData();
			const int32 NumSamples = SourceInfo.PreDistanceAttenuationBuffer.Num();

			// Update volume fade information if we're stopping
			if (SourceInfo.bIsStopping)
			{
				const int32 NumFadeFrames = FMath::Min(SourceInfo.VolumeFadeNumFrames - SourceInfo.VolumeFadeFramePosition, NumOutputFrames);

				SourceInfo.VolumeFadeFramePosition += NumFadeFrames;
				SourceInfo.VolumeSourceDestination = SourceInfo.VolumeFadeSlope * (float) SourceInfo.VolumeFadeFramePosition + SourceInfo.VolumeFadeStart;

				if (FMath::IsNearlyZero(SourceInfo.VolumeSourceDestination, KINDA_SMALL_NUMBER))
				{
					SourceInfo.VolumeSourceDestination = 0.0f;
				}

				const int32 NumFadeSamples = NumFadeFrames * SourceInfo.NumInputChannels;

				float VolumeStart = SourceInfo.VolumeSourceStart;
				float VolumeDestination = SourceInfo.VolumeSourceDestination;
				if (MixerDevice->IsModulationPluginEnabled() && MixerDevice->ModulationInterface.IsValid())
				{
					const bool bIsFirstProcessCall = SourceInfo.VolumeModulation.GetHasProcessed();
					const float ModVolumeStart = SourceInfo.VolumeModulation.GetValue();
					SourceInfo.VolumeModulation.ProcessControl(SourceInfo.VolumeModulationBase);
					const float ModVolumeEnd = SourceInfo.VolumeModulation.GetValue();
					if (bIsFirstProcessCall)
					{
						VolumeStart *= ModVolumeEnd;
					}
					else
					{
						VolumeStart *= ModVolumeStart;
					}
					VolumeDestination *= ModVolumeEnd;
				}
				Audio::FadeBufferFast(PreDistanceAttenBufferPtr, NumSamples, VolumeStart, VolumeDestination);

				// Zero the rest of the buffer
				if (NumFadeFrames < NumOutputFrames)
				{
					int32 SamplesLeft = NumSamples - NumFadeSamples;
					FMemory::Memzero(&PreDistanceAttenBufferPtr[NumFadeSamples], sizeof(float) * SamplesLeft);
				}
			}
			else
			{
				float VolumeStart = SourceInfo.VolumeSourceStart;
				float VolumeDestination = SourceInfo.VolumeSourceDestination;
				if (MixerDevice->IsModulationPluginEnabled() && MixerDevice->ModulationInterface.IsValid())
				{
					const bool bIsFirstProcessCall = SourceInfo.VolumeModulation.GetHasProcessed();
					const float ModVolumeStart = SourceInfo.VolumeModulation.GetValue();
					SourceInfo.VolumeModulation.ProcessControl(SourceInfo.VolumeModulationBase);
					const float ModVolumeEnd = SourceInfo.VolumeModulation.GetValue();
					if (bIsFirstProcessCall)
					{
						VolumeStart *= ModVolumeEnd;
					}
					else
					{
						VolumeStart *= ModVolumeStart;
					}
					VolumeDestination *= ModVolumeEnd;
				}
				Audio::FadeBufferFast(PreDistanceAttenBufferPtr, NumSamples, VolumeStart, VolumeDestination);
			}
			SourceInfo.VolumeSourceStart = SourceInfo.VolumeSourceDestination;

			// Now process the effect chain if it exists
			if (!DisableSourceEffectsCvar && SourceInfo.SourceEffects.Num() > 0)
			{
				// Prepare this source's effect chain input data
				SourceInfo.SourceEffectInputData.CurrentVolume = SourceInfo.VolumeSourceDestination;

				const float Pitch = Audio::GetFrequencyMultiplier(SourceInfo.PitchModulation.GetValue());
				SourceInfo.SourceEffectInputData.CurrentPitch = SourceInfo.PitchSourceParam.GetValue() * Pitch;
				SourceInfo.SourceEffectInputData.AudioClock = MixerDevice->GetAudioClock();
				if (SourceInfo.NumInputFrames > 0)
				{
					SourceInfo.SourceEffectInputData.CurrentPlayFraction = (float)SourceInfo.NumFramesPlayed / SourceInfo.NumInputFrames;
				}
				SourceInfo.SourceEffectInputData.SpatParams = SourceInfo.SpatParams;

				// Get a ptr to pre-distance attenuation buffer ptr
				float* OutputSourceEffectBufferPtr = SourceInfo.SourceEffectScratchBuffer.GetData();

				SourceInfo.SourceEffectInputData.InputSourceEffectBufferPtr = SourceInfo.PreDistanceAttenuationBuffer.GetData();
				SourceInfo.SourceEffectInputData.NumSamples = NumSamples;

				// Loop through the effect chain passing in buffers
				FScopeLock ScopeLock(&EffectChainMutationCriticalSection);
				{
					for (TSoundEffectSourcePtr& SoundEffectSource : SourceInfo.SourceEffects)
					{
						bool bPresetUpdated = false;
						if (SoundEffectSource->IsActive())
						{
							bPresetUpdated = SoundEffectSource->Update();
						}

						if (SoundEffectSource->IsActive())
						{
							SoundEffectSource->ProcessAudio(SourceInfo.SourceEffectInputData, OutputSourceEffectBufferPtr);

							// Copy output to input
							FMemory::Memcpy(SourceInfo.SourceEffectInputData.InputSourceEffectBufferPtr, OutputSourceEffectBufferPtr, sizeof(float) * NumSamples);
						}
					}
				}
			}

			const bool bWasEffectTailsDone = SourceInfo.bEffectTailsDone;

			if (!DisableEnvelopeFollowingCvar)
			{
				// Compute the source envelope using pre-distance attenuation buffer
				float AverageSampleValue = Audio::GetAverageAmplitude(PreDistanceAttenBufferPtr, NumSamples);
				SourceInfo.SourceEnvelopeFollower.ProcessAudio(AverageSampleValue);

				// Copy the current value of the envelope follower (block-rate value)
				SourceInfo.SourceEnvelopeValue = SourceInfo.SourceEnvelopeFollower.GetCurrentValue();

				SourceInfo.bEffectTailsDone = SourceInfo.bEffectTailsDone || SourceInfo.SourceEnvelopeValue < ENVELOPE_TAIL_THRESHOLD;
			}
			else
			{
				SourceInfo.bEffectTailsDone = true;
			}

			if (!bWasEffectTailsDone && SourceInfo.bEffectTailsDone)
			{
				SourceInfo.SourceListener->OnEffectTailsDone();
			}

			const bool bModActive = MixerDevice->IsModulationPluginEnabled() && MixerDevice->ModulationInterface.IsValid();
			bool bUpdateModFilters = bModActive && (SourceInfo.bModFiltersUpdated || SourceInfo.LowpassModulation.IsActive() || SourceInfo.HighpassModulation.IsActive());
			if (SourceInfo.IsRenderingToSubmixes() || bUpdateModFilters)
			{
				// Only scale with distance attenuation and send to source audio to plugins if we're not in output-to-bus only mode
				const int32 NumOutputSamplesThisSource = NumOutputFrames * SourceInfo.NumInputChannels;

				if (!SourceInfo.IsRenderingToSubmixes())
				{
					SourceInfo.LowpassModulation.ProcessControl(SourceInfo.LowpassModulationBase);
					SourceInfo.LowPassFilter.StartFrequencyInterpolation(SourceInfo.LowpassModulation.GetValue(), NumOutputFrames);

					SourceInfo.HighpassModulation.ProcessControl(SourceInfo.HighpassModulationBase);
					SourceInfo.HighPassFilter.StartFrequencyInterpolation(SourceInfo.HighpassModulation.GetValue(), NumOutputFrames);
				}
				else if (bUpdateModFilters)
				{
					const float LowpassFreq = FMath::Min(SourceInfo.LowpassModulationBase, SourceInfo.LowPassFreq);
					SourceInfo.LowpassModulation.ProcessControl(LowpassFreq);
					SourceInfo.LowPassFilter.StartFrequencyInterpolation(SourceInfo.LowpassModulation.GetValue(), NumOutputFrames);

					const float HighpassFreq = FMath::Max(SourceInfo.HighpassModulationBase, SourceInfo.HighPassFreq);
					SourceInfo.HighpassModulation.ProcessControl(HighpassFreq);
					SourceInfo.HighPassFilter.StartFrequencyInterpolation(SourceInfo.HighpassModulation.GetValue(), NumOutputFrames);
				}

				const bool BypassLPF = DisableFilteringCvar || (SourceInfo.LowPassFilter.GetCutoffFrequency() >= (MAX_FILTER_FREQUENCY - KINDA_SMALL_NUMBER));
				const bool BypassHPF = DisableFilteringCvar || DisableHPFilteringCvar || (SourceInfo.HighPassFilter.GetCutoffFrequency() <= (MIN_FILTER_FREQUENCY + KINDA_SMALL_NUMBER));

				float* SourceBuffer = SourceInfo.SourceBuffer.GetData();
				float* HpfInputBuffer = PreDistanceAttenBufferPtr; // assume bypassing LPF (HPF uses input buffer as input)

				if (!BypassLPF)
				{
					// Not bypassing LPF, so tell HPF to use LPF output buffer as input
					HpfInputBuffer = SourceBuffer;

					// process LPF audio block
					SourceInfo.LowPassFilter.ProcessAudioBuffer(PreDistanceAttenBufferPtr, SourceBuffer, NumOutputSamplesThisSource);
				}

				if (!BypassHPF)
				{
					// process HPF audio block
					SourceInfo.HighPassFilter.ProcessAudioBuffer(HpfInputBuffer, SourceBuffer, NumOutputSamplesThisSource);
				}

				// We manually reset interpolation to avoid branches in filter code
				SourceInfo.LowPassFilter.StopFrequencyInterpolation();
				SourceInfo.HighPassFilter.StopFrequencyInterpolation();

				if (BypassLPF && BypassHPF)
				{
					FMemory::Memcpy(SourceBuffer, PreDistanceAttenBufferPtr, NumSamples * sizeof(float));
				}
			}

			if (SourceInfo.IsRenderingToSubmixes())
			{
				// Apply distance attenuation
				ApplyDistanceAttenuation(SourceInfo, NumSamples);

				FMixerSourceSubmixOutputBuffer& SourceSubmixOutputBuffer = SourceSubmixOutputBuffers[SourceId];

				// Send source audio to plugins
				ComputePluginAudio(SourceInfo, SourceSubmixOutputBuffer, SourceId, NumSamples);
			}

			// Check the source effect tails condition
			if (SourceInfo.bIsLastBuffer && SourceInfo.bEffectTailsDone)
			{
				// If we're done and our tails our done, clear everything out
				SourceInfo.CurrentFrameValues.Reset();
				SourceInfo.NextFrameValues.Reset();
				SourceInfo.CurrentPCMBuffer = nullptr;
			}
		}
	}

	void FMixerSourceManager::ComputeOutputBuffersForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd)
	{
		CSV_SCOPED_TIMING_STAT(Audio, SourceOutputBuffers);
		SCOPE_CYCLE_COUNTER(STAT_AudioMixerSourceOutputBuffers);

		for (int32 SourceId = SourceIdStart; SourceId < SourceIdEnd; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Don't need to compute anything if the source is not playing or paused (it will remain at 0.0 volume)
			// Note that effect chains will still be able to continue to compute audio output. The source output 
			// will simply stop being read from.
			if (!SourceInfo.bIsBusy || !SourceInfo.bIsPlaying || (SourceInfo.bIsDone && SourceInfo.bEffectTailsDone))
			{
				continue;
			}

			// If we're in generate buses mode and not a bus, or vice versa, or if we're set to only output audio to buses.
			// If set to output buses, no need to do any panning for the source. The buses will do the panning.
			const bool bIsSourceBus = SourceInfo.AudioBusId != INDEX_NONE;
			if ((bGenerateBuses && !bIsSourceBus) || (!bGenerateBuses && bIsSourceBus) || !SourceInfo.IsRenderingToSubmixes())
			{
				continue;
			}

			FMixerSourceSubmixOutputBuffer& SourceSubmixOutputBuffer = SourceSubmixOutputBuffers[SourceId];
			SourceSubmixOutputBuffer.ComputeOutput(SourceInfo.SpatParams);
		}
	}

	void FMixerSourceManager::GenerateSourceAudio(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd)
	{
		// Buses generate their input buffers independently
		// Get the next block of frames from the source buffers
		ComputeSourceBuffersForIdRange(bGenerateBuses, SourceIdStart, SourceIdEnd);

		// Compute the audio source buffers after their individual effect chain processing
		ComputePostSourceEffectBufferForIdRange(bGenerateBuses, SourceIdStart, SourceIdEnd);

		// Get the audio for the output buffers
		ComputeOutputBuffersForIdRange(bGenerateBuses, SourceIdStart, SourceIdEnd);
	}

	void FMixerSourceManager::GenerateSourceAudio(const bool bGenerateBuses)
	{
		// If there are no buses, don't need to do anything here
		if (bGenerateBuses && !AudioBuses.Num())
		{
			return;
		}

		if (NumSourceWorkers > 0 && !DisableParallelSourceProcessingCvar)
		{
			AUDIO_MIXER_CHECK(SourceWorkers.Num() == NumSourceWorkers);
			for (int32 i = 0; i < SourceWorkers.Num(); ++i)
			{
				FAudioMixerSourceWorker& Worker = SourceWorkers[i]->GetTask();
				Worker.SetGenerateBuses(bGenerateBuses);

				SourceWorkers[i]->StartBackgroundTask();
			}

			for (int32 i = 0; i < SourceWorkers.Num(); ++i)
			{
				SourceWorkers[i]->EnsureCompletion();
			}
		}
		else
		{
			GenerateSourceAudio(bGenerateBuses, 0, NumTotalSources);
		}
	}

	void FMixerSourceManager::MixOutputBuffers(const int32 SourceId, int32 InNumOutputChannels, const float InSendLevel, EMixerSourceSubmixSendStage InSubmixSendStage, AlignedFloatBuffer& OutWetBuffer) const
	{
		if (InSendLevel > 0.0f)
		{
			const FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Don't need to mix into submixes if the source is paused
			if (!SourceInfo.bIsPaused && !SourceInfo.bIsPausedForQuantization && !SourceInfo.bIsDone && SourceInfo.bIsPlaying)
			{
				const FMixerSourceSubmixOutputBuffer& SourceSubmixOutputBuffer = SourceSubmixOutputBuffers[SourceId];
				SourceSubmixOutputBuffer.MixOutput(InSendLevel, InSubmixSendStage, OutWetBuffer);
			}
		}
	}

	void FMixerSourceManager::Get2DChannelMap(const int32 SourceId, int32 InNumOutputChannels, Audio::AlignedFloatBuffer& OutChannelMap)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		const FSourceInfo& SourceInfo = SourceInfos[SourceId];
		MixerDevice->Get2DChannelMap(SourceInfo.bIsVorbis, SourceInfo.NumInputChannels, InNumOutputChannels, SourceInfo.bIsCenterChannelOnly, OutChannelMap);
	}

	const ISoundfieldAudioPacket* FMixerSourceManager::GetEncodedOutput(const int32 SourceId, const FSoundfieldEncodingKey& InKey) const
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		const FSourceInfo& SourceInfo = SourceInfos[SourceId];

		// Don't need to mix into submixes if the source is paused
		if (!SourceInfo.bIsPaused && !SourceInfo.bIsPausedForQuantization && !SourceInfo.bIsDone && SourceInfo.bIsPlaying)
		{
			const FMixerSourceSubmixOutputBuffer& SourceSubmixOutputBuffer = SourceSubmixOutputBuffers[SourceId];
			return SourceSubmixOutputBuffer.GetSoundfieldPacket(InKey);
		}

		return nullptr;
	}

	const FQuat FMixerSourceManager::GetListenerRotation(const int32 SourceId) const
	{
		const FMixerSourceSubmixOutputBuffer& SubmixOutputBuffer = SourceSubmixOutputBuffers[SourceId];
		return SubmixOutputBuffer.GetListenerRotation();
	}

	void FMixerSourceManager::UpdateDeviceChannelCount(const int32 InNumOutputChannels)
	{
		AudioMixerThreadCommand([this, InNumOutputChannels]()
		{
			NumOutputSamples = NumOutputFrames * MixerDevice->GetNumDeviceChannels();

			// Update all source's to appropriate channel maps
			for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
			{
				FSourceInfo& SourceInfo = SourceInfos[SourceId];

				// Don't need to do anything if it's not active or not paused. 
				if (!SourceInfo.bIsActive && !SourceInfo.bIsPaused)
				{
					continue;
				}

				FMixerSourceSubmixOutputBuffer& SourceSubmixOutputBuffer = SourceSubmixOutputBuffers[SourceId];
				SourceSubmixOutputBuffer.SetNumOutputChannels(InNumOutputChannels);

				SourceInfo.ScratchChannelMap.Reset();
				const int32 NumSourceChannels = SourceInfo.bUseHRTFSpatializer ? 2 : SourceInfo.NumInputChannels;

				// If this is a 3d source, then just zero out the channel map, it'll cause a temporary blip
				// but it should reset in the next tick
				if (SourceInfo.bIs3D)
				{
					GameThreadInfo.bNeedsSpeakerMap[SourceId] = true;
					SourceInfo.ScratchChannelMap.AddZeroed(NumSourceChannels * InNumOutputChannels);
				}
				// If it's a 2D sound, then just get a new channel map appropriate for the new device channel count
				else
				{
					SourceInfo.ScratchChannelMap.Reset();
					MixerDevice->Get2DChannelMap(SourceInfo.bIsVorbis, NumSourceChannels, InNumOutputChannels, SourceInfo.bIsCenterChannelOnly, SourceInfo.ScratchChannelMap);
				}

				SourceSubmixOutputBuffer.SetChannelMap(SourceInfo.ScratchChannelMap, SourceInfo.bIsCenterChannelOnly);
			}
		});
	}

	void FMixerSourceManager::UpdateSourceEffectChain(const uint32 InSourceEffectChainId, const TArray<FSourceEffectChainEntry>& InSourceEffectChain, const bool bPlayEffectChainTails)
	{
		AudioMixerThreadCommand([this, InSourceEffectChainId, InSourceEffectChain, bPlayEffectChainTails]()
		{
			FSoundEffectSourceInitData InitData;
			InitData.AudioClock = MixerDevice->GetAudioClock();
			InitData.SampleRate = MixerDevice->SampleRate;
			InitData.AudioDeviceId = MixerDevice->DeviceID;

			for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
			{
				FSourceInfo& SourceInfo = SourceInfos[SourceId];

				if (SourceInfo.SourceEffectChainId == InSourceEffectChainId)
				{
					SourceInfo.bEffectTailsDone = !bPlayEffectChainTails;

					// Check to see if the chain didn't actually change
					FScopeLock ScopeLock(&EffectChainMutationCriticalSection);
					{
						TArray<TSoundEffectSourcePtr>& ThisSourceEffectChain = SourceInfo.SourceEffects;
						bool bReset = false;
						if (InSourceEffectChain.Num() == ThisSourceEffectChain.Num())
						{
							for (int32 SourceEffectId = 0; SourceEffectId < ThisSourceEffectChain.Num(); ++SourceEffectId)
							{
								const FSourceEffectChainEntry& ChainEntry = InSourceEffectChain[SourceEffectId];

								TSoundEffectSourcePtr SourceEffectInstance = ThisSourceEffectChain[SourceEffectId];
								if (!SourceEffectInstance->IsPreset(ChainEntry.Preset))
								{
									// As soon as one of the effects change or is not the same, then we need to rebuild the effect graph
									bReset = true;
									break;
								}

								// Otherwise just update if it's just to bypass
								SourceEffectInstance->SetEnabled(!ChainEntry.bBypass);
							}
						}
						else
						{
							bReset = true;
						}

						if (bReset)
						{
							InitData.NumSourceChannels = SourceInfo.NumInputChannels;

							// First reset the source effect chain
							ResetSourceEffectChain(SourceId);

							// Rebuild it
							TArray<TSoundEffectSourcePtr> SourceEffects;
							BuildSourceEffectChain(SourceId, InitData, InSourceEffectChain, SourceEffects);

							SourceInfo.SourceEffects = SourceEffects;
							SourceInfo.SourceEffectPresets.Add(nullptr);
						}
					}
				}
			}
		});
	}

	void FMixerSourceManager::PauseSoundForQuantizationCommand(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		FSourceInfo& SourceInfo = SourceInfos[SourceId];

		SourceInfo.bIsPausedForQuantization = true;
		SourceInfo.bIsActive = false;
	}

	void FMixerSourceManager::SetSubBufferDelayForSound(const int32 SourceId, const int32 FramesToDelay)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		FSourceInfo& SourceInfo = SourceInfos[SourceId];

		SourceInfo.SubCallbackDelayLengthInFrames = FramesToDelay;
	}

	void FMixerSourceManager::UnPauseSoundForQuantizationCommand(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		FSourceInfo& SourceInfo = SourceInfos[SourceId];

		SourceInfo.bIsPausedForQuantization = false;
		SourceInfo.bIsActive = !SourceInfo.bIsPaused;
	}

	const float* FMixerSourceManager::GetPreDistanceAttenuationBuffer(const int32 SourceId) const
	{
		return SourceInfos[SourceId].PreDistanceAttenuationBuffer.GetData();
	}

	const float* FMixerSourceManager::GetPreEffectBuffer(const int32 SourceId) const
	{
		return SourceInfos[SourceId].PreEffectBuffer.GetData();
	}

	const float* FMixerSourceManager::GetPreviousSourceBusBuffer(const int32 SourceId) const
	{
		if (SourceId < SourceInfos.Num())
		{
			return GetPreviousAudioBusBuffer(SourceInfos[SourceId].AudioBusId);
		}
		return nullptr;
	}

	const float* FMixerSourceManager::GetPreviousAudioBusBuffer(const int32 AudioBusId) const
	{
		const TSharedPtr<FMixerAudioBus> AudioBusPtr = AudioBuses.FindRef(AudioBusId);
		if (AudioBusPtr.IsValid())
		{
			return AudioBusPtr->GetPreviousBusBuffer();
		}
		return nullptr;
	}

	int32 FMixerSourceManager::GetNumChannels(const int32 SourceId) const
	{
		return SourceInfos[SourceId].NumInputChannels;
	}

	bool FMixerSourceManager::IsSourceBus(const int32 SourceId) const
	{
		return SourceInfos[SourceId].AudioBusId != INDEX_NONE;
	}

	void FMixerSourceManager::ComputeNextBlockOfSamples()
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		CSV_SCOPED_TIMING_STAT(Audio, SourceManagerUpdate);
		SCOPE_CYCLE_COUNTER(STAT_AudioMixerSourceManagerUpdate);

		if (FPlatformProcess::SupportsMultithreading())
		{
			// Get the this blocks commands before rendering audio
			PumpCommandQueue();
		}
		else if (bPumpQueue)
		{
			bPumpQueue = false;
			PumpCommandQueue();
		}

		// Notify modulation interface that we are beginning to update
		if (MixerDevice->IsModulationPluginEnabled() && MixerDevice->ModulationInterface.IsValid())
		{
			MixerDevice->ModulationInterface->ProcessModulators(MixerDevice->GetAudioClockDelta());
		}

		// Update pending tasks and release them if they're finished
		UpdatePendingReleaseData();

		// First generate non-bus audio (bGenerateBuses = false)
		GenerateSourceAudio(false);

		// Now mix in the non-bus audio into the buses
		ComputeBuses();

		// Now generate bus audio (bGenerateBuses = true)
		GenerateSourceAudio(true);

		// Update the buses now
		UpdateBuses();

		// Let the plugin know we finished processing all sources
		if (bUsingSpatializationPlugin)
		{
			AUDIO_MIXER_CHECK(SpatializationPlugin.IsValid());
			LLM_SCOPE(ELLMTag::AudioMixerPlugins);
			SpatializationPlugin->OnAllSourcesProcessed();
		}

		// Update the game thread copy of source doneness
		for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
		{		
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Check for the stopping condition to "turn the sound off"
			if (SourceInfo.bIsLastBuffer)
			{
				if (!SourceInfo.bIsDone)
				{
					SourceInfo.bIsDone = true;

					// Notify that we're now done with this source
					SourceInfo.SourceListener->OnDone();
				}
			}
		}
	}

	void FMixerSourceManager::ClearStoppingSounds()
	{
		for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			if (!SourceInfo.bIsDone && SourceInfo.bIsStopping && SourceInfo.VolumeSourceDestination == 0.0f)
			{
				SourceInfo.bIsStopping = false;
				SourceInfo.bIsDone = true;
				SourceInfo.SourceListener->OnDone();
			}

		}
	}


	void FMixerSourceManager::AudioMixerThreadCommand(TFunction<void()> InFunction)
	{
		// Here, we make sure that we don't flip our command double buffer while we are executing this function.
		FScopeLock ScopeLock(&CommandBufferIndexCriticalSection);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		// Add the function to the command queue:
		int32 AudioThreadCommandIndex = !RenderThreadCommandBufferIndex.GetValue();
		
#if !NO_LOGGING
		static uint32 WarnSize = 1024 * 1024;
		SIZE_T Size = CommandBuffers[AudioThreadCommandIndex].SourceCommandQueue.GetAllocatedSize();
		if (Size > WarnSize )
		{		
			SIZE_T Num = CommandBuffers[AudioThreadCommandIndex].SourceCommandQueue.Num();
			// NOTE: Although not really and error we want this to show up in shipping builds.
			UE_LOG(LogAudioMixer, Error, TEXT("Command Queue has grown to %uk bytes, containing %d cmds, last pump was %fms ago."), 
				Size >> 10, Num, FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - LastPumpTimeInCycles));
			WarnSize *= 2;
		}
#endif //!NO_LOGGING

		CommandBuffers[AudioThreadCommandIndex].SourceCommandQueue.Add(MoveTemp(InFunction));
		NumCommands.Increment();
	}

	void FMixerSourceManager::PumpCommandQueue()
	{
		// If we're already triggered, we need to wait for the audio thread to reset it before pumping
		if (FPlatformProcess::SupportsMultithreading())
		{
			if (CommandsProcessedEvent->Wait(0))
			{
				return;
			}
		}

		int32 CurrentRenderThreadIndex = RenderThreadCommandBufferIndex.GetValue();

		FCommands& Commands = CommandBuffers[CurrentRenderThreadIndex];

		// Pop and execute all the commands that came since last update tick
		for (int32 Id = 0; Id < Commands.SourceCommandQueue.Num(); ++Id)
		{
			TFunction<void()>& CommandFunction = Commands.SourceCommandQueue[Id];
			CommandFunction();
			NumCommands.Decrement();
		}

		LastPumpTimeInCycles = FPlatformTime::Cycles64();
		Commands.SourceCommandQueue.Reset();

		if (FPlatformProcess::SupportsMultithreading())
		{
			check(CommandsProcessedEvent != nullptr);
			CommandsProcessedEvent->Trigger();
		}
		else
		{
			RenderThreadCommandBufferIndex.Set(!CurrentRenderThreadIndex);
		}

	}

	void FMixerSourceManager::FlushCommandQueue(bool bPumpInCommand)
	{
		check(CommandsProcessedEvent != nullptr);

		// If we have no commands enqueued, exit
		if (NumCommands.GetValue() == 0)
		{
			UE_LOG(LogAudioMixer, Verbose, TEXT("No commands were queued while flushing the source manager."));
			return;
		}

		// Make sure current current executing 
		bool bTimedOut = false;
		if (!CommandsProcessedEvent->Wait(CommandBufferFlushWaitTimeMsCvar))
		{
			CommandsProcessedEvent->Trigger();
			bTimedOut = true;
			UE_LOG(LogAudioMixer, Warning, TEXT("Timed out waiting to flush the source manager command queue (1)."));
		}
		else
		{
			UE_LOG(LogAudioMixer, Verbose, TEXT("Flush succeeded in the source manager command queue (1)."));
		}

		// Call update to trigger a final pump of commands
		Update(bTimedOut);

		if (bPumpInCommand)
		{
			PumpCommandQueue();
		}

		// Wait one more time for the double pump
		if (!CommandsProcessedEvent->Wait(1000))
		{
			CommandsProcessedEvent->Trigger();
			UE_LOG(LogAudioMixer, Warning, TEXT("Timed out waiting to flush the source manager command queue (2)."));
		}
		else
		{
			UE_LOG(LogAudioMixer, Verbose, TEXT("Flush succeeded the source manager command queue (2)."));
		}
	}

	void FMixerSourceManager::UpdatePendingReleaseData(bool bForceWait)
	{
		// Don't block, but let tasks finish naturally
		for (int32 i = PendingSourceBuffers.Num() - 1; i >= 0; --i)
		{
			FMixerSourceBuffer* MixerSourceBuffer = PendingSourceBuffers[i].Get();

			bool bDeleteSourceBuffer = true;
			if (bForceWait)
			{
				MixerSourceBuffer->EnsureAsyncTaskFinishes();
			}
			else if (!MixerSourceBuffer->IsAsyncTaskDone())
			{			
				bDeleteSourceBuffer = false;
			}

			if (bDeleteSourceBuffer)
			{
				PendingSourceBuffers.RemoveAtSwap(i, 1, false);
			}
		}
	}

	bool FMixerSourceManager::FSourceInfo::IsRenderingToSubmixes() const
	{
		return bEnableBaseSubmix || bEnableSubmixSends;
	}

}
