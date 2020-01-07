// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioCaptureRtAudio.h"


Audio::FAudioCaptureRtAudioStream::FAudioCaptureRtAudioStream()
	: NumChannels(0)
	, SampleRate(0)
{
}

#if WITH_RTAUDIO
static int32 OnAudioCaptureCallback(void *OutBuffer, void* InBuffer, uint32 InBufferFrames, double StreamTime, RtAudioStreamStatus AudioStreamStatus, void* InUserData)
{
	Audio::FAudioCaptureRtAudioStream* AudioCapture = (Audio::FAudioCaptureRtAudioStream*) InUserData;

	AudioCapture->OnAudioCapture(InBuffer, InBufferFrames, StreamTime, AudioStreamStatus == RTAUDIO_INPUT_OVERFLOW);
	return 0;
}
#endif // WITH_RTAUDIO


bool Audio::FAudioCaptureRtAudioStream::GetCaptureDeviceInfo(FCaptureDeviceInfo& OutInfo, int32 DeviceIndex)
{
#if WITH_RTAUDIO
	uint32 InputDeviceId = CaptureDevice.getDefaultInputDevice();
	if (DeviceIndex != DefaultDeviceIndex)
	{
		InputDeviceId = DeviceIndex;
	}

	RtAudio::DeviceInfo DeviceInfo = CaptureDevice.getDeviceInfo(InputDeviceId);

	const char* NameStr = DeviceInfo.name.c_str();

	OutInfo.DeviceName = FString(ANSI_TO_TCHAR(NameStr));
	OutInfo.InputChannels = DeviceInfo.inputChannels;
	OutInfo.PreferredSampleRate = DeviceInfo.preferredSampleRate;

	return true;
#else // ^^^ WITH_RTAUDIO vvv !WITH_RTAUDIO
	return false;
#endif // !WITH_RTAUDIO
}

bool Audio::FAudioCaptureRtAudioStream::OpenCaptureStream(const FAudioCaptureDeviceParams& InParams, FOnCaptureFunction InOnCapture, uint32 NumFramesDesired)
{
#if WITH_RTAUDIO
	uint32 InputDeviceId = CaptureDevice.getDefaultInputDevice();
	if (InParams.DeviceIndex != DefaultDeviceIndex)
	{
		InputDeviceId = InParams.DeviceIndex - 1;
	}

	RtAudio::DeviceInfo DeviceInfo = CaptureDevice.getDeviceInfo(InputDeviceId);

	RtAudio::StreamParameters RtAudioStreamParams;
	RtAudioStreamParams.deviceId = InputDeviceId;
	RtAudioStreamParams.firstChannel = 0;
	RtAudioStreamParams.nChannels = FMath::Min((int32)DeviceInfo.inputChannels, 2);

	if (CaptureDevice.isStreamOpen())
	{
		CaptureDevice.stopStream();
		CaptureDevice.closeStream();
	}

	uint32 NumFrames = NumFramesDesired;
	NumChannels = RtAudioStreamParams.nChannels;
	SampleRate = DeviceInfo.preferredSampleRate;
	OnCapture = MoveTemp(InOnCapture);

	// Open up new audio stream
	CaptureDevice.openStream(nullptr, &RtAudioStreamParams, RTAUDIO_FLOAT32, SampleRate, &NumFrames, &OnAudioCaptureCallback, this);

	if (!CaptureDevice.isStreamOpen())
	{
		return false;
	}

	SampleRate = CaptureDevice.getStreamSampleRate();

	return true;
#else // ^^^ WITH_RTAUDIO vvv !WITH_RTAUDIO
	return false;
#endif // !WITH_RTAUDIO
}

bool Audio::FAudioCaptureRtAudioStream::CloseStream()
{
#if WITH_RTAUDIO
	if (CaptureDevice.isStreamOpen())
	{
		CaptureDevice.closeStream();
	}
	return true;
#else // ^^^ WITH_RTAUDIO vvv !WITH_RTAUDIO
	return false;
#endif // !WITH_RTAUDIO
}

bool Audio::FAudioCaptureRtAudioStream::StartStream()
{
#if WITH_RTAUDIO
	CaptureDevice.startStream();
	return true;
#else // ^^^ WITH_RTAUDIO vvv !WITH_RTAUDIO
	return false;
#endif // !WITH_RTAUDIO
}

bool Audio::FAudioCaptureRtAudioStream::StopStream()
{
#if WITH_RTAUDIO
	if (CaptureDevice.isStreamOpen())
	{
		CaptureDevice.stopStream();
	}
	return true;
#else // ^^^ WITH_RTAUDIO vvv !WITH_RTAUDIO
	return false;
#endif // !WITH_RTAUDIO
}

bool Audio::FAudioCaptureRtAudioStream::AbortStream()
{
#if WITH_RTAUDIO
	if (CaptureDevice.isStreamOpen())
	{
		CaptureDevice.abortStream();
	}
	return true;
#else // ^^^ WITH_RTAUDIO vvv !WITH_RTAUDIO
	return false;
#endif // !WITH_RTAUDIO
}

bool Audio::FAudioCaptureRtAudioStream::GetStreamTime(double& OutStreamTime)
{
#if WITH_RTAUDIO
	OutStreamTime = CaptureDevice.getStreamTime();
	return true;
#else // ^^^ WITH_RTAUDIO vvv !WITH_RTAUDIO
	return false;
#endif // !WITH_RTAUDIO
}

bool Audio::FAudioCaptureRtAudioStream::IsStreamOpen() const
{
#if WITH_RTAUDIO
	return CaptureDevice.isStreamOpen();
#else // ^^^ WITH_RTAUDIO vvv !WITH_RTAUDIO
	return false;
#endif // !WITH_RTAUDIO
}

bool Audio::FAudioCaptureRtAudioStream::IsCapturing() const
{
#if WITH_RTAUDIO
	return CaptureDevice.isStreamRunning();
#else // ^^^ WITH_RTAUDIO vvv !WITH_RTAUDIO
	return false;
#endif // !WITH_RTAUDIO
}

void Audio::FAudioCaptureRtAudioStream::OnAudioCapture(void* InBuffer, uint32 InBufferFrames, double StreamTime, bool bOverflow)
{
#if WITH_RTAUDIO
	float* InBufferData = (float*)InBuffer;
	OnCapture(InBufferData, InBufferFrames, NumChannels, StreamTime, bOverflow);
#endif // WITH_RTAUDIO
}

bool Audio::FAudioCaptureRtAudioStream::GetInputDevicesAvailable(TArray<FCaptureDeviceInfo>& OutDevices)
{
#if WITH_RTAUDIO
	uint32 NumDevices = CaptureDevice.getDeviceCount();
	OutDevices.Reset();
	for (uint32 DeviceIndex = 1; DeviceIndex < NumDevices; DeviceIndex++)
	{
		FCaptureDeviceInfo& DeviceInfo = OutDevices.AddDefaulted_GetRef();
		GetCaptureDeviceInfo(DeviceInfo, DeviceIndex);
	}

	return true;
#else // ^^^ WITH_RTAUDIO vvv !WITH_RTAUDIO
	return false;
#endif // !WITH_RTAUDIO
}
