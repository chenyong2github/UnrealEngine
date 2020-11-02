// Copyright Epic Games, Inc. All Rights Reserved.


#include "MeshIndexMappings.h"
#include "DynamicMeshAttributeSet.h"


void FMeshIndexMappings::Initialize(FDynamicMesh3* Mesh)
{
	if (Mesh->HasAttributes())
	{
		FDynamicMeshAttributeSet* Attribs = Mesh->Attributes();
		UVMaps.SetNum(Attribs->NumUVLayers());
		NormalMaps.SetNum(Attribs->NumNormalLayers());
	}
}
