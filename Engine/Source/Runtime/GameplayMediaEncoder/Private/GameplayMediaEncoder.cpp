// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameplayMediaEncoder.h"
#include "Engine/GameEngine.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"
#include "RendererInterface.h"
#include "ScreenRendering.h"
#include "ShaderCore.h"
#include "PipelineStateCache.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "IbmLiveStreaming.h"

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	#include "Microsoft/WmfAudioEncoder.h"
#endif

#if PLATFORM_WINDOWS
	#include "Microsoft/Windows/EncoderDevice.h"
	#include "Microsoft/Windows/NvVideoEncoder.h"
	#include "Microsoft/Windows/WmfVideoEncoder.h"
	#include "Microsoft/Windows/AmdAmfVideoEncoder.h"
#elif PLATFORM_XBOXONE
	#include "Microsoft/XboxOne/XboxOneVideoEncoder.h"
#endif

DEFINE_LOG_CATEGORY(GameplayMediaEncoder);
CSV_DEFINE_CATEGORY(GameplayMediaEncoder, true);

// right now we support only 48KHz audio sample rate as it's the only config UE4 seems to output
// WMF AAC encoder supports also 44100Hz so its support can be easily added
const uint32 HardcodedAudioSamplerate = 48000;
// for now we downsample to stereo. WMF AAC encoder also supports 6 (5.1) channels
// so it can be added too
const uint32 HardcodedAudioNumChannels = 2;
// currently neither IVideoRecordingSystem neither HighlightFeature APIs allow to configure
// audio stream parameters
const uint32 HardcodedAudioBitrate = 24000;

// currently neither IVideoRecordingSystem neither HighlightFeature APIs allow to configure
// video stream parameters
#if PLATFORM_WINDOWS
const uint32 HardcodedVideoFPS = 60;
#else
const uint32 HardcodedVideoFPS = 30;
#endif
const uint32 HardcodedVideoBitrate = 5000000;
const uint32 MinVideoBitrate = 1000000;
const uint32 MaxVideoBitrate = 20000000;
const uint32 MinVideoFPS = 10;
const uint32 MaxVideoFPS = 60;

const uint32 MaxWidth = 1920;
const uint32 MaxHeight = 1080;

GAMEPLAYMEDIAENCODER_START

FAutoConsoleCommand GameplayMediaEncoderInitialize(
	TEXT("GameplayMediaEncoder.Initialize"),
	TEXT("Constructs the audio/video encoding objects. Does not start encoding"),
	FConsoleCommandDelegate::CreateStatic(&FGameplayMediaEncoder::InitializeCmd)
);

FAutoConsoleCommand GameplayMediaEncoderStart(
	TEXT("GameplayMediaEncoder.Start"),
	TEXT("Starts encoding"),
	FConsoleCommandDelegate::CreateStatic(&FGameplayMediaEncoder::StartCmd)
);

FAutoConsoleCommand GameplayMediaEncoderStop(
	TEXT("GameplayMediaEncoder.Stop"),
	TEXT("Stops encoding"),
	FConsoleCommandDelegate::CreateStatic(&FGameplayMediaEncoder::StopCmd)
);

FAutoConsoleCommand GameplayMediaEncoderShutdown(
	TEXT("GameplayMediaEncoder.Shutdown"),
	TEXT("Releases all systems."),
	FConsoleCommandDelegate::CreateStatic(&FGameplayMediaEncoder::ShutdownCmd)
);

//////////////////////////////////////////////////////////////////////////
//
// FGameplayMediaEncoder
//
//////////////////////////////////////////////////////////////////////////

FGameplayMediaEncoder* FGameplayMediaEncoder::Get()
{
	static FGameplayMediaEncoder Singleton;
	return &Singleton;
}

FGameplayMediaEncoder::FGameplayMediaEncoder()
{
#if PLATFORM_WINDOWS
	EncoderDevice = MakeShared<FEncoderDevice>();
#endif
}

FGameplayMediaEncoder::~FGameplayMediaEncoder()
{
}

bool FGameplayMediaEncoder::RegisterListener(IGameplayMediaEncoderListener* Listener)
{
	check(IsInGameThread());
	FScopeLock Lock(&ListenersCS);

	if (Listeners.Num() == 0)
	{
		UE_LOG(GameplayMediaEncoder, Log, TEXT("Registering the first listener"));
		if (!Start())
		{
			return false;
		}
	}

	Listeners.AddUnique(Listener);
	return true;
}

