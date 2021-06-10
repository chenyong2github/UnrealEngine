// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernelSurfaceExtension.h"

#include "CADKernelTools.h"
#include "CADData.h"
#include "CADOptions.h"
#include "DatasmithPayload.h"
#include "Engine/StaticMesh.h"
#include "IDatasmithSceneElements.h"
#include "MeshDescription.h"
#include "MeshDescriptionHelper.h"
#include "StaticMeshAttributes.h"
#include "UObject/EnterpriseObjectVersion.h"

#include "CADKernel/Core/CADKernelArchive.h"
#include "CADKernel/Core/Entity.h"
#include "CADKernel/Core/Session.h"
#include "CADKernel/Core/Types.h"
#include "CADKernel/Mesh/Meshers/ParametricMesher.h"
#include "CADKernel/Mesh/Structure/ModelMesh.h"
#include "CADKernel/Topo/Model.h"
#include "CADKernel/Topo/Body.h"

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

		TSharedRef<CADKernel::FSession> CADKernelSession = MakeShared<CADKernel::FSession>(0.00001 / ImportParameters.MetricUnit);
		CADKernelSession->AddDatabase(RawData);

		TSharedRef<CADKernel::FModel> CADKernelModel = CADKernelSession->GetModel();

		// Tesselate the model
		TSharedRef<CADKernel::FModelMesh> CADKernelModelMesh = CADKernel::FEntity::MakeShared<CADKernel::FModelMesh>();

		FCADKernelTools::DefineMeshCriteria(CADKernelModelMesh, ImportParameters);

		CADKernel::FParametricMesher Mesher(CADKernelModelMesh);
		Mesher.MeshEntity(CADKernelModel);

		TArray<TSharedPtr<CADKernel::FBody>> CADKernelBodies = CADKernelModel->GetBodies();
		if (CADKernelBodies.Num() != 1)
		{
			return bSuccessfulTessellation;
		}
		const TSharedRef<CADKernel::FBody> Body = CADKernelBodies[0].ToSharedRef();

		CADLibrary::FBodyMesh BodyMesh;
		uint32 DefaultMaterialHash = 0;

		FCADKernelTools::GetBodyTessellation(CADKernelModelMesh, Body, BodyMesh, DefaultMaterialHash, [](CADLibrary::FObjectDisplayDataId, CADLibrary::FObjectDisplayDataId, int32) {;});
		if (CADLibrary::ConvertBodyMeshToMeshDescription(ImportParameters, CadMeshParameters, BodyMesh, MeshDescription))
		{
			// To update the SectionInfoMap 
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
		// Store CoreTech additional data if provided
		FString CADKernelDatabase = InMeshElement->GetFile();
		if (FPaths::FileExists(*CADKernelDatabase))
		{
			TArray<uint8> ByteArray;
			if (FFileHelper::LoadFileToArray(ByteArray, *CADKernelDatabase))
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

}
