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
		if (!Session.IsCoreTechSessionValid())
		{
			return false;
		}

		CADLibrary::CTKIO_ChangeUnit(ImportParameters.GetMetricUnit());
		uint64 MainObjectID;
		if(!CADLibrary::CTKIO_LoadModel(*FileName, MainObjectID, 0x00020000 /* CT_LOAD_FLAGS_READ_META_DATA */))
		{
			// Something wrong happened during the load, abort
			return false;
		}

		if (ImportParameters.GetStitchingTechnique() != CADLibrary::EStitchingTechnique::StitchingNone)
		{
			CADLibrary::CTKIO_Repair(MainObjectID, CADLibrary::EStitchingTechnique::StitchingSew);
		}

		return Tessellate(MainObjectID, ImportParameters, MeshParameters, MeshDescription);
	}
}
