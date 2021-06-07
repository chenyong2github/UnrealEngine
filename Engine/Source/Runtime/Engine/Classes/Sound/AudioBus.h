// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "AudioBus.generated.h"

class FAudioDevice;

// The number of channels to mix audio into the source bus
UENUM(BlueprintType)
enum class EAudioBusChannels : uint8
{
	Mono,
	Stereo,
};

// Function to retrieve an audio bus buffer given a handle
// static float* GetAudioBusBuffer(const FAudioBusHandle& AudioBusHandle);

// An audio bus is an object which represents an audio patch chord. Audio can be sent to it. It can be sonified using USoundSourceBuses.
// Instances of the audio bus are created in the audio engine. 
UCLASS(hidecategories= (Compression, SoundWave, Streaming, Subtitles, Sound, Info, ImportSettings), ClassGroup = Sound, meta = (BlueprintSpawnableComponent))
class ENGINE_API UAudioBus : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	/** Number of channels to use for the Audio Bus. */
	UPROPERTY(EditAnywhere, Category = BusProperties)
	EAudioBusChannels AudioBusChannels;

	//~ Begin UObject
	virtual void BeginDestroy() override;
	//~ End UObject

	//~ Begin UObject Interface.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface.
};
