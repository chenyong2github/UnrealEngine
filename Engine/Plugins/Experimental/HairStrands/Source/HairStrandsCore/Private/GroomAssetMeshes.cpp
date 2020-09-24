// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomAssetMeshes.h"

FHairGroupsMeshesSourceDescription::FHairGroupsMeshesSourceDescription()
{
	 ImportedMesh = nullptr;
	 Material = nullptr;
	 GroupIndex = 0;
	 LODIndex = -1;
}

bool FHairGroupsMeshesSourceDescription::operator==(const FHairGroupsMeshesSourceDescription& A) const
{
	return
		GroupIndex == A.GroupIndex && 
		LODIndex == A.LODIndex &&
		Material == A.Material &&
		ImportedMesh == A.ImportedMesh;
}
