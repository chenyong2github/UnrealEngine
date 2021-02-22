// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "ContextualAnimEdModeSettings.generated.h"

class AActor;
class ACharacter;
class UContextualAnimSceneAsset;

UCLASS()
class UContextualAnimEdModeSettings : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Settings")
	UContextualAnimSceneAsset* SceneAsset;

	UContextualAnimEdModeSettings(const FObjectInitializer& ObjectInitializer);
};