// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	EmptyCommands.cpp: Empty RHI commands implementation.
=============================================================================*/

#include "EmptyRHIPrivate.h"

void FEmptyDynamicRHI::RHISetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset)
{
	FEmptyVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

}

void FEmptyDynamicRHI::RHISetRasterizerState(FRHIRasterizerState* NewStateRHI)
{
	FEmptyRasterizerState* NewState = ResourceCast(NewStateRHI);

}

void FEmptyDynamicRHI::RHISetComputeShader(FRHIComputeShader* ComputeShaderRHI)
{
	FEmptyComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);

}

void FEmptyDynamicRHI::RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) 
{ 

}

void FEmptyDynamicRHI::RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBufferRHI, uint32 ArgumentOffset)
{ 
	FEmptyVertexBuffer* ArgumentBuffer = ResourceCast(ArgumentBufferRHI);

}

void FEmptyDynamicRHI::RHISetViewport(uint32 MinX,uint32 MinY,float MinZ,uint32 MaxX,uint32 MaxY,float MaxZ)
{

}

void FEmptyDynamicRHI::RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) 
{ 

}

void FEmptyDynamicRHI::RHISetScissorRect(bool bEnable,uint32 MinX,uint32 MinY,uint32 MaxX,uint32 MaxY)
{

}

void FEmptyDynamicRHI::RHISetBoundShaderState(FRHIBoundShaderState* BoundShaderStateRHI)
{
	FEmptyBoundShaderState* BoundShaderState = ResourceCast(BoundShaderStateRHI);

}


void FEmptyDynamicRHI::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI, uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI)
{
	FEmptyUnorderedAccessView* UAV = ResourceCast(UAVRHI);

}

void FEmptyDynamicRHI::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI,uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI, uint32 InitialCount)
{
	FEmptyUnorderedAccessView* UAV = ResourceCast(UAVRHI);

}


