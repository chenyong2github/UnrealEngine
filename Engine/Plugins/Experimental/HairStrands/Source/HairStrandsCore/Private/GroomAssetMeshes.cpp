// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomAssetMeshes.h"

FHairGroupsMeshesSourceDescription::FHairGroupsMeshesSourceDescription()
{
	 ImportedMesh = nullptr;
	 MaterialSlotName = NAME_None;
	 GroupIndex = 0;
	 LODIndex = -1;
}

bool FHairGroupsMeshesSourceDescription::operator==(const FHairGroupsMeshesSourceDescription& A) const
{
	return
		GroupIndex == A.GroupIndex && 
		LODIndex == A.LODIndex &&
		MaterialSlotName == A.MaterialSlotName &&
		ImportedMesh == A.ImportedMesh;
}
