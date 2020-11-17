// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLStructuredBuffer.cpp: OpenGL Index buffer RHI implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Containers/ResourceArray.h"
#include "OpenGLDrv.h"

FStructuredBufferRHIRef FOpenGLDynamicRHI::RHICreateStructuredBuffer(uint32 Stride,uint32 Size,uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	VERIFY_GL_SCOPE();

	const void *Data = NULL;

	// If a resource array was provided for the resource, create the resource pre-populated
	if(CreateInfo.ResourceArray)
	{
		check(Size == CreateInfo.ResourceArray->GetResourceDataSize());
		Data = CreateInfo.ResourceArray->GetResourceData();
	}

	TRefCountPtr<FOpenGLBuffer> StructuredBuffer = new FOpenGLBuffer(GL_ARRAY_BUFFER, Stride, Size, InUsage | BUF_StructuredBuffer, Data);
	return StructuredBuffer.GetReference();
}

void* FOpenGLDynamicRHI::LockStructuredBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	VERIFY_GL_SCOPE();
	FOpenGLBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
	return StructuredBuffer->Lock(Offset, Size, LockMode == RLM_ReadOnly, StructuredBuffer->IsDynamic());
}

void FOpenGLDynamicRHI::UnlockStructuredBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBufferRHI)
{
	VERIFY_GL_SCOPE();
	FOpenGLBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
	StructuredBuffer->Unlock();
}
