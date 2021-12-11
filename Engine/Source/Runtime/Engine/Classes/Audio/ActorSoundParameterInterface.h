// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioParameterControllerInterface.h"
#include "Containers/Array.h"
#include "GameFramework/Actor.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"

#include "ActorSoundParameterInterface.generated.h"


// Forward Declarations
class AActor;

/** Interface used to allow an actor to automatically populate any sounds with parameters */
class ENGINE_API IActorSoundParameterInterface : public IInterface
{
	GENERATED_IINTERFACE_BODY()

public:

	UFUNCTION(BlueprintImplementableEvent)
	void GetActorSoundParams(TArray<FAudioParameter>& Params) const;
};

UINTERFACE(BlueprintType)
class ENGINE_API UActorSoundParameterInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()

public:

	static void Fill(const AActor* OwningActor, TArray<FAudioParameter>& OutParams);
};
