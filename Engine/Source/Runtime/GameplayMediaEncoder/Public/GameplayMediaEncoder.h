// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Logging/LogMacros.h"
#include "AudioMixerDevice.h"
#include "GameplayMediaEncoderSample.h"

#include "RHI.h"
#include "RHIResources.h"

class FBaseVideoEncoder;
class FWmfAudioEncoder;
class SWindow;

class IGameplayMediaEncoderListener
{
public:
	virtual void OnMediaSample(const FGameplayMediaEncoderSample& Sample) = 0;
};

class GAMEPLAYMEDIAENCODER_API FGameplayMediaEncoder final : private ISubmixBufferListener
{
public:

	/**
	 * Get the singleton
	 */
	static FGameplayMediaEncoder* Get();

	FGameplayMediaEncoder();
	~FGameplayMediaEncoder();

	bool RegisterListener(IGameplayMediaEncoderListener* Listener);
	void UnregisterListener(IGameplayMediaEncoderListener* Listener);

	bool GetAudioOutputType(TRefCountPtr<IMFMediaType>& OutType);
	bool GetVideoOutputType(TRefCountPtr<IMFMediaType>& OutType);

	void SetVideoBitrate(uint32 Bitrate);
	void SetVideoFramerate(uint32 Framerate);

	bool Initialize();
	void Shutdown();
	bool Start();
	void Stop();

	static void InitializeCmd()
	{
		// We call Get(), so it creates the singleton
		Get()->Initialize();
	}

	static void ShutdownCmd()
	{
		// We call Get(), so it creates the singleton
		Get()->Shutdown();
	}

	static void StartCmd()
	{
		// We call Get(), so it creates the singleton
		Get()->Start();
	}

	static void StopCmd()
	{
		// We call Get(), so it creates the singleton
		Get()->Stop();
	}

private:
	FTimespan GetMediaTimestamp() const;

	bool OnMediaSampleReady(const FGameplayMediaEncoderSample& Sample);

	// Back buffer capture
	void OnBackBufferReady(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
	// ISubmixBufferListener interface
	void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;

	bool ProcessAudioFrame(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate);
	bool ProcessVideoFrame(const FTexture2DRHIRef& BackBuffer);

	bool ChangeVideoConfig();

	FCriticalSection ListenersCS;
	TArray<IGameplayMediaEncoderListener*> Listeners;

	FCriticalSection AudioProcessingCS;
	FCriticalSection VideoProcessingCS;

	TUniquePtr<FWmfAudioEncoder> AudioEncoder;
	TUniquePtr<FBaseVideoEncoder> VideoEncoder;
#if PLATFORM_WINDOWS
	TSharedPtr<class FEncoderDevice> EncoderDevice;
#endif
	bool bAudioFormatChecked = false;
	bool bDoFrameSkipping = false;

	// Keep this as a member variables, to reuse the memory allocation
	Audio::TSampleBuffer<int16> PCM16;

	uint64 NumCapturedFrames = 0;
	FTimespan StartTime = 0;
	// Instead of using the AudioClock parameter ISubmixBufferListener::OnNewSubmixBuffer gives us, we calculate our own, by
	// advancing it as we receive more data.
	// This is so that we can adjust the clock if things get out of sync, such as if we break into the debugger.
	double AudioClock = 0;

	FTimespan LastVideoInputTimestamp = 0;

	// It is possible to suspend the processing of media samples which is
	// required during resolution change.
	FCriticalSection ProcessMediaSamplesCS;
	bool bProcessMediaSamples = true;

	// live streaming: quality adaptation to available uplink b/w
	TAtomic<uint32> NewVideoBitrate{ 0 };
	FThreadSafeBool bChangeBitrate = false;
	FTimespan FramerateMonitoringStart = -1;
	TAtomic<uint32> NewVideoFramerate{ 0 };
	FThreadSafeBool bChangeFramerate = false;
};

