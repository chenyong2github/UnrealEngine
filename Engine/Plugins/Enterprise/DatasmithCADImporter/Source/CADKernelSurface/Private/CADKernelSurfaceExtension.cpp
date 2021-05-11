// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernelSurfaceExtension.h"

#include "CADOptions.h"
#include "DatasmithPayload.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "MeshDescriptionHelper.h"
#include "StaticMeshAttributes.h"
#include "UObject/EnterpriseObjectVersion.h"

void UCADKernelParametricSurfaceData::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FEnterpriseObjectVersion::GUID);
	Super::Serialize(Ar);
	Ar << RawData;
}

bool UCADKernelParametricSurfaceData::Tessellate(UStaticMesh& StaticMesh, const FDatasmithRetessellationOptions& RetessellateOptions)
{
	bool bSuccessfulTessellation = false;

#if WITH_EDITOR
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

		if (false /*CADLibrary::LoadFile(ResourceFile, MeshDescription, ImportParameters, CadMeshParameters)*/)
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
#endif 

	return bSuccessfulTessellation;
}

namespace CADKernelSurface
{
	void AddSurfaceDataForMesh(const TSharedRef<IDatasmithMeshElement>& InMeshElement, const CADLibrary::FImportParameters& InSceneParameters, const CADLibrary::FMeshParameters& InMeshParameters, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload)
	{
		// Store CADKernel additional data if provided
		// TODO
		TArray<uint8> ByteArray;
		if (ByteArray.Num() > 0)
		{
			UCADKernelParametricSurfaceData* CADKernelData = Datasmith::MakeAdditionalData<UCADKernelParametricSurfaceData>();
			CADKernelData->RawData = MoveTemp(ByteArray);
			CADKernelData->SceneParameters.ModelCoordSys = uint8(InSceneParameters.ModelCoordSys);
			CADKernelData->SceneParameters.MetricUnit = InSceneParameters.MetricUnit;
			CADKernelData->SceneParameters.ScaleFactor = InSceneParameters.ScaleFactor;

			CADKernelData->MeshParameters.bNeedSwapOrientation = InMeshParameters.bNeedSwapOrientation;
			CADKernelData->MeshParameters.bIsSymmetric = InMeshParameters.bIsSymmetric;
			CADKernelData->MeshParameters.SymmetricNormal = InMeshParameters.SymmetricNormal;
			CADKernelData->MeshParameters.SymmetricOrigin = InMeshParameters.SymmetricOrigin;

			CADKernelData->LastTessellationOptions = InTessellationOptions;
			OutMeshPayload.AdditionalData.Add(CADKernelData);
		}
	}

}
