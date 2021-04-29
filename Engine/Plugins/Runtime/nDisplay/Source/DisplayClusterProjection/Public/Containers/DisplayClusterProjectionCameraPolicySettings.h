// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDisplayClusterProjectionCameraPolicySettings
{
	float FOVMultiplier = 1;

	// Rotate incamera frustum on this value to fix broken lens on physic camera
	FRotator  FrustumRotation = FRotator::ZeroRotator;

	// Move incamera frustum on this value to fix broken lens on physic camera
	FVector  FrustumOffset = FVector::ZeroVector;
};
