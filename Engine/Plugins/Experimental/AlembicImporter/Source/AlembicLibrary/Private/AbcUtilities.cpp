// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbcUtilities.h"

#include "AbcFile.h"
#include "AbcImportUtilities.h"
#include "AbcPolyMesh.h"

void FAbcUtilities::GetFrameMeshData(FAbcFile& AbcFile, int32 FrameIndex, FGeometryCacheMeshData& OutMeshData)
{
	AbcFile.ReadFrame(FrameIndex, EFrameReadFlags::ApplyMatrix, 0);

	// TODO: Get the FaceSetNames of the PolyMeshes in the Alembic
	TArray<FString> UniqueFaceSetNames;
	UniqueFaceSetNames.Insert(TEXT("DefaultMaterial"), 0);

	FGeometryCacheMeshData MeshData;
	int32 PreviousNumVertices = 0;
	bool bConstantTopology = false;

	AbcImporterUtilities::MergePolyMeshesToMeshData(FrameIndex, 0, AbcFile.GetPolyMeshes(), UniqueFaceSetNames, MeshData, PreviousNumVertices, bConstantTopology);

	OutMeshData = MoveTemp(MeshData);

	AbcFile.CleanupFrameData(0);
}
