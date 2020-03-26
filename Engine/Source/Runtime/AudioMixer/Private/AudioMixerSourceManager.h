// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/* Public dependencies
*****************************************************************************/

#include "CoreMinimal.h"
#include "AudioMixerBuffer.h"
#include "AudioMixerSubmix.h"
#include "AudioMixerBus.h"
#include "DSP/InterpolatedOnePole.h"
#include "DSP/Filter.h"
#include "DSP/InterpolatedOnePole.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/ParamInterpolator.h"
#include "DSP/BufferVectorOperations.h"
#include "IAudioExtensionPlugin.h"
#include "ISoundfieldFormat.h"
#include "Containers/Queue.h"


namespace Audio
{
	class FMixerSubmix;
	class FMixerDevice;
	class FMixerSourceVoice;
	class FMixerSourceBuffer;
	class ISourceListener;

	/** Struct defining a source voice buffer. */
	struct FMixerSourceVoiceBuffer
	{
		/** PCM float data. */
		AlignedFloatBuffer AudioData;

		/** How many times this buffer will loop. */
		int32 LoopCount;

		/** If this buffer is from real-time decoding and needs to make callbacks for more data. */
		uint32 bRealTimeBuffer : 1;

		FMixerSourceVoiceBuffer()
		{
			FMemory::Memzero(this, sizeof(*this));
		}
	};

	typedef TSharedPtr<FMixerSubmix, ESPMode::ThreadSafe> FMixerSubmixPtr;
	typedef TWeakPtr<FMixerSubmix, ESPMode::ThreadSafe> FMixerSubmixWeakPtr;

	class ISourceListener
	{
	public:
		// Called before a source begins to generate audio. 
		virtual void OnBeginGenerate() = 0;

		// Called when a loop point is hit
		virtual void OnLoopEnd() = 0;

		// Called when the source finishes on the audio render thread
		virtual void OnDone() = 0;

		// Called when the source's effect tails finish on the audio render thread.
		virtual void OnEffectTailsDone() = 0;

	};

	struct FMixerSourceSubmixSend
	{
		// The submix ptr
		FMixerSubmixWeakPtr Submix;

		// The amount of audio that is to be mixed into this submix
		float SendLevel;

		// Whather or not this is the primary send (i.e. first in the send chain)
		bool bIsMainSend;

		// If this is a soundfield submix, this is a pointer to the submix's Soundfield Factory.
		// If this is nullptr, the submix is not a soundfield submix.
		ISoundfieldFactory* SoundfieldFactory;
	};

	// Struct holding mappings of bus ids (unique ids) to send level
	struct FMixerBusSend
	{
		uint32 BusId;
		float SendLevel;
	};

	struct FMixerSourceVoiceInitParams
	{
		TSharedPtr<FMixerSourceBuffer, ESPMode::ThreadSafe> MixerSourceBuffer;
		ISourceListener* SourceListener;
		TArray<FMixerSourceSubmixSend> SubmixSends;
		TArray<FMixerBusSend> BusSends[(int32)EBusSendType::Count];
		uint32 BusId;
		float BusDuration;
		uint32 SourceEffectChainId;
		TArray<FSourceEffectChainEntry> SourceEffectChain;
		FMixerSourceVoice* SourceVoice;
		int32 NumInputChannels;
		int32 NumInputFrames;
		float EnvelopeFollowerAttackTime;
		float EnvelopeFollowerReleaseTime;
		FString DebugName;
		USpatializationPluginSourceSettingsBase* SpatializationPluginSettings;
		UOcclusionPluginSourceSettingsBase* OcclusionPluginSettings;
		UReverbPluginSourceSettingsBase* ReverbPluginSettings;
		USoundModulationPluginSourceSettingsBase* ModulationPluginSettings;
		FName AudioComponentUserID;
		uint64 AudioComponentID;
		uint8 bPlayEffectChainTails : 1;
		uint8 bUseHRTFSpatialization : 1;
		uint8 bIsExternalSend : 1;
		uint8 bIsDebugMode : 1;
		uint8 bOutputToBusOnly : 1;
		uint8 bIsVorbis : 1;
		uint8 bIsAmbisonics : 1;
		uint8 bIsSeeking : 1;

