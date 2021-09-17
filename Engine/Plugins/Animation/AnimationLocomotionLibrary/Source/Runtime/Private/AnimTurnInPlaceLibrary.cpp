// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimTurnInPlaceLibrary.h"

void UAnimTurnInPlaceLibrary::UpdateTurnInPlace(float DeltaTime, bool bAllowTurnInPlace, bool bHoldYawOffset, bool bIsTurnTransitionStateRelevant,
	const FRotator& MeshWorldRotation, const FAnimTurnInPlaceAnimSet& AnimSet, UPARAM(ref) FAnimTurnInPlaceState& TurnInPlaceState)
{
	TurnInPlaceState.Update(DeltaTime, bAllowTurnInPlace, bHoldYawOffset, bIsTurnTransitionStateRelevant, MeshWorldRotation, AnimSet, TurnInPlaceState);
}