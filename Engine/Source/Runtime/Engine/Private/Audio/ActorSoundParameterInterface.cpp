// Copyright Epic Games, Inc. All Rights Reserved.
#include "Audio/ActorSoundParameterInterface.h"
#include "GameFramework/Actor.h"


UActorSoundParameterInterface::UActorSoundParameterInterface(FObjectInitializer const& InObjectInitializer)
	: Super(InObjectInitializer)
{
}

void UActorSoundParameterInterface::Fill(const AActor* OwningActor, TArray<FAudioParameter>& OutParams)
{
	// Retrieve additional params from the actor if they implement a special interface
	if (OwningActor && OwningActor->Implements<UActorSoundParameterInterface>())
	{
		TArray<FAudioParameter> ActorAudioParams;
		IActorSoundParameterInterface::Execute_GetActorSoundParams(OwningActor, ActorAudioParams);

		OutParams.Append(ActorAudioParams);
	}
}