void FGameplayMediaEncoder::UnregisterListener(IGameplayMediaEncoderListener* Listener)
{
	check(IsInGameThread());

	ListenersCS.Lock();

	Listeners.Remove(Listener);

	bool bAnyListenersLeft = Listeners.Num() > 0;

	ListenersCS.Unlock();

	if (bAnyListenersLeft == false)
	{
		UE_LOG(GameplayMediaEncoder, Log, TEXT("Unregistered the last listener"));
		Stop();
	}
}

bool FGameplayMediaEncoder::GetAudioOutputType(TRefCountPtr<IMFMediaType>& OutType)
{
	return AudioEncoder ? AudioEncoder->GetOutputType(OutType) : false;
}

bool FGameplayMediaEncoder::GetVideoOutputType(TRefCountPtr<IMFMediaType>& OutType)
{
	return VideoEncoder ? VideoEncoder->GetOutputType(OutType) : false;
}

bool FGameplayMediaEncoder::Initialize()
{
	MemoryCheckpoint("Initial");

	if (VideoEncoder)
	{
		UE_LOG(GameplayMediaEncoder, Log, TEXT("Already initialized"));
		return true;
	}

	auto OnMediaSampleReadyCallback = [this](const FGameplayMediaEncoderSample& Sample)
	{
		return OnMediaSampleReady(Sample);
	};

	//
	// Audio
	// 
	AudioEncoder.Reset(new FWmfAudioEncoder(OnMediaSampleReadyCallback));
	FWmfAudioEncoderConfig AudioConfig;
	AudioConfig.SampleRate = HardcodedAudioSamplerate;
	AudioConfig.NumChannels = HardcodedAudioNumChannels;
	AudioConfig.Bitrate = HardcodedAudioBitrate;
	if (!AudioEncoder->Initialize(AudioConfig))
	{
		Stop();
		return false;
	}

	bAudioFormatChecked = false;
	MemoryCheckpoint("Audio encoder initialized");

	//
	// Video
	//
	FVideoEncoderConfig VideoConfig;

	FParse::Value(FCommandLine::Get(), TEXT("GameplayMediaEncoder.ResY="), VideoConfig.Height);
	if (VideoConfig.Height == 0 || VideoConfig.Height == 720)
	{
		VideoConfig.Width = 1280;
		VideoConfig.Height = 720;
	}
	else if (VideoConfig.Height == 1080)
	{
		VideoConfig.Width = 1920;
		VideoConfig.Height = 1080;
	}
	else
	{
		UE_LOG(GameplayMediaEncoder, Fatal, TEXT("GameplayMediaEncoder.ResY can only have a value of 720 or 1080"));
		return false;
	}

	// Specifying 0 will completely disable frame skipping (therefore encoding as many frames as possible)
	FParse::Value(FCommandLine::Get(), TEXT("GameplayMediaEncoder.FPS="), VideoConfig.Framerate);
	if (VideoConfig.Framerate == 0)
	{
		// Note : When disabling frame skipping, we lie to the encoder when initializing.
		// We still specify a framerate, but then feed frames without skipping
		VideoConfig.Framerate = HardcodedVideoFPS;
		bDoFrameSkipping = false;
		UE_LOG(GameplayMediaEncoder, Log, TEXT("Uncapping FPS"));
	}
	else
	{
		VideoConfig.Framerate = FMath::Clamp(VideoConfig.Framerate, (uint32)MinVideoFPS, (uint32)MaxVideoFPS);
		bDoFrameSkipping = true;
		UE_LOG(GameplayMediaEncoder, Log, TEXT("Capping FPS %u"), VideoConfig.Framerate);
	}

	VideoConfig.Bitrate = HardcodedVideoBitrate;
	FParse::Value(FCommandLine::Get(), TEXT("GameplayMediaEncoder.Bitrate="), VideoConfig.Bitrate);
	VideoConfig.Bitrate = FMath::Clamp(VideoConfig.Bitrate, (uint32)MinVideoBitrate, (uint32)MaxVideoBitrate);

	UE_LOG(GameplayMediaEncoder, Log, TEXT("Using a config of {Width=%u, Height=%u, Framerate=%u, Bitrate=%u}"), VideoConfig.Width, VideoConfig.Height, VideoConfig.Framerate, VideoConfig.Bitrate);

	//
	// Try to initialize the best encoder for the current platform
	//
#if PLATFORM_WINDOWS
	VideoEncoder.Reset(new FNvVideoEncoder(OnMediaSampleReadyCallback, EncoderDevice));
	if (!VideoEncoder->Initialize(VideoConfig))
	{
		VideoEncoder.Reset(new FAmdAmfVideoEncoder(OnMediaSampleReadyCallback));
		if (!VideoEncoder->Initialize(VideoConfig))
		{
			VideoEncoder.Reset(new FWmfVideoEncoder(OnMediaSampleReadyCallback));
			if (!VideoEncoder->Initialize(VideoConfig))
			{
				Stop();
				return false;
			}
		}
	}
#elif PLATFORM_XBOXONE
	VideoEncoder.Reset(new FXboxOneVideoEncoder(OnMediaSampleReadyCallback));
	verify(VideoEncoder->Initialize(VideoConfig));
#endif

	StartTime = 0;

	MemoryCheckpoint("Video encoder initialized");

	return true;
}

