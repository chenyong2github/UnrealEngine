// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/LightWeightInstanceBlueprintFunctionLibrary.h"
#include "GameFramework/LightWeightInstanceSubsystem.h"
#include "GameFramework/LightWeightInstanceManager.h"

FActorInstanceHandle ULightWeightInstanceBlueprintFunctionLibrary::CreateNewLightWeightInstance(UClass* InActorClass, FTransform InTransform, ULevel* InLevel)
{
	// set up initialization data
	FLWIData PerInstanceData;
	PerInstanceData.Transform = InTransform;

	return FLightWeightInstanceSubsystem::Get().CreateNewLightWeightInstance(InActorClass, &PerInstanceData, InLevel);
}

FActorInstanceHandle ULightWeightInstanceBlueprintFunctionLibrary::ConvertActorToLightWeightInstance(AActor* InActor)
{
	if (!ensure(InActor))
	{
		return FActorInstanceHandle();
	}

	// Get or create a light weight instance for this class and level
	if (ALightWeightInstanceManager* LWIManager = FLightWeightInstanceSubsystem::Get().FindOrAddLightWeightInstanceManager(InActor->GetClass(), InActor->GetLevel()))
	{
		return LWIManager->ConvertActorToLightWeightInstance(InActor);
	}

	return FActorInstanceHandle(InActor);
}
