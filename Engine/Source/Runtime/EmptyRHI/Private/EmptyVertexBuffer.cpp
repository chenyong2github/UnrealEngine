// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	EmptyVertexBuffer.cpp: Empty vertex buffer RHI implementation.
=============================================================================*/

#include "EmptyRHIPrivate.h"


FEmptyVertexBuffer::FEmptyVertexBuffer(uint32 InSize, uint32 InUsage)
	: FRHIBuffer(InSize, InUsage)
{
}

void* FEmptyVertexBuffer::Lock(EResourceLockMode LockMode, uint32 Size)
{
	return NULL;
}

void FEmptyVertexBuffer::Unlock()
{
	
}

FBufferRHIRef FEmptyDynamicRHI::RHICreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	if (CreateInfo.bCreateRHIObjectOnly)
	{
		return new FEmptyVertexBuffer();
	}

	// make the RHI object, which will allocate memory
	FEmptyVertexBuffer* VertexBuffer = new FEmptyVertexBuffer(Size, InUsage);

	if(CreateInfo.ResourceArray)
	{
		check(Size == CreateInfo.ResourceArray->GetResourceDataSize());

		// make a buffer usable by CPU
		void* Buffer = RHILockBuffer(VertexBuffer, 0, Size, RLM_WriteOnly);

		// copy the contents of the given data into the buffer
		FMemory::Memcpy(Buffer, CreateInfo.ResourceArray->GetResourceData(), Size);

		RHIUnlockBuffer(VertexBuffer);

		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}

	return VertexBuffer;
}

void* FEmptyDynamicRHI::LockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIBuffer* VertexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	FEmptyVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	// default to vertex buffer memory
	return (uint8*)VertexBuffer->Lock(LockMode, Size) + Offset;
}

void FEmptyDynamicRHI::UnlockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIBuffer* VertexBufferRHI)
{
	FEmptyVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	VertexBuffer->Unlock();
}

void FEmptyDynamicRHI::RHICopyBuffer(FRHIBuffer* SourceBufferRHI, FRHIBuffer* DestBufferRHI)
{

}

void FEmptyDynamicRHI::RHITransferBufferUnderlyingResource(FRHIBuffer* DestBuffer, FRHIBuffer* SrcBuffer)
{

}