bool FGameplayMediaEncoder::Start()
{
	// Initialize if not initialized
	if (!VideoEncoder)
	{
		if (!Initialize())
			return false;
	}

	if (StartTime != 0)
	{
		UE_LOG(GameplayMediaEncoder, Log, TEXT("Already running"));
		return true;
	}

	if (!VideoEncoder)
	{
		UE_LOG(GameplayMediaEncoder, Log, TEXT("Not initialized yet , so also performing a Intialize()"));
		if (!Initialize())
		{
			return false;
		}
	}

	StartTime = FTimespan::FromSeconds(FPlatformTime::Seconds());
	LastAudioInputTimestamp = LastVideoInputTimestamp = GetMediaTimestamp();
	NumCapturedFrames = 0;

	//
	// subscribe to engine delegates for audio output and back buffer
	//

	FAudioDevice* AudioDevice = GEngine->GetMainAudioDevice();
	if (AudioDevice)
	{
		AudioDevice->RegisterSubmixBufferListener(this);
	}

	VideoEncoder->Start();
	FSlateRenderer::FOnBackBufferReadyToPresent OnBackBufferReadyDelegate;
	OnBackBufferReadyDelegate.AddRaw(this, &FGameplayMediaEncoder::OnBackBufferReady);
	FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent() = OnBackBufferReadyDelegate;

	return true;
}

void FGameplayMediaEncoder::Stop()
{
	check(IsInGameThread());

	if (StartTime == 0)
	{
		UE_LOG(GameplayMediaEncoder, Log, TEXT("Not running"));
		return;
	}

	if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
	{
		FAudioDevice* AudioDevice = GameEngine->GetMainAudioDevice();
		if (AudioDevice)
		{
			AudioDevice->UnregisterSubmixBufferListener(this);
		}

		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
		}
	}

	VideoEncoder->Stop();
	StartTime = 0;
}

void FGameplayMediaEncoder::Shutdown()
{
	if (StartTime != 0)
	{
		UE_LOG(GameplayMediaEncoder, Log, TEXT("Currently running, so also performing a Stop()"));
		Stop();
	}

	{
		FScopeLock Lock(&AudioProcessingCS);
		AudioEncoder.Reset();
	}
	{
		FScopeLock Lock(&VideoProcessingCS);
		VideoEncoder.Reset();
	}
}

bool FGameplayMediaEncoder::OnMediaSampleReady(const FGameplayMediaEncoderSample& Sample)
{
	FScopeLock ProcessMediaSamplesLock(&ProcessMediaSamplesCS);
	if (bProcessMediaSamples)
	{
		FScopeLock Lock(&ListenersCS);

		for (auto&& Listener : Listeners)
		{
			Listener->OnMediaSample(Sample);
		}
	}

	return true;
}

FTimespan FGameplayMediaEncoder::GetMediaTimestamp() const
{
	return FTimespan::FromSeconds(FPlatformTime::Seconds()) - StartTime;
}

