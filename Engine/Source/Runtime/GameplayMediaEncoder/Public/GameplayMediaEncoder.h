// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Logging/LogMacros.h"
#include "AudioMixerDevice.h"
#include "AVEncoder.h"

#include "RHI.h"
#include "RHIResources.h"

class SWindow;

class IGameplayMediaEncoderListener
{
public:
	virtual void OnMediaSample(const AVEncoder::FAVPacket& Sample) = 0;
};

class GAMEPLAYMEDIAENCODER_API FGameplayMediaEncoder final : private ISubmixBufferListener, private AVEncoder::IAudioEncoderListener, private AVEncoder::IVideoEncoderListener
{
public:

	/**
	 * Get the singleton
	 */
	static FGameplayMediaEncoder* Get();

	~FGameplayMediaEncoder();

	bool RegisterListener(IGameplayMediaEncoderListener* Listener);
	void UnregisterListener(IGameplayMediaEncoderListener* Listener);

	void SetVideoBitrate(uint32 Bitrate);
	void SetVideoFramerate(uint32 Framerate);

	/*
	 * When capturing frame data, we should use the App Time rather than Platform time
	 * Default is to use Platform Time
	 * This is useful for fixed framerate video rendering
	 * **AUDIO is not supported**
	*/
	void SetFramesShouldUseAppTime(bool bUseAppTime);

	bool IsFramesUsingAppTime() const
	{
		static bool bIsForcedAppTime = FParse::Param(FCommandLine::Get(), TEXT("GameplayMediaEncoder.UseAppTime"));

		return bIsForcedAppTime || bShouldFramesUseAppTime;
	}

	double QueryClock() const
	{
		return IsFramesUsingAppTime() ? FApp::GetCurrentTime() : FPlatformTime::Seconds();
	}

	/**
	 * Returns the audio codec name and configuration
	 */
	TPair<FString, AVEncoder::FAudioEncoderConfig> GetAudioConfig() const;
	TPair<FString, AVEncoder::FVideoEncoderConfig> GetVideoConfig() const;

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

	// Private to control how our single instance is created
	FGameplayMediaEncoder();

	// Returns how long it has been recording for.
	FTimespan GetMediaTimestamp() const;

	// Back buffer capture
	void OnBackBufferReady(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
	// ISubmixBufferListener interface
	void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;

	void ProcessAudioFrame(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate);
	void ProcessVideoFrame(const FTexture2DRHIRef& BackBuffer);

	bool ChangeVideoConfig();

	//
	// AVEncoder::IAudioEncoderListener interface
	void OnEncodedAudioFrame(const AVEncoder::FAVPacket& Packet) override;
	//
	// AVEncoder::IVideoEncoderListener interface
	void OnEncodedVideoFrame(const AVEncoder::FAVPacket& Packet, AVEncoder::FEncoderVideoFrameCookie* Cookie) override;

	void OnEncodedFrame(const AVEncoder::FAVPacket& Packet);

	FCriticalSection ListenersCS;
	TArray<IGameplayMediaEncoderListener*> Listeners;

	FCriticalSection AudioProcessingCS;
	FCriticalSection VideoProcessingCS;
	TUniquePtr<AVEncoder::FAudioEncoder> AudioEncoder;
	TUniquePtr<AVEncoder::FVideoEncoder> VideoEncoder;

	uint64 NumCapturedFrames = 0;
	FTimespan StartTime = 0;

	// Instead of using the AudioClock parameter ISubmixBufferListener::OnNewSubmixBuffer gives us, we calculate our own, by
	// advancing it as we receive more data.
	// This is so that we can adjust the clock if things get out of sync, such as if we break into the debugger.
	double AudioClock = 0;

	FTimespan LastVideoInputTimestamp = 0;

	bool bAudioFormatChecked = false;
	bool bDoFrameSkipping = false;

	friend class FGameplayMediaEncoderModule;
	static FGameplayMediaEncoder* Singleton;

	// live streaming: quality adaptation to available uplink b/w
	TAtomic<uint32> NewVideoBitrate{ 0 };
	FThreadSafeBool bChangeBitrate = false;
	TAtomic<uint32> NewVideoFramerate{ 0 };
	FThreadSafeBool bChangeFramerate = false;

	bool bShouldFramesUseAppTime = false;
};

