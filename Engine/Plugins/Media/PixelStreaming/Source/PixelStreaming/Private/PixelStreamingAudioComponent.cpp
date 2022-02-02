// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingAudioComponent.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingPrivate.h"
#include "CoreMinimal.h"

/*
* Component that recieves audio from a remote webrtc connection and outputs it into UE using a "synth component".
*/
UPixelStreamingAudioComponent::UPixelStreamingAudioComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PlayerToHear(FPixelStreamingPlayerId())
	, bAutoFindPeer(true)
	, AudioSink(nullptr)
	, SoundGenerator(MakeShared<FWebRTCSoundGenerator, ESPMode::ThreadSafe>())
{

	bool bPixelStreamingLoaded = IPixelStreamingModule::IsAvailable();

	// Only output this warning if we are actually running this component (not in commandlet).
	if (!bPixelStreamingLoaded && !IsRunningCommandlet())
	{
		UE_LOG(LogPixelStreaming, Warning, TEXT("Pixel Streaming audio component will not tick because Pixel Streaming module is not loaded. This is expected on dedicated servers."));
	}

	NumChannels = 1; //2 channels seem to cause problems
	PreferredBufferLength = 512u;
	PrimaryComponentTick.bCanEverTick = bPixelStreamingLoaded;
	SetComponentTickEnabled(bPixelStreamingLoaded);
	bAutoActivate = true;
};

ISoundGeneratorPtr UPixelStreamingAudioComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	SoundGenerator->SetParameters(InParams);
	return SoundGenerator;
}

void UPixelStreamingAudioComponent::OnBeginGenerate()
{
	SoundGenerator->bGeneratingAudio = true;
}

void UPixelStreamingAudioComponent::OnEndGenerate()
{
	SoundGenerator->bGeneratingAudio = false;
}

void UPixelStreamingAudioComponent::BeginDestroy()
{
	Super::BeginDestroy();
	Reset();
}

bool UPixelStreamingAudioComponent::ListenTo(FString PlayerToListenTo)
{

	if (!IPixelStreamingModule::IsAvailable())
	{
		UE_LOG(LogPixelStreaming, Verbose, TEXT("Pixel Streaming audio component could not listen to anything because Pixel Streaming module is not loaded. This is expected on dedicated servers."));
		return false;
	}

	IPixelStreamingModule& PixelStreamingModule = IPixelStreamingModule::Get();
	if (!PixelStreamingModule.IsReady())
	{
		return false;
	}

	PlayerToHear = PlayerToListenTo;

	IPixelStreamingAudioSink* CandidateSink = WillListenToAnyPlayer() ? PixelStreamingModule.GetUnlistenedAudioSink() : PixelStreamingModule.GetPeerAudioSink(FPixelStreamingPlayerId(PlayerToHear));

	if (CandidateSink == nullptr)
	{
		return false;
	}

	AudioSink = CandidateSink;
	AudioSink->AddAudioConsumer(this);

	return true;
}

void UPixelStreamingAudioComponent::Reset()
{
	PlayerToHear = FString();
	SoundGenerator->bShouldGenerateAudio = false;
	if (AudioSink)
	{
		AudioSink->RemoveAudioConsumer(this);
	}
	AudioSink = nullptr;
	SoundGenerator->EmptyBuffers();
}

bool UPixelStreamingAudioComponent::IsListeningToPlayer()
{
	return SoundGenerator->bShouldGenerateAudio;
}

bool UPixelStreamingAudioComponent::WillListenToAnyPlayer()
{
	return PlayerToHear == FString();
}

void UPixelStreamingAudioComponent::ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames)
{
	if (SoundGenerator->GetSampleRate() != InSampleRate || SoundGenerator->GetNumChannels() != NChannels)
	{
		SoundGenerator->UpdateChannelsAndSampleRate(NChannels, InSampleRate);

		//this is the smallest buffer size we can set without triggering internal checks to fire
		PreferredBufferLength = FGenericPlatformMath::Max(512.0f, InSampleRate * NChannels / 100.0f);

		NumChannels = NChannels;
		Initialize(InSampleRate);
	}
	else
	{
		SoundGenerator->AddAudio(AudioData, InSampleRate, NChannels, NFrames);
	}
}

