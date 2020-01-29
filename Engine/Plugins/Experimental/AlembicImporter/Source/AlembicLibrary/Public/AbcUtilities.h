// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FAbcFile;
struct FGeometryCacheMeshData;

class ALEMBICLIBRARY_API FAbcUtilities
{
public:
	/** Populates a FGeometryCacheMeshData instance (with merged meshes) for the given frame of an Alembic */
	static void GetFrameMeshData(FAbcFile& AbcFile, int32 FrameIndex, FGeometryCacheMeshData& OutMeshData);
};