		FMixerSourceVoiceInitParams()
			: MixerSourceBuffer(nullptr)
			, SourceListener(nullptr)
			, BusId(INDEX_NONE)
			, BusDuration(0.0f)
			, SourceEffectChainId(INDEX_NONE)
			, SourceVoice(nullptr)
			, NumInputChannels(0)
			, NumInputFrames(0)
			, EnvelopeFollowerAttackTime(10.0f)
			, EnvelopeFollowerReleaseTime(100.0f)
			, SpatializationPluginSettings(nullptr)
			, OcclusionPluginSettings(nullptr)
			, ReverbPluginSettings(nullptr)
			, ModulationPluginSettings(nullptr)
			, AudioComponentID(0)
			, bPlayEffectChainTails(false)
			, bUseHRTFSpatialization(false)
			, bIsExternalSend(false)
			, bIsDebugMode(false)
			, bOutputToBusOnly(false)
			, bIsVorbis(false)
			, bIsAmbisonics(false)
			, bIsSeeking(false)
		{}
	};

	struct FSourceChannelMap
	{
		alignas(16) float ChannelStartGains[AUDIO_MIXER_MAX_OUTPUT_CHANNELS * AUDIO_MIXER_MAX_OUTPUT_CHANNELS];
		alignas(16) float ChannelDestinationGains[AUDIO_MIXER_MAX_OUTPUT_CHANNELS * AUDIO_MIXER_MAX_OUTPUT_CHANNELS];

		// This is the number of bytes the gain array is using:
		// (Number of input channels * number of output channels) * sizeof float.
		int32 CopySize;
		bool bIsInit;

		FSourceChannelMap(int32 InNumInChannels, int32 InNumOutChannels) 
			: CopySize(InNumInChannels * InNumOutChannels * sizeof(float))
		{
			checkSlow(InNumInChannels <= AUDIO_MIXER_MAX_OUTPUT_CHANNELS);
			checkSlow(InNumOutChannels <= AUDIO_MIXER_MAX_OUTPUT_CHANNELS);
			FMemory::Memzero(ChannelStartGains, CopySize);
		}

		FORCEINLINE void Reset(int32 InNumInChannels, int32 InNumOutChannels)
		{
			checkSlow(InNumInChannels <= AUDIO_MIXER_MAX_OUTPUT_CHANNELS);
			checkSlow(InNumOutChannels <= AUDIO_MIXER_MAX_OUTPUT_CHANNELS);

			CopySize = InNumInChannels * InNumOutChannels * sizeof(float);
			FMemory::Memzero(ChannelStartGains, CopySize);
			FMemory::Memzero(ChannelDestinationGains, CopySize);
			bIsInit = false;
		}

		FORCEINLINE void CopyDestinationToStart()
		{
			FMemory::Memcpy(ChannelStartGains, ChannelDestinationGains, CopySize);
		}

		FORCEINLINE void SetChannelMap(const float* RESTRICT InChannelGains)
		{
			FMemory::Memcpy(ChannelDestinationGains, InChannelGains, CopySize);
			if (!bIsInit)
			{
				FMemory::Memcpy(ChannelStartGains, InChannelGains, CopySize);
				bIsInit = true;
			}
		}

	private:
		FSourceChannelMap()
			: CopySize(0)
			, bIsInit(false)
		{
		}
	};

	struct FSourceManagerInitParams
	{
		// Total number of sources to use in the source manager
		int32 NumSources;

		// Number of worker threads to use for the source manager.
		int32 NumSourceWorkers;
	};

	class FMixerSourceManager
	{
	public:
		FMixerSourceManager(FMixerDevice* InMixerDevice);
		~FMixerSourceManager();

		void Init(const FSourceManagerInitParams& InitParams);
		void Update(bool bTimedOut = false);

		bool GetFreeSourceId(int32& OutSourceId);
		int32 GetNumActiveSources() const;
		int32 GetNumActiveBuses() const;

		void ReleaseSourceId(const int32 SourceId);
		void InitSource(const int32 SourceId, const FMixerSourceVoiceInitParams& InitParams);

