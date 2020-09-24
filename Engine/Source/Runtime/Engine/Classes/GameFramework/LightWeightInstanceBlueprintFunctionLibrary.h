// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineTypes.h"

#include "LightWeightInstanceBlueprintFunctionLibrary.generated.h"

UCLASS()
class ENGINE_API ULightWeightInstanceBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Light Weight Instance")
	static FActorInstanceHandle CreateNewLightWeightInstance(UClass* ActorClass, FTransform Transform, ULevel* Level);
};