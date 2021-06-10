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
	class FModel;
	class FModelMesh;
}

namespace CADLibrary
{
	struct FImportParameters;
}

class CADTOOLS_API FCADKernelTools
{
public:
	static void DefineMeshCriteria(TSharedRef<CADKernel::FModelMesh>& MeshModel, const CADLibrary::FImportParameters& ImportParameters);

	static void GetBodyTessellation(const TSharedRef<CADKernel::FModelMesh>& ModelMesh, const TSharedRef<CADKernel::FBody>& Body, CADLibrary::FBodyMesh& OutBodyMesh, uint32 DefaultMaterialHash, TFunction<void(CADLibrary::FObjectDisplayDataId, CADLibrary::FObjectDisplayDataId, int32)> SetFaceMainMaterial);

	static uint32 GetFaceTessellation(const TSharedRef<CADKernel::FFaceMesh>& FaceMesh, CADLibrary::FBodyMesh& OutBodyMesh);
};
