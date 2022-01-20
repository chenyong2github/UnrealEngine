// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioCapturer.h"
#include "AudioMixerDevice.h"
#include "SampleBuffer.h"
#include "Engine/GameEngine.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPixelStreamingAudioCapturer, Log, All);
DEFINE_LOG_CATEGORY(LogPixelStreamingAudioCapturer);

// These are copied from webrtc internals
#define CHECKinitialized_() \
	{                       \
		if (!bInitialized)  \
		{                   \
			return -1;      \
		};                  \
	}
#define CHECKinitialized__BOOL() \
	{                            \
		if (!bInitialized)       \
		{                        \
			return false;        \
		};                       \
	}

constexpr int UE::PixelStreaming::FAudioCapturer::SampleRate;
constexpr int UE::PixelStreaming::FAudioCapturer::NumChannels;

void UE::PixelStreaming::FAudioCapturer::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 InNumChannels, const int32 InSampleRate, double AudioClock)
{
	if (!(bInitialized && bRecordingInitialized))
	{
		return;
	}

	// Only 48000hz supported for now
	if (InSampleRate != SampleRate)
	{
		// Only report the problem once
		if (!bFormatChecked)
		{
			bFormatChecked = true;
			UE_LOG(LogPixelStreamingAudioCapturer, Error, TEXT("Audio samplerate needs to be 48000hz"));
		}
		return;
	}

	UE_LOG(LogPixelStreamingAudioCapturer, VeryVerbose, TEXT("captured %d samples, %dc, %dHz"), NumSamples, NumChannels, SampleRate);

	Audio::TSampleBuffer<float> Buffer(AudioData, NumSamples, InNumChannels, SampleRate);
	// Mix to stereo if required, since PixelStreaming only accepts stereo at the moment
	if (Buffer.GetNumChannels() != NumChannels)
	{
		Buffer.MixBufferToChannels(NumChannels);
	}

	// Convert to signed PCM 16-bits
	PCM16.Reset(Buffer.GetNumSamples());
	PCM16.AddZeroed(Buffer.GetNumSamples());
	const float* Ptr = reinterpret_cast<const float*>(Buffer.GetData());
	for (int16& S : PCM16)
	{
		int32 N = *Ptr >= 0 ? *Ptr * int32(MAX_int16) : *Ptr * (int32(MAX_int16) + 1);
		S = static_cast<int16>(FMath::Clamp(N, int32(MIN_int16), int32(MAX_int16)));
		Ptr++;
	}

	RecordingBuffer.Append(reinterpret_cast<const uint8*>(PCM16.GetData()), PCM16.Num() * sizeof(PCM16[0]));
	int BytesPer10Ms = (SampleRate * NumChannels * static_cast<int>(sizeof(uint16))) / 100;

	// Feed in 10ms chunks
	while (RecordingBuffer.Num() >= BytesPer10Ms)
	{
		{
			FScopeLock Lock(&DeviceBufferCS);
			if (DeviceBuffer)
			{
				DeviceBuffer->SetRecordedBuffer(RecordingBuffer.GetData(), BytesPer10Ms / (sizeof(uint16) * NumChannels));
				DeviceBuffer->DeliverRecordedData();
				UE_LOG(LogPixelStreamingAudioCapturer, VeryVerbose, TEXT("passed %d bytes"), BytesPer10Ms);
			}
		}

		RecordingBuffer.RemoveAt(0, BytesPer10Ms, false);
	}
}

int32 UE::PixelStreaming::FAudioCapturer::ActiveAudioLayer(AudioLayer* audioLayer) const
{
	*audioLayer = AudioDeviceModule::kDummyAudio;
	return 0;
}

int32 UE::PixelStreaming::FAudioCapturer::RegisterAudioCallback(webrtc::AudioTransport* audioCallback)
{
	FScopeLock Lock(&DeviceBufferCS);
	DeviceBuffer->RegisterAudioCallback(audioCallback);

	return 0;
}

int32 UE::PixelStreaming::FAudioCapturer::Init()
{
	if (bInitialized)
		return 0;

	{
		FScopeLock Lock(&DeviceBufferCS);

		m_taskQueueFactory = webrtc::CreateDefaultTaskQueueFactory();
		DeviceBuffer = MakeUnique<webrtc::AudioDeviceBuffer>(m_taskQueueFactory.get());
	}

	// subscribe to audio data
	if (!GEngine)
	{
		return -1;
	}

	FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
	if (!AudioDevice)
	{
		UE_LOG(LogPixelStreamingAudioCapturer, Warning, TEXT("No audio device"));
		return -1;
	}

	bInitialized = true;
	AudioDevice->RegisterSubmixBufferListener(this);

	UE_LOG(LogPixelStreamingAudioCapturer, Verbose, TEXT("Init"));

	return 0;
}

int32 UE::PixelStreaming::FAudioCapturer::Terminate()
{
	if (!bInitialized)
		return 0;

	// unsubscribe from audio data
	if (!GEngine)
	{
		return -1;
	}

	FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
	if (!AudioDevice)
	{
		return -1;
	}

	AudioDevice->UnregisterSubmixBufferListener(this);
	bInitialized = false;

	{
		FScopeLock Lock(&DeviceBufferCS);
		DeviceBuffer.Reset();
	}

	UE_LOG(LogPixelStreamingAudioCapturer, Verbose, TEXT("Terminate"));

	return 0;
}

