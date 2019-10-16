// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundControlBus.h"
#include "SoundModulatorBase.h"
#include "SoundModulationValue.h"

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
	UPROPERTY(EditAnywhere, Category = Channel, BlueprintReadOnly)
	USoundControlBusBase* Bus;

	/* Value mix is set to. */
	UPROPERTY(EditAnywhere, Category = Channel, BlueprintReadWrite, meta = (ShowOnlyInnerProperties))
	FSoundModulationValue Value;
};

UCLASS(config = Engine, autoexpandcategories = (Channel, Mix), editinlinenew, BlueprintType, MinimalAPI)
class USoundControlBusMix : public USoundModulatorBase
{
	GENERATED_UCLASS_BODY()

public:

	virtual void BeginDestroy() override;

	/* Array of channels controlled by mix. */
	UPROPERTY(EditAnywhere, Category = Mix, BlueprintReadOnly)
	TArray<FSoundControlBusMixChannel> Channels;
};
