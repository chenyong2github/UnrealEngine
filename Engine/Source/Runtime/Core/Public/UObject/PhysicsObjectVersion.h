// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// Custom serialization version for changes made in Dev-Physics stream
struct CORE_API FPhysicsObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,
		// Adding PerShapeData to serialization
		PerShapeData,
		// Add serialization from handle back to particle
		SerializeGTGeometryParticles,
		// Groom serialization with hair description as bulk data
		GroomWithDescription,
		// Groom serialization with import option
		GroomWithImportSettings,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FPhysicsObjectVersion() {}
};
