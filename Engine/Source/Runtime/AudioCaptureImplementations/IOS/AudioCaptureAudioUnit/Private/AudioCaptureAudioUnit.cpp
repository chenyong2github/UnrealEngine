// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioCaptureAudioUnit.h"


Audio::FAudioCaptureAudioUnitStream::FAudioCaptureAudioUnitStream()
	: NumChannels(0)
	, SampleRate(0)
{
}

static OSStatus RecordingCallback(void *inRefCon,
	AudioUnitRenderActionFlags *ioActionFlags,
	const AudioTimeStamp *inTimeStamp,
	UInt32 inBusNumber,
	UInt32 inNumberFrames,
	AudioBufferList *ioData)
{
	OSStatus status = noErr;
	Audio::FAudioCaptureAudioUnitStream* AudioCapture = (Audio::FAudioCaptureAudioUnitStream*)inRefCon;

	status = AudioUnitRender(AudioCapture->AudioUnit,
		ioActionFlags,
		inTimeStamp,
		inBusNumber,
		inNumberFrames,
		ioData);
	check(status == noErr);


	void* InBuffer = (void*)ioData->mBuffers[0].mData;
	AudioCapture->OnAudioCapture(InBuffer, inNumberFrames, 0.0, false);

	return noErr;
}

bool Audio::FAudioCaptureAudioUnitStream::GetCaptureDeviceInfo(FCaptureDeviceInfo& OutInfo, int32 DeviceIndex)
{
	OutInfo.DeviceName = FString(TEXT("Remote IO Audio Component"));
	OutInfo.InputChannels = 1;
	OutInfo.PreferredSampleRate = 48000;

	return true;
}

bool Audio::FAudioCaptureAudioUnitStream::OpenCaptureStream(const FAudioCaptureDeviceParams& InParams, FOnCaptureFunction InOnCapture, uint32 NumFramesDesired)
{
	AudioComponentDescription desc;
	desc.componentType = kAudioUnitType_Output;
	if (InParams.bUseHardwareAEC)
	{
		desc.componentSubType = kAudioUnitSubType_RemoteIO;
	}
	else
	{
		desc.componentSubType = kAudioUnitSubType_RemoteIO;
	}

	desc.componentManufacturer = kAudioUnitManufacturer_Apple;
	desc.componentFlags = 0;
	desc.componentFlagsMask = 0;

	AudioComponent InputComponent = AudioComponentFindNext(NULL, &desc);

	AudioUnitStatus = AudioComponentInstanceNew(InputComponent, &AudioUnit);
	check(AudioUnitStatus == noErr);

	// Enable IO via AudioUnitSetProperty:
	static const uint32 AudioUnitTrue = 1;
	AudioUnitStatus = AudioUnitSetProperty(AudioUnit,
		kAudioOutputUnitProperty_EnableIO,
		kAudioUnitScope_Input,
		1,
		&AudioUnitTrue,
		sizeof(AudioUnitTrue));
	check(AudioUnitStatus == noErr);

	AudioUnitStatus = AudioUnitSetProperty(AudioUnit,
		kAudioOutputUnitProperty_EnableIO,
		kAudioUnitScope_Output,
		0,
		&AudioUnitTrue,
		sizeof(AudioUnitTrue));
	check(AudioUnitStatus == noErr);

	AudioStreamBasicDescription StreamDescription;

	StreamDescription.mSampleRate = 48000.00;
	StreamDescription.mFormatID = kAudioFormatLinearPCM;
	StreamDescription.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
	StreamDescription.mFramesPerPacket = NumFramesDesired;
	StreamDescription.mChannelsPerFrame = 1;
	StreamDescription.mBitsPerChannel = 8 * sizeof(float);
	StreamDescription.mBytesPerPacket = NumFramesDesired * sizeof(float);
	StreamDescription.mBytesPerFrame = sizeof(float);

	AudioUnitStatus = AudioUnitSetProperty(AudioUnit,
		kAudioUnitProperty_StreamFormat,
		kAudioUnitScope_Output,
		0,
		&StreamDescription,
		sizeof(StreamDescription));
	check(AudioUnitStatus == noErr);

	AURenderCallbackStruct CallbackInfo;
	CallbackInfo.inputProc = RecordingCallback;
	CallbackInfo.inputProcRefCon = this;
	AudioUnitStatus = AudioUnitSetProperty(AudioUnit,
		kAudioOutputUnitProperty_SetInputCallback,
		kAudioUnitScope_Global,
		0,
		&CallbackInfo,
		sizeof(CallbackInfo));
	check(AudioUnitStatus == noErr);

	AudioUnitStatus = AudioUnitInitialize(AudioUnit);
	check(AudioUnitStatus == noErr);

	return AudioUnitStatus == noErr;
}

bool Audio::FAudioCaptureAudioUnitStream::CloseStream()
{
	StopStream();
	AudioComponentInstanceDispose(AudioUnit);
	return true;
}

bool Audio::FAudioCaptureAudioUnitStream::StartStream()
{
	AudioUnitStatus = AudioOutputUnitStart(AudioUnit);
	return AudioUnitStatus == noErr;
}

bool Audio::FAudioCaptureAudioUnitStream::StopStream()
{
	AudioUnitStatus = AudioOutputUnitStop(AudioUnit);
	return AudioUnitStatus == noErr;
}

bool Audio::FAudioCaptureAudioUnitStream::AbortStream()
{
	StopStream();
	CloseStream();
	return true;
}

bool Audio::FAudioCaptureAudioUnitStream::GetStreamTime(double& OutStreamTime)
{
	OutStreamTime = 0.0f;
	return true;
}

bool Audio::FAudioCaptureAudioUnitStream::IsStreamOpen() const
{
	return true;
}

bool Audio::FAudioCaptureAudioUnitStream::IsCapturing() const
{
	return true;
}

void Audio::FAudioCaptureAudioUnitStream::OnAudioCapture(void* InBuffer, uint32 InBufferFrames, double StreamTime, bool bOverflow)
{
	float* InBufferData = (float*)InBuffer;
	OnCapture(InBufferData, InBufferFrames, NumChannels, StreamTime, bOverflow);
}

bool Audio::FAudioCaptureAudioUnitStream::GetInputDevicesAvailable(TArray<FCaptureDeviceInfo>& OutDevices)
{
	// TODO: Add individual devices for different ports here.
	OutDevices.Reset();

	FCaptureDeviceInfo& DeviceInfo = OutDevices.AddDefaulted_GetRef();
	GetCaptureDeviceInfo(DeviceInfo, 0);

	return true;
}
