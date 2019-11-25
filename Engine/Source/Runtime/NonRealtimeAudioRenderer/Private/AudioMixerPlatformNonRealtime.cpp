// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioMixerPlatformNonRealtime.h"
#include "AudioMixer.h"
#include "AudioMixerDevice.h"
#include "AudioPluginUtilities.h"
#include "HAL/PlatformAffinity.h"
#include "Misc/App.h"

#ifndef WITH_XMA2
#define WITH_XMA2 0
#endif

#if WITH_XMA2
#include "XMAAudioInfo.h"
#endif  //#if WITH_XMA2
#include "OpusAudioInfo.h"
#include "VorbisAudioInfo.h"
#include "Interfaces/IAudioFormat.h"

#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "AudioCompressionSettingsUtils.h"

static int32 DefaultRenderFrameSizeCvar = 256;
FAutoConsoleVariableRef CVarDefaultRenderFrameSize(
	TEXT("au.nrt.RenderFrameSize"),
	DefaultRenderFrameSizeCvar,
	TEXT("Selects the number of frames to render in a single callback .\n")
	TEXT("n: Number of frames to render."),
	ECVF_Default);

static int32 RenderEveryTickCvar = 1;
FAutoConsoleVariableRef CVarRenderEveryTick(
	TEXT("au.nrt.RenderEveryTick"),
	RenderEveryTickCvar,
	TEXT("When set to 1, calls the RenderAudio call every tick.\n")
	TEXT("n: Number of frames to render."),
	ECVF_Default);

namespace Audio
{
	FMixerPlatformNonRealtime::FMixerPlatformNonRealtime(float InSampleRate /*= 48000*/, float InNumChannels /*= 2*/, float ExpectedCallbackDuration /*= 0.033f*/)
		: SampleRate(InSampleRate)
		, NumChannels(InNumChannels)
		, TotalDurationRendered(0.0)
		, TickDelta(0.0)
		, bIsInitialized(false)
		, bIsDeviceOpen(false)
	{
	}

	FMixerPlatformNonRealtime::~FMixerPlatformNonRealtime()
	{
	}

	void FMixerPlatformNonRealtime::RenderAudio(double NumSecondsToRender)
	{
		if (!bIsInitialized || !bIsDeviceOpen)
		{
			return;
		}

		const double TimePerCallback = ((double) AudioStreamInfo.NumOutputFrames) / AudioStreamInfo.DeviceInfo.SampleRate;
		double SecondsRendered = TotalDurationRendered;
		TotalDurationRendered += NumSecondsToRender;

		CurrentBufferWriteIndex = 0;
		CurrentBufferReadIndex = 0;


		while (SecondsRendered < TotalDurationRendered)
		{
			// RenderTimeAnalysis.Start();
			OutputBuffers[CurrentBufferWriteIndex].MixNextBuffer();
			// RenderTimeAnalysis.End();

			ReadNextBuffer();
			SecondsRendered += TimePerCallback;
		}

		CurrentBufferReadIndex = INDEX_NONE;
		CurrentBufferWriteIndex = INDEX_NONE;
	}

	void FMixerPlatformNonRealtime::OpenFileToWriteAudioTo(const FString& OutPath)
	{
		// Construct full path:
		FString AbsoluteFilePath;

		const bool bIsRelativePath = FPaths::IsRelative(OutPath);
		if (bIsRelativePath)
		{
			AbsoluteFilePath = FPaths::ProjectSavedDir() + OutPath;
			AbsoluteFilePath = FPaths::ConvertRelativePathToFull(AbsoluteFilePath);
		}
		else
		{
			AbsoluteFilePath = OutPath;
		}

		FSoundQualityInfo QualityInfo;
		QualityInfo.SampleRate = SampleRate;
		QualityInfo.NumChannels = NumChannels;
		QualityInfo.Quality = 100;

		// Gotcha for bouncing wav files: this has to be filled in.
		QualityInfo.Duration = 5.0f;
		QualityInfo.SampleDataSize = 5.0f * SampleRate * NumChannels * sizeof(int16);

		AudioFileWriter.Reset(new FAudioFileWriter(AbsoluteFilePath, QualityInfo));
	}

	void FMixerPlatformNonRealtime::CloseFile()
	{
		AudioFileWriter.Reset();
	}

