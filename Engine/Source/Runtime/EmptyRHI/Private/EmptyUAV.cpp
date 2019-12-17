// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "EmptyRHIPrivate.h"

FEmptyShaderResourceView::~FEmptyShaderResourceView()
{
	SourceVertexBuffer = NULL;
	SourceTexture = NULL;
}



FUnorderedAccessViewRHIRef FEmptyDynamicRHI::RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	FEmptyStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	// create the UAV buffer to point to the structured buffer's memory
	FEmptyUnorderedAccessView* UAV = new FEmptyUnorderedAccessView;
	UAV->SourceStructuredBuffer = StructuredBuffer;

	return UAV;
}

FUnorderedAccessViewRHIRef FEmptyDynamicRHI::RHICreateUnorderedAccessView(FRHITexture* TextureRHI, uint32 MipLevel)
{
	FEmptySurface& Surface = GetEmptySurfaceFromRHITexture(TextureRHI);

	// create the UAV buffer to point to the structured buffer's memory
	FEmptyUnorderedAccessView* UAV = new FEmptyUnorderedAccessView;
	UAV->SourceTexture = (FRHITexture*)TextureRHI;

	return UAV;
}

FUnorderedAccessViewRHIRef FEmptyDynamicRHI::RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBufferRHI, uint8 Format)
{
	FEmptyVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	// create the UAV buffer to point to the structured buffer's memory
	FEmptyUnorderedAccessView* UAV = new FEmptyUnorderedAccessView;
	UAV->SourceVertexBuffer = VertexBuffer;

	return UAV;
}

FUnorderedAccessViewRHIRef FEmptyDynamicRHI::RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBufferRHI, uint8 Format)
{
	FEmptyIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);

	// create the UAV buffer to point to the structured buffer's memory
	FEmptyUnorderedAccessView* UAV = new FEmptyUnorderedAccessView;
	UAV->SourceIndexBuffer = IndexBuffer;

	return UAV;
}

FShaderResourceViewRHIRef FEmptyDynamicRHI::RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBufferRHI)
{
	FEmptyStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	FEmptyShaderResourceView* SRV = new FEmptyShaderResourceView;
	return SRV;
}

FShaderResourceViewRHIRef FEmptyDynamicRHI::RHICreateShaderResourceView(FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	FEmptyVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	FEmptyShaderResourceView* SRV = new FEmptyShaderResourceView;
	SRV->SourceVertexBuffer = VertexBuffer;
	return SRV;
}

FShaderResourceViewRHIRef FEmptyDynamicRHI::RHICreateShaderResourceView(FRHIIndexBuffer* BufferRHI)
{
	// there should be no need to create an object
	return FShaderResourceViewRHIRef();
}

FShaderResourceViewRHIRef FEmptyDynamicRHI::RHICreateShaderResourceView(FRHITexture2D* Texture2DRHI, uint8 MipLevel)
{
	FEmptyShaderResourceView* SRV = new FEmptyShaderResourceView;
	SRV->SourceTexture = (FRHITexture*)Texture2DRHI;
	return SRV;
}

FShaderResourceViewRHIRef FEmptyDynamicRHI::RHICreateShaderResourceView(FRHITexture2D* Texture2DRHI, uint8 MipLevel, uint8 NumMipLevels, uint8 Format)
{
	FEmptyShaderResourceView* SRV = new FEmptyShaderResourceView;
	SRV->SourceTexture = (FRHITexture*)Texture2DRHI;
	return SRV;
}

FShaderResourceViewRHIRef FEmptyDynamicRHI::RHICreateShaderResourceView(FRHITexture3D* Texture3DRHI, uint8 MipLevel)
{
	FEmptyShaderResourceView* SRV = new FEmptyShaderResourceView;
	SRV->SourceTexture = (FRHITexture*)Texture3DRHI;
	return SRV;
}

FShaderResourceViewRHIRef FEmptyDynamicRHI::RHICreateShaderResourceView(FRHITexture2DArray* Texture2DArrayRHI, uint8 MipLevel)
{
	FEmptyShaderResourceView* SRV = new FEmptyShaderResourceView;
	SRV->SourceTexture = (FRHITexture*)Texture2DArrayRHI;
	return SRV;
}

FShaderResourceViewRHIRef FEmptyDynamicRHI::RHICreateShaderResourceView(FRHITextureCube* TextureCubeRHI, uint8 MipLevel)
{
	FEmptyShaderResourceView* SRV = new FEmptyShaderResourceView;
	SRV->SourceTexture = (FRHITexture*)TextureCubeRHI;
	return SRV;
}

void FEmptyDynamicRHI::RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values)
{
	FEmptyUnorderedAccessView* UnorderedAccessView = ResourceCast(UnorderedAccessViewRHI);
}

void FEmptyDynamicRHI::RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values)
{
	FEmptyUnorderedAccessView* UnorderedAccessView = ResourceCast(UnorderedAccessViewRHI);
}

