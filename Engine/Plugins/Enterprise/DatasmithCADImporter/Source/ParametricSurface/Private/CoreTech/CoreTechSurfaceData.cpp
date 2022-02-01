// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechSurfaceData.h"

#include "CADInterfacesModule.h"
#include "CoreTechTypes.h"
#include "DatasmithAdditionalData.h"
#include "DatasmithPayload.h"
#include "IDatasmithSceneElements.h"

#include "Engine/StaticMesh.h"
#include "HAL/PlatformFileManager.h"
#include "MeshDescriptionHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "UObject/EnterpriseObjectVersion.h"

namespace CoreTechParametricSurfaceDataUtils
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
		if (!CADLibrary::CTKIO_LoadModel(*FileName, MainObjectID, 0x00020000 /* CT_LOAD_FLAGS_READ_META_DATA */))
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

bool UCoreTechParametricSurfaceData::SetFile(const TCHAR* FilePath)
{
	if (UParametricSurfaceData::SetFile(FilePath))
	{
		SourceFile = FilePath;
		return true;
	}

	return false;
}

bool UCoreTechParametricSurfaceData::Tessellate(UStaticMesh& StaticMesh, const FDatasmithRetessellationOptions& RetessellateOptions)
{
	bool bSuccessfulTessellation = false;

#if WITH_EDITOR
	// make a temporary file as CoreTech can only deal with files.
	int32 Hash = GetTypeHash(StaticMesh.GetPathName());
	FString ResourceFile = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / FString::Printf(TEXT("0x%08x.ct"), Hash));

	FFileHelper::SaveArrayToFile(RawData, *ResourceFile);

	CADLibrary::FCTMesh Mesh;

	CADLibrary::FImportParameters ImportParameters(SceneParameters.MetricUnit, SceneParameters.ScaleFactor, (FDatasmithUtils::EModelCoordSystem) SceneParameters.ModelCoordSys);
	ImportParameters.SetTesselationParameters(RetessellateOptions.ChordTolerance, RetessellateOptions.MaxEdgeLength, RetessellateOptions.NormalTolerance, (CADLibrary::EStitchingTechnique)RetessellateOptions.StitchingTechnique);

	CADLibrary::FMeshParameters CadMeshParameters;
	CadMeshParameters.bNeedSwapOrientation = MeshParameters.bNeedSwapOrientation;
	CadMeshParameters.bIsSymmetric = MeshParameters.bIsSymmetric;
	CadMeshParameters.SymmetricNormal = MeshParameters.SymmetricNormal;
	CadMeshParameters.SymmetricOrigin = MeshParameters.SymmetricOrigin;

	// Previous MeshDescription is get to be able to create a new one with the same order of PolygonGroup (the matching of color and partition is currently based on their order)
	if (FMeshDescription* DestinationMeshDescription = StaticMesh.GetMeshDescription(0))
	{
		FMeshDescription MeshDescription;
		FStaticMeshAttributes MeshDescriptionAttributes(MeshDescription);
		MeshDescriptionAttributes.Register();

		if (RetessellateOptions.RetessellationRule == EDatasmithCADRetessellationRule::SkipDeletedSurfaces)
		{
			CADLibrary::CopyPatchGroups(*DestinationMeshDescription, MeshDescription);
		}

		if (CoreTechParametricSurfaceDataUtils::LoadFile(ResourceFile, ImportParameters, CadMeshParameters, MeshDescription))
		{
			// To update the SectionInfoMap 
			{
				TPolygonGroupAttributesConstRef<FName> MaterialSlotNames = MeshDescriptionAttributes.GetPolygonGroupMaterialSlotNames();
				FMeshSectionInfoMap& SectionInfoMap = StaticMesh.GetSectionInfoMap();

				for (FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
				{
					FMeshSectionInfo Section = SectionInfoMap.Get(0, PolygonGroupID.GetValue());
					int32 MaterialIndex = StaticMesh.GetMaterialIndex(MaterialSlotNames[PolygonGroupID]);
					if (MaterialIndex < 0)
					{
						MaterialIndex = 0;
					}
					Section.MaterialIndex = MaterialIndex;
					SectionInfoMap.Set(0, PolygonGroupID.GetValue(), Section);
				}
			}
			*DestinationMeshDescription = MoveTemp(MeshDescription);
			bSuccessfulTessellation = true;
		}
	}

	// Remove temporary file
	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*ResourceFile);
#endif
	return bSuccessfulTessellation;
}


