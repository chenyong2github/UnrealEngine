// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// TODO: Remove this once we are sure we can run with FVirtualizedUntypedBulkData with no issues
/** When set to 1 we use FVirtualizedUntypedBulkData, when 0 we use what ever code was there before. */
#define UE_USE_VIRTUALBULKDATA 0

// Custom serialization version for changes made in the UE5 Dev-Cooker stream
struct CORE_API FUE5CookerObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,
		
		// -----<new versions can be added above this line>-------------------------------------------------

		// Virtualization version numbers are added at this point. 
		// When UE_USE_VIRTUALBULKDATA is removed then they can be moved to the normal section

#if UE_USE_VIRTUALBULKDATA
		
		// Switch FMeshDescriptionBulkData to use virtualized bulkdata
		MeshDescriptionVirtualization,

#endif //UE_USE_VIRTUALBULKDATA

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FUE5CookerObjectVersion() {}
};