void FEmptyDynamicRHI::RHISetShaderTexture(FRHIVertexShader* VertexShaderRHI, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{

}

void FEmptyDynamicRHI::RHISetShaderTexture(FRHIHullShader* HullShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{

}

void FEmptyDynamicRHI::RHISetShaderTexture(FRHIDomainShader* DomainShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{

}

void FEmptyDynamicRHI::RHISetShaderTexture(FRHIGeometryShader* GeometryShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{

}

void FEmptyDynamicRHI::RHISetShaderTexture(FRHIPixelShader* PixelShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{

}

void FEmptyDynamicRHI::RHISetShaderTexture(FRHIComputeShader* ComputeShader, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{

}


void FEmptyDynamicRHI::RHISetShaderResourceViewParameter(FRHIVertexShader* VertexShaderRHI, uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{

}

void FEmptyDynamicRHI::RHISetShaderResourceViewParameter(FRHIHullShader* HullShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{

}

void FEmptyDynamicRHI::RHISetShaderResourceViewParameter(FRHIDomainShader* DomainShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{

}

void FEmptyDynamicRHI::RHISetShaderResourceViewParameter(FRHIGeometryShader* GeometryShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{

}

void FEmptyDynamicRHI::RHISetShaderResourceViewParameter(FRHIPixelShader* PixelShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{

}

void FEmptyDynamicRHI::RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{

}


void FEmptyDynamicRHI::RHISetShaderSampler(FRHIVertexShader* VertexShaderRHI, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	FEmptySamplerState* NewState = ResourceCast(NewStateRHI);
	FEmptyVertexShader* VertexShader = ResourceCast(VertexShaderRHI);

}

void FEmptyDynamicRHI::RHISetShaderSampler(FRHIHullShader* HullShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	FEmptySamplerState* NewState = ResourceCast(NewStateRHI);

}

void FEmptyDynamicRHI::RHISetShaderSampler(FRHIDomainShader* DomainShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	FEmptySamplerState* NewState = ResourceCast(NewStateRHI);

}

void FEmptyDynamicRHI::RHISetShaderSampler(FRHIGeometryShader* GeometryShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	FEmptySamplerState* NewState = ResourceCast(NewStateRHI);

}

void FEmptyDynamicRHI::RHISetShaderSampler(FRHIPixelShader* PixelShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	FEmptySamplerState* NewState = ResourceCast(NewStateRHI);

}

void FEmptyDynamicRHI::RHISetShaderSampler(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	FEmptySamplerState* NewState = ResourceCast(NewStateRHI);

}


void FEmptyDynamicRHI::RHISetShaderParameter(FRHIVertexShader* VertexShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{

}

void FEmptyDynamicRHI::RHISetShaderParameter(FRHIHullShader* HullShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{

}

void FEmptyDynamicRHI::RHISetShaderParameter(FRHIPixelShader* PixelShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{

}

void FEmptyDynamicRHI::RHISetShaderParameter(FRHIDomainShader* DomainShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{

}

void FEmptyDynamicRHI::RHISetShaderParameter(FRHIGeometryShader* GeometryShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{

}

void FEmptyDynamicRHI::RHISetShaderParameter(FRHIComputeShader* ComputeShaderRHI,uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{

}

void FEmptyDynamicRHI::RHISetShaderUniformBuffer(FRHIVertexShader* VertexShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	FEmptyUniformBuffer* Buffer = ResourceCast(BufferRHI);
	FEmptyVertexShader* VertexShader = ResourceCast(VertexShaderRHI);

}

void FEmptyDynamicRHI::RHISetShaderUniformBuffer(FRHIHullShader* HullShader, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	FEmptyUniformBuffer* Buffer = ResourceCast(BufferRHI);
}

void FEmptyDynamicRHI::RHISetShaderUniformBuffer(FRHIDomainShader* DomainShader, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	FEmptyUniformBuffer* Buffer = ResourceCast(BufferRHI);
}

void FEmptyDynamicRHI::RHISetShaderUniformBuffer(FRHIGeometryShader* GeometryShader, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	FEmptyUniformBuffer* Buffer = ResourceCast(BufferRHI);
}

void FEmptyDynamicRHI::RHISetShaderUniformBuffer(FRHIPixelShader* PixelShader, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	FEmptyUniformBuffer* Buffer = ResourceCast(BufferRHI);
}

void FEmptyDynamicRHI::RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShader, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	FEmptyUniformBuffer* Buffer = ResourceCast(BufferRHI);
}


void FEmptyDynamicRHI::RHISetDepthStencilState(FRHIDepthStencilState* NewStateRHI, uint32 StencilRef)
{
	FEmptyDepthStencilState* NewState = ResourceCast(NewStateRHI);

}

void FEmptyDynamicRHI::RHISetBlendState(FRHIBlendState* NewStateRHI, const FLinearColor& BlendFactor)
{
	FEmptyBlendState* NewState = ResourceCast(NewStateRHI);

}


void FEmptyDynamicRHI::RHISetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets, 
	FRHITexture* NewDepthStencilTargetRHI, uint32 NumUAVs, FRHIUnorderedAccessView* const* UAVs)
{

}

void FEmptyDynamicRHI::RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo)
{
}

// Occlusion/Timer queries.
void FEmptyDynamicRHI::RHIBeginRenderQuery(FRHIRenderQuery* QueryRHI)
{
	FEmptyRenderQuery* Query = ResourceCast(QueryRHI);

	Query->Begin();
}

void FEmptyDynamicRHI::RHIEndRenderQuery(FRHIRenderQuery* QueryRHI)
{
	FEmptyRenderQuery* Query = ResourceCast(QueryRHI);

	Query->End();
}


void FEmptyDynamicRHI::RHIDrawPrimitive(uint32 PrimitiveType, uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)
{
}

void FEmptyDynamicRHI::RHIDrawPrimitiveIndirect(uint32 PrimitiveType, FRHIVertexBuffer* ArgumentBufferRHI, uint32 ArgumentOffset)
{
	FEmptyVertexBuffer* ArgumentBuffer = ResourceCast(ArgumentBufferRHI);

}


void FEmptyDynamicRHI::RHIDrawIndexedPrimitive(FRHIIndexBuffer* IndexBufferRHI, uint32 PrimitiveType, int32 BaseVertexIndex, uint32 FirstInstance,
	uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	FEmptyIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);

}

void FEmptyDynamicRHI::RHIDrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, uint32 PrimitiveType, FRHIStructuredBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances)
{
	FEmptyIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	FEmptyStructuredBuffer* ArgumentsBuffer = ResourceCast(ArgumentsBufferRHI);

}

void FEmptyDynamicRHI::RHIDrawIndexedPrimitiveIndirect(uint32 PrimitiveType, FRHIIndexBuffer* IndexBufferRHI, FRHIVertexBuffer* ArgumentBufferRHI,uint32 ArgumentOffset)
{
	FEmptyIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	FEmptyVertexBuffer* ArgumentBuffer = ResourceCast(ArgumentBufferRHI);

}


/** Some locally global variables to track the pending primitive information uised in RHIEnd*UP functions */
static void *GPendingDrawPrimitiveUPVertexData = NULL;
static uint32 GPendingNumVertices;
static uint32 GPendingVertexDataStride;

static void *GPendingDrawPrimitiveUPIndexData = NULL;
static uint32 GPendingPrimitiveType;
static uint32 GPendingNumPrimitives;
static uint32 GPendingMinVertexIndex;
static uint32 GPendingIndexDataStride;

void FEmptyDynamicRHI::RHIBeginDrawPrimitiveUP( uint32 PrimitiveType, uint32 NumPrimitives, uint32 NumVertices, uint32 VertexDataStride, void*& OutVertexData)
{
	checkSlow(GPendingDrawPrimitiveUPVertexData == NULL);
//	GPendingDrawPrimitiveUPVertexData = 
	OutVertexData = GPendingDrawPrimitiveUPVertexData;

	GPendingPrimitiveType = PrimitiveType;
	GPendingNumPrimitives = NumPrimitives;
	GPendingNumVertices = NumVertices;
	GPendingVertexDataStride = VertexDataStride;
}


void FEmptyDynamicRHI::RHIEndDrawPrimitiveUP()
{

	// free used mem
	GPendingDrawPrimitiveUPVertexData = NULL;
}

void FEmptyDynamicRHI::RHIBeginDrawIndexedPrimitiveUP( uint32 PrimitiveType, uint32 NumPrimitives, uint32 NumVertices, uint32 VertexDataStride, void*& OutVertexData, uint32 MinVertexIndex, uint32 NumIndices, uint32 IndexDataStride, void*& OutIndexData)
{
	checkSlow(GPendingDrawPrimitiveUPVertexData == NULL);
	checkSlow(GPendingDrawPrimitiveUPIndexData == NULL);

//	GPendingDrawPrimitiveUPVertexData = 
	OutVertexData = GPendingDrawPrimitiveUPVertexData;

//	GPendingDrawPrimitiveUPIndexData = 
	OutIndexData = GPendingDrawPrimitiveUPIndexData;

	GPendingPrimitiveType = PrimitiveType;
	GPendingNumPrimitives = NumPrimitives;
	GPendingMinVertexIndex = MinVertexIndex;
	GPendingIndexDataStride = IndexDataStride;

	GPendingNumVertices = NumVertices;
	GPendingVertexDataStride = VertexDataStride;
}

void FEmptyDynamicRHI::RHIEndDrawIndexedPrimitiveUP()
{

	// free used mem
	GPendingDrawPrimitiveUPIndexData = NULL;
	GPendingDrawPrimitiveUPVertexData = NULL;
}

void FEmptyDynamicRHI::RHIClearMRT(bool bClearColor,int32 NumClearColors,const FLinearColor* ClearColorArray,bool bClearDepth,float Depth,bool bClearStencil,uint32 Stencil)
{

}

void FEmptyDynamicRHI::RHIBindClearMRTValues(bool bClearColor, bool bClearDepth, bool bClearStencil)
{
}

void FEmptyDynamicRHI::RHIBlockUntilGPUIdle()
{

}

uint32 FEmptyDynamicRHI::RHIGetGPUFrameCycles()
{
	return GGPUFrameTime;
}

void FEmptyDynamicRHI::RHIAutomaticCacheFlushAfterComputeShader(bool bEnable) 
{

}

void FEmptyDynamicRHI::RHIFlushComputeShaderCache()
{

}

void* FEmptyDynamicRHI::RHIGetNativeDevice()
{
	return nullptr;
}

void FEmptyDynamicRHI::RHIExecuteCommandList(FRHICommandList* CmdList)
{
}

void FEmptyDynamicRHI::RHIEnableDepthBoundsTest(bool bEnable, float MinDepth, float MaxDepth)
{
}