	bool FMixerPlatformNonRealtime::InitializeHardware()
	{
		if (bIsInitialized)
		{
			return false;
		}

#if WITH_XMA2
		//Initialize our XMA2 decoder context
		FXMAAudioInfo::Initialize();
#endif //#if WITH_XMA2

		// Load ogg and vorbis dlls if they haven't been loaded yet
		LoadVorbisLibraries();

		bIsInitialized = true;

		TickDelta = FApp::GetDeltaTime();

		return true;
	}

	bool FMixerPlatformNonRealtime::TeardownHardware()
	{
		if (!bIsInitialized)
		{
			return false;
		}

#if WITH_XMA2
		FXMAAudioInfo::Shutdown();
#endif

		bIsInitialized = false;

		return true;
	}

	bool FMixerPlatformNonRealtime::IsInitialized() const
	{
		return bIsInitialized;
	}

	bool FMixerPlatformNonRealtime::GetNumOutputDevices(uint32& OutNumOutputDevices)
	{
		if (!bIsInitialized)
		{
			return false;
		}
		
		OutNumOutputDevices = 1;

		return true;
	}

	bool FMixerPlatformNonRealtime::GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo)
	{
		if (!bIsInitialized)
		{
			return false;
		}

		OutInfo.bIsSystemDefault = true;
		OutInfo.SampleRate = SampleRate;
		OutInfo.DeviceId = 0;
		OutInfo.Format = EAudioMixerStreamDataFormat::Float;
		OutInfo.Name = TEXT("Non-realtime Renderer");
		OutInfo.NumChannels = NumChannels;

		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::FrontLeft);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::FrontRight);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::FrontCenter);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::LowFrequency);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::BackLeft);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::BackRight);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::SideLeft);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::SideRight);

		return true;
	}

	bool FMixerPlatformNonRealtime::GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const
	{
		OutDefaultDeviceIndex = 0;
		return true;
	}

	bool FMixerPlatformNonRealtime::OpenAudioStream(const FAudioMixerOpenStreamParams& Params)
	{
		if (!bIsInitialized)
		{
			return false;
		}

		if (bIsDeviceOpen)
		{
			return false;
		}

		OpenStreamParams = Params;
		//OpenStreamParams.NumFrames = DefaultRenderFrameSizeCvar;
		

		AudioStreamInfo.Reset();

		AudioStreamInfo.OutputDeviceIndex = OpenStreamParams.OutputDeviceIndex;
		AudioStreamInfo.NumOutputFrames = OpenStreamParams.NumFrames;
		AudioStreamInfo.NumBuffers = OpenStreamParams.NumBuffers;
		AudioStreamInfo.AudioMixer = OpenStreamParams.AudioMixer;


		if (!GetOutputDeviceInfo(AudioStreamInfo.OutputDeviceIndex, AudioStreamInfo.DeviceInfo))
		{
			return false;
		}

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Open;
		bIsDeviceOpen = true;

		return true;
	}

	FAudioPlatformDeviceInfo FMixerPlatformNonRealtime::GetPlatformDeviceInfo() const
	{
		return AudioStreamInfo.DeviceInfo;
	}

	bool FMixerPlatformNonRealtime::CloseAudioStream()
	{
		if (!bIsInitialized || AudioStreamInfo.StreamState == EAudioOutputStreamState::Closed)
		{
			return false;
		}

		if (bIsDeviceOpen && !StopAudioStream())
		{
			return false;
		}

		bIsDeviceOpen = false;

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Closed;

		return true;
	}

	bool FMixerPlatformNonRealtime::StartAudioStream()
	{
		// Start generating audio with our output source voice
		BeginGeneratingAudio();

		return true;
	}

	bool FMixerPlatformNonRealtime::StopAudioStream()
	{
		return true;
	}

	bool FMixerPlatformNonRealtime::CheckAudioDeviceChange()
	{
		return false;
	}

	bool FMixerPlatformNonRealtime::MoveAudioStreamToNewAudioDevice(const FString& InNewDeviceId)
	{
		return true;
	}

	void FMixerPlatformNonRealtime::ResumePlaybackOnNewDevice()
	{
		SubmitBuffer(OutputBuffers[CurrentBufferReadIndex].GetBufferData());
		check(OpenStreamParams.NumFrames * AudioStreamInfo.DeviceInfo.NumChannels == OutputBuffers[CurrentBufferReadIndex].GetBuffer().Num());

		AudioRenderEvent->Trigger();
	}

	void FMixerPlatformNonRealtime::SubmitBuffer(const uint8* Buffer)
	{
		// Do actual buffer submissions here.
		if (AudioFileWriter.IsValid())
		{
			AudioFileWriter->PushAudio((const float*) Buffer, NumChannels * AudioStreamInfo.NumOutputFrames);
		}
	}

	FName FMixerPlatformNonRealtime::GetRuntimeFormat(USoundWave* InSoundWave)
	{
		//TODO: Set this up to propogate to 

		static FName NAME_OGG(TEXT("OGG"));
		static FName NAME_OPUS(TEXT("OPUS"));
		static FName NAME_XMA(TEXT("XMA"));

#if WITH_XMA2 && USE_XMA2_FOR_STREAMING
		if (InSoundWave->IsStreaming() && InSoundWave->NumChannels <= 2)
		{
			return NAME_XMA;
		}
#endif

		if (InSoundWave->IsStreaming())
		{
#if USE_VORBIS_FOR_STREAMING
			return NAME_OGG;
#else
			return NAME_OPUS;
#endif
		}

#if WITH_XMA2
		if (InSoundWave->NumChannels <= 2)
		{
			return NAME_XMA;
		}
#endif //#if WITH_XMA2

		return NAME_OGG;
	}

	bool FMixerPlatformNonRealtime::HasCompressedAudioInfoClass(USoundWave* InSoundWave)
	{
#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
		return true;
#else
		return false;
#endif // PLATFORM_WINDOWS || PLATFORM_XBOXONE
	}

	ICompressedAudioInfo* FMixerPlatformNonRealtime::CreateCompressedAudioInfo(USoundWave* InSoundWave)
	{
		// TODO: Currently this is a copy paste of the XAudio2 platform interface. Ultimately, this function needs to propogate to the current platform's correct CrateCompressedAudioInfo call.
#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
		check(InSoundWave);

#if WITH_XMA2 && USE_XMA2_FOR_STREAMING
		if (InSoundWave->IsStreaming() && InSoundWave->NumChannels <= 2 )
		{
			return new FXMAAudioInfo();
		}
#endif

		if (InSoundWave->IsStreaming())
		{
#if USE_VORBIS_FOR_STREAMING
			return new FVorbisAudioInfo();
#else
			return new FOpusAudioInfo();
#endif
		}

		static const FName NAME_OGG(TEXT("OGG"));
		if (FPlatformProperties::RequiresCookedData() ? InSoundWave->HasCompressedData(NAME_OGG) : (InSoundWave->GetCompressedData(NAME_OGG) != nullptr))
		{
			return new FVorbisAudioInfo();
		}

#if WITH_XMA2
		static const FName NAME_XMA(TEXT("XMA"));
		if (FPlatformProperties::RequiresCookedData() ? InSoundWave->HasCompressedData(NAME_XMA) : (InSoundWave->GetCompressedData(NAME_XMA) != nullptr))
		{
			return new FXMAAudioInfo();
		}
#endif // WITH_XMA2
#endif // PLATFORM_WINDOWS || WITH_XMA2

		return nullptr;
	}

	FString FMixerPlatformNonRealtime::GetDefaultDeviceName()
	{
		//GConfig->GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("AudioDevice"), WindowsAudioDeviceName, GEngineIni);
		return FString();
	}

	FAudioPlatformSettings FMixerPlatformNonRealtime::GetPlatformSettings() const
	{
		return FAudioPlatformSettings::GetPlatformSettings(FPlatformProperties::GetRuntimeSettingsClassName());
	}

	void FMixerPlatformNonRealtime::OnHardwareUpdate()
	{
		if (RenderEveryTickCvar)
		{
			RenderAudio(TickDelta);
		}
	}

	bool FMixerPlatformNonRealtime::IsNonRealtime() const
	{
		return true;
	}

	uint32 FMixerPlatformNonRealtime::RunInternal()
	{
		// Not used.
		return 0;
	}

	bool FMixerPlatformNonRealtime::DisablePCMAudioCaching() const
	{
		return true;
	}
}