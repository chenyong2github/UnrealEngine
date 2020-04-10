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
	UFUNCTION(Category = Mix, meta = (CallInEditor = "true"))
	void LoadMixFromProfile();

	UFUNCTION(Category = Mix, meta = (CallInEditor = "true"))
	void SaveMixToProfile();
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
