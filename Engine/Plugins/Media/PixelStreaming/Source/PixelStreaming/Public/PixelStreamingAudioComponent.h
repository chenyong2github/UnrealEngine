// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlayerId.h"
#include "Components/SynthComponent.h"
#include "IPixelStreamingAudioConsumer.h"
#include "IPixelStreamingAudioSink.h"
#include "PixelStreamingAudioComponent.generated.h"


/**
 * Allows in-engine playback of incoming WebRTC audio from a particular Pixel Streaming player/peer using their mic in the browser.
 * Note: Each audio component associates itself with a particular Pixel Streaming player/peer (using the the Pixel Streaming player id).
 */
UCLASS(Blueprintable, ClassGroup = (PixelStreamer), meta = (BlueprintSpawnableComponent))
class PIXELSTREAMING_API UPixelStreamingAudioComponent : public USynthComponent, public IPixelStreamingAudioConsumer
{
	GENERATED_BODY()

    protected:

        UPixelStreamingAudioComponent(const FObjectInitializer& ObjectInitializer);

        //~ Begin USynthComponent interface
        virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
        virtual void OnBeginGenerate() override;
        virtual void OnEndGenerate() override;
        //~ End USynthComponent interface

        //~ Begin UObject interface
        virtual void BeginDestroy() override;
        //~ End UObject interface

        //~ Begin UActorComponent interface
        virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
        //~ End UActorComponent interface

        bool UpdateChannelsAndSampleRate(int InNumChannels, int InSampleRate);

    public:

        /** 
        *   The Pixel Streaming player/peer whose audio we wish to listen to.
        *   If this is left blank this component will listen to the first non-listened to peer that connects after this component is ready.
        *   Note: that when the listened to peer disconnects this component is reset to blank and will once again listen to the next non-listened to peer that connects.
        */
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pixel Streaming Audio Component")
        FString PlayerToHear;

        /**
         *  If not already listening to a player/peer will try to attach for listening to the "PlayerToHear" each tick.
         */
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pixel Streaming Audio Component")
        bool bAutoFindPeer;

    private:

        TArray<int16_t> Buffer;
        bool bIsListeningToPeer;
        bool bComponentWantsAudio;
        IPixelStreamingAudioSink* AudioSink;
        FCriticalSection CriticalSection;
        uint32 SampleRate;

    public:

        // Listen to a specific player. If the player is not found this component will be silent.
        UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Audio Component")
        bool ListenTo(FString PlayerToListenTo);

        // True if listening to a connected WebRTC peer through Pixel Streaming.
        UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Audio Component")
        bool IsListeningToPlayer();

        bool WillListenToAnyPlayer();

        // Stops listening to any connected player/peer and resets internal state so component is ready to listen again.
        UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Audio Component")
        void Reset();

        //~ Begin IPixelStreamingAudioConsumer interface
        void ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames);
        void OnConsumerAdded();
        void OnConsumerRemoved();
        //~ End IPixelStreamingAudioConsumer interface

};