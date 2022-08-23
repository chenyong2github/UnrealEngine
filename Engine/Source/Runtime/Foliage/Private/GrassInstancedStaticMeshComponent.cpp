// Copyright Epic Games, Inc. All Rights Reserved.
#include "GrassInstancedStaticMeshComponent.h"


static TAutoConsoleVariable<bool> CVarEnableGrassInstancedWPOVelocity(
	TEXT("r.Velocity.EnableLandscapeGrass"),
	true,
	TEXT("Specify if you want to output velocity for the grass component for WPO.\n")
	TEXT(" True (default)\n")
	TEXT(" False")
	);

bool UGrassInstancedStaticMeshComponent::SupportsWorldPositionOffsetVelocity() const 
{ 
	return CVarEnableGrassInstancedWPOVelocity.GetValueOnAnyThread();
}