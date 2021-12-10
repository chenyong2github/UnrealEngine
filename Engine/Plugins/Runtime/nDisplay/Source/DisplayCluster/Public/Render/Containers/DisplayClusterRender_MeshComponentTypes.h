// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Support special mesh proxy geometry funcs
enum class EDisplayClusterRender_MeshComponentProxyDataFunc : uint8
{
	Disabled = 0,
	OutputRemapScreenSpace
};

// Assigned geometry type
enum class EDisplayClusterRender_MeshComponentGeometrySource: uint8
{
	Disabled = 0,
	StaticMeshComponentRef,
	ProceduralMeshComponentRef,
	ProceduralMeshSection,
	StaticMeshAsset,
	MeshGeometry,
};

/** The vertex data used to filter a texture. */
struct FDisplayClusterMeshVertex
{
	FVector4 Position;
	FVector2D UV;
	FVector2D UV_Chromakey;
};
