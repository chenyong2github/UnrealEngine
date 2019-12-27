// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Math/Transform.h"
#include "Math/Vector.h"


// Data produced by a clothing simulation
struct FClothSimulData
{
	void Reset()
	{
		Positions.Reset();
		Normals.Reset();
	}

	// Positions of the simulation mesh particles
	TArray<FVector> Positions;

	// Normals at the simulation mesh particles
	TArray<FVector> Normals;

	// Transform applied per position/normal element when loaded
	FTransform Transform;

	// Transform relative to the component to update clothing root transform when not ticking clothing but rendering a component
	FTransform ComponentRelativeTransform;
};

enum class EClothingTeleportMode : uint8
{
	// No teleport, simulate as normal
	None = 0,
	// Teleport the simulation, causing no intertial effects but keep the sim mesh shape
	Teleport,
	// Teleport the simulation, causing no intertial effects and reset the sim mesh shape
	TeleportAndReset
};

