// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernelTools.h"

#include "CADData.h"
#include "CADOptions.h"

#include "CADKernel/Core/Entity.h"
#include "CADKernel/Core/MetadataDictionary.h"
#include "CADKernel/Core/Session.h"
#include "CADKernel/Core/Types.h"

#include "CADKernel/Mesh/Criteria/Criterion.h"
#include "CADKernel/Mesh/Meshers/ParametricMesher.h"
#include "CADKernel/Mesh/Structure/FaceMesh.h"
#include "CADKernel/Mesh/Structure/ModelMesh.h"

#include "CADKernel/Topo/Model.h"
#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalVertex.h"

uint32 FCADKernelTools::GetFaceTessellation(const TSharedRef<CADKernel::FFaceMesh>& FaceMesh, CADLibrary::FBodyMesh& OutBodyMesh)
{
	// Something wrong happened, either an error or no data to collect
	if (FaceMesh->TrianglesVerticesIndex.Num() == 0)
	{
		return 0;
	}

	CADLibrary::FTessellationData& Tessellation = OutBodyMesh.Faces.Emplace_GetRef();

	const TSharedRef<CADKernel::FMetadataDictionary>& HaveMetadata = StaticCastSharedRef<CADKernel::FMetadataDictionary>(StaticCastSharedRef<CADKernel::FTopologicalFace>(FaceMesh->GetGeometricEntity()));
	Tessellation.PatchId = HaveMetadata->GetPatchId();

	Tessellation.PositionIndices = FaceMesh->VerticesGlobalIndex;
	Tessellation.VertexIndices = FaceMesh->TrianglesVerticesIndex;

	Tessellation.NormalArray.Reserve(FaceMesh->Normals.Num());
	for (const CADKernel::FPoint& Normal : FaceMesh->Normals)
	{
		Tessellation.NormalArray.Emplace((float)Normal.X, (float)Normal.Y, (float)Normal.Z);
	}

	Tessellation.TexCoordArray.Reserve(FaceMesh->UVMap.Num());
	for (const CADKernel::FPoint2D& TexCoord : FaceMesh->UVMap)
	{
		Tessellation.TexCoordArray.Emplace((float)TexCoord.U, (float)TexCoord.V);
	}

	return Tessellation.VertexIndices.Num() / 3;
}

template<class ClassType>
void GetDisplayDataIds(const TSharedRef<ClassType>& Entity, CADLibrary::FObjectDisplayDataId& DisplayDataId)
{
	const TSharedRef<CADKernel::FMetadataDictionary>& HaveMetadata = StaticCastSharedRef<CADKernel::FMetadataDictionary>(Entity);
	DisplayDataId.Color = HaveMetadata->GetColorId();
	DisplayDataId.Material = HaveMetadata->GetMaterialId();
}

void FCADKernelTools::GetBodyTessellation(const TSharedRef<CADKernel::FModelMesh>& ModelMesh, const TSharedRef<CADKernel::FBody>& Body, CADLibrary::FBodyMesh& OutBodyMesh, uint32 DefaultMaterialHash, TFunction<void(CADLibrary::FObjectDisplayDataId, CADLibrary::FObjectDisplayDataId, int32)> SetFaceMainMaterial)
{
	ModelMesh->GetNodeCoordinates(OutBodyMesh.VertexArray);

	uint32 FaceSize = Body->FaceCount();

	// Allocate memory space for tessellation data
	OutBodyMesh.Faces.Reserve(FaceSize);
	OutBodyMesh.ColorSet.Reserve(FaceSize);
	OutBodyMesh.MaterialSet.Reserve(FaceSize);

	CADLibrary::FObjectDisplayDataId BodyMaterial;
	BodyMaterial.DefaultMaterialName = DefaultMaterialHash;

	GetDisplayDataIds(Body, BodyMaterial);

	// Loop through the face of bodies and collect all tessellation data
	int32 FaceIndex = 0;
	for (const TSharedPtr<CADKernel::FShell>& Shell : Body->GetShells())
	{
		if (!Shell.IsValid())
		{
			continue;
		}

		CADLibrary::FObjectDisplayDataId ShellMaterial = BodyMaterial;
		GetDisplayDataIds(Shell.ToSharedRef(), ShellMaterial);

		for (const CADKernel::FOrientedFace& Face : Shell->GetFaces())
		{
			if (!Face.Entity.IsValid())
			{
				continue;
			}

			if (!Face.Entity->HasTesselation())
			{
				continue;
			}

			CADLibrary::FObjectDisplayDataId FaceMaterial;
			GetDisplayDataIds(Face.Entity.ToSharedRef(), FaceMaterial);

			uint32 TriangleNum = GetFaceTessellation(Face.Entity->GetMesh(), OutBodyMesh);

			if (TriangleNum == 0)
			{
				continue;
			}

			OutBodyMesh.TriangleCount += TriangleNum;

			if (SetFaceMainMaterial)
			{
				SetFaceMainMaterial(FaceMaterial, BodyMaterial, FaceIndex);
			}
			FaceIndex++;
		}
	}
}

void FCADKernelTools::DefineMeshCriteria(TSharedRef<CADKernel::FModelMesh>& MeshModel, const CADLibrary::FImportParameters& ImportParameters)
{
	{
		TSharedPtr<CADKernel::FCriterion> CurvatureCriterion = CADKernel::FCriterion::CreateCriterion(CADKernel::ECriterion::CADCurvature);
		MeshModel->AddCriterion(CurvatureCriterion);
	}

	if (ImportParameters.MaxEdgeLength > SMALL_NUMBER)
	{
		TSharedPtr<CADKernel::FCriterion> MaxSizeCriterion = CADKernel::FCriterion::CreateCriterion(CADKernel::ECriterion::MaxSize, ImportParameters.MaxEdgeLength);
		MeshModel->AddCriterion(MaxSizeCriterion);
	}

	if (ImportParameters.ChordTolerance > SMALL_NUMBER)
	{
		TSharedPtr<CADKernel::FCriterion> ChordCriterion = CADKernel::FCriterion::CreateCriterion(CADKernel::ECriterion::Sag, ImportParameters.ChordTolerance);
		MeshModel->AddCriterion(ChordCriterion);
	}

	if (ImportParameters.MaxNormalAngle > SMALL_NUMBER)
	{
		TSharedPtr<CADKernel::FCriterion> MaxNormalAngleCriterion = CADKernel::FCriterion::CreateCriterion(CADKernel::ECriterion::Angle, ImportParameters.MaxNormalAngle);
		MeshModel->AddCriterion(MaxNormalAngleCriterion);
	}
}
