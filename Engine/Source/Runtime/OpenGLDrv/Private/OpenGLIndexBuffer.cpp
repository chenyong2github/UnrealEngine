// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLIndexBuffer.cpp: OpenGL Index buffer RHI implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Containers/ResourceArray.h"
#include "OpenGLDrv.h"

FIndexBufferRHIRef FOpenGLDynamicRHI::RHICreateIndexBuffer(uint32 Stride,uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FOpenGLBuffer();
	}

	const void *Data = NULL;

	// If a resource array was provided for the resource, create the resource pre-populated
	if(CreateInfo.ResourceArray)
	{
		check(Size == CreateInfo.ResourceArray->GetResourceDataSize());
		Data = CreateInfo.ResourceArray->GetResourceData();
	}

	TRefCountPtr<FOpenGLBuffer> IndexBuffer = new FOpenGLBuffer(GL_ELEMENT_ARRAY_BUFFER, Stride, Size, InUsage | BUF_IndexBuffer, Data);

	if (CreateInfo.ResourceArray)
	{
		CreateInfo.ResourceArray->Discard();
	}

	return IndexBuffer.GetReference();
}

FIndexBufferRHIRef FOpenGLDynamicRHI::CreateIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	return this->RHICreateIndexBuffer(Stride, Size, InUsage, InResourceState, CreateInfo);
}

void FOpenGLDynamicRHI::RHITransferBufferUnderlyingResource(FRHIBuffer* DestBuffer, FRHIBuffer* SrcBuffer)
{
	VERIFY_GL_SCOPE();
	check(DestBuffer);
	FOpenGLBuffer* Dest = ResourceCast(DestBuffer);
	if (!SrcBuffer)
	{
		TRefCountPtr<FOpenGLBuffer> Src = new FOpenGLBuffer();
		Dest->Swap(*Src);
	}
	else
	{
		FOpenGLBuffer* Src = ResourceCast(SrcBuffer);
		Dest->Swap(*Src);
	}
}
