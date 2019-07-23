// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioMixerSourceBuffer.h"
#include "AudioMixerSourceDecode.h"
#include "ContentStreaming.h"

namespace Audio
{
	bool FRawPCMDataBuffer::GetNextBuffer(FMixerSourceVoiceBuffer* OutSourceBufferPtr, const uint32 NumSampleToGet)
	{
		// TODO: support loop counts
		float* OutBufferPtr = OutSourceBufferPtr->AudioData.GetData();
		int16* DataPtr = (int16*)Data;

		if (LoopCount == Audio::LOOP_FOREVER)
		{
			bool bLooped = false;
			for (uint32 Sample = 0; Sample < NumSampleToGet; ++Sample)
			{
				OutBufferPtr[Sample] = DataPtr[CurrentSample++] / 32768.0f;

				// Loop around if we're looping
				if (CurrentSample >= NumSamples)
				{
					CurrentSample = 0;
					bLooped = true;
				}
			}
			return bLooped;
		}
		else if (CurrentSample < NumSamples)
		{
			uint32 Sample = 0;
			while (Sample < NumSampleToGet && CurrentSample < NumSamples)
			{
				OutBufferPtr[Sample++] = (float)DataPtr[CurrentSample++] / 32768.0f;
			}

			// Zero out the rest of the buffer
			while (Sample < NumSampleToGet)
			{
				OutBufferPtr[Sample++] = 0.0f;
			}
		}
		else
		{
			for (uint32 Sample = 0; Sample < NumSampleToGet; ++Sample)
			{
				OutBufferPtr[Sample] = 0.0f;
			}
		}

		// If the current sample is greater or equal to num samples we hit the end of the buffer
		return CurrentSample >= NumSamples;
	}

	FMixerSourceBuffer::FMixerSourceBuffer()
		: NumBuffersQeueued(0)
		, CurrentBuffer(0)
		, SoundWave(nullptr)
		, AsyncRealtimeAudioTask(nullptr)
		, DecompressionState(nullptr)
		, LoopingMode(ELoopingMode::LOOP_Never)
		, NumChannels(0)
		, BufferType(Audio::EBufferType::Invalid)
		, NumPrecacheFrames(0)
		, bInitialized(false)
		, bBufferFinished(false)
		, bPlayedCachedBuffer(false)
		, bIsSeeking(false)
		, bLoopCallback(false)
		, bProcedural(false)
		, bIsBus(false)
	{
	}

	FMixerSourceBuffer::~FMixerSourceBuffer()
	{
		// Make sure we have completed our async realtime task before deleting the decompression state
		if (AsyncRealtimeAudioTask)
		{
			AsyncRealtimeAudioTask->CancelTask();
			delete AsyncRealtimeAudioTask;
			AsyncRealtimeAudioTask = nullptr;
		}

		OnEndGenerate();

		// Clean up decompression state after things have been finished using it
		if (DecompressionState)
		{
			if (BufferType == EBufferType::Streaming)
			{
				IStreamingManager::Get().GetAudioStreamingManager().RemoveDecoder(DecompressionState);
			}

			delete DecompressionState;
			DecompressionState = nullptr;
		}

		if (SoundWave)
		{
			SoundWave->RemovePlayingSource();
		}
	}

	bool FMixerSourceBuffer::PreInit(FMixerBuffer* InBuffer, USoundWave* InWave, ELoopingMode InLoopingMode, bool bInIsSeeking)
	{
		LLM_SCOPE(ELLMTag::AudioMixer);
		if (!InWave)
		{
			return false;
		}

		NumChannels = InBuffer->NumChannels;
		BufferType = InBuffer->GetType();
		bIsBus = InWave->bIsBus;
		bProcedural = InWave->bProcedural;
		NumPrecacheFrames = InWave->NumPrecacheFrames;
		
		// Prevent double-triggering procedural soundwaves
		if (bProcedural && InWave->IsGeneratingAudio())
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Procedural sound wave is reinitializing even though it is currently actively generating audio. Please stop sound before trying to play it again."));
			return false;
		}

		check(SoundWave == nullptr);
		// Only need to have a handle to a USoundWave for procedural sound waves
		SoundWave = InWave;
		check(SoundWave);
		SoundWave->AddPlayingSource();

		LoopingMode = InLoopingMode;
		bIsSeeking = bInIsSeeking;
		bLoopCallback = false;

		BufferQueue.Empty();