void FGameplayMediaEncoder::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
{
	CSV_SCOPED_TIMING_STAT(GameplayMediaEncoder, OnNewSubmixBuffer);
	if (SampleRate != HardcodedAudioSamplerate)
	{
		// Only report the problem once
		if (!bAudioFormatChecked)
		{
			bAudioFormatChecked = true;
			UE_LOG(GameplayMediaEncoder, Error, TEXT("Audio SampleRate needs to be %d HZ, current value is %d. VideoRecordingSystem won't record audio"), HardcodedAudioSamplerate, SampleRate);
		}
		return;
	}

	ProcessAudioFrame(AudioData, NumSamples, NumChannels, SampleRate);
}

void FGameplayMediaEncoder::OnBackBufferReady(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
{
	CSV_SCOPED_TIMING_STAT(GameplayMediaEncoder, OnBackBufferReady);
	check(IsInRenderingThread());
	ProcessVideoFrame(BackBuffer);
}

bool FGameplayMediaEncoder::ProcessAudioFrame(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate)
{
	Audio::AlignedFloatBuffer InData;
	InData.Append(AudioData, NumSamples);
	Audio::TSampleBuffer<float> FloatBuffer(InData, NumChannels, SampleRate);

	// Mix to stereo if required, since PixelStreaming only accept stereo at the moment
	if (FloatBuffer.GetNumChannels() != HardcodedAudioNumChannels)
	{
		FloatBuffer.MixBufferToChannels(HardcodedAudioNumChannels);
	}

	PCM16 = FloatBuffer;

	{
		FScopeLock Lock(&AudioProcessingCS);

		FTimespan Timestamp = GetMediaTimestamp();
		FTimespan Duration = Timestamp - LastAudioInputTimestamp;
		LastAudioInputTimestamp = Timestamp;
		AudioEncoder->Process(reinterpret_cast<const int8*>(PCM16.GetData()), PCM16.GetNumSamples() * sizeof(*PCM16.GetData()), Timestamp, Duration);
	}

	return true;
}

bool FGameplayMediaEncoder::ProcessVideoFrame(const FTexture2DRHIRef& BackBuffer)
{
	FScopeLock Lock(&VideoProcessingCS);

	FTimespan Now = GetMediaTimestamp();

	if (bDoFrameSkipping)
	{
		uint64 NumExpectedFrames = static_cast<uint64>(Now.GetTotalSeconds() * VideoEncoder->GetConfig().Framerate);
		UE_LOG(GameplayMediaEncoder, VeryVerbose, TEXT("time %.3f: captured %d, expected %d"), Now.GetTotalSeconds(), NumCapturedFrames + 1, NumExpectedFrames);
		if (NumCapturedFrames + 1 > NumExpectedFrames)
		{
			UE_LOG(GameplayMediaEncoder, Verbose, TEXT("Framerate control dropped captured frame"));
			return true;
		}
	}

	if (!ChangeVideoConfig())
	{
		return false;
	}

	FTimespan Duration = Now - LastVideoInputTimestamp;
	if (!VideoEncoder->Process(BackBuffer, Now, Duration))
	{
		return false;
	}

	LastVideoInputTimestamp = Now;
	NumCapturedFrames++;
	return true;
}

void FGameplayMediaEncoder::SetVideoBitrate(uint32 Bitrate)
{
	NewVideoBitrate = Bitrate;
	bChangeBitrate = true;
}

void FGameplayMediaEncoder::SetVideoFramerate(uint32 Framerate)
{
	NewVideoFramerate = FMath::Clamp(Framerate, MinVideoFPS, MaxVideoFPS);
	bChangeFramerate = true;
}

bool FGameplayMediaEncoder::ChangeVideoConfig()
{
	if (bChangeBitrate)
	{
		if (!VideoEncoder->SetBitrate(NewVideoBitrate))
		{
			return false;
		}
		bChangeBitrate = false;
	}

	if (bChangeFramerate)
	{
		UE_LOG(GameplayMediaEncoder, Verbose, TEXT("framerate -> %d"), NewVideoFramerate.Load());

		if (!VideoEncoder->SetFramerate(NewVideoFramerate))
		{
			return false;
		}
		bChangeFramerate = false;
		// reset framerate control
		FramerateMonitoringStart = GetMediaTimestamp();
		NumCapturedFrames = 0;
	}

	return true;
}

GAMEPLAYMEDIAENCODER_END

