// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"

#include "ChaosVDEditorSettings.generated.h"

UCLASS(config = Engine)
class UChaosVDEditorSettings : public UObject
{
	GENERATED_BODY()
public:

	UPROPERTY(Config, EditAnywhere, Category = Debug)
	TSoftObjectPtr<UStaticMesh> DebugMesh;

	UPROPERTY(Config, EditAnywhere, Category = Debug)
	TSoftObjectPtr<UWorld> BasePhysicsVDWorld;
};