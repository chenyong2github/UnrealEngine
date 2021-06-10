// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// Custom serialization version for changes to DMX Runtime Objects
struct FDMXRuntimeObjectVersion
{
	enum Type
	{
		// Roughly corresponds to 4.26
		BeforeCustomVersionWasAdded = 0,

		// Update to DMX Library Section using normalized values by default
		DefaultToNormalizedValuesInDMXLibrarySection,

		// Update to DMX Library Section using normalized values by default
		ReplaceWeakWithStrongFixturePatchReferncesInLibrarySection,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FDMXRuntimeObjectVersion() {}
};
