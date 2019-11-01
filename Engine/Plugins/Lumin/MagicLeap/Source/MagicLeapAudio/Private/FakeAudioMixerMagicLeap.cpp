// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FakeAudioMixerMagicLeap.h"
#include "Modules/ModuleManager.h"
#include "AudioMixer.h"
#include "AudioMixerDevice.h"
#include "CoreGlobals.h"
#include "VorbisAudioInfo.h"
#include "ADPCMAudioInfo.h"
#include "HeadMountedDisplayFunctionLibrary.h"

namespace Audio
{
	// ML1 currently only has stereo speakers and stereo aux support.
	const uint32 FakeDefaultNumChannels = 2;
	// TODO: @Epic check the value to be used. Setting default for now.
	const uint32 FakeDefaultSamplesPerSecond = 48000;  // presumed 48KHz and 16 bits for the sample

	FFakeAudioMixerMagicLeap::FFakeAudioMixerMagicLeap()
	: bInitialized(false)
	, FakeCallback(this)
	{}

	FFakeAudioMixerMagicLeap::~FFakeAudioMixerMagicLeap()
	{
		TeardownHardware();
	}

	bool FFakeAudioMixerMagicLeap::InitializeHardware()
	{
		bInitialized = true;
		return true;
	}

	bool FFakeAudioMixerMagicLeap::TeardownHardware()
	{
		bInitialized = false;
		return true;
	}

	bool FFakeAudioMixerMagicLeap::IsInitialized() const
	{
		return bInitialized;
	}

	bool FFakeAudioMixerMagicLeap::GetNumOutputDevices(uint32& OutNumOutputDevices)
	{
		// ML1 will always have just one device.
		OutNumOutputDevices = 1;
		return true;
	}

	bool FFakeAudioMixerMagicLeap::GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo)
	{
		OutInfo.Name = TEXT("Magic Leap Audio Device");
		OutInfo.DeviceId = 0;
		OutInfo.bIsSystemDefault = true;
		OutInfo.NumChannels = FakeDefaultNumChannels;
		OutInfo.OutputChannelArray.SetNum(2);
		OutInfo.OutputChannelArray[0] = EAudioMixerChannel::FrontLeft;
		OutInfo.OutputChannelArray[1] = EAudioMixerChannel::FrontRight;
		OutInfo.SampleRate = FakeDefaultSamplesPerSecond;
		OutInfo.Format = EAudioMixerStreamDataFormat::Float;

		PlatformDeviceInfo = OutInfo;

		return true;
	}

	bool FFakeAudioMixerMagicLeap::GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const
	{
		OutDefaultDeviceIndex = 0;
		return true;
	}

	bool FFakeAudioMixerMagicLeap::OpenAudioStream(const FAudioMixerOpenStreamParams& Params)
	{
		if (!bInitialized || AudioStreamInfo.StreamState != EAudioOutputStreamState::Closed)
		{
			return false;
		}

		if (!GetOutputDeviceInfo(AudioStreamInfo.OutputDeviceIndex, AudioStreamInfo.DeviceInfo))
		{
			return false;
		}

		OpenStreamParams = Params;
		OpenStreamParams.NumFrames = 3200;

		AudioStreamInfo.Reset();
		AudioStreamInfo.OutputDeviceIndex = 0;
		AudioStreamInfo.NumOutputFrames = OpenStreamParams.NumFrames;
		AudioStreamInfo.NumBuffers = OpenStreamParams.NumBuffers;
		AudioStreamInfo.AudioMixer = OpenStreamParams.AudioMixer;
		GetOutputDeviceInfo(0, AudioStreamInfo.DeviceInfo);

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Open;

		// Always use the fake callback with FFakeAudioMixerMagicLeap
		FakeCallback.SetShouldUseCallback(true);

		return true;
	}

	bool FFakeAudioMixerMagicLeap::CloseAudioStream()
	{
		if (!bInitialized || (AudioStreamInfo.StreamState != EAudioOutputStreamState::Open && AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopped))
		{
			return false;
		}

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Closed;
		return true;
	}

	bool FFakeAudioMixerMagicLeap::StartAudioStream()
	{
		BeginGeneratingAudio();
		return true;
	}

	bool FFakeAudioMixerMagicLeap::StopAudioStream()
	{
		if (!bInitialized || AudioStreamInfo.StreamState != EAudioOutputStreamState::Running)
		{
			return false;
		}

		StopGeneratingAudio();

		check(AudioStreamInfo.StreamState == EAudioOutputStreamState::Stopped);
		return true;
	}

	FAudioPlatformDeviceInfo FFakeAudioMixerMagicLeap::GetPlatformDeviceInfo() const
	{
		return PlatformDeviceInfo;
	}

	FAudioPlatformSettings FFakeAudioMixerMagicLeap::GetPlatformSettings() const
	{
		FAudioPlatformSettings PlatformSettings;
		PlatformSettings.CallbackBufferFrameSize = 3200;
		PlatformSettings.MaxChannels = 0;
		PlatformSettings.NumBuffers = 2;
		PlatformSettings.SampleRate = 4800;
		return PlatformSettings;
	}

	void FFakeAudioMixerMagicLeap::SubmitBuffer(const uint8* Buffer)
	{}

	FName FFakeAudioMixerMagicLeap::GetRuntimeFormat(USoundWave* InSoundWave)
	{
#if WITH_OGGVORBIS
		static FName NAME_OGG(TEXT("OGG"));
		if (InSoundWave->HasCompressedData(NAME_OGG))
		{
			return NAME_OGG;
		}
#endif

		static FName NAME_ADPCM(TEXT("ADPCM"));

		return NAME_ADPCM;
	}

	bool FFakeAudioMixerMagicLeap::HasCompressedAudioInfoClass(USoundWave* InSoundWave)
	{
		return true;
	}

	ICompressedAudioInfo* FFakeAudioMixerMagicLeap::CreateCompressedAudioInfo(USoundWave* InSoundWave)
	{
#if WITH_OGGVORBIS
		static FName NAME_OGG(TEXT("OGG"));
		if (InSoundWave->HasCompressedData(NAME_OGG))
		{
			return new FVorbisAudioInfo();
		}
#endif
		static FName NAME_ADPCM(TEXT("ADPCM"));
		return new FADPCMAudioInfo();
	}

	FString FFakeAudioMixerMagicLeap::GetDefaultDeviceName()
	{
		return FString(TEXT("MLAudio"));
	}

	int32 FFakeAudioMixerMagicLeap::GetNumFrames(const int32 InNumReqestedFrames)
	{
		return 3200;
	}
}
