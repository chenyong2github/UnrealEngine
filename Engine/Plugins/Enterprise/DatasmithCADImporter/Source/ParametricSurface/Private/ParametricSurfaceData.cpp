// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParametricSurfaceData.h"

#include "Misc/FileHelper.h"
#include "UObject/EnterpriseObjectVersion.h"

#define LOCTEXT_NAMESPACE "ParametricSurfaceData"

bool UParametricSurfaceData::SetFile(const TCHAR* FilePath)
{
	if (FPaths::FileExists(FilePath))
	{
		TArray<uint8> ByteArray;

		if (FFileHelper::LoadFileToArray(ByteArray, FilePath))
		{
			RawData = MoveTemp(ByteArray);
			return true;
		}
	}

	return false;
}

void UParametricSurfaceData::SetImportParameters(const CADLibrary::FImportParameters& InSceneParameters)
{
	SceneParameters.ModelCoordSys = uint8(InSceneParameters.GetModelCoordSys());
	SceneParameters.MetricUnit = InSceneParameters.GetMetricUnit();
	SceneParameters.ScaleFactor = InSceneParameters.GetScaleFactor();
}

void UParametricSurfaceData::SetMeshParameters(const CADLibrary::FMeshParameters& InMeshParameters)
{
	MeshParameters.bNeedSwapOrientation = InMeshParameters.bNeedSwapOrientation;
	MeshParameters.bIsSymmetric = InMeshParameters.bIsSymmetric;
	MeshParameters.SymmetricNormal = InMeshParameters.SymmetricNormal;
	MeshParameters.SymmetricOrigin = InMeshParameters.SymmetricOrigin;
}

void UParametricSurfaceData::Serialize(FArchive& Ar)
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

#undef LOCTEXT_NAMESPACE // "ParametricSurfaceData"

