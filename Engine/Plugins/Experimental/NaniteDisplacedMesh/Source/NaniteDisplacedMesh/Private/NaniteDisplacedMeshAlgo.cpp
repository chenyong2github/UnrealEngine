// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMeshAlgo.h"

#if WITH_EDITOR

#include "NaniteDisplacedMesh.h"
#include "NaniteBuilder.h"

bool DisplaceNaniteMesh(
	const FNaniteDisplacedMeshParams& Parameters,
	const uint32 NumTextureCoord,
	Nanite::IBuilderModule::FVertexMeshData& VertexMeshData
)
{
	// TODO: Currently just passthrough
	return true;
}

#endif
