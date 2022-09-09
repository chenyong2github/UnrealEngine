// Copyright Epic Games, Inc. All Rights Reserved.
#include "UObject/FortniteMainBranchObjectVersion.h"

TMap<FGuid, FGuid> FFortniteMainBranchObjectVersion::GetSystemGuids()
{
	TMap<FGuid, FGuid> SystemGuids;
	const FDevSystemGuids& DevGuids = FDevSystemGuids::Get();

	SystemGuids.Add(DevGuids.GLOBALSHADERMAP_DERIVEDDATA_VER, FGuid("8D3A12924CDF4D9F84C0A900E9445CAD"));
	SystemGuids.Add(DevGuids.LANDSCAPE_MOBILE_COOK_VERSION, FGuid("32D02EF867C74B71A0D4E0FA41392732"));
	SystemGuids.Add(DevGuids.MATERIALSHADERMAP_DERIVEDDATA_VER, FGuid("1DA85DA5733C4A489A1BE117CDC5ACCC"));
	SystemGuids.Add(DevGuids.NANITE_DERIVEDDATA_VER, FGuid("93379C2F29624E74AFB14FB13E327FA2"));
	SystemGuids.Add(DevGuids.NIAGARASHADERMAP_DERIVEDDATA_VER, FGuid("2289D5116CF94BC0AFEAEC541468E645"));
	SystemGuids.Add(DevGuids.Niagara_LatestScriptCompileVersion, FGuid("D6A13E2AE4D2453EB1A44529AAAB8305"));
	SystemGuids.Add(DevGuids.SkeletalMeshDerivedDataVersion, FGuid("ACF593EAAE354FCBB34CF44F5AA1BFD2"));
	SystemGuids.Add(DevGuids.STATICMESH_DERIVEDDATA_VER, FGuid("4696D39D9BC743AFBFA1DAB359AD7A7D"));

	return SystemGuids;
}
