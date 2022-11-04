// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/ChaosDeformableSolverAsset.h"


UChaosDeformableSolver::UChaosDeformableSolver(const FObjectInitializer& ObjectInitializer)
	: UObject(ObjectInitializer)
{
	check(ObjectInitializer.GetClass() == GetClass());
}


