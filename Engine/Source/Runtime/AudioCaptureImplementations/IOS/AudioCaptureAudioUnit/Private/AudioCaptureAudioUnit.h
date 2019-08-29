// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioCaptureCore.h"
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>

namespace Audio
{
	class FAudioCaptureAudioUnitStream : public IAudioCaptureStream
	{
	public:
		FAudioCaptureAudioUnitStream();

		virtual bool GetCaptureDeviceInfo(FCaptureDeviceInfo& OutInfo, int32 DeviceIndex) override;
		virtual bool OpenCaptureStream(const FAudioCaptureDeviceParams& InParams, FOnCaptureFunction InOnCapture, uint32 NumFramesDesired) override;
		virtual bool CloseStream() override;
		virtual bool StartStream() override;
		virtual bool StopStream() override;
		virtual bool AbortStream() override;
		virtual bool GetStreamTime(double& OutStreamTime) override;
		virtual int32 GetSampleRate() const override { return SampleRate; }
		virtual bool IsStreamOpen() const override;
		virtual bool IsCapturing() const override;
		virtual void OnAudioCapture(void* InBuffer, uint32 InBufferFrames, double StreamTime, bool bOverflow) override;
		virtual bool GetInputDevicesAvailable(TArray<FCaptureDeviceInfo>& OutDevices) override;

		AudioComponentInstance AudioUnit;

	private:
		FOnCaptureFunction OnCapture;
		int32 NumChannels;
		int32 SampleRate;

		OSStatus AudioUnitStatus;
	};
}