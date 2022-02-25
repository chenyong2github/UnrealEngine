// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADOptions.h"

namespace CADKernel
{
	class FBody;
	class FBodyMesh;
	class FFaceMesh;
	class FTopologicalShapeEntity;
	class FModelMesh;
}

struct FMeshDescription;

namespace CADLibrary
{
	class FImportParameters;
	struct FMeshParameters;

	class CADLIBRARY_API FCADKernelTools
	{
	public:
		static void DefineMeshCriteria(CADKernel::FModelMesh& MeshModel, const FImportParameters& ImportParameters, double GeometricTolerance);

		static void GetBodyTessellation(const CADKernel::FModelMesh& ModelMesh, const CADKernel::FBody& Body, FBodyMesh& OutBodyMesh);

		/**
		 * Tessellate a CADKernel entity and update the MeshDescription
		 */
		static bool Tessellate(CADKernel::FTopologicalShapeEntity& InCADKernelEntity, const FImportParameters& ImportParameters, const FMeshParameters& MeshParameters, FMeshDescription& OutMeshDescription);

		static uint32 GetFaceTessellation(CADKernel::FFaceMesh& FaceMesh, FBodyMesh& OutBodyMesh, FObjectDisplayDataId FaceMaterial);
	};
}
