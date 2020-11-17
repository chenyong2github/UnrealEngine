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

void* FOpenGLDynamicRHI::LockIndexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	RHITHREAD_GLCOMMAND_PROLOGUE();
	VERIFY_GL_SCOPE();
	FOpenGLBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	return IndexBuffer->Lock(Offset, Size, LockMode == RLM_ReadOnly, IndexBuffer->IsDynamic());
	RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(void*);
}

void FOpenGLDynamicRHI::UnlockIndexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI)
{
	RHITHREAD_GLCOMMAND_PROLOGUE();
	VERIFY_GL_SCOPE();
	FOpenGLBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	IndexBuffer->Unlock();
	RHITHREAD_GLCOMMAND_EPILOGUE();
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
