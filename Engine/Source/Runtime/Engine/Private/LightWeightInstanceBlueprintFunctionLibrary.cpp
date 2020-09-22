// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/LightWeightInstanceBlueprintFunctionLibrary.h"
#include "GameFramework/LightWeightInstanceSubsystem.h"
#include "GameFramework/LightWeightInstanceManager.h"

FActorInstanceHandle ULightWeightInstanceBlueprintFunctionLibrary::CreateNewLightWeightInstance(UClass* InActorClass, FTransform InTransform, ULevel* InLevel)
{
	// Get or create a light weight instance for this class and level
	if (ALightWeightInstanceManager* LightWeightInstance = FLightWeightInstanceSubsystem::Get().FindOrAddLightWeightInstanceManager(InActorClass, InLevel))
	{
		// create an instance with the given transform
		FLWIData PerInstanceData;
		PerInstanceData.Transform = InTransform;
		int32 InstanceIdx = LightWeightInstance->AddNewInstance(&PerInstanceData);

		return FActorInstanceHandle(LightWeightInstance, InstanceIdx);
	}

	return FActorInstanceHandle();
}
