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
	class FTopologicalEntity;
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
		static void DefineMeshCriteria(TSharedRef<CADKernel::FModelMesh>& MeshModel, const FImportParameters& ImportParameters, double GeometricTolerance);

		static void GetBodyTessellation(const TSharedRef<CADKernel::FModelMesh>& ModelMesh, const TSharedRef<CADKernel::FBody>& Body, FBodyMesh& OutBodyMesh, uint32 DefaultMaterialHash, TFunction<void(FObjectDisplayDataId, FObjectDisplayDataId, int32)> SetFaceMainMaterial);

		/**
		 * Tessellate a CADKernel entity and update the MeshDescription
		 */
		static bool Tessellate(TSharedRef<CADKernel::FTopologicalEntity>& InCADKernelEntity, const FImportParameters& ImportParameters, const FMeshParameters& MeshParameters, FMeshDescription& OutMeshDescription);

		static uint32 GetFaceTessellation(const TSharedRef<CADKernel::FFaceMesh>& FaceMesh, FBodyMesh& OutBodyMesh);
	};
}
