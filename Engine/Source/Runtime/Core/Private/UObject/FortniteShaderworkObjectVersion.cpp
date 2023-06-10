// Copyright Epic Games, Inc. All Rights Reserved.
#include "UObject/FortniteShaderworkObjectVersion.h"

TMap<FGuid, FGuid> FFortniteShaderworkObjectVersion::GetSystemGuids()
{
	TMap<FGuid, FGuid> SystemGuids;
	const FDevSystemGuids& DevGuids = FDevSystemGuids::Get();

	SystemGuids.Add(DevGuids.NANITE_DERIVEDDATA_VER, FGuid("64D2BA4FF333408E90CDD22CA38B1E33"));
	SystemGuids.Add(DevGuids.STATICMESH_DERIVEDDATA_VER, FGuid("EB4D2761094040DD889CC1BE3D24E1B3"));
	return SystemGuids;
}