bool UE::PixelStreaming::FAudioCapturer::Initialized() const
{
	return bInitialized;
}

int16 UE::PixelStreaming::FAudioCapturer::PlayoutDevices()
{
	CHECKinitialized_();
	return -1;
}

int16 UE::PixelStreaming::FAudioCapturer::RecordingDevices()
{
	CHECKinitialized_();
	return -1;
}

int32 UE::PixelStreaming::FAudioCapturer::PlayoutDeviceName(
	uint16 index, char name[webrtc::kAdmMaxDeviceNameSize], char guid[webrtc::kAdmMaxGuidSize])
{
	CHECKinitialized_();
	return -1;
}

int32 UE::PixelStreaming::FAudioCapturer::RecordingDeviceName(
	uint16 index, char name[webrtc::kAdmMaxDeviceNameSize], char guid[webrtc::kAdmMaxGuidSize])
{
	CHECKinitialized_();
	return -1;
}

int32 UE::PixelStreaming::FAudioCapturer::SetPlayoutDevice(uint16 index)
{
	CHECKinitialized_();
	return 0;
}

int32 UE::PixelStreaming::FAudioCapturer::SetPlayoutDevice(WindowsDeviceType device)
{
	CHECKinitialized_();
	return 0;
}

int32 UE::PixelStreaming::FAudioCapturer::SetRecordingDevice(uint16 index)
{
	CHECKinitialized_();
	return 0;
}

int32 UE::PixelStreaming::FAudioCapturer::SetRecordingDevice(WindowsDeviceType device)
{
	CHECKinitialized_();
	return 0;
}

int32 UE::PixelStreaming::FAudioCapturer::PlayoutIsAvailable(bool* available)
{
	CHECKinitialized_();
	return -1;
}

int32 UE::PixelStreaming::FAudioCapturer::InitPlayout()
{
	CHECKinitialized_();
	return -1;
}

bool UE::PixelStreaming::FAudioCapturer::PlayoutIsInitialized() const
{
	CHECKinitialized__BOOL();
	return false;
}

int32 UE::PixelStreaming::FAudioCapturer::RecordingIsAvailable(bool* available)
{
	CHECKinitialized_();
	return -1;
}

int32 UE::PixelStreaming::FAudioCapturer::InitRecording()
{
	CHECKinitialized_();

	{
		FScopeLock Lock(&DeviceBufferCS);
		// #Audio : Allow dynamic values for samplerate and/or channels ,
		// or receive those from UnrealEngine ?
		DeviceBuffer->SetRecordingSampleRate(SampleRate);
		DeviceBuffer->SetRecordingChannels(NumChannels);
	}

	bRecordingInitialized = true;
	return 0;
}

bool UE::PixelStreaming::FAudioCapturer::RecordingIsInitialized() const
{
	CHECKinitialized__BOOL();
	return bRecordingInitialized == true;
}

int32 UE::PixelStreaming::FAudioCapturer::StartPlayout()
{
	CHECKinitialized_();
	return -1;
}

int32 UE::PixelStreaming::FAudioCapturer::StopPlayout()
{
	CHECKinitialized_();
	return -1;
}

bool UE::PixelStreaming::FAudioCapturer::Playing() const
{
	CHECKinitialized__BOOL();
	return false;
}

int32 UE::PixelStreaming::FAudioCapturer::StartRecording()
{
	CHECKinitialized_();
	return -1;
}

int32 UE::PixelStreaming::FAudioCapturer::StopRecording()
{
	CHECKinitialized_();
	return -1;
}

bool UE::PixelStreaming::FAudioCapturer::Recording() const
{
	CHECKinitialized__BOOL();
	return bRecordingInitialized;
}

int32 UE::PixelStreaming::FAudioCapturer::InitSpeaker()
{
	CHECKinitialized_();
	return -1;
}

bool UE::PixelStreaming::FAudioCapturer::SpeakerIsInitialized() const
{
	CHECKinitialized__BOOL();
	return false;
}

int32 UE::PixelStreaming::FAudioCapturer::InitMicrophone()
{
	CHECKinitialized_();
	return 0;
}

bool UE::PixelStreaming::FAudioCapturer::MicrophoneIsInitialized() const
{
	CHECKinitialized__BOOL();
	return true;
}

int32 UE::PixelStreaming::FAudioCapturer::StereoPlayoutIsAvailable(bool* available) const
{
	CHECKinitialized_();
	return -1;
}

int32 UE::PixelStreaming::FAudioCapturer::SetStereoPlayout(bool enable)
{
	CHECKinitialized_();
	return -1;
}

int32 UE::PixelStreaming::FAudioCapturer::StereoPlayout(bool* enabled) const
{
	CHECKinitialized_();
	return -1;
}

int32 UE::PixelStreaming::FAudioCapturer::StereoRecordingIsAvailable(bool* available) const
{
	CHECKinitialized_();
	*available = true;
	return 0;
}

int32 UE::PixelStreaming::FAudioCapturer::SetStereoRecording(bool enable)
{
	CHECKinitialized_();
	return 0;
}

int32 UE::PixelStreaming::FAudioCapturer::StereoRecording(bool* enabled) const
{
	CHECKinitialized_();
	*enabled = true;
	return 0;
}
