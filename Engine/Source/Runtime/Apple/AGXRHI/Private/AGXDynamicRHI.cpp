// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXDynamicRHI.cpp: AGX Dynamic RHI Class Implementation.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "AGXRHIRenderQuery.h"
#include "AGXRHIStagingBuffer.h"
#include "AGXShaderTypes.h"
#include "AGXVertexDeclaration.h"
#include "AGXGraphicsPipelineState.h"
#include "AGXComputePipelineState.h"
#include "AGXTransitionData.h"


//------------------------------------------------------------------------------

#pragma mark - AGX Dynamic RHI Vertex Declaration Methods -


FVertexDeclarationRHIRef FAGXDynamicRHI::RHICreateVertexDeclaration(const FVertexDeclarationElementList& Elements)
{
	@autoreleasepool {
		uint32 Key = FCrc::MemCrc32(Elements.GetData(), Elements.Num() * sizeof(FVertexElement));

		// look up an existing declaration
		FVertexDeclarationRHIRef* VertexDeclarationRefPtr = VertexDeclarationCache.Find(Key);
		if (VertexDeclarationRefPtr == NULL)
		{
			// create and add to the cache if it doesn't exist.
			VertexDeclarationRefPtr = &VertexDeclarationCache.Add(Key, new FAGXVertexDeclaration(Elements));
		}

		return *VertexDeclarationRefPtr;
	} // autoreleasepool
}


//------------------------------------------------------------------------------

#pragma mark - AGX Dynamic RHI Pipeline State Methods -


FGraphicsPipelineStateRHIRef FAGXDynamicRHI::RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer)
{
	@autoreleasepool {
		FAGXGraphicsPipelineState* State = new FAGXGraphicsPipelineState(Initializer);

		if(!State->Compile())
		{
			// Compilation failures are propagated up to the caller.
			State->Delete();
			return nullptr;
		}

		State->VertexDeclaration = ResourceCast(Initializer.BoundShaderState.VertexDeclarationRHI);
		State->VertexShader = ResourceCast(Initializer.BoundShaderState.VertexShaderRHI);
		State->PixelShader = ResourceCast(Initializer.BoundShaderState.PixelShaderRHI);
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		State->GeometryShader = ResourceCast(Initializer.BoundShaderState.GetGeometryShader());
#endif // PLATFORM_SUPPORTS_GEOMETRY_SHADERS

		State->DepthStencilState = ResourceCast(Initializer.DepthStencilState);
		State->RasterizerState = ResourceCast(Initializer.RasterizerState);

		return State;
	} // autoreleasepool
}

TRefCountPtr<FRHIComputePipelineState> FAGXDynamicRHI::RHICreateComputePipelineState(FRHIComputeShader* ComputeShader)
{
	@autoreleasepool {
		return new FAGXComputePipelineState(ResourceCast(ComputeShader));
	} // autoreleasepool
}


//------------------------------------------------------------------------------

#pragma mark - AGX Dynamic RHI Staging Buffer Methods -


FStagingBufferRHIRef FAGXDynamicRHI::RHICreateStagingBuffer()
{
	return new FAGXRHIStagingBuffer();
}

void* FAGXDynamicRHI::RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	FAGXRHIStagingBuffer* Buffer = ResourceCast(StagingBuffer);
	return Buffer->Lock(Offset, SizeRHI);
}

void FAGXDynamicRHI::RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
{
	FAGXRHIStagingBuffer* Buffer = ResourceCast(StagingBuffer);
	Buffer->Unlock();
}


//------------------------------------------------------------------------------

#pragma mark - AGX Dynamic RHI Resource Transition Methods -


void FAGXDynamicRHI::RHICreateTransition(FRHITransition* Transition, const FRHITransitionCreateInfo& CreateInfo)
{
	// Construct the data in-place on the transition instance
	new (Transition->GetPrivateData<FAGXTransitionData>()) FAGXTransitionData(CreateInfo.SrcPipelines, CreateInfo.DstPipelines, CreateInfo.Flags, CreateInfo.TransitionInfos);
}

void FAGXDynamicRHI::RHIReleaseTransition(FRHITransition* Transition)
{
	// Destruct the private data object of the transition instance.
	Transition->GetPrivateData<FAGXTransitionData>()->~FAGXTransitionData();
}


//------------------------------------------------------------------------------

#pragma mark - AGX Dynamic RHI Render Query Methods -


FRenderQueryRHIRef FAGXDynamicRHI::RHICreateRenderQuery(ERenderQueryType QueryType)
{
	@autoreleasepool {
		FRenderQueryRHIRef Query = new FAGXRHIRenderQuery(QueryType);
		return Query;
	}
}

FRenderQueryRHIRef FAGXDynamicRHI::RHICreateRenderQuery_RenderThread(class FRHICommandListImmediate& RHICmdList, ERenderQueryType QueryType)
{
	@autoreleasepool {
		return GDynamicRHI->RHICreateRenderQuery(QueryType);
	}
}

bool FAGXDynamicRHI::RHIGetRenderQueryResult(FRHIRenderQuery* QueryRHI, uint64& OutNumPixels, bool bWait, uint32 GPUIndex)
{
	@autoreleasepool {
		check(IsInRenderingThread());
		FAGXRHIRenderQuery* Query = ResourceCast(QueryRHI);
		return Query->GetResult(OutNumPixels, bWait, GPUIndex);
	}
}
