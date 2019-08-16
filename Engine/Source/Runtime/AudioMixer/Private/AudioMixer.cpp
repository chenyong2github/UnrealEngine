// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioMixer.h"
#include "AudioMixerDevice.h"
#include "HAL/RunnableThread.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/ThreadSafeCounter.h"
#include "ProfilingDebugging/CsvProfiler.h"


// Defines the "Audio" category in the CSV profiler.
// This should only be defined here. Modules who wish to use this category should contain the line
// 		CSV_DECLARE_CATEGORY_MODULE_EXTERN(AUDIOMIXER_API, Audio);
// 		
CSV_DEFINE_CATEGORY_MODULE(AUDIOMIXER_API, Audio, true);

// Command to enable logging to display accurate audio render times
static int32 LogRenderTimesCVar = 0;
FAutoConsoleVariableRef CVarLogRenderTimes(
	TEXT("au.LogRenderTimes"),
	LogRenderTimesCVar,
	TEXT("Logs Audio Render Times.\n")
	TEXT("0: Not Log, 1: Log"),
	ECVF_Default);

// Command for setting the audio render thread priority.
static int32 SetRenderThreadPriorityCVar = (int32)TPri_Highest;
FAutoConsoleVariableRef CVarLogRenderThreadPriority(
	TEXT("au.RenderThreadPriority"),
	LogRenderTimesCVar,
	TEXT("Sets audio render thread priority. Defaults to 3.\n")
	TEXT("0: Normal, 1: Above Normal, 2: Below Normal, 3: Highest, 4: Lowest, 5: Slightly Below Normal, 6: Time Critical"),
	ECVF_Default);

