// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassCommonTypes.h"

#include "MassMovementFragments.generated.h"

USTRUCT()
struct MASSMOVEMENT_API FMassVelocityFragment : public FLWComponentData
{
	GENERATED_BODY()

	FVector Value = FVector::ZeroVector;
};
