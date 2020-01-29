// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderResource.h"


/** The vertex data used to filter a texture. */
struct FWarpMeshVertex
{
	FVector4 Position;
	FVector2D UV;
	FVector2D UV_Chromakey;
};


/** The filter vertex declaration resource type. */
class FWarpMeshVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	/** Destructor. */
	virtual ~FWarpMeshVertexDeclaration()
	{ }

	virtual void InitRHI()
	{
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FWarpMeshVertex);
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FWarpMeshVertex, Position), VET_Float4, 0, Stride));
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FWarpMeshVertex, UV), VET_Float2, 1, Stride));
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FWarpMeshVertex, UV_Chromakey), VET_Float2, 2, Stride));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()
	{
		VertexDeclarationRHI.SafeRelease();
	}
};
