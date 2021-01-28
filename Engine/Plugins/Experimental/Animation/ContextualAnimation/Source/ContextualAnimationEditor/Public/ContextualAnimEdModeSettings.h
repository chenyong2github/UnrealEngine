// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "ContextualAnimEdModeSettings.generated.h"

class ACharacter;

UCLASS()
class UContextualAnimEdModeSettings : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Settings")
	TSubclassOf<ACharacter> TestCharacterClass;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FName MotionWarpSyncPointName;

	UContextualAnimEdModeSettings(const FObjectInitializer& ObjectInitializer);
};