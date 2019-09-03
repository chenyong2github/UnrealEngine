// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioCaptureMagicLeap.h"
#include "Misc/CoreDelegates.h"

Audio::FAudioCaptureMagicLeapStream::FAudioCaptureMagicLeapStream()
	: bStreamStarted(false)
	, NumChannels(1)
	, SampleRate(16000)
#if WITH_MLSDK
	, InputDeviceHandle(ML_INVALID_HANDLE)
#endif // WITH_MLSDK
{
}

#if WITH_MLSDK
namespace
{
	static void OnAudioCaptureCallback(MLHandle Handle, void* CallbackContext)
	{
		(void)Handle;
		Audio::FAudioCaptureMagicLeapStream* AudioCapture = (Audio::FAudioCaptureMagicLeapStream*)CallbackContext;
		check(MLHandleIsValid(AudioCapture->InputDeviceHandle));
		MLAudioBuffer OutputBuffer;
		MLResult Result = MLAudioGetInputStreamBuffer(AudioCapture->InputDeviceHandle, &OutputBuffer);
		if (Result == MLResult_Ok)
		{
			AudioCapture->OnAudioCapture(OutputBuffer.ptr, OutputBuffer.size / sizeof(int16), 0.0, false);
			Result = MLAudioReleaseInputStreamBuffer(AudioCapture->InputDeviceHandle);
		}
	}
}
#endif // WITH_MLSDK

bool Audio::FAudioCaptureMagicLeapStream::GetCaptureDeviceInfo(FCaptureDeviceInfo& OutInfo, int32 DeviceIndex)
{
#if WITH_MLSDK
	// set up variables to be populated by ML Audio
	uint32 ChannelCount = NumChannels;
	MLAudioBufferFormat DefaultBufferFormat;
	uint32 BufferSize = 0;
	uint32 MinBufferSize = 0;
	uint32 UnsignedSampleRate = SampleRate;

	MLResult Result = MLAudioGetInputStreamDefaults(ChannelCount, UnsignedSampleRate, &DefaultBufferFormat, &BufferSize, &MinBufferSize);
	if (Result == MLResult_Ok)
	{
		OutInfo.DeviceName = TEXT("MLAudio Microphones");
		OutInfo.InputChannels = ChannelCount;
		OutInfo.PreferredSampleRate = SampleRate;
		return true;
	}
#endif // WITH_MLSDK

	return false;
}

bool Audio::FAudioCaptureMagicLeapStream::OpenCaptureStream(const FAudioCaptureDeviceParams& InParams, FOnCaptureFunction InOnCapture, uint32 NumFramesDesired)
{
#if WITH_MLSDK
	if (!InOnCapture)
	{
		return false;
	}

	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &Audio::FAudioCaptureMagicLeapStream::OnApplicationSuspend);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &Audio::FAudioCaptureMagicLeapStream::OnApplicationResume);

	// set up variables to be populated by ML Audio
	OnCapture = MoveTemp(InOnCapture);
	uint32 ChannelCount = NumChannels;
	MLAudioBufferFormat DefaultBufferFormat;
	uint32 RecommendedBufferSize = 0;
	uint32 MinBufferSize = 0;
	uint32 UnsignedSampleRate = SampleRate;
	uint32 BufferSize = NumFramesDesired * NumChannels * sizeof(int16);

	MLResult Result = MLAudioGetInputStreamDefaults(ChannelCount, UnsignedSampleRate, &DefaultBufferFormat, &RecommendedBufferSize, &MinBufferSize);
	if (Result != MLResult_Ok)
	{
		return false;
	}

	DefaultBufferFormat.bits_per_sample = 16;
	DefaultBufferFormat.sample_format = MLAudioSampleFormat_Int;
	DefaultBufferFormat.channel_count = ChannelCount;
	DefaultBufferFormat.samples_per_second = SampleRate;

	// Open up new audio stream
	Result = MLAudioCreateInputFromVoiceComm(&DefaultBufferFormat, BufferSize, &OnAudioCaptureCallback, this, &InputDeviceHandle);
	if (Result == MLResult_Ok && MLHandleIsValid(InputDeviceHandle))
	{
		return true;
	}
