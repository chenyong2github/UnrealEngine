// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RadialBoxSettings.generated.h"

USTRUCT(BlueprintType)
struct FRadialBoxSettings
{
	GENERATED_BODY()

	/* Distribute Items evenly in the whole circle. Checking this option ignores AngleBetweenItems */
	UPROPERTY(EditAnywhere, Category = "Items")
	bool bDistributeItemsEvenly;

	/* Amount of Euler degrees that separate each item */
	UPROPERTY(EditAnywhere, Category = "Items")
	float AngleBetweenItems;

	/* At what angle will we place the first element of the wheel? */
	UPROPERTY(EditAnywhere, Category = "Items")
	float StartingAngle;

	FRadialBoxSettings()
		: bDistributeItemsEvenly(true)
		, AngleBetweenItems(0.f)
		, StartingAngle(0.f)
	{
	}
};