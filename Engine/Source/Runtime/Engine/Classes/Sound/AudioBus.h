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
	Mono = 0,
	Stereo = 1,
	Quad = 3,
	FivePointOne = 5,
	SevenPointOne = 7
};

// Function to retrieve an audio bus buffer given a handle
// static float* GetAudioBusBuffer(const FAudioBusHandle& AudioBusHandle);

// An audio bus is an object which represents an audio patch cord. Audio can be sent to it. It can be sonified using USoundSourceBuses.
// Instances of the audio bus are created in the audio engine. 
UCLASS(ClassGroup = Sound, meta = (BlueprintSpawnableComponent))
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

	// Returns the number of channels of the audio bus in integer format
	int32 GetNumChannels() const { return (int32)AudioBusChannels + 1; }

	//~ Begin UObject Interface.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface.
};