#endif // WITH_MLSDK

	return false;
}

bool Audio::FAudioCaptureMagicLeapStream::CloseStream()
{
#if WITH_MLSDK
	MLResult Result = MLAudioDestroyInput(InputDeviceHandle);
	if (Result != MLResult_Ok)
	{
		return false;
	}

	InputDeviceHandle = ML_INVALID_HANDLE;

	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.RemoveAll(this);

	return true;
#else // ^^^ WITH_MLSDK vvv !WITH_MLSDK
	return false;
#endif // !WITH_MLSDK
}

bool Audio::FAudioCaptureMagicLeapStream::StartStream()
{
#if WITH_MLSDK
	MLResult Result = MLAudioStartInput(InputDeviceHandle);
	if (Result != MLResult_Ok)
	{
		return false;
	}

	bStreamStarted = true;
	return true;
#else // ^^^ WITH_MLSDK vvv !WITH_MLSDK
	return false;
#endif // !WITH_MLSDK
}

bool Audio::FAudioCaptureMagicLeapStream::StopStream()
{
#if WITH_MLSDK
	MLResult Result = MLAudioStopInput(InputDeviceHandle);
	if (Result != MLResult_Ok)
	{
		return false;
	}

	bStreamStarted = false;
	return true;
#else // ^^^ WITH_MLSDK vvv !WITH_MLSDK
	return false;
#endif // !WITH_MLSDK
}

bool Audio::FAudioCaptureMagicLeapStream::AbortStream()
{
	StopStream();
	CloseStream();
	return true;
}

bool Audio::FAudioCaptureMagicLeapStream::GetStreamTime(double& OutStreamTime)
{
	OutStreamTime = 0.0f;
	return true;
}

bool Audio::FAudioCaptureMagicLeapStream::IsStreamOpen() const
{
#if WITH_MLSDK
	return MLHandleIsValid(InputDeviceHandle);
#else // ^^^  WITH_MLSDK vvv !WITH_MLSDK
	return false;
#endif // !WITH_MLSDK
}

bool Audio::FAudioCaptureMagicLeapStream::IsCapturing() const
{
	return bStreamStarted;
}

void Audio::FAudioCaptureMagicLeapStream::OnAudioCapture(void* InBuffer, uint32 InBufferFrames, double StreamTime, bool bOverflow)
{
	FScopeLock ScopeLock(&ApplicationResumeCriticalSection);
	int32 NumSamples = (int32)(InBufferFrames * NumChannels);

	//TODO: Check to see if float conversion is needed:
	FloatBuffer.Reset(InBufferFrames);
	FloatBuffer.AddUninitialized(NumSamples);

	int16* InBufferData = (int16*)InBuffer;
	float* FloatBufferPtr = FloatBuffer.GetData();

	for (int32 i = 0; i < NumSamples; ++i)
	{
		FloatBufferPtr[i] = (((float)InBufferData[i]) / 32767.0f);
	};

	OnCapture(FloatBufferPtr, InBufferFrames, NumChannels, StreamTime, bOverflow);
}

bool Audio::FAudioCaptureMagicLeapStream::GetInputDevicesAvailable(TArray<FCaptureDeviceInfo>& OutDevices)
{
	// TODO: Add individual devices for different ports here.
	OutDevices.Reset();

	FCaptureDeviceInfo& DeviceInfo = OutDevices.AddDefaulted_GetRef();
	GetCaptureDeviceInfo(DeviceInfo, 0);

	return true;
}

void Audio::FAudioCaptureMagicLeapStream::OnApplicationSuspend()
{
#if WITH_MLSDK
	FScopeLock ScopeLock(&ApplicationResumeCriticalSection);
	MLResult Result = MLAudioStopInput(InputDeviceHandle);
#endif
}

void Audio::FAudioCaptureMagicLeapStream::OnApplicationResume()
{
#if WITH_MLSDK
	FScopeLock ScopeLock(&ApplicationResumeCriticalSection);
	MLResult Result = MLAudioStartInput(InputDeviceHandle);
#endif
}
