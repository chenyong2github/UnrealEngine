// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioModulation.h"
#include "SoundControlBus.h"
#include "SoundModulationValue.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "SoundControlBusMix.generated.h"


// Forward Declarations
class USoundControlBusBase;


USTRUCT(BlueprintType)
struct FSoundControlBusMixChannel
{
	GENERATED_USTRUCT_BODY()

	FSoundControlBusMixChannel();
	FSoundControlBusMixChannel(USoundControlBusBase* InBus, const float TargetValue);

	/* Bus controlled by channel. */
	UPROPERTY(EditAnywhere, Category = Channel, BlueprintReadWrite)
	USoundControlBusBase* Bus;

	/* Value mix is set to. */
	UPROPERTY(EditAnywhere, Category = Channel, BlueprintReadWrite, meta = (ShowOnlyInnerProperties))
	FSoundModulationValue Value;
};

UCLASS(config = Engine, autoexpandcategories = (Channel, Mix), editinlinenew, BlueprintType, MinimalAPI)
class USoundControlBusMix : public UObject
{
	GENERATED_UCLASS_BODY()

protected:
#if WITH_EDITOR
	// Loads the mix from the provided profile index
	UFUNCTION(Category = Mix, meta = (CallInEditor = "true"))
	void LoadMixFromProfile();

	// Saves the mix to the provided profile index
	UFUNCTION(Category = Mix, meta = (CallInEditor = "true"))
	void SaveMixToProfile();

	// Solos this mix, deactivating all others and activating this 
	// (if its not already active) while testing in-editor in all
	// active worlds
	UFUNCTION(Category = Mix, meta = (CallInEditor = "true"))
	void SoloMix();

	// Deactivates this mix while testing in-editor in all active worlds
	UFUNCTION(Category = Mix, meta = (CallInEditor = "true"))
	void ActivateMix();

	// Deactivates this mix while testing in-editor in all active worlds
	UFUNCTION(Category = Mix, meta = (CallInEditor = "true"))
	void DeactivateMix();

	// Deactivates all mixes while testing in-editor in all active worlds
	UFUNCTION(Category = Mix, meta = (CallInEditor = "true"))
	void DeactivateAllMixes();
#endif // WITH_EDITOR

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Transient, Category = Mix)
	uint32 ProfileIndex;
#endif // WITH_EDITORONLY_DATA

	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	/* Array of channels controlled by mix. */
	UPROPERTY(EditAnywhere, Category = Mix, BlueprintReadOnly)
	TArray<FSoundControlBusMixChannel> Channels;
};
