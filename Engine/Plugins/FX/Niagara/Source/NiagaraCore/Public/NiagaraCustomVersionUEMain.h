// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

// Custom serialization version for all packages containing Niagara asset types
struct FNiagaraCustomVersionUEMain
{
	enum Type
	{
		// GPU none interpolated spawning no longer calls the update script
		FixGpuAlwaysRunningUpdateScriptNoneInterpolated,

		// DO NOT ADD A NEW VERSION UNLESS YOU HAVE TALKED TO THE NIAGARA LEAD. Mismanagement of these versions can lead to data loss if it is adjusted in multiple streams simultaneously.
		// -----<new versions can be added above this line>  -------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1,
	};

	// The GUID for this custom version number
	NIAGARACORE_API const static FGuid GUID;

private:
	FNiagaraCustomVersionUEMain() {}
};
