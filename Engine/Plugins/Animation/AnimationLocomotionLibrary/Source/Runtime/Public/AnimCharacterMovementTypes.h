// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimCharacterMovementTypes.generated.h"

class UAnimSequence;

UENUM(BlueprintType)
enum class EAnimCardinalDirection : uint8
{
	North,
	East,
	South,
	West
};

/**
 * Animations for a locomotion set authored with only four cardinal directions.
 * This will often be accompanied by Orientation Warping to account for diagonals.
 */
USTRUCT(BlueprintType)
struct FCardinalDirectionAnimSet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimSequence> NorthAnim = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimSequence> EastAnim = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimSequence> SouthAnim = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimSequence> WestAnim = nullptr;
};