// Copyright Epic Games, Inc. All Rights Reserved.
#include "UObject/FortniteNCBranchObjectVersion.h"

TMap<FGuid, FGuid> FFortniteNCBranchObjectVersion::GetSystemGuids()
{
	TMap<FGuid, FGuid> SystemGuids;
	const FDevSystemGuids& DevGuids = FDevSystemGuids::Get();

	SystemGuids.Add(DevGuids.STATICMESH_DERIVEDDATA_VER, FGuid("F16D2728168B4CDEBFB382B38FFE9E06"));
	SystemGuids.Add(DevGuids.Niagara_LatestScriptCompileVersion, FGuid("08A58144744FCA4A9BF6DD45DFC4EC13"));

	return SystemGuids;
}
