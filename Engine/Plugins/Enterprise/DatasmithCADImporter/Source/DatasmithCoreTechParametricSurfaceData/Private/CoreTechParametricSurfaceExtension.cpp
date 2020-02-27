// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechParametricSurfaceExtension.h"
#include "DatasmithPayload.h"
#include "IDatasmithSceneElements.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/EnterpriseObjectVersion.h"

#ifdef CAD_LIBRARY
#include "CADOptions.h"
#endif // CAD_LIBRARY

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

namespace DatasmithCoreTechParametricSurfaceData
{
	// TODO: convert to FCoreTechSceneParameters/FCoreTechMeshParameters ?
	void AddCoreTechSurfaceDataForMesh(const TSharedRef<IDatasmithMeshElement>& InMeshElement, const CADLibrary::FImportParameters& InSceneParameters, const CADLibrary::FMeshParameters& InMeshParameters, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload)
	{
#ifdef CAD_LIBRARY
		// Store CoreTech additional data if provided
		const TCHAR* CoretechFile = InMeshElement->GetFile();
		if (FPaths::FileExists(CoretechFile))
		{
			TArray<uint8> ByteArray;
			if (FFileHelper::LoadFileToArray(ByteArray, CoretechFile))
			{
				UCoreTechParametricSurfaceData* CoreTechData = Datasmith::MakeAdditionalData<UCoreTechParametricSurfaceData>();
				CoreTechData->SourceFile = CoretechFile;
				CoreTechData->RawData = MoveTemp(ByteArray);
				CoreTechData->SceneParameters.ModelCoordSys = uint8(InSceneParameters.ModelCoordSys);
				CoreTechData->SceneParameters.MetricUnit = InSceneParameters.MetricUnit;
				CoreTechData->SceneParameters.ScaleFactor = InSceneParameters.ScaleFactor;

				CoreTechData->MeshParameters.bNeedSwapOrientation = InMeshParameters.bNeedSwapOrientation;
				CoreTechData->MeshParameters.bIsSymmetric = InMeshParameters.bIsSymmetric;
				CoreTechData->MeshParameters.SymmetricNormal = InMeshParameters.SymmetricNormal;
				CoreTechData->MeshParameters.SymmetricOrigin = InMeshParameters.SymmetricOrigin;

				CoreTechData->LastTessellationOptions = InTessellationOptions;
				OutMeshPayload.AdditionalData.Add(CoreTechData);
			}
		}
#endif // CAD_LIBRARY
	}

}
