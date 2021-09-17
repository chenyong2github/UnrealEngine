// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingAudioComponent.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingPrivate.h"
#include "CoreMinimal.h"


UPixelStreamingAudioComponent::UPixelStreamingAudioComponent(const FObjectInitializer& ObjectInitializer) 
    :   Super(ObjectInitializer)
    ,   PlayerToHear(FPlayerId())
    ,   bAutoFindPeer(true)
    ,   Buffer()
    ,   bIsListeningToPeer(false)
    ,   bComponentWantsAudio(false)
    ,   AudioSink(nullptr)
    ,   CriticalSection()
    ,   SampleRate(16000)
    {

        bool bPixelStreamingLoaded = IPixelStreamingModule::IsAvailable();

        // Only output this warning if we are actually running this component (not in commandlet).
        if(!bPixelStreamingLoaded && !IsRunningCommandlet())
        {
            UE_LOG(PixelStreamer, Warning, TEXT("Pixel Streaming audio component will not tick because Pixel Streaming module is not loaded. This is expected on dedicated servers."));
        }

        //this->NumChannels = 2; //2 channels seem to cause problems
        this->PrimaryComponentTick.bCanEverTick = bPixelStreamingLoaded;
        this->SetComponentTickEnabled(bPixelStreamingLoaded);
        this->bAutoActivate = true;
    };

int32 UPixelStreamingAudioComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
    // Not listening to peer, return zero'd buffer.
    if(!this->bIsListeningToPeer)
    {
        return 0;
    }

    // Critical section
    {
        FScopeLock Lock(&CriticalSection);

        int32 NumSamplesToCopy = FGenericPlatformMath::Min(NumSamples, this->Buffer.Num());

        // Not enough samples to copy anything across
        if(NumSamplesToCopy <= 0)
        {
            return 0;
        }

        // Copy from local buffer into OutAudio if we have enough samples
        for(int SampleIndex = 0; SampleIndex < NumSamplesToCopy; SampleIndex++)
        {
            // Convert from int16 to float audio
            *OutAudio = ((float)this->Buffer[SampleIndex]) / 32767.0f;
            OutAudio++;
        }

        // Remove front NumSamples from the local buffer
        this->Buffer.RemoveAt(0, NumSamplesToCopy, false);
        return NumSamplesToCopy;
    }

}

bool UPixelStreamingAudioComponent::UpdateChannelsAndSampleRate(int InNumChannels, int InSampleRate)
{

    if(InNumChannels != this->NumChannels || InSampleRate != this->SampleRate)
    {
        
        // Critical Section - empty buffer because sample rate/num channels changed
        FScopeLock Lock(&CriticalSection);
        this->Buffer.Empty();

        //this is the smallest buffer size we can set without triggering internal checks to fire
        this->PreferredBufferLength = FGenericPlatformMath::Max(512, InSampleRate * InNumChannels / 100); 
        this->NumChannels = InNumChannels;
        this->SampleRate = InSampleRate;
        this->Initialize(this->SampleRate);
        
        return true;
    }

    return false;
}

void UPixelStreamingAudioComponent::OnBeginGenerate()
{
    this->bComponentWantsAudio = true;
}

void UPixelStreamingAudioComponent::OnEndGenerate()
{
    this->bComponentWantsAudio = false;
}

void UPixelStreamingAudioComponent::BeginDestroy()
{
    Super::BeginDestroy();
    this->Reset();
}

bool UPixelStreamingAudioComponent::ListenTo(FString PlayerToListenTo)
{

    if(!IPixelStreamingModule::IsAvailable())
    {
        UE_LOG(PixelStreamer, Verbose, TEXT("Pixel Streaming audio component could not listen to anything because Pixel Streaming module is not loaded. This is expected on dedicated servers."));
        return false;
    }

    IPixelStreamingModule& PixelStreamingModule = IPixelStreamingModule::Get();

    this->PlayerToHear = PlayerToListenTo;

    IPixelStreamingAudioSink* CandidateSink = this->WillListenToAnyPlayer() ? 
        PixelStreamingModule.GetUnlistenedAudioSink() : PixelStreamingModule.GetPeerAudioSink( FPlayerId(this->PlayerToHear) );
    
    if(CandidateSink == nullptr)
    {
        return false;
    }

    this->AudioSink = CandidateSink;
    this->AudioSink->AddAudioConsumer(this);

    return true;
}

void UPixelStreamingAudioComponent::Reset()
{
    this->PlayerToHear = FString();
    this->bIsListeningToPeer = false;
    if(this->AudioSink)
    {
        this->AudioSink->RemoveAudioConsumer(this);
    }
    this->AudioSink = nullptr;
    
    // Critical section
    {
        FScopeLock Lock(&CriticalSection);
        this->Buffer.Empty();
    }
    
}

bool UPixelStreamingAudioComponent::IsListeningToPlayer()
{
    return this->bIsListeningToPeer;
}

bool UPixelStreamingAudioComponent::WillListenToAnyPlayer()
{
    return this->PlayerToHear == FString();
}

void UPixelStreamingAudioComponent::ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames)
{

   if(!this->bComponentWantsAudio)
   {
       return;
   }

    // Trigger a latent update for channels and sample rate, if required
    bool bSampleRateOrChannelMismatch = this->UpdateChannelsAndSampleRate(NChannels, InSampleRate);

    // If there was a mismatch then skip this incoming audio until this the samplerate/channels update is completed on the gamethread 
    if(bSampleRateOrChannelMismatch)
    {
        return;
    }

    // Copy into our local TArray<int16_t> Buffer;
    int NSamples = NFrames * NChannels;
    
    // Critical Section
    {
        FScopeLock Lock(&CriticalSection);
        this->Buffer.Append(AudioData, NSamples);
        checkf( (uint32)this->Buffer.Num() < this->SampleRate, 
        TEXT("Pixel Streaming Audio Component internal buffer is getting too big, for some reason OnGenerateAudio is not consuming samples quickly enough."))
    }
    
}

void UPixelStreamingAudioComponent::OnConsumerAdded()
{
    this->bIsListeningToPeer = true;
}

void UPixelStreamingAudioComponent::OnConsumerRemoved()
{
    this->Reset();
}

void UPixelStreamingAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{

    // if auto connect turned off don't bother
    if(!this->bAutoFindPeer)
    {
        return;
    }

    // if listening to a peer don't auto connect
    if(this->bIsListeningToPeer)
    {
        return;
    }

    if(this->ListenTo(this->PlayerToHear))
    {
        UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming audio component found a WebRTC peer to listen to."));
    }
}
