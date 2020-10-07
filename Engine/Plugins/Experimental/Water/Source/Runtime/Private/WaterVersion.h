// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


// Custom serialization version for Water plugin
struct FWaterCustomVersion
{
	enum Type
	{
		// Before any version changes were made in the plugin
		BeforeCustomVersionWasAdded = 0,
		// Refactor of AWaterBody into sub-classes, waves refactor, etc.
		WaterBodyRefactor = 1,
		// Transfer of TerrainCarvingSettings from landmass to water
		MoveTerrainCarvingSettingsToWater = 1,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FWaterCustomVersion() {}
};