		const uint32 TotalSamples = MONO_PCM_BUFFER_SAMPLES * NumChannels;
		for (int32 BufferIndex = 0; BufferIndex < Audio::MAX_BUFFERS_QUEUED; ++BufferIndex)
		{
			SourceVoiceBuffers.Add(TSharedPtr<FMixerSourceVoiceBuffer>(new FMixerSourceVoiceBuffer()));

			// Prepare the memory to fit the max number of samples
			SourceVoiceBuffers[BufferIndex]->AudioData.Reset(TotalSamples);
			SourceVoiceBuffers[BufferIndex]->bRealTimeBuffer = true;
			SourceVoiceBuffers[BufferIndex]->LoopCount = 0;
		}
		return true;
	}

	void FMixerSourceBuffer::SetDecoder(ICompressedAudioInfo* InCompressedAudioInfo)
	{
		if (DecompressionState == nullptr)
		{
			DecompressionState = InCompressedAudioInfo;
			if (BufferType == EBufferType::Streaming)
			{
				IStreamingManager::Get().GetAudioStreamingManager().AddDecoder(DecompressionState);
			}
		}
	}

	void FMixerSourceBuffer::SetPCMData(const FRawPCMDataBuffer& InPCMDataBuffer)
	{
		check(BufferType == EBufferType::PCM || BufferType == EBufferType::PCMPreview);
		RawPCMDataBuffer = InPCMDataBuffer;
	}

	void FMixerSourceBuffer::SetCachedRealtimeFirstBuffers(TArray<uint8>&& InPrecachedBuffers)
	{
		CachedRealtimeFirstBuffer = MoveTemp(InPrecachedBuffers);
	}

	bool FMixerSourceBuffer::Init()
	{
		// We have successfully initialized which means our SoundWave has been flagged as bIsActive
		// GC can run between PreInit and Init so when cleaning up FMixerSourceBuffer, we don't want to touch SoundWave unless bInitailized is true.
		// SoundWave->bIsSoundActive will prevent GC until it is released in audio render thread
		bInitialized = true;

		switch (BufferType)
		{
			case EBufferType::PCM:
			case EBufferType::PCMPreview:
				SubmitInitialPCMBuffers();
				break;

			case EBufferType::PCMRealTime:
			case EBufferType::Streaming:
				SubmitInitialRealtimeBuffers();
				break;

			case EBufferType::Invalid:
				break;
		}

		return true;
	}

	void FMixerSourceBuffer::OnBufferEnd()
	{
		if ((NumBuffersQeueued == 0 && bBufferFinished) || (bProcedural && !SoundWave))
		{
			return;
		}

		ProcessRealTimeSource();
	}

	TSharedPtr<FMixerSourceVoiceBuffer> FMixerSourceBuffer::GetNextBuffer()
	{
		TSharedPtr<FMixerSourceVoiceBuffer> NewBufferPtr;
		BufferQueue.Dequeue(NewBufferPtr);
		--NumBuffersQeueued;
		return NewBufferPtr;
	}

	void FMixerSourceBuffer::SubmitInitialPCMBuffers()
	{
		CurrentBuffer = 0;

		RawPCMDataBuffer.NumSamples = RawPCMDataBuffer.DataSize / sizeof(int16);
		RawPCMDataBuffer.CurrentSample = 0;

		// Only submit data if we've successfully loaded it
		if (!RawPCMDataBuffer.Data || !RawPCMDataBuffer.DataSize)
		{
			return;
		}

		RawPCMDataBuffer.LoopCount = (LoopingMode != LOOP_Never) ? Audio::LOOP_FOREVER : 0;

		// Submit the first two format-converted chunks to the source voice
		const uint32 NumSamplesPerBuffer = MONO_PCM_BUFFER_SAMPLES * NumChannels;
		int16* RawPCMBufferDataPtr = (int16*)RawPCMDataBuffer.Data;

		// Prepare the buffer for the PCM submission
		SourceVoiceBuffers[0]->AudioData.Reset(NumSamplesPerBuffer);
		SourceVoiceBuffers[0]->AudioData.AddZeroed(NumSamplesPerBuffer);

		RawPCMDataBuffer.GetNextBuffer(SourceVoiceBuffers[0].Get(), NumSamplesPerBuffer);

		SubmitBuffer(SourceVoiceBuffers[0]);

		CurrentBuffer = 1;
	}

	void FMixerSourceBuffer::SubmitInitialRealtimeBuffers()
	{
		static_assert(PLATFORM_NUM_AUDIODECOMPRESSION_PRECACHE_BUFFERS <= 2 && PLATFORM_NUM_AUDIODECOMPRESSION_PRECACHE_BUFFERS >= 0, "Unsupported number of precache buffers.");

		CurrentBuffer = 0;

		bPlayedCachedBuffer = false;
		if (!bIsSeeking && CachedRealtimeFirstBuffer.Num() > 0)
		{
			bPlayedCachedBuffer = true;

			const uint32 NumSamples = NumPrecacheFrames * NumChannels;
			const uint32 BufferSize = NumSamples * sizeof(int16);

			// Format convert the first cached buffers
#if (PLATFORM_NUM_AUDIODECOMPRESSION_PRECACHE_BUFFERS == 2)
			{
				// Prepare the precache buffer memory
				for (int32 i = 0; i < 2; ++i)
				{
					SourceVoiceBuffers[i]->AudioData.Reset();
					SourceVoiceBuffers[i]->AudioData.AddZeroed(NumSamples);
				}

				int16* CachedBufferPtr0 = (int16*)CachedRealtimeFirstBuffer.GetData();
				int16* CachedBufferPtr1 = (int16*)(CachedRealtimeFirstBuffer.GetData() + BufferSize);
				float* AudioData0 = SourceVoiceBuffers[0]->AudioData.GetData();
				float* AudioData1 = SourceVoiceBuffers[1]->AudioData.GetData();
				for (uint32 Sample = 0; Sample < NumSamples; ++Sample)
				{
					AudioData0[Sample] = CachedBufferPtr0[Sample] / 32768.0f;
				}

				for (uint32 Sample = 0; Sample < NumSamples; ++Sample)
				{
					AudioData1[Sample] = CachedBufferPtr1[Sample] / 32768.0f;
				}

				// Submit the already decoded and cached audio buffers
				SubmitBuffer(SourceVoiceBuffers[0]);
				SubmitBuffer(SourceVoiceBuffers[1]);

				CurrentBuffer = 2;
			}
#elif (PLATFORM_NUM_AUDIODECOMPRESSION_PRECACHE_BUFFERS == 1)
			{
				SourceVoiceBuffers[0]->AudioData.Reset();
				SourceVoiceBuffers[0]->AudioData.AddZeroed(NumSamples);

				int16* CachedBufferPtr0 = (int16*)CachedRealtimeFirstBuffer.GetData();

				float* AudioData0 = SourceVoiceBuffers[0]->AudioData.GetData();
				for (uint32 Sample = 0; Sample < NumSamples; ++Sample)
				{
					AudioData0[Sample] = CachedBufferPtr0[Sample] / 32768.0f;
				}

				// Submit the already decoded and cached audio buffers
				SubmitBuffer(SourceVoiceBuffers[0]);

				CurrentBuffer = 1;
			}
#endif
		}
		else if (!bIsBus)
		{
			ProcessRealTimeSource();
		}
	}

	bool FMixerSourceBuffer::ReadMoreRealtimeData(ICompressedAudioInfo* InDecoder, const int32 BufferIndex, EBufferReadMode BufferReadMode)
	{
		const int32 MaxSamples = MONO_PCM_BUFFER_SAMPLES * NumChannels;
		SourceVoiceBuffers[BufferIndex]->AudioData.Reset();
		SourceVoiceBuffers[BufferIndex]->AudioData.AddZeroed(MaxSamples);

		if (bProcedural)
		{
			check(SoundWave && SoundWave->bProcedural);
			FProceduralAudioTaskData NewTaskData;
			NewTaskData.ProceduralSoundWave = SoundWave;
			NewTaskData.AudioData = SourceVoiceBuffers[BufferIndex]->AudioData.GetData();
			NewTaskData.NumSamples = MaxSamples;
			NewTaskData.NumChannels = NumChannels;
			check(!AsyncRealtimeAudioTask);
			AsyncRealtimeAudioTask = CreateAudioTask(NewTaskData);

			// Procedural sound waves never loop
			return false;
		}
		else if (BufferType != EBufferType::PCMRealTime && BufferType != EBufferType::Streaming)
		{
			check(RawPCMDataBuffer.Data != nullptr);

			// Read the next raw PCM buffer into the source buffer index. This converts raw PCM to float.
			return RawPCMDataBuffer.GetNextBuffer(SourceVoiceBuffers[BufferIndex].Get(), MaxSamples);
		}

		check(InDecoder != nullptr);

		FDecodeAudioTaskData NewTaskData;
		NewTaskData.AudioData = SourceVoiceBuffers[BufferIndex]->AudioData.GetData();
		NewTaskData.DecompressionState = InDecoder;
		NewTaskData.BufferType = BufferType;
		NewTaskData.NumChannels = NumChannels;
		NewTaskData.bLoopingMode = LoopingMode != LOOP_Never;
		NewTaskData.bSkipFirstBuffer = (BufferReadMode == EBufferReadMode::AsynchronousSkipFirstFrame);
		NewTaskData.NumFramesToDecode = MONO_PCM_BUFFER_SAMPLES;
		NewTaskData.NumPrecacheFrames = NumPrecacheFrames;

		check(!AsyncRealtimeAudioTask);
		AsyncRealtimeAudioTask = CreateAudioTask(NewTaskData);

		return false;
	}

	void FMixerSourceBuffer::SubmitRealTimeSourceData(const bool bLooped)
	{
		// Have we reached the end of the sound
		if (bLooped)
		{
			switch (LoopingMode)
			{
			case LOOP_Never:
				// Play out any queued buffers - once there are no buffers left, the state check at the beginning of IsFinished will fire
				bBufferFinished = true;
				break;

			case LOOP_WithNotification:
				// If we have just looped, and we are looping, send notification
				// This will trigger a WaveInstance->NotifyFinished() in the FXAudio2SoundSournce::IsFinished() function on main thread.
				bLoopCallback = true;
				break;

			case LOOP_Forever:
				// Let the sound loop indefinitely
				break;
			}
		}

		if (SourceVoiceBuffers[CurrentBuffer]->AudioData.Num() > 0)
		{
			SubmitBuffer(SourceVoiceBuffers[CurrentBuffer]);
		}
	}

	void FMixerSourceBuffer::ProcessRealTimeSource()
	{
		if (AsyncRealtimeAudioTask)
		{
			AsyncRealtimeAudioTask->EnsureCompletion();

			bool bLooped = false;

			switch (AsyncRealtimeAudioTask->GetType())
			{
			case EAudioTaskType::Decode:
			{
				FDecodeAudioTaskResults TaskResult;
				AsyncRealtimeAudioTask->GetResult(TaskResult);

				bLooped = TaskResult.bLooped;
			}
			break;

			case EAudioTaskType::Procedural:
			{
				FProceduralAudioTaskResults TaskResult;
				AsyncRealtimeAudioTask->GetResult(TaskResult);

				SourceVoiceBuffers[CurrentBuffer]->AudioData.SetNum(TaskResult.NumSamplesWritten);
			}
			break;
			}

			delete AsyncRealtimeAudioTask;
			AsyncRealtimeAudioTask = nullptr;

			SubmitRealTimeSourceData(bLooped);
		}

		if (!AsyncRealtimeAudioTask)
		{
			// Update the buffer index
			if (++CurrentBuffer > 2)
			{
				CurrentBuffer = 0;
			}

			EBufferReadMode DataReadMode;
			if (bPlayedCachedBuffer)
			{
				bPlayedCachedBuffer = false;
				DataReadMode = EBufferReadMode::AsynchronousSkipFirstFrame;
			}
			else
			{
				DataReadMode = EBufferReadMode::Asynchronous;
			}

			const bool bLooped = ReadMoreRealtimeData(DecompressionState, CurrentBuffer, DataReadMode);

			// If this was a synchronous read, then immediately write it
			if (AsyncRealtimeAudioTask == nullptr)
			{
				SubmitRealTimeSourceData(bLooped);
			}
		}
	}

	void FMixerSourceBuffer::SubmitBuffer(TSharedPtr<FMixerSourceVoiceBuffer> InSourceVoiceBuffer)
	{
		NumBuffersQeueued++;
		BufferQueue.Enqueue(InSourceVoiceBuffer);
	}

	bool FMixerSourceBuffer::IsAsyncTaskInProgress() const
	{ 
		return AsyncRealtimeAudioTask != nullptr; 
	}

	bool FMixerSourceBuffer::IsAsyncTaskDone() const 
	{
		if (AsyncRealtimeAudioTask)
		{
			return AsyncRealtimeAudioTask->IsDone();
		}
		return true; 
	}
	
	void FMixerSourceBuffer::EnsureAsyncTaskFinishes() 
	{
		if (AsyncRealtimeAudioTask) 
		{ 
			AsyncRealtimeAudioTask->CancelTask(); 

			delete AsyncRealtimeAudioTask;
			AsyncRealtimeAudioTask = nullptr;
		}
	}

	bool FMixerSourceBuffer::IsBeginDestroy()
	{
		check(SoundWave);
		return SoundWave->bIsBeginDestroy;
	}

	void FMixerSourceBuffer::ClearSoundWave()
	{
		// Call on end generate right now, before destructor
		OnEndGenerate();
	}

	void FMixerSourceBuffer::OnBeginGenerate()
	{
		if (bProcedural)
		{
			check(SoundWave && SoundWave->bProcedural);
			SoundWave->OnBeginGenerate();
		}
	}

	void FMixerSourceBuffer::OnEndGenerate()
	{
		// Make sure the async task finishes!
		EnsureAsyncTaskFinishes();

		// Only need to call OnEndGenerate and access SoundWave here if we successfully initialized
		if (bInitialized && bProcedural)
		{
			check(SoundWave && SoundWave->bProcedural);
			SoundWave->OnEndGenerate();
		}
	}

}
