// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingAudioDeviceModule.h"
#include "AudioMixerDevice.h"
#include "SampleBuffer.h"
#include "Engine/GameEngine.h"
#include "PixelStreamingSettings.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPixelStreamingAudioDeviceModule, Log, All);
DEFINE_LOG_CATEGORY(LogPixelStreamingAudioDeviceModule);

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

int32 FPixelStreamingAudioDeviceModule::ActiveAudioLayer(AudioLayer* audioLayer) const
{
	*audioLayer = AudioDeviceModule::kDummyAudio;
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::RegisterAudioCallback(webrtc::AudioTransport* audioCallback)
{
	this->Requester.RegisterAudioCallback(audioCallback);
	this->Capturer.RegisterAudioCallback(audioCallback);
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::Init()
{
	this->InitRecording();
	
	UE_LOG(LogPixelStreamingAudioDeviceModule, Verbose, TEXT("Init PixelStreamingAudioDeviceModule"));
	this->bInitialized = true;
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::Terminate()
{
	if (!bInitialized)
	{
		return 0;
	}

	this->Requester.Uninitialise();
	this->Capturer.Uninitialise();

	UE_LOG(LogPixelStreamingAudioDeviceModule, Verbose, TEXT("Terminate"));

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
	*available = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableReceiveAudio.GetValueOnAnyThread();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::InitPlayout()
{
	CHECKinitialized_();
	bool bIsPlayoutAvailable = false;
	this->PlayoutIsAvailable(&bIsPlayoutAvailable);
	if(!bIsPlayoutAvailable)
	{
		return -1;
	}
	
	this->Requester.InitPlayout();
	return 0;
}

bool FPixelStreamingAudioDeviceModule::PlayoutIsInitialized() const
{
	CHECKinitialized__BOOL();
	return this->Requester.PlayoutIsInitialized();
}

int32 FPixelStreamingAudioDeviceModule::StartPlayout()
{
	CHECKinitialized_();

	if(!this->Requester.PlayoutIsInitialized())
	{
		return -1;
	}

	this->Requester.StartPlayout();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::StopPlayout()
{
	CHECKinitialized_();
	if(!this->Requester.PlayoutIsInitialized())
	{
		return -1;
	}

	this->Requester.StopPlayout();
	return 0;
}

bool FPixelStreamingAudioDeviceModule::Playing() const
{
	CHECKinitialized__BOOL();
	return this->Requester.Playing();
}

int32 FPixelStreamingAudioDeviceModule::RecordingIsAvailable(bool* available)
{
	CHECKinitialized_();
	*available = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread();
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::InitRecording()
{
	CHECKinitialized_();

	bool bIsRecordingAvailable = false;
	this->RecordingIsAvailable(&bIsRecordingAvailable);
	if(!bIsRecordingAvailable)
	{
		return -1;
	}

	if(!this->Capturer.IsInitialised())
	{
		this->Capturer.Init();
	}

	return 0;
}

bool FPixelStreamingAudioDeviceModule::RecordingIsInitialized() const
{
	CHECKinitialized__BOOL();
	return this->Capturer.IsInitialised();
}

int32 FPixelStreamingAudioDeviceModule::StartRecording()
{
	CHECKinitialized_();
	if(!this->Capturer.IsCapturing())
	{
		this->Capturer.StartCapturing();
	}
	return 0;
}

int32 FPixelStreamingAudioDeviceModule::StopRecording()
{
	CHECKinitialized_();
	if(this->Capturer.IsCapturing())
	{
		this->Capturer.EndCapturing();
	}
	return 0;
}

bool FPixelStreamingAudioDeviceModule::Recording() const
{
	CHECKinitialized__BOOL();
	return this->Capturer.IsCapturing();
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
	UE_LOG(LogPixelStreamingAudioDeviceModule, Verbose, TEXT("WebRTC has requested browser audio playout in UE be: %s"), *AudioChannelStr);
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

//--FSubmixCapturer--

uint32_t FSubmixCapturer::GetVolume() const
{
	return this->VolumeLevel;
}

void FSubmixCapturer::SetVolume(uint32_t NewVolume)
{
	this->VolumeLevel = NewVolume;
}

bool FSubmixCapturer::Init()
{
	FScopeLock Lock(&CriticalSection);

	// subscribe to audio data
	if (!GEngine)
	{
		this->bInitialised = false;
		return false;
	}

	// already initialised
	if(this->bInitialised)
	{
		return true;
	}

	FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
	if (!AudioDevice)
	{
		UE_LOG(LogPixelStreamingAudioDeviceModule, Warning, TEXT("No audio device"));
		this->bInitialised = false;
		return false;
	}

	AudioDevice->RegisterSubmixBufferListener(this);
	this->bInitialised = true;
	return true;
}

void FSubmixCapturer::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
{
	FScopeLock Lock(&CriticalSection);

	if (!this->bInitialised || !this->bCapturing)
	{
		return;
	}

	// No point doing anything with UE audio if the callback from WebRTC has not been set yet.
	if(this->AudioCallback == nullptr)
	{
		return;
	}

	// Check if the sample rate from UE matches our target sample rate
	if (this->TargetSampleRate != SampleRate)
	{
		// Only report the problem once
		if (!this->bReportedSampleRateMismatch)
		{
			this->bReportedSampleRateMismatch = true;
			UE_LOG(PixelStreaming, Error, TEXT("Audio sample rate mismatch. Expected: %d | Actual: %d"), this->TargetSampleRate, SampleRate);
		}
		return;
	}

	UE_LOG(LogPixelStreamingAudioDeviceModule, VeryVerbose, TEXT("captured %d samples, %dc, %dHz"), NumSamples, NumChannels, SampleRate);

	// Note: TSampleBuffer takes in AudioData as float* and internally converts to int16
	Audio::TSampleBuffer<int16> Buffer(AudioData, NumSamples, NumChannels, SampleRate);
	
	// Mix to our target number of channels if the source does not already match.
	if (Buffer.GetNumChannels() != this->TargetNumChannels)
	{
		Buffer.MixBufferToChannels(this->TargetNumChannels);
	}

	RecordingBuffer.Append(Buffer.GetData(), Buffer.GetNumSamples());
	
	const float ChunkDurationSecs = 0.01f; //10ms
	const int32 SamplesPer10Ms = this->GetSamplesPerDurationSecs(ChunkDurationSecs);
	
	// Feed in 10ms chunks
	while(RecordingBuffer.Num() > SamplesPer10Ms)
	{

		// Extract a 10ms chunk of samples from recording buffer
		TArray<int16_t> SubmitBuffer(RecordingBuffer.GetData(), SamplesPer10Ms);
		const size_t frames = SubmitBuffer.Num() / this->TargetNumChannels;
  		const size_t bytes_per_frame = this->TargetNumChannels * sizeof(int16_t);

		uint32_t OutMicLevel = this->VolumeLevel;

		int32_t WebRTCRes = this->AudioCallback->RecordedDataIsAvailable(
			SubmitBuffer.GetData(), 
			frames, 
			bytes_per_frame, 
			this->TargetNumChannels, 
			this->TargetSampleRate, 
			0, 
			0, 
			this->VolumeLevel, 
			false, 
			OutMicLevel);

		this->SetVolume(OutMicLevel);

		// Remove 10ms of samples from the recording buffer now it is submitted
		RecordingBuffer.RemoveAt(0, SamplesPer10Ms, false);
	}

}

int32 FSubmixCapturer::GetSamplesPerDurationSecs(float InSeconds) const
{
	int32 SamplesPerSecond = this->TargetNumChannels * this->TargetSampleRate;
	int32 NumSamplesPerDuration = (int32) (SamplesPerSecond * InSeconds);
	return NumSamplesPerDuration;
}

void FSubmixCapturer::RegisterAudioCallback(webrtc::AudioTransport* AudioTransportCallback)
{
	FScopeLock Lock(&CriticalSection);
	this->AudioCallback = AudioTransportCallback;
}

bool FSubmixCapturer::IsInitialised() const
{
	return this->bInitialised;
}

void FSubmixCapturer::Uninitialise()
{
	FScopeLock Lock(&CriticalSection);

	if (GEngine)
	{
		FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
		if (AudioDevice)
		{
			AudioDevice->UnregisterSubmixBufferListener(this);
		}
	}

	this->RecordingBuffer.Empty();
	this->bInitialised = false;
	this->bCapturing = false;
}

bool FSubmixCapturer::StartCapturing()
{
	if(!this->bInitialised)
	{
		return false;
	}
	this->bCapturing = true;
	return true;
}

bool FSubmixCapturer::EndCapturing()
{
	if(!this->bInitialised)
	{
		return false;
	}
	this->bCapturing = false;
	return true;
}

bool FSubmixCapturer::IsCapturing() const
{
	return this->bCapturing;
}

//--FAudioPlayoutRequester--

void FAudioPlayoutRequester::Uninitialise()
{
	this->StopPlayout();
	this->AudioCallback = nullptr;
	this->bIsPlayoutInitialised = false;
	this->PlayoutBuffer.Empty();
}

void FAudioPlayoutRequester::InitPlayout()
{
	this->bIsPlayoutInitialised = true;
}

void FAudioPlayoutRequester::StartPlayout()
{
	if(this->PlayoutIsInitialized() && !this->Playing())
	{
		TFunction<void()> RequesterFunc = [this](){
			
			FScopeLock Lock(&this->PlayoutCriticalSection);

			
			// Only request audio if audio callback is valid
			if(!this->AudioCallback)
			{
				return;
			}
			
			// Our intention is to request samples at some fixed interval (i.e 10ms)
			int32 NSamplesPerChannel = (this->SampleRate) / (1000 / FAudioPlayoutRequester::RequestIntervalMs);
			const size_t BytesPerFrame = this->NumChannels * sizeof(int16_t);
			
			// Ensure buffer has the total number of samples we need for all audio frames
			PlayoutBuffer.Reserve(NSamplesPerChannel * this->NumChannels);
			
			size_t OutNSamples = 0;
			int64_t ElapsedTimeMs = -1;
  			int64_t NtpTimeMs = -1;

			// Note this is mixed result of all audio sources, which in turn triggers the sinks for each audio source to be called.
			// For example, if you had 3 audio sources in the browser sending 16kHz mono they would all be mixed down into the number
			// of channels and sample rate specified below. However, for listening to each audio source prior to mixing refer to
			// FPixelStreamingAudioSink.
			uint32_t Result = this->AudioCallback->NeedMorePlayData(
				NSamplesPerChannel, 
				BytesPerFrame, 
				this->NumChannels, 
				this->SampleRate, 
				PlayoutBuffer.GetData(), 
				OutNSamples, 
				&ElapsedTimeMs, 
				&NtpTimeMs);

			if(Result != 0)
			{
				UE_LOG(LogPixelStreamingAudioDeviceModule, Error, TEXT("NeedMorePlayData return non-zero result indicating an error"));
			}
			
		};

		this->RequesterThread.Reset(nullptr);
		this->RequesterRunnable = MakeUnique<FAudioPlayoutRequester::Runnable>(RequesterFunc);
		this->RequesterThread.Reset(FRunnableThread::Create(this->RequesterRunnable.Get(), TEXT("Pixel Streaming WebRTC Audio Requester")));
		this->bIsPlaying = true;
	}
}

void FAudioPlayoutRequester::StopPlayout()
{
	if(this->PlayoutIsInitialized() && this->Playing())
	{
		this->RequesterRunnable->Stop();
		this->bIsPlaying = false;
	}
}

bool FAudioPlayoutRequester::Playing() const
{
	return this->bIsPlaying;
}

bool FAudioPlayoutRequester::PlayoutIsInitialized() const
{
	return this->bIsPlayoutInitialised;
}

void FAudioPlayoutRequester::RegisterAudioCallback(webrtc::AudioTransport* AudioCb)
{
	FScopeLock Lock(&this->PlayoutCriticalSection);
	this->AudioCallback = AudioCb;
}

//--FAudioRequester::Runnable--

bool FAudioPlayoutRequester::Runnable::Init()
{
	return this->RequestPlayoutFunc != nullptr;
}

uint32 FAudioPlayoutRequester::Runnable::Run()
{
	this->LastAudioRequestTimeMs = rtc::TimeMillis();
	this->bIsRunning = true;

	// Request audio in a loop until this boolean is toggled off.
	while(this->bIsRunning)
	{
		int64_t Now = rtc::TimeMillis();
		int64_t DeltaMs = Now - this->LastAudioRequestTimeMs;

		// Check if the 10ms delta has elapsed, if it has not, then sleep the remaining
		if(DeltaMs < FAudioPlayoutRequester::RequestIntervalMs)
		{
			int64_t SleepTimeMs = FAudioPlayoutRequester::RequestIntervalMs - DeltaMs;
			float SleepTimeSecs = (float)SleepTimeMs / 1000.0f;
			FPlatformProcess::Sleep(SleepTimeSecs);
			continue;
		}

		// Update request time to now seeing as the 10ms delta has elapsed
		this->LastAudioRequestTimeMs = Now;

		// Actually request playout
		this->RequestPlayoutFunc();

	}
	
	return 0;
}

void FAudioPlayoutRequester::Runnable::Stop()
{
	this->bIsRunning = false;
}

void FAudioPlayoutRequester::Runnable::Exit()
{
	this->bIsRunning = false;
	this->RequestPlayoutFunc = nullptr;
}