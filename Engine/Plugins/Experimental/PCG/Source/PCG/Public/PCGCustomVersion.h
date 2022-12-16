// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

// Custom serialization version for assets/classes in the PCG plugin
struct PCG_API FPCGCustomVersion
{
	enum Type
	{
		// Before any version changes were made in the plugin
		BeforeCustomVersionWasAdded = 0,

		// Split projection nodes inputs to separate source edges and target edge
		SplitProjectionNodeInputs = 1,
		
		MoveSelfPruningParamsOffFirstPin = 2,

		MoveParamsOffFirstPinDensityNodes = 3,

		// Split samplers to give a sampling shape and a bounding shape inputs
		SplitSamplerNodesInputs = 4,

		MovePointFilterParamsOffFirstPin = 5,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FPCGCustomVersion() {}
};
