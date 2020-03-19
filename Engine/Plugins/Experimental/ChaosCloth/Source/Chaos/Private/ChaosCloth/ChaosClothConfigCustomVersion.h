// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

// Custom serialization version for cloth config
struct FChaosClothConfigCustomVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,
		// Update drag default to better preserve legacy behavior
		UpdateDragDefault = 1,
		// Added damping and collision thickness per cloth
		AddDampingThicknessMigration = 2,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FChaosClothConfigCustomVersion() {}
};
