// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioCapture.h"
#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "AudioCaptureInternal.h"

DEFINE_LOG_CATEGORY(LogAudioCapture);

namespace Audio 
{

FAudioCapture::FAudioCapture()
{
	Impl = CreateImpl();
}

FAudioCapture::~FAudioCapture()
{
}

bool FAudioCapture::GetDefaultCaptureDeviceInfo(FCaptureDeviceInfo& OutInfo)
{
	if (Impl.IsValid())
	{
		return Impl->GetDefaultCaptureDeviceInfo(OutInfo);
	}

	return false;
}

bool FAudioCapture::OpenDefaultCaptureStream(FOnCaptureFunction OnCapture, uint32 NumFramesDesired)
{
	if (Impl.IsValid())
	{
		return Impl->OpenDefaultCaptureStream(MoveTemp(OnCapture), NumFramesDesired);
	}

	return false;
}

bool FAudioCapture::CloseStream()
{
	if (Impl.IsValid())
	{
		return Impl->CloseStream();
	}
	return false;
}

bool FAudioCapture::StartStream()
{
	if (Impl.IsValid())
	{
		return Impl->StartStream();
	}
	return false;
}

bool FAudioCapture::StopStream()
{
	if (Impl.IsValid())
	{
		return Impl->StopStream();
	}
	return false;
}

bool FAudioCapture::AbortStream()
{
	if (Impl.IsValid())
	{
		return Impl->AbortStream();
	}
	return false;
}

bool FAudioCapture::GetStreamTime(double& OutStreamTime) const
{
	if (Impl.IsValid())
	{
		return Impl->GetStreamTime(OutStreamTime);
	}
	return false;
}

int32 FAudioCapture::GetSampleRate() const
{
	if (Impl.IsValid())
	{
		return Impl->GetSampleRate();
	}
	return 0;
}

bool FAudioCapture::IsStreamOpen() const
{
	if (Impl.IsValid())
	{
		return Impl->IsStreamOpen();
	}
	return false;
}

bool FAudioCapture::IsCapturing() const
{
	if (Impl.IsValid())
	{
		return Impl->IsCapturing();
	}
	return false;
}

FAudioCaptureSynth::FAudioCaptureSynth()
	: NumSamplesEnqueued(0)
	, bInitialized(false)
	, bIsCapturing(false)
{
}

FAudioCaptureSynth::~FAudioCaptureSynth()
{
}

bool FAudioCaptureSynth::GetDefaultCaptureDeviceInfo(FCaptureDeviceInfo& OutInfo)
{
	return AudioCapture.GetDefaultCaptureDeviceInfo(OutInfo);
}

bool FAudioCaptureSynth::OpenDefaultStream()
{
	bool bSuccess = true;
	if (!AudioCapture.IsStreamOpen())
	{
		FOnCaptureFunction OnCapture = [this](const float* AudioData, int32 NumFrames, int32 NumChannels, double StreamTime, bool bOverFlow)
		{
			int32 NumSamples = NumChannels * NumFrames;

			FScopeLock Lock(&CaptureCriticalSection);

			if (bIsCapturing)
			{
				// Append the audio memory to the capture data buffer
				int32 Index = AudioCaptureData.AddUninitialized(NumSamples);
				float* AudioCaptureDataPtr = AudioCaptureData.GetData();
				FMemory::Memcpy(&AudioCaptureDataPtr[Index], AudioData, NumSamples * sizeof(float));
			}
		};

		// Prepare the audio buffer memory for 2 seconds of stereo audio at 48k SR to reduce chance for allocation in callbacks
		AudioCaptureData.Reserve(2 * 2 * 48000);

		// Start the stream here to avoid hitching the audio render thread. 
		if (AudioCapture.OpenDefaultCaptureStream(MoveTemp(OnCapture), 1024))
		{
			AudioCapture.StartStream();
		}
		else
		{
			bSuccess = false;
		}
	}
	return bSuccess;
}

bool FAudioCaptureSynth::StartCapturing()
{
	FScopeLock Lock(&CaptureCriticalSection);

	AudioCaptureData.Reset();

	check(AudioCapture.IsStreamOpen());

	bIsCapturing = true;
	return true;
}

void FAudioCaptureSynth::StopCapturing()
{
	check(AudioCapture.IsStreamOpen());
	check(AudioCapture.IsCapturing());
	FScopeLock Lock(&CaptureCriticalSection);
	bIsCapturing = false;
}

void FAudioCaptureSynth::AbortCapturing()
{
	AudioCapture.AbortStream();
	AudioCapture.CloseStream();
}

bool FAudioCaptureSynth::IsStreamOpen() const
{
	return AudioCapture.IsStreamOpen();
}

bool FAudioCaptureSynth::IsCapturing() const
{
	return bIsCapturing;
}

int32 FAudioCaptureSynth::GetNumSamplesEnqueued()
{
	FScopeLock Lock(&CaptureCriticalSection);
	return AudioCaptureData.Num();
}

bool FAudioCaptureSynth::GetAudioData(TArray<float>& OutAudioData)
{
	FScopeLock Lock(&CaptureCriticalSection);

	int32 CaptureDataSamples = AudioCaptureData.Num();
	if (CaptureDataSamples > 0)
	{
		// Append the capture audio to the output buffer
		int32 OutIndex = OutAudioData.AddUninitialized(CaptureDataSamples);
		float* OutDataPtr = OutAudioData.GetData();
		FMemory::Memcpy(&OutDataPtr[OutIndex], AudioCaptureData.GetData(), CaptureDataSamples * sizeof(float));

		// Reset the capture data buffer since we copied the audio out
		AudioCaptureData.Reset();
		return true;
	}
	return false;
}

} // namespace audio

