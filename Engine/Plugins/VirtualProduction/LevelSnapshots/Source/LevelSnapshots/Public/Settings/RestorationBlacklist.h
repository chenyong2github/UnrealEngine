// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "RestorationBlacklist.generated.h"

USTRUCT()
struct LEVELSNAPSHOTS_API FRestorationBlacklist
{
	GENERATED_BODY()

	/* These actor classes are not allowed. Child classes are included. */
	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TSet<TSubclassOf<AActor>> ActorClasses;

	/* These component classes are not allowed. Child classes are included. */
	UPROPERTY(EditAnywhere, Category = "Level Snapshots")
	TSet<TSubclassOf<UActorComponent>> ComponentClasses;
};