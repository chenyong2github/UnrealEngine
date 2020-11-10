// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomAssetMeshes.h"
#include "Engine/StaticMesh.h"

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


bool FHairGroupsMeshesSourceDescription::HasMeshChanged() const
{
#if WITH_EDITORONLY_DATA
	if (ImportedMesh)
	{
		ImportedMesh->ConditionalPostLoad();
		return ImportedMeshKey == ImportedMesh->GetRenderData()->DerivedDataKey;
	}
#endif
	return false;
}

void FHairGroupsMeshesSourceDescription::UpdateMeshKey()
{
#if WITH_EDITORONLY_DATA
	if (ImportedMesh)
	{
		ImportedMesh->ConditionalPostLoad();
		ImportedMeshKey = ImportedMesh->GetRenderData()->DerivedDataKey;
	}
#endif
}