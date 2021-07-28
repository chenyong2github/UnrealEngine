// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Support special mesh proxy geometry funcs
enum class FDisplayClusterRender_MeshComponentProxyDataFunc : uint8
{
	None = 0,
	OutputRemapScreenSpace
};

/** The vertex data used to filter a texture. */
struct FDisplayClusterMeshVertex
{
	FVector4 Position;
	FVector2D UV;
	FVector2D UV_Chromakey;
};
