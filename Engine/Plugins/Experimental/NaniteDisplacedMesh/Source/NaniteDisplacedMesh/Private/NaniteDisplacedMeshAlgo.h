// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "NaniteBuilder.h"

struct FNaniteDisplacedMeshParams;

bool DisplaceNaniteMesh(
	const FNaniteDisplacedMeshParams& Parameters,
	const uint32 NumTextureCoord,
	Nanite::IBuilderModule::FVertexMeshData& VertexMeshData
);

#endif
