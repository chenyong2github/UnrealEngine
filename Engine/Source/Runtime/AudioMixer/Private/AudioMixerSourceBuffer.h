// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AudioMixerBuffer.h"
#include "AudioMixerSourceManager.h"
#include "Sound/SoundWave.h"

namespace Audio
{
	struct FMixerSourceVoiceBuffer;

	static const int32 MAX_BUFFERS_QUEUED = 3;
	static const int32 LOOP_FOREVER = -1;

	struct FRawPCMDataBuffer
	{
		uint8* Data;
		uint32 DataSize;
		int32 LoopCount;
		uint32 CurrentSample;
		uint32 NumSamples;

		bool GetNextBuffer(FMixerSourceVoiceBuffer* OutSourceBufferPtr, const uint32 NumSampleToGet);

		FRawPCMDataBuffer()
			: Data(nullptr)
			, DataSize(0)
			, LoopCount(0)
			, CurrentSample(0)
			, NumSamples(0)
		{}
	};

	/** Enum describing the data-read mode of an audio buffer. */
	enum class EBufferReadMode : uint8
	{
		/** Read the next buffer asynchronously. */
		Asynchronous,

		/** Read the next buffer asynchronously but skip the first chunk of audio. */
		AsynchronousSkipFirstFrame
	};

	using FMixerSourceBufferPtr = TSharedPtr<class FMixerSourceBuffer>;

	/** Class which handles decoding audio for a particular source buffer. */
	class FMixerSourceBuffer : public ISoundWaveClient
	{
	public:		
		static FMixerSourceBufferPtr Create(FMixerBuffer& InBuffer, USoundWave& InWave, ELoopingMode InLoopingMode, bool bInIsSeeking);

		~FMixerSourceBuffer();

		bool Init();

		// Sets the decoder to use for realtime async decoding
		void SetDecoder(ICompressedAudioInfo* InCompressedAudioInfo);

		// Sets the raw PCM data buffer to use for the source buffer
		void SetPCMData(const FRawPCMDataBuffer& InPCMDataBuffer);

		// Sets the precached buffers
		void SetCachedRealtimeFirstBuffers(TArray<uint8>&& InPrecachedBuffer);

		// Called by source manager when needing more buffers
		void OnBufferEnd();

		// Return the number of buffers enqueued on the mixer source buffer
		int32 GetNumBuffersQueued() const { return NumBuffersQeueued; }
		
		// Returns the next enqueued buffer, returns nullptr if no buffers enqueued
		TSharedPtr<FMixerSourceVoiceBuffer> GetNextBuffer();

		// Returns if buffer looped
		bool DidBufferLoop() const { return bLoopCallback; }

		// Returns true if buffer finished
		bool DidBufferFinish() const { return bBufferFinished; }

		// Called to start an async task to read more data
		bool ReadMoreRealtimeData(ICompressedAudioInfo* InDecoder, int32 BufferIndex, EBufferReadMode BufferReadMode);

		// Returns true if async task is in progress
		bool IsAsyncTaskInProgress() const;

		// Returns true if the async task is done
		bool IsAsyncTaskDone() const;

		// Ensures the async task finishes
		void EnsureAsyncTaskFinishes();

		// Begin and end generation on the audio render thread (audio mixer only)
		void OnBeginGenerate();
		void OnEndGenerate();
		void ClearWave() { SoundWave = nullptr; }
	private:
		FMixerSourceBuffer(FMixerBuffer& InBuffer, USoundWave& InWave, ELoopingMode InLoopingMode, bool bInIsSeeking);

		void SubmitInitialPCMBuffers();
		void SubmitInitialRealtimeBuffers();
		void SubmitRealTimeSourceData(const bool bLooped);
		void ProcessRealTimeSource();
		void SubmitBuffer(TSharedPtr<FMixerSourceVoiceBuffer> InSourceVoiceBuffer);


		int32 NumBuffersQeueued;
		FRawPCMDataBuffer RawPCMDataBuffer;

		TArray<TSharedPtr<FMixerSourceVoiceBuffer>> SourceVoiceBuffers;
		TQueue<TSharedPtr<FMixerSourceVoiceBuffer>> BufferQueue;
		int32 CurrentBuffer;
		// SoundWaves are only set for procedural sound waves
		USoundWave* SoundWave;
		IAudioTask* AsyncRealtimeAudioTask;
		ICompressedAudioInfo* DecompressionState;
		ELoopingMode LoopingMode;
		int32 NumChannels;
		Audio::EBufferType::Type BufferType;
		int32 NumPrecacheFrames;
		TArray<uint8> CachedRealtimeFirstBuffer;

		uint32 bInitialized : 1;
		uint32 bBufferFinished : 1;
		uint32 bPlayedCachedBuffer : 1;
		uint32 bIsSeeking : 1;
		uint32 bLoopCallback : 1;
		uint32 bProcedural : 1;
		uint32 bIsBus : 1;
	
		virtual void OnBeginDestroy(class USoundWave* Wave) override;
		virtual bool OnIsReadyForFinishDestroy(class USoundWave* Wave) const override;
		virtual void OnFinishDestroy(class USoundWave* Wave) override;
	};
}