		void Play(const int32 SourceId);
		void Stop(const int32 SourceId);
		void StopFade(const int32 SourceId, const int32 NumFrames);
		void Pause(const int32 SourceId);
		void SetPitch(const int32 SourceId, const float Pitch);
		void SetVolume(const int32 SourceId, const float Volume);
		void SetDistanceAttenuation(const int32 SourceId, const float DistanceAttenuation);
		void SetSpatializationParams(const int32 SourceId, const FSpatializationParams& InParams);
		void SetChannelMap(const int32 SourceId, const uint32 NumInputChannels, const Audio::AlignedFloatBuffer& InChannelMap, const bool bInIs3D, const bool bInIsCenterChannelOnly);
		void SetLPFFrequency(const int32 SourceId, const float Frequency);
		void SetHPFFrequency(const int32 SourceId, const float Frequency);

		void SetListenerTransforms(const TArray<FTransform>& ListenerTransforms);
		const TArray<FTransform>* GetListenerTransforms() const;

		int64 GetNumFramesPlayed(const int32 SourceId) const;
		float GetEnvelopeValue(const int32 SourceId) const;
		bool IsUsingHRTFSpatializer(const int32 SourceId) const;
		bool NeedsSpeakerMap(const int32 SourceId) const;
		void ComputeNextBlockOfSamples();
		void ClearStoppingSounds();
		void MixOutputBuffers(const int32 SourceId, int32 InNumOutputChannels, const float SendLevel, AlignedFloatBuffer& OutWetBuffer) const;

		// Called by a soundfield submix to get encoded audio.
		// If this source wasn't encoded (possibly because it is paused or finished playing),
		// this returns nullptr.
		// Returned nonnull pointers are only guaranteed to be valid on the audio mixer render thread.
		const ISoundfieldAudioPacket* GetEncodedOutput(const int32 SourceId, const FSoundfieldEncodingKey& InKey) const;

		const FQuat GetListenerRotation(const int32 SourceId) const;

		void SetSubmixSendInfo(const int32 SourceId, const FMixerSourceSubmixSend& SubmixSend);
		void SetBusSendInfo(const int32 SourceId, EBusSendType InBusSendType, FMixerBusSend& BusSend);

		void UpdateDeviceChannelCount(const int32 InNumOutputChannels);

		void UpdateSourceEffectChain(const uint32 SourceEffectChainId, const TArray<FSourceEffectChainEntry>& SourceEffectChain, const bool bPlayEffectChainTails);
		void UpdateModulationControls(const int32 SourceId, const FSoundModulationControls& InControls);

		const float* GetPreDistanceAttenuationBuffer(const int32 SourceId) const;
		const float* GetPreEffectBuffer(const int32 SourceId) const;
		const float* GetPreviousBusBuffer(const int32 SourceId) const;
		int32 GetNumChannels(const int32 SourceId) const;
		int32 GetNumOutputFrames() const { return NumOutputFrames; }
		bool IsBus(const int32 SourceId) const;
		void PumpCommandQueue();
		void UpdatePendingReleaseData(bool bForceWait = false);
		void FlushCommandQueue(bool bPumpCommandQueue = false);
	private:

		void ReleaseSource(const int32 SourceId);
		void BuildSourceEffectChain(const int32 SourceId, FSoundEffectSourceInitData& InitData, const TArray<FSourceEffectChainEntry>& SourceEffectChain);
		void ResetSourceEffectChain(const int32 SourceId);
		void ReadSourceFrame(const int32 SourceId);

		void GenerateSourceAudio(const bool bGenerateBuses);
		void GenerateSourceAudio(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd);