void UPixelStreamingAudioComponent::OnConsumerAdded()
{
	SoundGenerator->bShouldGenerateAudio = true;
	Start();
}

void UPixelStreamingAudioComponent::OnConsumerRemoved()
{
	Reset();
}

void UPixelStreamingAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{

	// if auto connect turned off don't bother
	if (!bAutoFindPeer)
	{
		return;
	}

	// if listening to a peer don't auto connect
	if (IsListeningToPlayer())
	{
		return;
	}

	if (ListenTo(PlayerToHear))
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("PixelStreaming audio component found a WebRTC peer to listen to."));
	}
}

/*
* ---------------- FWebRTCSoundGenerator -------------------------
*/

FWebRTCSoundGenerator::FWebRTCSoundGenerator()
	: Params()
	, Buffer()
	, CriticalSection()
{
}

void FWebRTCSoundGenerator::SetParameters(const FSoundGeneratorInitParams& InitParams)
{
	Params = InitParams;
	UpdateChannelsAndSampleRate(Params.NumChannels, Params.SampleRate);
}

void FWebRTCSoundGenerator::EmptyBuffers()
{
	FScopeLock Lock(&CriticalSection);
	Buffer.Empty();
}

bool FWebRTCSoundGenerator::UpdateChannelsAndSampleRate(int InNumChannels, int InSampleRate)
{

	if (InNumChannels != Params.NumChannels || InSampleRate != Params.SampleRate)
	{

		// Critical Section - empty buffer because sample rate/num channels changed
		FScopeLock Lock(&CriticalSection);
		Buffer.Empty();

		Params.NumChannels = InNumChannels;
		Params.SampleRate = InSampleRate;

		return true;
	}

	return false;
}

void FWebRTCSoundGenerator::AddAudio(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames)
{
	if (!bGeneratingAudio)
	{
		return;
	}

	// Trigger a latent update for channels and sample rate, if required
	bool bSampleRateOrChannelMismatch = UpdateChannelsAndSampleRate(NChannels, InSampleRate);

	// If there was a mismatch then skip this incoming audio until this the samplerate/channels update is completed on the gamethread
	if (bSampleRateOrChannelMismatch)
	{
		return;
	}

	// Copy into our local TArray<int16_t> Buffer;
	int NSamples = NFrames * NChannels;

	// Critical Section
	{
		FScopeLock Lock(&CriticalSection);
		Buffer.Append(AudioData, NSamples);
		// checkf((uint32)Buffer.Num() < SampleRate,
		// 	TEXT("Pixel Streaming Audio Component internal buffer is getting too big, for some reason OnGenerateAudio is not consuming samples quickly enough."))
	}
}

// Called when a new buffer is required.
int32 FWebRTCSoundGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	// Not listening to peer, return zero'd buffer.
	if (!bShouldGenerateAudio || Buffer.Num() == 0)
	{
		return NumSamples;
	}

	// Critical section
	{
		FScopeLock Lock(&CriticalSection);

		int32 NumSamplesToCopy = FGenericPlatformMath::Min(NumSamples, Buffer.Num());

		// Copy from local buffer into OutAudio if we have enough samples
		for (int SampleIndex = 0; SampleIndex < NumSamplesToCopy; SampleIndex++)
		{
			// Convert from int16 to float audio
			*OutAudio = ((float)Buffer[SampleIndex]) / 32767.0f;
			OutAudio++;
		}

		// Remove front NumSamples from the local buffer
		Buffer.RemoveAt(0, NumSamplesToCopy, false);
		return NumSamplesToCopy;
	}
}
