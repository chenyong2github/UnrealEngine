// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaIOCoreAudioOutput.h"

#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "AudioMixerSubmix.h"
#include "Sound/AudioSettings.h"

FMediaIOAudioOutput::FMediaIOAudioOutput(Audio::FPatchOutputStrongPtr InPatchOutput, uint32 InNumInputChannels, uint32 InNumOutputChannels, FFrameRate InTargetFrameRate, uint32 InMaxSampleLatency, uint32 InOutputSampleRate)
	: PatchOutput(MoveTemp(InPatchOutput))
    , NumInputChannels(InNumInputChannels)
    , NumOutputChannels(InNumOutputChannels)
    , TargetFrameRate(MoveTemp(InTargetFrameRate))
    , MaxSampleLatency(InMaxSampleLatency)
    , OutputSampleRate(InOutputSampleRate)
{
}

int32 FMediaIOAudioOutput::GetAudioBuffer(int32 InNumSamplesToPop, float* OutBuffer) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FAudioOutput::GetAudioBuffer);

	if (PatchOutput)
	{
		constexpr bool bUseLatestAudio = false;
		return PatchOutput->MixInAudio(OutBuffer, InNumSamplesToPop, bUseLatestAudio);
	}
	return 0;
}

Audio::FAlignedFloatBuffer FMediaIOAudioOutput::GetFloatBuffer() const
{
	// @todo: Depend on frame number to correctly fetch the right amount of frames on framerates like 59.97
	const int32 NumSamplesPerFrame = FMath::CeilToInt(NumInputChannels * OutputSampleRate / TargetFrameRate.AsDecimal());

	// NumSamplesToPop must be a multiple of 4 in order to avoid an assertion in the audio pipeline.
	const int32 NumSamplesToPop = Align(NumSamplesPerFrame, 4);

	Audio::FAlignedFloatBuffer FloatBuffer;
	FloatBuffer.SetNumUninitialized(NumSamplesToPop);

	GetAudioBuffer(NumSamplesToPop, FloatBuffer.GetData());

	// Trim back the buffer after we get the buffer since it might be bigger because of alignment
	constexpr bool bAllowShrinking = false;
	FloatBuffer.SetNum(NumSamplesPerFrame, bAllowShrinking);
	return FloatBuffer;
}

FMediaIOAudioCapture::FMediaIOAudioCapture()
{
	if (FAudioDevice* AudioDevice = GEngine->GetMainAudioDeviceRaw())
	{
		if (AudioDevice->IsAudioMixerEnabled())
		{
			Audio::FMixerDevice* MixerDevice = static_cast<Audio::FMixerDevice*>(AudioDevice);
			NumChannels = MixerDevice->GetDeviceOutputChannels();
			SampleRate = MixerDevice->GetSampleRate();
			MasterSubmixName = *GetDefault<UAudioSettings>()->MasterSubmix.GetAssetName();
			AudioDevice->RegisterSubmixBufferListener(this);
		}
	}
}

FMediaIOAudioCapture::~FMediaIOAudioCapture()
{
	if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
	{
		if (FAudioDevice* AudioDevice = GEngine->GetMainAudioDeviceRaw())
		{
			AudioDevice->UnregisterSubmixBufferListener(this);
		}
	}
}

void FMediaIOAudioCapture::OnNewSubmixBuffer(const USoundSubmix* InOwningSubmix, float* InAudioData, int32 InNumSamples, int32 InNumChannels, const int32 InSampleRate, double InAudioClock)
{
	check(InOwningSubmix);
	if (InOwningSubmix->GetFName() == MasterSubmixName)
	{
		if (ensureMsgf(NumChannels == InNumChannels, TEXT("Expected %d channels from submix buffer but got %d instead."), NumChannels, InNumChannels))
		{
			AudioSplitter.PushAudio(InAudioData, InNumSamples);
		}
	}
}

TSharedPtr<FMediaIOAudioOutput> FMediaIOAudioCapture::CreateAudioOutput(int32 InNumOutputChannels, FFrameRate InTargetFrameRate, uint32 InMaxSampleLatency, uint32 InOutputSampleRate)
{
	if (ensureMsgf(InOutputSampleRate == SampleRate, TEXT("The engine's sample rate is different from the output sample rate and resampling is not yet supported in Media Captutre.")))
	{
		constexpr float Gain = 1.0f;
		
		checkf(NumChannels <= InNumOutputChannels, TEXT("At the moment MediaIOAudioCapture only supports up mixing."));
		check(InNumOutputChannels != 0);
		
		// @todo: Don't pass strong ptr, to patch output
		Audio::FPatchOutputStrongPtr PatchOutput = AudioSplitter.AddNewPatch(InMaxSampleLatency, Gain);
		return MakeShared<FMediaIOAudioOutput>(MoveTemp(PatchOutput), NumChannels, InNumOutputChannels, InTargetFrameRate, InMaxSampleLatency, InOutputSampleRate);
	}

	return nullptr;
}