		void ComputeSourceBuffersForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd);
		void ComputePostSourceEffectBufferForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd);
		void ComputeOutputBuffersForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd);

		void ComputeBuses();
		void UpdateBuses();

		void AudioMixerThreadCommand(TFunction<void()> InFunction);

		static const int32 NUM_BYTES_PER_SAMPLE = 2;

		// Private class which perform source buffer processing in a worker task
		class FAudioMixerSourceWorker : public FNonAbandonableTask
		{
			FMixerSourceManager* SourceManager;
			int32 StartSourceId;
			int32 EndSourceId;
			bool bGenerateBuses;

		public:
			FAudioMixerSourceWorker(FMixerSourceManager* InSourceManager, const int32 InStartSourceId, const int32 InEndSourceId)
				: SourceManager(InSourceManager)
				, StartSourceId(InStartSourceId)
				, EndSourceId(InEndSourceId)
				, bGenerateBuses(false)
			{
			}

			void SetGenerateBuses(bool bInGenerateBuses)
			{
				bGenerateBuses = bInGenerateBuses;
			}

			void DoWork()
			{
				SourceManager->GenerateSourceAudio(bGenerateBuses, StartSourceId, EndSourceId);
			}

			FORCEINLINE TStatId GetStatId() const
			{
				RETURN_QUICK_DECLARE_CYCLE_STAT(FAudioMixerSourceWorker, STATGROUP_ThreadPoolAsyncTasks);
			}
		};

		// Critical section to ensure mutating effect chains is thread-safe
		FCriticalSection EffectChainMutationCriticalSection;

		FMixerDevice* MixerDevice;

		// Cached ptr to an optional spatialization plugin
		TAudioSpatializationPtr SpatializationPlugin;

		// Array of pointers to game thread audio source objects
		TArray<FMixerSourceVoice*> MixerSources;

		// A command queue to execute commands from audio thread (or game thread) to audio mixer device thread.
		struct FCommands
		{
			TArray<TFunction<void()>> SourceCommandQueue;
		};

		FCommands CommandBuffers[2];
		FThreadSafeCounter RenderThreadCommandBufferIndex;

		FEvent* CommandsProcessedEvent;
		FCriticalSection CommandBufferIndexCriticalSection;

		TArray<int32> DebugSoloSources;

		// All state having to do with a non-soundfield downmix.
		struct FSubmixChannelData
		{
			FSourceChannelMap ChannelMap;
			Audio::AlignedFloatBuffer OutputBuffer;

			FSubmixChannelData(uint32 InNumInChannels, uint32 InNumOutputChannels, uint32 NumFrames)
				: ChannelMap(InNumInChannels, InNumOutputChannels)
			{
				OutputBuffer.Reset();
				OutputBuffer.AddUninitialized(NumFrames * InNumOutputChannels);
			}

			void Reset(uint32 InNumInChannels, uint32 InNumOutputChannels, uint32 NumFrames)
			{
				ChannelMap.Reset(InNumInChannels, InNumOutputChannels);

				OutputBuffer.Reset();
				OutputBuffer.AddUninitialized(NumFrames * InNumOutputChannels);
			}
		};

		struct FSubmixSoundfieldData
		{
			TUniquePtr<ISoundfieldEncoderStream> Encoder;
			// If this is an ambisonics source, we use a transcoder stream.
			TUniquePtr<ISoundfieldTranscodeStream> AmbiTranscoder;
			TUniquePtr<ISoundfieldEncodingSettingsProxy> EncoderSettings;
			TUniquePtr<ISoundfieldAudioPacket> EncodedPacket;
			// If this is a unreal ambisonics soundfield buffer, we hand it the submixed buffer directly.
			bool bIsUnrealAmbisonicsSubmix;
		};

		struct FSourceDownmixData
		{
			// cached parameters for encoding to a soundfield format.
			FSoundfieldSpeakerPositionalData PositionalData;
			FQuat SourceRotation;

			// Output data, after computing a block of sample data, this is read back from mixers
			Audio::AlignedFloatBuffer ReverbPluginOutputBuffer;
			Audio::AlignedFloatBuffer* PostEffectBuffers;

			// Data needed for outputting to submixes for the default channel configuration for the output device.
			FSubmixChannelData DeviceSubmixInfo;

			// Whether this source's output is being sent to a device submix (i.e. a USoundSubmix).
			bool bIsSourceBeingSentToDeviceSubmix;

			TMap<FSoundfieldEncodingKey, FSubmixSoundfieldData> EncodedSoundfieldDownmixes;
			TArray<Audio::FChannelPositionInfo> InputChannelPositions;

			uint32 NumInputChannels;
			const uint32 NumFrames;
			uint32 NumDeviceChannels;
			uint8 bIsInitialDownmix : 1;

			// If this source is an ambisonics source, we use this to downmix the source to our output.
			TUniquePtr<ISoundfieldDecoderStream> AmbisonicsDecoder;

			FSourceDownmixData(uint32 SourceNumChannels, uint32 NumDeviceOutputChannels, uint32 InNumFrames)
				: SourceRotation(FQuat::Identity)
				, PostEffectBuffers(nullptr)
				, DeviceSubmixInfo(FSubmixChannelData(SourceNumChannels, NumDeviceOutputChannels, InNumFrames))
				, bIsSourceBeingSentToDeviceSubmix(false)
				, NumInputChannels(SourceNumChannels)
				, NumFrames(InNumFrames)
				, NumDeviceChannels(NumDeviceOutputChannels)
				, bIsInitialDownmix(true) 
			{
			}

			void ResetNumberOfDeviceChannels(const uint32 NumDeviceOutputChannels)
			{
				NumDeviceChannels = NumDeviceOutputChannels;
				DeviceSubmixInfo.Reset(NumInputChannels, NumDeviceOutputChannels, NumFrames);
			}

			void ResetData(const uint32 InNumInputChannels, int32 InNumDeviceChannels)
			{
				bIsSourceBeingSentToDeviceSubmix = false;
				NumDeviceChannels = InNumDeviceChannels;
				NumInputChannels = InNumInputChannels;
				PostEffectBuffers = nullptr;

				DeviceSubmixInfo.Reset(NumInputChannels, NumDeviceChannels, NumFrames);
				EncodedSoundfieldDownmixes.Reset();
				AmbisonicsDecoder.Reset();
				bIsInitialDownmix = true;

				PositionalData.Rotation = FQuat::Identity;
			}
		};

		struct FSourceInfo
		{
			FSourceInfo() {}
			~FSourceInfo() {}

			// Object which handles source buffer decoding
			TSharedPtr<FMixerSourceBuffer, ESPMode::ThreadSafe> MixerSourceBuffer;
			ISourceListener* SourceListener;

			// Data used for rendering sources
			TSharedPtr<FMixerSourceVoiceBuffer, ESPMode::ThreadSafe> CurrentPCMBuffer;
			int32 CurrentAudioChunkNumFrames;

			// The post-attenuation source buffer, used to send audio to submixes
			Audio::AlignedFloatBuffer SourceBuffer;
			Audio::AlignedFloatBuffer PreEffectBuffer;
			Audio::AlignedFloatBuffer PreDistanceAttenuationBuffer;
			Audio::AlignedFloatBuffer SourceEffectScratchBuffer;

			TArray<float> CurrentFrameValues;
			TArray<float> NextFrameValues;
			float CurrentFrameAlpha;
			int32 CurrentFrameIndex;
			int64 NumFramesPlayed;

			// The number of frames to wait before starting the source
			double StartTime;

			TArray<FMixerSourceSubmixSend> SubmixSends;

			// What bus Id this source is, if it is a bus. This is INDEX_NONE for sources which are not buses.
			uint32 BusId;

			// Number of samples to count for bus
			int64 BusDurationFrames;

			// What buses this source is sending its audio to. Used to remove this source from the bus send list.
			TArray<uint32> BusSends[(int32)EBusSendType::Count];

			// Interpolated source params
			FParam PitchSourceParam;
			float VolumeSourceStart;
			float VolumeSourceDestination;
			float VolumeFadeSlope;
			float VolumeFadeStart;
			int32 VolumeFadeFramePosition;
			int32 VolumeFadeNumFrames;

			float DistanceAttenuationSourceStart;
			float DistanceAttenuationSourceDestination;

			// One-Pole LPFs and HPFs per source
			Audio::FInterpolatedLPF LowPassFilter;
			Audio::FInterpolatedHPF HighPassFilter;

			// Source effect instances
			uint32 SourceEffectChainId;
			TArray<TSoundEffectSourcePtr> SourceEffects;
			TArray<USoundEffectSourcePreset*> SourceEffectPresets;
			bool bEffectTailsDone;
			FSoundEffectSourceInputData SourceEffectInputData;

			FAudioPluginSourceOutputData AudioPluginOutputData;

			// A DSP object which tracks the amplitude envelope of a source.
			Audio::FEnvelopeFollower SourceEnvelopeFollower;
			float SourceEnvelopeValue;

			// Modulation control state
			FSoundModulationControls ModulationControls;

			FSpatializationParams SpatParams;
			Audio::AlignedFloatBuffer ScratchChannelMap;

			// State management
			uint8 bIs3D:1;
			uint8 bIsCenterChannelOnly:1;
			uint8 bIsActive:1;
			uint8 bIsPlaying:1;
			uint8 bIsPaused:1;
			uint8 bIsStopping:1;
			uint8 bHasStarted:1;
			uint8 bIsBusy:1;
			uint8 bUseHRTFSpatializer:1;
			uint8 bIsExternalSend:1;
			uint8 bUseOcclusionPlugin:1;
			uint8 bUseReverbPlugin:1;
			uint8 bIsDone:1;
			uint8 bIsLastBuffer:1;
			uint8 bOutputToBusOnly:1;
			uint8 bIsVorbis:1;

			// TODO: Make this generic to soundfield formats as a whole.
			// For now, if this is true we just wrap the audio buffer in an FAmbisonicsSoundfieldBuffer.
			uint8 bIsAmbisonics:1;
			uint8 bIsBypassingLPF:1;
			uint8 bIsBypassingHPF:1;
			uint8 bIsModulationUpdated:1;

			// Source format info
			int32 NumInputChannels;
			int32 NumPostEffectChannels;
			int32 NumInputFrames;

			// ID for associated Audio Component if there is one, 0 otherwise
			uint64 AudioComponentID;

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
			uint8 bIsDebugMode : 1;
			FString DebugName;
#endif // AUDIO_MIXER_ENABLE_DEBUG_MODE
		};

		static void ApplyDistanceAttenuation(FSourceInfo& InSourceInfo, int32 NumSamples);
		void ComputePluginAudio(FSourceInfo& InSourceInfo, FSourceDownmixData& DownmixData, int32 SourceId, int32 NumSamples);

		static void ComputeDownmix3D(FSourceDownmixData& DownmixData, FMixerDevice* MixerDevice);
		static void ComputeDownmix2D(FSourceDownmixData& DownmixData, FMixerDevice* MixerDevice);
		static void EncodeToSoundfieldFormats(FSourceDownmixData& DownmixData, const FSourceInfo& InSource, FMixerDevice* InDevice);
		static void ConvertCartesianToSpherical(const FVector& InVector, float& OutAzimuth, float& OutElevation, float& OutRadius);

		// This function is effectively equivalent to calling DownmixDataArray.EmplaceAt_GetRef(args...), but bypasses that function's intrinsic call to AddUninitialized.
		FSourceDownmixData& InitializeDownmixForSource(const int32 SourceId, const int32 NumInputChannels, const int32 NumOutputChannels, const int32 InNumOutputFrames);

		const FSubmixChannelData& GetChannelInfoForDevice(const FSourceDownmixData& InDownmixData) const;
		FSubmixChannelData& GetChannelInfoForDevice(FSourceDownmixData& InDownmixData);

		const FSubmixSoundfieldData& GetChannelInfoForFormat(const FSoundfieldEncodingKey& InFormat, const FSourceDownmixData& InDownmixData) const;
		FSubmixSoundfieldData& GetChannelInfoForFormat(const FSoundfieldEncodingKey& InFormat, FSourceDownmixData& InDownmixData);

		// Array of listener transforms
		TArray<FTransform> ListenerTransforms;

		// Array of source infos.
		TArray<FSourceInfo> SourceInfos;

		// These structs are used for guaranteed vectorization when downmixing
		// sources.
		TArray<FSourceDownmixData> DownmixDataArray;

		// Map of bus object Id's to bus data. 
		TMap<uint32, FMixerBus> Buses;

		// Async task workers for processing sources in parallel
		TArray<FAsyncTask<FAudioMixerSourceWorker>*> SourceWorkers;

		// Array of task data waiting to finished. Processed on audio render thread.
		TArray<TSharedPtr<FMixerSourceBuffer, ESPMode::ThreadSafe>> PendingSourceBuffers;

		// General information about sources in source manager accessible from game thread
		struct FGameThreadInfo
		{
			TArray<int32> FreeSourceIndices;
			TArray<bool> bIsBusy;
			TArray<bool> bNeedsSpeakerMap;
			TArray<bool> bIsDebugMode;
			TArray<bool> bIsUsingHRTFSpatializer;
		} GameThreadInfo;

		int32 NumActiveSources;
		int32 NumTotalSources;
		int32 NumOutputFrames;
		int32 NumOutputSamples;
		int32 NumSourceWorkers;

		// Commands queued up to execute
		FThreadSafeCounter NumCommands;

		uint8 bInitialized : 1;
		uint8 bUsingSpatializationPlugin : 1;
		int32 MaxChannelsSupportedBySpatializationPlugin;

		// Set to true when the audio source manager should pump the command queue
		FThreadSafeBool bPumpQueue;

		friend class FMixerSourceVoice;
	};
}
