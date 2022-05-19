// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

struct FNaniteDisplacedMeshParams;

namespace Nanite
{
	struct IBuilderModule::FVertexMeshData;
}

bool DisplaceNaniteMesh(
	const FNaniteDisplacedMeshParams& Parameters,
	const uint32 NumTextureCoord,
	Nanite::IBuilderModule::FVertexMeshData& VertexMeshData
);

#endif