UAudioCapture::UAudioCapture()
{

}

UAudioCapture::~UAudioCapture()
{

}

bool UAudioCapture::OpenDefaultAudioStream()
{
	if (!AudioCapture.IsStreamOpen())
	{
		Audio::FOnCaptureFunction OnCapture = [this](const float* AudioData, int32 NumFrames, int32 InNumChannels, double StreamTime, bool bOverFlow)
		{
			OnGeneratedAudio(AudioData, NumFrames * InNumChannels);
		};

		// Start the stream here to avoid hitching the audio render thread. 
		if (AudioCapture.OpenDefaultCaptureStream(MoveTemp(OnCapture), 1024))
		{
			// If we opened the capture stream succesfully, get the capture device info and initialize the UAudioGenerator
			Audio::FCaptureDeviceInfo Info;
			if (AudioCapture.GetDefaultCaptureDeviceInfo(Info))
			{
				Init(Info.PreferredSampleRate, Info.InputChannels);
				return true;
			}
		}
	}
	return false;
}

bool UAudioCapture::GetAudioCaptureDeviceInfo(FAudioCaptureDeviceInfo& OutInfo)
{
	Audio::FCaptureDeviceInfo Info;
	if (AudioCapture.GetDefaultCaptureDeviceInfo(Info))
	{
		OutInfo.DeviceName = FName(*Info.DeviceName);
		OutInfo.NumInputChannels = Info.InputChannels;
		OutInfo.SampleRate = Info.PreferredSampleRate;
		return true;
	}
	return false;
}

void UAudioCapture::StartCapturingAudio()
{
	if (AudioCapture.IsStreamOpen())
	{
		AudioCapture.StartStream();
	}
}

void UAudioCapture::StopCapturingAudio()
{
	if (AudioCapture.IsStreamOpen())
	{
		AudioCapture.StopStream();
	}

}

bool UAudioCapture::IsCapturingAudio()
{
	return AudioCapture.IsCapturing();
}

UAudioCapture* UAudioCaptureFunctionLibrary::CreateAudioCapture()
{
	UAudioCapture* NewAudioCapture = NewObject<UAudioCapture>();
	if (NewAudioCapture->OpenDefaultAudioStream())
	{
		return NewAudioCapture;
	}
	
	UE_LOG(LogAudioCapture, Error, TEXT("Failed to open a default audio stream to the audio capture device."));
	return nullptr;
}

void FAudioCaptureModule::StartupModule()
{
}

void FAudioCaptureModule::ShutdownModule()
{
}


IMPLEMENT_MODULE(FAudioCaptureModule, AudioCapture);