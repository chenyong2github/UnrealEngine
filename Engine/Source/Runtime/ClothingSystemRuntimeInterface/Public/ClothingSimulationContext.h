// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothingSystemRuntimeTypes.h"
#include "Containers/Array.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

/** Empty interface, derived simulation modules define the contents of the context. */
class CLOTHINGSYSTEMRUNTIMEINTERFACE_API IClothingSimulationContext
{
public:
	IClothingSimulationContext();
	virtual ~IClothingSimulationContext();
};

/** Base simulation data that just about every simulation would need. */
class CLOTHINGSYSTEMRUNTIMEINTERFACE_API FClothingSimulationContextBase : public IClothingSimulationContext
{
public:

	FClothingSimulationContextBase();
	virtual ~FClothingSimulationContextBase();

	// Delta for this tick
	float DeltaSeconds;

	// World space bone transforms of the owning component
	TArray<FTransform> BoneTransforms;

	// Component to world transform of the owning component
	FTransform ComponentToWorld;

	// The predicted LOD of the skeletal mesh component running the simulation
	int32 PredictedLod;

	// Wind velocity at the component location
	FVector WindVelocity;

	// Gravity extracted from the world
	FVector WorldGravity;

	// Wind adaption, a measure of how quickly to adapt to the wind speed
	// when using the legacy wind calculation mode
	float WindAdaption;

	// Whether and how we should teleport the simulation this tick
	EClothingTeleportMode TeleportMode;

	// Scale for the max distance constraints of the simulation mesh
	float MaxDistanceScale;
};

