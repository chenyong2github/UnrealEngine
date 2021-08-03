// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ActorFactories/ActorFactory.h"
#include "WaterMeshActorFactory.generated.h"

UCLASS(MinimalAPI, config = Editor)
class UWaterMeshActorFactory : public UActorFactory
{
	GENERATED_UCLASS_BODY()

	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
};