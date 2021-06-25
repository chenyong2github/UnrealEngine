// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreTechSurfaceHelper.h"

#include "CADInterfacesModule.h"
#include "CADOptions.h"
#include "CoreTechSurfaceExtension.h"
#include "CoreTechTypes.h"
#include "DatasmithPayload.h"
#include "IDatasmithSceneElements.h"
#include "MeshDescription.h"
#include "MeshDescriptionHelper.h"
#include "Misc/FileHelper.h"
#include "UObject/NameTypes.h"

typedef uint32 TriangleIndex[3];

namespace CoreTechSurface
{
	bool Tessellate(uint64 MainObjectId, const CADLibrary::FImportParameters& ImportParams, const CADLibrary::FMeshParameters& MeshParameters, FMeshDescription& MeshDesc)
	{
		CADLibrary::CTKIO_SetCoreTechTessellationState(ImportParams);

		CADLibrary::FBodyMesh BodyMesh;
		BodyMesh.BodyID = 1;

		CADLibrary::CTKIO_GetTessellation(MainObjectId, BodyMesh, false);

		if (BodyMesh.Faces.Num() == 0)
		{
			return false;
		}

		if (!CADLibrary::ConvertBodyMeshToMeshDescription(ImportParams, MeshParameters, BodyMesh, MeshDesc))
		{
			ensureMsgf(false, TEXT("Error during mesh conversion"));
			return false;
		}

		return true;
	}

	bool LoadFile(const FString& FileName, const CADLibrary::FImportParameters& ImportParameters, const CADLibrary::FMeshParameters& MeshParameters, FMeshDescription& MeshDescription)
	{
		CADLibrary::FCoreTechSessionBase Session(TEXT("CoreTechMeshLoader::LoadFile"));
		if (!Session.IsSessionValid())
		{
			return false;
		}

		CADLibrary::CTKIO_ChangeUnit(ImportParameters.MetricUnit);
		uint64 MainObjectID;
		if(!CADLibrary::CTKIO_LoadModel(*FileName, MainObjectID, 0x00020000 /* CT_LOAD_FLAGS_READ_META_DATA */))
		{
			// Something wrong happened during the load, abort
			return false;
		}

		if (ImportParameters.StitchingTechnique != CADLibrary::EStitchingTechnique::StitchingNone)
		{
			CADLibrary::CTKIO_Repair(MainObjectID, CADLibrary::EStitchingTechnique::StitchingSew);
		}

		return Tessellate(MainObjectID, ImportParameters, MeshParameters, MeshDescription);
	}

	void AddSurfaceDataForMesh(const TCHAR* InFilePath, const CADLibrary::FImportParameters& InSceneParameters, const CADLibrary::FMeshParameters& InMeshParameters, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload)
	{
		if (ICADInterfacesModule::GetAvailability() == ECADInterfaceAvailability::Available)
		{
			if (FPaths::FileExists(InFilePath))
			{
				TArray<uint8> ByteArray;
				if (FFileHelper::LoadFileToArray(ByteArray, InFilePath))
				{
					UCoreTechParametricSurfaceData* CoreTechData = Datasmith::MakeAdditionalData<UCoreTechParametricSurfaceData>();
					CoreTechData->SourceFile = InFilePath;
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
		}
	}

}
