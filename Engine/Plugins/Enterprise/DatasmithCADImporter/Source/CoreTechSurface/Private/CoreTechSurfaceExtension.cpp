// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechSurfaceExtension.h"

#include "CADInterfacesModule.h"
#include "CoreTechSurfaceHelper.h"
#include "CoreTechTypes.h"
#include "DatasmithAdditionalData.h"
#include "DatasmithPayload.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformFileManager.h"
#include "IDatasmithSceneElements.h"
#include "MeshDescriptionHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
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

bool UCoreTechParametricSurfaceData::Tessellate(UStaticMesh& StaticMesh, const FDatasmithRetessellationOptions& RetessellateOptions)
{
	bool bSuccessfulTessellation = false;

#if WITH_EDITOR
	// make a temporary file as CoreTech can only deal with files.
	int32 Hash = GetTypeHash(StaticMesh.GetPathName());
	FString ResourceFile = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / FString::Printf(TEXT("0x%08x.ct"), Hash));

	FFileHelper::SaveArrayToFile(RawData, *ResourceFile);

	CADLibrary::FCTMesh Mesh;

	CADLibrary::FImportParameters ImportParameters;
	ImportParameters.MetricUnit = SceneParameters.MetricUnit;
	ImportParameters.ScaleFactor = SceneParameters.ScaleFactor;
	ImportParameters.ChordTolerance = RetessellateOptions.ChordTolerance;
	ImportParameters.MaxEdgeLength = RetessellateOptions.MaxEdgeLength;
	ImportParameters.MaxNormalAngle = RetessellateOptions.NormalTolerance;
	ImportParameters.ModelCoordSys = static_cast<FDatasmithUtils::EModelCoordSystem>(SceneParameters.ModelCoordSys);
	ImportParameters.StitchingTechnique = CADLibrary::EStitchingTechnique(RetessellateOptions.StitchingTechnique);

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

		if (CoreTechSurface::LoadFile(ResourceFile, ImportParameters, CadMeshParameters, MeshDescription))
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


