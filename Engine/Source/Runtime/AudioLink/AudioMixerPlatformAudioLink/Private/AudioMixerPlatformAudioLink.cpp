// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerPlatformAudioLink.h"
#include "Modules/ModuleManager.h"
#include "AudioMixer.h"
#include "AudioMixerDevice.h"
#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"

#include "Misc/CoreMisc.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"

namespace AudioMixerPlatformAudioLinkPrivate
{	
	using namespace Audio;
	static const FAudioPlatformDeviceInfo& GetPlatformInfo()
	{
		static const FAudioPlatformDeviceInfo DeviceInfo = []()
		{
			// For now just hard-code a virtual audio platform, but this will ultimately match an external endpoint via audio link.
			FAudioPlatformDeviceInfo Info;
			Info.Name = TEXT("AudioLink Virtual Platform");
			Info.DeviceId = Info.Name;
			Info.SampleRate	= 48000;
			Info.NumChannels = 8;
			Info.bIsSystemDefault = true;
			Info.Format = EAudioMixerStreamDataFormat::Float;
			Info.OutputChannelArray.Add(EAudioMixerChannel::FrontLeft);
			Info.OutputChannelArray.Add(EAudioMixerChannel::FrontRight);
			Info.OutputChannelArray.Add(EAudioMixerChannel::FrontCenter);
			Info.OutputChannelArray.Add(EAudioMixerChannel::LowFrequency);
			Info.OutputChannelArray.Add(EAudioMixerChannel::SideLeft);
			Info.OutputChannelArray.Add(EAudioMixerChannel::SideRight);
			Info.OutputChannelArray.Add(EAudioMixerChannel::BackLeft);
			Info.OutputChannelArray.Add(EAudioMixerChannel::BackRight);

			return Info;
		}();
		return DeviceInfo;
	}
}

namespace Audio
{		
	FAudioMixerPlatformAudioLink::FAudioMixerPlatformAudioLink()
	{}

	bool FAudioMixerPlatformAudioLink::InitializeHardware()
	{
		if (IAudioMixer::ShouldRecycleThreads())
		{
			// Pre-create the null render device thread on XAudio2, so we can simple wake it up when we need it.
			// Give it nothing to do, with a slow tick as the default, but ask it to wait for a signal to wake up.
			CreateNullDeviceThread([] {}, 1.0f, true);
		}

		bInitialized = true;
		return true;
	}

	bool FAudioMixerPlatformAudioLink::TeardownHardware()
	{		
		StopAudioStream();
		CloseAudioStream();
		return true;
	}

	bool FAudioMixerPlatformAudioLink::IsInitialized() const
	{
		return bInitialized;
	}

	bool FAudioMixerPlatformAudioLink::GetNumOutputDevices(uint32& OutNumOutputDevices)
	{
		OutNumOutputDevices = 1;
		return true;
	}
	
	bool FAudioMixerPlatformAudioLink::GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo)
	{	
		using namespace AudioMixerPlatformAudioLinkPrivate;
		
		OutInfo = GetPlatformInfo();
		return true;
	}

	bool FAudioMixerPlatformAudioLink::GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const
	{
		// It's not possible to know what index the default audio device is.
		OutDefaultDeviceIndex = AUDIO_MIXER_DEFAULT_DEVICE_INDEX;
		return true;
	}

	bool FAudioMixerPlatformAudioLink::OpenAudioStream(const FAudioMixerOpenStreamParams& Params)
	{
		if (!bInitialized || AudioStreamInfo.StreamState != EAudioOutputStreamState::Closed)
		{
			return false;
		}
			
		AudioStreamInfo.Reset();
		if(!GetOutputDeviceInfo(Params.OutputDeviceIndex, AudioStreamInfo.DeviceInfo))
		{
			return false;
		}

		OpenStreamParams = Params;

		AudioStreamInfo.AudioMixer = Params.AudioMixer;
		AudioStreamInfo.NumBuffers = Params.NumBuffers;
		AudioStreamInfo.NumOutputFrames = Params.NumFrames;
		AudioStreamInfo.StreamState = EAudioOutputStreamState::Open;

		return true;
	}

	bool FAudioMixerPlatformAudioLink::CloseAudioStream()
	{
		if (AudioStreamInfo.StreamState == EAudioOutputStreamState::Closed)
		{
			return false;
		}

		if (!StopAudioStream())
		{
			return false;
		}

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Closed;
		return true;
	}

	bool FAudioMixerPlatformAudioLink::StartAudioStream()
	{
		if (!bInitialized || (AudioStreamInfo.StreamState != EAudioOutputStreamState::Open && AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopped))
		{
			return false;
		}

		// Start generating audio
		BeginGeneratingAudio();

		StartRunningNullDevice();

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Running;

		return true;
	}

	bool FAudioMixerPlatformAudioLink::StopAudioStream()
	{
		if (AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopped && AudioStreamInfo.StreamState != EAudioOutputStreamState::Closed)
		{
			if(bIsUsingNullDevice)
			{
				StopRunningNullDevice();
			}
			
			if (AudioStreamInfo.StreamState == EAudioOutputStreamState::Running)
			{
				StopGeneratingAudio();
			}

			check(AudioStreamInfo.StreamState == EAudioOutputStreamState::Stopped);
		}

		return true;
	}

	FAudioPlatformDeviceInfo FAudioMixerPlatformAudioLink::GetPlatformDeviceInfo() const
	{
		return AudioStreamInfo.DeviceInfo;
	}

	FString FAudioMixerPlatformAudioLink::GetDefaultDeviceName()
	{
		using namespace AudioMixerPlatformAudioLinkPrivate;
		return GetPlatformInfo().Name;
	}
	
	FAudioPlatformSettings FAudioMixerPlatformAudioLink::GetPlatformSettings() const
	{
#if WITH_ENGINE
		return FAudioPlatformSettings::GetPlatformSettings(FPlatformProperties::GetRuntimeSettingsClassName());
#else
		return FAudioPlatformSettings();
#endif // WITH_ENGINE
	}
}
