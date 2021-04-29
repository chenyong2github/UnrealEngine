// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RenderResource.h"


/** The vertex data used to filter a texture. */
struct FDisplayClusterMeshVertex
{
	FVector4 Position;
	FVector2D UV;
	FVector2D UV_Chromakey;
};


/** The filter vertex declaration resource type. */
class FDisplayClusterMeshVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual void InitRHI() override
	{
		FVertexDeclarationElementList Elements;

		uint32 Stride = sizeof(FDisplayClusterMeshVertex);

		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDisplayClusterMeshVertex, Position),     VET_Float4, 0, Stride));
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDisplayClusterMeshVertex, UV),           VET_Float2, 1, Stride));
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDisplayClusterMeshVertex, UV_Chromakey), VET_Float2, 2, Stride));

		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};
