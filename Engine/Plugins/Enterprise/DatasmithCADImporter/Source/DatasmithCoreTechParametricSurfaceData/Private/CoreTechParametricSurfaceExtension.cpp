// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechParametricSurfaceExtension.h"
#include "UObject/EnterpriseObjectVersion.h"

void UCoreTechParametricSurfaceData::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FEnterpriseObjectVersion::GUID);

	Super::Serialize(Ar);

	if (Ar.IsSaving() || (Ar.IsLoading() && Ar.CustomVer(FEnterpriseObjectVersion::GUID) >= FEnterpriseObjectVersion::CoreTechParametricSurfaceOptim))
	{
		Ar << RawData;
	}

	if (RawData_DEPRECATED.Num() && RawData.Num() == 0)
	{
		RawData = MoveTemp(RawData_DEPRECATED);
	}
}
