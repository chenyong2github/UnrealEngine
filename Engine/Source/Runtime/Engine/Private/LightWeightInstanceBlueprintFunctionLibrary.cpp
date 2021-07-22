// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/LightWeightInstanceBlueprintFunctionLibrary.h"
#include "GameFramework/LightWeightInstanceSubsystem.h"
#include "GameFramework/LightWeightInstanceManager.h"

FActorInstanceHandle ULightWeightInstanceBlueprintFunctionLibrary::CreateNewLightWeightInstance(UClass* InActorClass, FTransform InTransform, UDataLayer* InLayer, UWorld* World)
{
	// set up initialization data
	FLWIData PerInstanceData;
	PerInstanceData.Transform = InTransform;

	return FLightWeightInstanceSubsystem::Get().CreateNewLightWeightInstance(InActorClass, &PerInstanceData, InLayer, World);
}

FActorInstanceHandle ULightWeightInstanceBlueprintFunctionLibrary::ConvertActorToLightWeightInstance(AActor* InActor)
{
	if (!ensure(InActor))
	{
		return FActorInstanceHandle();
	}

	// Get or create a light weight instance for this class and layer
	// use the first layer the actor is in if it's in multiple layers
#if WITH_EDITOR
	TArray<const UDataLayer*> DataLayers = InActor->GetDataLayerObjects();
	const UDataLayer* Layer = DataLayers.Num() > 0 ? DataLayers[0] : nullptr;
#else
	const UDataLayer* Layer = nullptr;
#endif // WITH_EDITOR
	if (ALightWeightInstanceManager* LWIManager = FLightWeightInstanceSubsystem::Get().FindOrAddLightWeightInstanceManager(InActor->GetClass(), Layer, InActor->GetWorld()))
	{
		return LWIManager->ConvertActorToLightWeightInstance(InActor);
	}

	return FActorInstanceHandle(InActor);
}
