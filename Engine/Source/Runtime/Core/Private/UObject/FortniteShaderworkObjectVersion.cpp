// Copyright Epic Games, Inc. All Rights Reserved.
#include "UObject/FortniteShaderworkObjectVersion.h"

TMap<FGuid, FGuid> FFortniteShaderworkObjectVersion::GetSystemGuids()
{
	TMap<FGuid, FGuid> SystemGuids;
	const FDevSystemGuids& DevGuids = FDevSystemGuids::Get();

	SystemGuids.Add(DevGuids.NANITE_DERIVEDDATA_VER, FGuid("31E8D296168E4302BD55627BE50D2029"));
	return SystemGuids;
}
