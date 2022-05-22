// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingAudioDeviceModule.h"
#include "AudioSubmixCapturer.h"
#include "AudioPlayoutRequester.h"
#include "PixelStreamingPrivate.h"
#include "Settings.h"

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

FPixelStreamingAudioDeviceModule::FPixelStreamingAudioDeviceModule()
	: bInitialized(false)
	, Capturer(MakeUnique<UE::PixelStreaming::FAudioSubmixCapturer>())
	, Requester(MakeUnique<UE::PixelStreaming::FAudioPlayoutRequester>())
{
}

int32 FPixelStreamingAudioDeviceModule::ActiveAudioLayer(AudioLayer* audioLayer) const
{
	*audioLayer = AudioDeviceModule::kDummyAudio;
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::RegisterAudioCallback(webrtc::AudioTransport* audioCallback)
{
	Requester->RegisterAudioCallback(audioCallback);
	Capturer->RegisterAudioCallback(audioCallback);
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::Init()
{
	InitRecording();

	UE_LOG(LogPixelStreaming, Verbose, TEXT("Init PixelStreamingAudioDeviceModule"));
	bInitialized = true;
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::Terminate()
{
	if (!bInitialized)
	{
		return 0;
	}

	Requester->Uninitialise();
	Capturer->Uninitialise();

	UE_LOG(LogPixelStreaming, Verbose, TEXT("Terminate"));

	return 0;
}

bool FPixelStreamingAudioDeviceModule::Initialized() const
{
	return bInitialized;
}

int16 FPixelStreamingAudioDeviceModule::PlayoutDevices()
{
	CHECKinitialized_();
	return -1;
}

int16 FPixelStreamingAudioDeviceModule::RecordingDevices()
{
	CHECKinitialized_();
	return -1;
}

int32 FPixelStreamingAudioDeviceModule::PlayoutDeviceName(uint16 index, char name[webrtc::kAdmMaxDeviceNameSize], char guid[webrtc::kAdmMaxGuidSize])
{
	CHECKinitialized_();
	return -1;
}

int32 FPixelStreamingAudioDeviceModule::RecordingDeviceName(uint16 index, char name[webrtc::kAdmMaxDeviceNameSize], char guid[webrtc::kAdmMaxGuidSize])
{
	CHECKinitialized_();
	return -1;
}

int32 FPixelStreamingAudioDeviceModule::SetPlayoutDevice(uint16 index)
{
	CHECKinitialized_();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::SetPlayoutDevice(WindowsDeviceType device)
{
	CHECKinitialized_();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::SetRecordingDevice(uint16 index)
{
	CHECKinitialized_();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::SetRecordingDevice(WindowsDeviceType device)
{
	CHECKinitialized_();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::PlayoutIsAvailable(bool* available)
{
	CHECKinitialized_();
	*available = !UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCDisableReceiveAudio.GetValueOnAnyThread();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::InitPlayout()
{
	CHECKinitialized_();
	bool bIsPlayoutAvailable = false;
	PlayoutIsAvailable(&bIsPlayoutAvailable);
	if (!bIsPlayoutAvailable)
	{
		return -1;
	}

	Requester->InitPlayout();
	return 0;
}

bool FPixelStreamingAudioDeviceModule::PlayoutIsInitialized() const
{
	CHECKinitialized__BOOL();
	return Requester->PlayoutIsInitialized();
}

int32 FPixelStreamingAudioDeviceModule::StartPlayout()
{
	CHECKinitialized_();

	if (!Requester->PlayoutIsInitialized())
	{
		return -1;
	}

	Requester->StartPlayout();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::StopPlayout()
{
	CHECKinitialized_();
	if (!Requester->PlayoutIsInitialized())
	{
		return -1;
	}

	Requester->StopPlayout();
	return 0;
}

bool FPixelStreamingAudioDeviceModule::Playing() const
{
	CHECKinitialized__BOOL();
	return Requester->Playing();
}

int32 FPixelStreamingAudioDeviceModule::RecordingIsAvailable(bool* available)
{
	CHECKinitialized_();
	*available = !UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::InitRecording()
{
	CHECKinitialized_();

	bool bIsRecordingAvailable = false;
	RecordingIsAvailable(&bIsRecordingAvailable);
	if (!bIsRecordingAvailable)
	{
		return -1;
	}

	if (!Capturer->IsInitialised())
	{
		Capturer->Init();
	}

	return 0;
}

bool FPixelStreamingAudioDeviceModule::RecordingIsInitialized() const
{
	CHECKinitialized__BOOL();
	return Capturer->IsInitialised();
}

int32 FPixelStreamingAudioDeviceModule::StartRecording()
{
	CHECKinitialized_();
	if (!Capturer->IsCapturing())
	{
		Capturer->StartCapturing();
	}
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::StopRecording()
{
	CHECKinitialized_();
	if (Capturer->IsCapturing())
	{
		Capturer->EndCapturing();
	}
	return 0;
}

bool FPixelStreamingAudioDeviceModule::Recording() const
{
	CHECKinitialized__BOOL();
	return Capturer->IsCapturing();
}

int32 FPixelStreamingAudioDeviceModule::InitSpeaker()
{
	CHECKinitialized_();
	return -1;
}

bool FPixelStreamingAudioDeviceModule::SpeakerIsInitialized() const
{
	CHECKinitialized__BOOL();
	return false;
}

int32 FPixelStreamingAudioDeviceModule::InitMicrophone()
{
	CHECKinitialized_();
	return 0;
}

bool FPixelStreamingAudioDeviceModule::MicrophoneIsInitialized() const
{
	CHECKinitialized__BOOL();
	return true;
}

int32 FPixelStreamingAudioDeviceModule::SpeakerVolumeIsAvailable(bool* available)
{
	return -1;
}
int32 FPixelStreamingAudioDeviceModule::SetSpeakerVolume(uint32 volume)
{
	return -1;
}
int32 FPixelStreamingAudioDeviceModule::SpeakerVolume(uint32* volume) const
{
	return -1;
}
int32 FPixelStreamingAudioDeviceModule::MaxSpeakerVolume(uint32* maxVolume) const
{
	return -1;
}
int32 FPixelStreamingAudioDeviceModule::MinSpeakerVolume(uint32* minVolume) const
{
	return -1;
}

int32 FPixelStreamingAudioDeviceModule::MicrophoneVolumeIsAvailable(bool* available)
{
	return 0;
}
int32 FPixelStreamingAudioDeviceModule::SetMicrophoneVolume(uint32 volume)
{
	// Capturer.SetVolume(volume);
	return 0;
}
int32 FPixelStreamingAudioDeviceModule::MicrophoneVolume(uint32* volume) const
{
	//*volume = Capturer.GetVolume();
	return 0;
}
int32 FPixelStreamingAudioDeviceModule::MaxMicrophoneVolume(uint32* maxVolume) const
{
	*maxVolume = UE::PixelStreaming::FAudioSubmixCapturer::MaxVolumeLevel;
	return 0;
}
int32 FPixelStreamingAudioDeviceModule::MinMicrophoneVolume(uint32* minVolume) const
{
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::SpeakerMuteIsAvailable(bool* available)
{
	return -1;
}
int32 FPixelStreamingAudioDeviceModule::SetSpeakerMute(bool enable)
{
	return -1;
}
int32 FPixelStreamingAudioDeviceModule::SpeakerMute(bool* enabled) const
{
	return -1;
}

int32 FPixelStreamingAudioDeviceModule::MicrophoneMuteIsAvailable(bool* available)
{
	*available = false;
	return -1;
}
int32 FPixelStreamingAudioDeviceModule::SetMicrophoneMute(bool enable)
{
	return -1;
}
int32 FPixelStreamingAudioDeviceModule::MicrophoneMute(bool* enabled) const
{
	return -1;
}

int32 FPixelStreamingAudioDeviceModule::StereoPlayoutIsAvailable(bool* available) const
{
	CHECKinitialized_();
	*available = true;
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::SetStereoPlayout(bool enable)
{
	CHECKinitialized_();
	FString AudioChannelStr = enable ? TEXT("stereo") : TEXT("mono");
	UE_LOG(LogPixelStreaming, Verbose, TEXT("WebRTC has requested browser audio playout in UE be: %s"), *AudioChannelStr);
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::StereoPlayout(bool* enabled) const
{
	CHECKinitialized_();
	*enabled = true;
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::StereoRecordingIsAvailable(bool* available) const
{
	CHECKinitialized_();
	*available = true;
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::SetStereoRecording(bool enable)
{
	CHECKinitialized_();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::StereoRecording(bool* enabled) const
{
	CHECKinitialized_();
	*enabled = true;
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::PlayoutDelay(uint16* delayMS) const
{
	*delayMS = 0;
	return 0;
}

bool FPixelStreamingAudioDeviceModule::BuiltInAECIsAvailable() const
{
	return false;
}
bool FPixelStreamingAudioDeviceModule::BuiltInAGCIsAvailable() const
{
	return false;
}
bool FPixelStreamingAudioDeviceModule::BuiltInNSIsAvailable() const
{
	return false;
}

int32 FPixelStreamingAudioDeviceModule::EnableBuiltInAEC(bool enable)
{
	return -1;
}
int32 FPixelStreamingAudioDeviceModule::EnableBuiltInAGC(bool enable)
{
	return -1;
}
int32 FPixelStreamingAudioDeviceModule::EnableBuiltInNS(bool enable)
{
	return -1;
}