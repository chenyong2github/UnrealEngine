// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldBuildingCharacterMovement.h"
#include "WorldBuildingPawn.h"

//----------------------------------------------------------------------//
// UWorldBuildingCharacterMovement
//----------------------------------------------------------------------//
UWorldBuildingCharacterMovement::UWorldBuildingCharacterMovement(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


float UWorldBuildingCharacterMovement::GetMaxSpeed() const
{
	float MaxSpeed = Super::GetMaxSpeed();

	const AWorldBuildingPawn* Pawn = Cast<AWorldBuildingPawn>(PawnOwner);
	if (Pawn)
	{
		if (Pawn->IsRunning())
		{
			MaxSpeed *= Pawn->GetRunningSpeedModifier();
		}
	}

	return MaxSpeed;
}