static int32 EnableDetailedWindowsDeviceLoggingCVar = 0;
FAutoConsoleVariableRef CVarEnableDetailedWindowsDeviceLogging(
	TEXT("au.EnableDetailedWindowsDeviceLogging"),
	EnableDetailedWindowsDeviceLoggingCVar,
	TEXT("Enables detailed windows device logging.\n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default);

static int32 DisableDeviceSwapCVar = 0;
FAutoConsoleVariableRef CVarDisableDeviceSwap(
	TEXT("au.DisableDeviceSwap"),
	DisableDeviceSwapCVar,
	TEXT("Disable device swap handling code for Audio Mixer on Windows.\n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default);


static int32 OverrunTimeoutCVar = 1000;
FAutoConsoleVariableRef CVarOverrunTimeout(
	TEXT("au.OverrunTimeoutMSec"),
	OverrunTimeoutCVar,
	TEXT("Amount of time to wait for the render thread to time out before swapping to the null device. \n"),
	ECVF_Default);

namespace Audio
{
	int32 sRenderInstanceIds = 0;

	FThreadSafeCounter AudioMixerTaskCounter;

	FAudioRenderTimeAnalysis::FAudioRenderTimeAnalysis()
		: AvgRenderTime(0.0)
		, MaxRenderTime(0.0)
		, TotalRenderTime(0.0)
		, StartTime(0.0)
		, RenderTimeCount(0)
		, RenderInstanceId(sRenderInstanceIds++)
	{}

	void FAudioRenderTimeAnalysis::Start()
	{
		StartTime = FPlatformTime::Cycles();
	}

	void FAudioRenderTimeAnalysis::End()
	{
		uint32 DeltaCycles = FPlatformTime::Cycles() - StartTime;
		double DeltaTime = DeltaCycles * FPlatformTime::GetSecondsPerCycle();

		TotalRenderTime += DeltaTime;
		RenderTimeSinceLastLog += DeltaTime;
		++RenderTimeCount;
		AvgRenderTime = TotalRenderTime / RenderTimeCount;
		
		if (DeltaTime > MaxRenderTime)
		{
			MaxRenderTime = DeltaTime;
		}
		
		if (DeltaTime > MaxSinceTick)
		{
			MaxSinceTick = DeltaTime;
		}

		if (LogRenderTimesCVar == 1)
		{
			if (RenderTimeCount % 32 == 0)
			{
				RenderTimeSinceLastLog /= 32.0f;
				UE_LOG(LogAudioMixerDebug, Display, TEXT("Render Time [id:%d] - Max: %.2f ms, MaxDelta: %.2f ms, Delta Avg: %.2f ms, Global Avg: %.2f ms"), 
					RenderInstanceId, 
					(float)MaxRenderTime * 1000.0f, 
					(float)MaxSinceTick * 1000.0f,
					RenderTimeSinceLastLog * 1000.0f, 
					(float)AvgRenderTime * 1000.0f);

				RenderTimeSinceLastLog = 0.0f;
				MaxSinceTick = 0.0f;
			}
		}
	}

	void FOutputBuffer::Init(IAudioMixer* InAudioMixer, const int32 InNumSamples, const EAudioMixerStreamDataFormat::Type InDataFormat)
	{
		Buffer.SetNumZeroed(InNumSamples);
		DataFormat = InDataFormat;
		AudioMixer = InAudioMixer;

		switch (DataFormat)
		{
			case EAudioMixerStreamDataFormat::Float:
				// nothing to do...
				break;

			case EAudioMixerStreamDataFormat::Int16:
				FormattedBuffer.SetNumZeroed(InNumSamples * sizeof(int16));	
				break;

			default:
				// Not implemented/supported
				check(false);
				break;
		}
	}

	void FOutputBuffer::MixNextBuffer()
 	{
		CSV_SCOPED_TIMING_STAT(Audio, RenderAudio);

		// Zero the buffer
		FPlatformMemory::Memzero(Buffer.GetData(), Buffer.Num() * sizeof(float));
		AudioMixer->OnProcessAudioStream(Buffer);

		switch (DataFormat)
		{
			// Doesn't do anything...
		case EAudioMixerStreamDataFormat::Float:
			break;

		case EAudioMixerStreamDataFormat::Int16:
		{
			int16* BufferInt16 = (int16*)FormattedBuffer.GetData();
			const int32 NumSamples = Buffer.Num();
			for (int32 i = 0; i < NumSamples; ++i)
			{
				BufferInt16[i] = (int16)(Buffer[i] * 32767.0f);
			}
		}
		break;

		default:
			// Not implemented/supported
			check(false);
			break;
		}

		// Mark that we're ready
		bIsReady = true;
 	}
 
	const uint8* FOutputBuffer::GetBufferData() const
	{
		if (DataFormat == EAudioMixerStreamDataFormat::Float)
		{
			return (const uint8*)Buffer.GetData();
		}
		else
		{
			return (const uint8*)FormattedBuffer.GetData();
		}
	}

	uint8* FOutputBuffer::GetBufferData()
	{
		if (DataFormat == EAudioMixerStreamDataFormat::Float)
		{
			return (uint8*)Buffer.GetData();
		}
		else
		{
			return (uint8*)FormattedBuffer.GetData();
		}
	}

	int32 FOutputBuffer::GetNumFrames() const
	{
		return Buffer.Num();
	}

	void FOutputBuffer::Reset(const int32 InNewNumSamples)
	{
		Buffer.Reset();
		Buffer.AddZeroed(InNewNumSamples);

		switch (DataFormat)
		{
			// Doesn't do anything...
			case EAudioMixerStreamDataFormat::Float:
				break;

			case EAudioMixerStreamDataFormat::Int16:
			{
				FormattedBuffer.Reset();
				FormattedBuffer.AddZeroed(InNewNumSamples * sizeof(int16));
			}
			break;
		}

		bIsReady = false;
	}

	/**
	 * IAudioMixerPlatformInterface
	 */

	IAudioMixerPlatformInterface::IAudioMixerPlatformInterface()
		: bWarnedBufferUnderrun(false)
		, AudioRenderThread(nullptr)
		, AudioRenderEvent(nullptr)
		, bIsInDeviceSwap(false)
		, AudioFadeEvent(nullptr)
		, CurrentBufferReadIndex(INDEX_NONE)
		, CurrentBufferWriteIndex(INDEX_NONE)
		, NumOutputBuffers(0)
		, FadeVolume(0.0f)
		, LastError(TEXT("None"))
		, bPerformingFade(true)
		, bFadedOut(false)
		, bIsDeviceInitialized(false)
		, bMoveAudioStreamToNewAudioDevice(false)
		, bIsUsingNullDevice(false)
		, NullDeviceCallback(nullptr)
	{
		FadeParam.SetValue(0.0f);
	}

	IAudioMixerPlatformInterface::~IAudioMixerPlatformInterface()
	{
		check(AudioStreamInfo.StreamState == EAudioOutputStreamState::Closed);
	}

	void IAudioMixerPlatformInterface::FadeIn()
	{
		bPerformingFade = true;
		bFadedOut = false;
		FadeVolume = 1.0f;
	}

	void IAudioMixerPlatformInterface::FadeOut()
	{
		if (bFadedOut || FadeVolume == 0.0f)
		{
			return;
		}

		bPerformingFade = true;
		AudioFadeEvent->Wait();
		FadeVolume = 0.0f;
	}

	void IAudioMixerPlatformInterface::PostInitializeHardware()
	{
		bIsDeviceInitialized = true;
	}

	template<typename BufferType>
	void IAudioMixerPlatformInterface::ApplyAttenuationInternal(BufferType* BufferDataPtr, const int32 NumFrames)
	{
		// Perform fade in and fade out global attenuation to avoid clicks/pops on startup/shutdown
		if (bPerformingFade)
		{
			FadeParam.SetValue(FadeVolume, NumFrames);

			for (int32 i = 0; i < NumFrames; ++i)
			{
				BufferDataPtr[i] = (BufferType)(BufferDataPtr[i] * FadeParam.Update());
			}

			bFadedOut = (FadeVolume == 0.0f);
			bPerformingFade = false;
			AudioFadeEvent->Trigger();
		}
		else if (bFadedOut)
		{
			// If we're faded out, then just zero the data.
			FPlatformMemory::Memzero((void*)BufferDataPtr, sizeof(BufferType)*NumFrames);
		}

		FadeParam.Reset();
	}

	void IAudioMixerPlatformInterface::StartRunningNullDevice()
	{
		if (!NullDeviceCallback.IsValid())
		{
			// Reset all of the buffers, then immediately kick off another render.
			for (int32 Index = 0; Index < OutputBuffers.Num(); ++Index)
			{
				OutputBuffers[Index].Reset(OpenStreamParams.NumFrames * AudioStreamInfo.DeviceInfo.NumChannels);
			}

			CurrentBufferReadIndex = 0;
			CurrentBufferWriteIndex = 1;

			SubmitBuffer(OutputBuffers[CurrentBufferReadIndex].GetBufferData());
			check(OpenStreamParams.NumFrames * AudioStreamInfo.DeviceInfo.NumChannels == OutputBuffers[CurrentBufferReadIndex].GetBuffer().Num());

			AudioRenderEvent->Trigger();

			float BufferDuration = ((float) OpenStreamParams.NumFrames) / OpenStreamParams.SampleRate;
			NullDeviceCallback.Reset(new FMixerNullCallback( BufferDuration, [this]()
			{
				this->ReadNextBuffer();
			}));
			bIsUsingNullDevice = true;
		}
	}

	void IAudioMixerPlatformInterface::StopRunningNullDevice()
	{
		if (NullDeviceCallback.IsValid())
		{
			NullDeviceCallback.Reset();
			bIsUsingNullDevice = false;
		}
	}

	void IAudioMixerPlatformInterface::ApplyMasterAttenuation()
	{
		const int32 NextReadIndex = (CurrentBufferReadIndex + 1) % NumOutputBuffers;
		FOutputBuffer& CurrentReadBuffer = OutputBuffers[NextReadIndex];

		EAudioMixerStreamDataFormat::Type Format = CurrentReadBuffer.GetFormat();
		uint8* BufferDataPtr = CurrentReadBuffer.GetBufferData();
		
		if (Format == EAudioMixerStreamDataFormat::Float)
		{
			ApplyAttenuationInternal((float*)BufferDataPtr, CurrentReadBuffer.GetNumFrames());
		}
		else
		{
			ApplyAttenuationInternal((int16*)BufferDataPtr, CurrentReadBuffer.GetNumFrames());
		}
	}

	void IAudioMixerPlatformInterface::ReadNextBuffer()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		// If we are flushing buffers for our output voice and this is being called on the audio thread directly,
		// early exit.
		if (bIsInDeviceSwap)
		{
			return;
		}

		// If we are currently swapping devices and OnBufferEnd is being triggered in an XAudio2Thread,
		// early exit.
		if (!DeviceSwapCriticalSection.TryLock())
		{
			return;
		}

		// Don't read any more audio if we're not running or changing device
		if (AudioStreamInfo.StreamState != EAudioOutputStreamState::Running)
		{
			DeviceSwapCriticalSection.Unlock();
			return;
		}

		check(CurrentBufferReadIndex != INDEX_NONE);
		check(CurrentBufferWriteIndex != INDEX_NONE);

		// Reset the ready state of the buffer which was just finished playing
		FOutputBuffer& CurrentReadBuffer = OutputBuffers[CurrentBufferReadIndex];
		CurrentReadBuffer.ResetReadyState();

		// Get the next index that we want to read
		int32 NextReadIndex = (CurrentBufferReadIndex + 1) % NumOutputBuffers;

		// If it's not ready, warn, and then wait here. This will cause underruns but is preferable than getting out-of-order buffer state.
		static int32 UnderrunCount = 0;
		static int32 CurrentUnderrunCount = 0;
		
		if (!OutputBuffers[NextReadIndex].IsReady())
		{

			UnderrunCount++;
			CurrentUnderrunCount++;
			
			if (!bWarnedBufferUnderrun)
			{						
				UE_LOG(LogAudioMixerDebug, Log, TEXT("Audio Buffer Underrun detected."));
				bWarnedBufferUnderrun = true;
			}
		
			SubmitBuffer(UnderrunBuffer.GetBufferData());
		}
		else
		{
			ApplyMasterAttenuation();

			// As soon as a valid buffer goes through, allow more warning
			if (bWarnedBufferUnderrun)
			{
				UE_LOG(LogAudioMixerDebug, Log, TEXT("Audio had %d underruns [Total: %d]."), CurrentUnderrunCount, UnderrunCount);
			}
			CurrentUnderrunCount = 0;
			bWarnedBufferUnderrun = false;

			// Submit the buffer at the next read index, but don't set the read index value yet
			SubmitBuffer(OutputBuffers[NextReadIndex].GetBufferData());

			// Update the current read index to the next read index
			CurrentBufferReadIndex = NextReadIndex;
		}

		DeviceSwapCriticalSection.Unlock();

		// Kick off rendering of the next set of buffers
		AudioRenderEvent->Trigger();
	}

	void IAudioMixerPlatformInterface::BeginGeneratingAudio()
	{
		// Setup the output buffers
		const int32 NumOutputFrames = OpenStreamParams.NumFrames;
		const int32 NumOutputChannels = AudioStreamInfo.DeviceInfo.NumChannels;
		const int32 NumOutputSamples = NumOutputFrames * NumOutputChannels;

		// Set the number of buffers to be one more than the number to queue.
		NumOutputBuffers = FMath::Max(OpenStreamParams.NumBuffers, 2);

		CurrentBufferReadIndex = 0;
		CurrentBufferWriteIndex = 1;

		OutputBuffers.Reset();
		OutputBuffers.AddDefaulted(NumOutputBuffers);
		for (int32 Index = 0; Index < NumOutputBuffers; ++Index)
		{
			OutputBuffers[Index].Init(AudioStreamInfo.AudioMixer, NumOutputSamples, AudioStreamInfo.DeviceInfo.Format);
		}

		// Create an underrun buffer
		UnderrunBuffer.Init(AudioStreamInfo.AudioMixer, NumOutputSamples, AudioStreamInfo.DeviceInfo.Format);

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Running;

		check(AudioRenderEvent == nullptr);
		AudioRenderEvent = FPlatformProcess::GetSynchEventFromPool();
		check(AudioRenderEvent != nullptr);

		check(AudioFadeEvent == nullptr);
		AudioFadeEvent = FPlatformProcess::GetSynchEventFromPool();
		check(AudioFadeEvent != nullptr);

		check(AudioRenderThread == nullptr);
		AudioRenderThread = FRunnableThread::Create(this, *FString::Printf(TEXT("AudioMixerRenderThread(%d)"), AudioMixerTaskCounter.Increment()), 0, (EThreadPriority)SetRenderThreadPriorityCVar, FPlatformAffinity::GetAudioThreadMask());
		check(AudioRenderThread != nullptr);
	}

	void IAudioMixerPlatformInterface::StopGeneratingAudio()
	{
		// Stop the FRunnable thread

		if (AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopped)
		{
			AudioStreamInfo.StreamState = EAudioOutputStreamState::Stopping;
		}

		if (AudioRenderEvent != nullptr)
		{
			// Make sure the thread wakes up
			AudioRenderEvent->Trigger();
		}

		if (AudioRenderThread != nullptr)
		{
			AudioRenderThread->WaitForCompletion();
			check(AudioStreamInfo.StreamState == EAudioOutputStreamState::Stopped);

			delete AudioRenderThread;
			AudioRenderThread = nullptr;
		}

		if (AudioRenderEvent != nullptr)
		{
			FPlatformProcess::ReturnSynchEventToPool(AudioRenderEvent);
			AudioRenderEvent = nullptr;
		}

		if (AudioFadeEvent != nullptr)
		{
			FPlatformProcess::ReturnSynchEventToPool(AudioFadeEvent);
			AudioFadeEvent = nullptr;
		}
	}

	void IAudioMixerPlatformInterface::Tick()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		// In single-threaded mode, we simply render buffers until we run out of space
		// The single-thread audio backend will consume these rendered buffers when they need to
		if (AudioStreamInfo.StreamState == EAudioOutputStreamState::Running && bIsDeviceInitialized)
		{
			// Render mixed buffers till our queued buffers are filled up
			while (CurrentBufferReadIndex != CurrentBufferWriteIndex)
			{
				RenderTimeAnalysis.Start();
				OutputBuffers[CurrentBufferWriteIndex].MixNextBuffer();
				RenderTimeAnalysis.End();

				CurrentBufferWriteIndex = (CurrentBufferWriteIndex + 1) % NumOutputBuffers;
			}
		}
	}

	uint32 IAudioMixerPlatformInterface::MainAudioDeviceRun()
	{
		return RunInternal();
	}

	uint32 IAudioMixerPlatformInterface::RunInternal()
	{
		// Lets prime and submit the first buffer (which is going to be the buffer underrun buffer)
		SubmitBuffer(UnderrunBuffer.GetBufferData());

		OutputBuffers[CurrentBufferWriteIndex].MixNextBuffer();

		check(CurrentBufferReadIndex == 0);
		check(CurrentBufferWriteIndex == 1);

		// Start immediately processing the next buffer

		while (AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopping)
		{
			RenderTimeAnalysis.Start();

			// Render mixed buffers till our queued buffers are filled up
			while (CurrentBufferReadIndex != CurrentBufferWriteIndex && bIsDeviceInitialized)
			{
				OutputBuffers[CurrentBufferWriteIndex].MixNextBuffer();

				CurrentBufferWriteIndex = (CurrentBufferWriteIndex + 1) % NumOutputBuffers;
			}

			RenderTimeAnalysis.End();

			// Bounds check the timeout for our audio render event.
			OverrunTimeoutCVar = FMath::Clamp(OverrunTimeoutCVar, 500, 5000);

			// Now wait for a buffer to be consumed, which will bump up the read index.
			if (AudioRenderEvent && !AudioRenderEvent->Wait(static_cast<uint32>(OverrunTimeoutCVar)))
			{
				// if we reached this block, we timed out, and should attempt to
				// bail on our current device.
				bMoveAudioStreamToNewAudioDevice = true;
			}
		}

		OpenStreamParams.AudioMixer->OnAudioStreamShutdown();

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Stopped;
		return 0;
	}

	uint32 IAudioMixerPlatformInterface::Run()
	{	
		LLM_SCOPE(ELLMTag::AudioMixer);

		// Call different functions depending on if it's the "main" audio mixer instance. Helps debugging callstacks.
		if (AudioStreamInfo.AudioMixer->IsMainAudioMixer())
		{
			return MainAudioDeviceRun();
		}

		return RunInternal();
	}

	/** The default channel orderings to use when using pro audio interfaces while still supporting surround sound. */
	static EAudioMixerChannel::Type DefaultChannelOrder[AUDIO_MIXER_MAX_OUTPUT_CHANNELS];

	static void InitializeDefaultChannelOrder()
	{
		static bool bInitialized = false;
		if (bInitialized)
		{
			return;
		}

		bInitialized = true;

		// Create a hard-coded default channel order
		check(ARRAY_COUNT(DefaultChannelOrder) == AUDIO_MIXER_MAX_OUTPUT_CHANNELS);
		DefaultChannelOrder[0] = EAudioMixerChannel::FrontLeft;
		DefaultChannelOrder[1] = EAudioMixerChannel::FrontRight;
		DefaultChannelOrder[2] = EAudioMixerChannel::FrontCenter;
		DefaultChannelOrder[3] = EAudioMixerChannel::LowFrequency;
		DefaultChannelOrder[4] = EAudioMixerChannel::SideLeft;
		DefaultChannelOrder[5] = EAudioMixerChannel::SideRight;
		DefaultChannelOrder[6] = EAudioMixerChannel::BackLeft;
		DefaultChannelOrder[7] = EAudioMixerChannel::BackRight;

		bool bOverridden = false;
		EAudioMixerChannel::Type ChannelMapOverride[AUDIO_MIXER_MAX_OUTPUT_CHANNELS];
		for (int32 i = 0; i < AUDIO_MIXER_MAX_OUTPUT_CHANNELS; ++i)
		{
			ChannelMapOverride[i] = DefaultChannelOrder[i];
		}

		// Now check the ini file to see if this is overridden
		for (int32 i = 0; i < AUDIO_MIXER_MAX_OUTPUT_CHANNELS; ++i)
		{
			int32 ChannelPositionOverride = 0;

			const TCHAR* ChannelName = EAudioMixerChannel::ToString(DefaultChannelOrder[i]);
			if (GConfig->GetInt(TEXT("AudioDefaultChannelOrder"), ChannelName, ChannelPositionOverride, GEngineIni))
			{
				if (ChannelPositionOverride >= 0 && ChannelPositionOverride < AUDIO_MIXER_MAX_OUTPUT_CHANNELS)
				{
					bOverridden = true;
					ChannelMapOverride[ChannelPositionOverride] = DefaultChannelOrder[i];
				}
				else
				{
					UE_LOG(LogAudioMixer, Error, TEXT("Invalid channel index '%d' in AudioDefaultChannelOrder in ini file."), i);
					bOverridden = false;
					break;
				}
			}
		}

		// Now validate that there's no duplicates.
		if (bOverridden)
		{
			bool bIsValid = true;
			for (int32 i = 0; i < AUDIO_MIXER_MAX_OUTPUT_CHANNELS; ++i)
			{
				for (int32 j = 0; j < AUDIO_MIXER_MAX_OUTPUT_CHANNELS; ++j)
				{
					if (j != i && ChannelMapOverride[j] == ChannelMapOverride[i])
					{
						bIsValid = false;
						break;
					}
				}
			}

			if (!bIsValid)
			{
				UE_LOG(LogAudioMixer, Error, TEXT("Invalid channel index or duplicate entries in AudioDefaultChannelOrder in ini file."));
			}
			else
			{
				for (int32 i = 0; i < AUDIO_MIXER_MAX_OUTPUT_CHANNELS; ++i)
				{
					DefaultChannelOrder[i] = ChannelMapOverride[i];
				}
			}
		}
	}

	bool IAudioMixerPlatformInterface::GetChannelTypeAtIndex(const int32 Index, EAudioMixerChannel::Type& OutType)
	{
		InitializeDefaultChannelOrder();

		if (Index >= 0 && Index < AUDIO_MIXER_MAX_OUTPUT_CHANNELS)
		{
			OutType = DefaultChannelOrder[Index];
			return true;
		}
		return false;
	}

	bool IAudioMixer::ShouldIgnoreDeviceSwaps()
	{
		return DisableDeviceSwapCVar != 0;
	}

	bool IAudioMixer::ShouldLogDeviceSwaps()
	{
		return EnableDetailedWindowsDeviceLoggingCVar != 0;
	}
}
