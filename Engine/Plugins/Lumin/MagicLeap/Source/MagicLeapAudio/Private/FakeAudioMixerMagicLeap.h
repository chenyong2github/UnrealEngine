// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"
#include "FakeDeviceCallbackRunnable.h"

namespace Audio
{
	// For an audio mixer module to be used in PIE, 
	// it needs to have been successfully created on editor launch, even though a new instance is created for PIE.
	// If MLRemote is not already running when the editor is launched, FMixerPlatformMagicLeap will cause the engine to crash.
	// Thus, create the actual ML audio mixer only if an ML HMD is connected i.e. MLRemote is running.
	// Otherwise, use a fake audio mixer so this module gets registered on editor startup, and works without crashing.
	class FFakeAudioMixerMagicLeap : public IAudioMixerPlatformInterface
	{

	public:

		FFakeAudioMixerMagicLeap();
		~FFakeAudioMixerMagicLeap();

		//~ Begin IAudioMixerPlatformInterface
		virtual EAudioMixerPlatformApi::Type GetPlatformApi() const override { return EAudioMixerPlatformApi::Null; }
		virtual bool InitializeHardware() override;
		virtual bool TeardownHardware() override;
		virtual bool IsInitialized() const override;
		virtual bool GetNumOutputDevices(uint32& OutNumOutputDevices) override;
		virtual bool GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo) override;
		virtual bool GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const override;
		virtual bool OpenAudioStream(const FAudioMixerOpenStreamParams& Params) override;
		virtual bool CloseAudioStream() override;
		virtual bool StartAudioStream() override;
		virtual bool StopAudioStream() override;
		virtual FAudioPlatformDeviceInfo GetPlatformDeviceInfo() const override;
		virtual void SubmitBuffer(const uint8* Buffer) override;
		virtual FName GetRuntimeFormat(USoundWave* InSoundWave) override;
		virtual int32 GetNumFrames(const int32 InNumReqestedFrames) override;
		virtual bool HasCompressedAudioInfoClass(USoundWave* InSoundWave) override;
		virtual ICompressedAudioInfo* CreateCompressedAudioInfo(USoundWave* InSoundWave) override;
		virtual FString GetDefaultDeviceName() override;
		virtual FAudioPlatformSettings GetPlatformSettings() const override;

		//~ End IAudioMixerPlatformInterface

	private:
		FAudioPlatformDeviceInfo PlatformDeviceInfo;
		bool bInitialized;
		FFakeDeviceCallbackRunnable FakeCallback;
	};
}
