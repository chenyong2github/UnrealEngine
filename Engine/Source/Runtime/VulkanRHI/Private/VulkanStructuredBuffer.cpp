// Copyright Epic Games, Inc. All Rights Reserved.


#include "VulkanRHIPrivate.h"
#include "Containers/ResourceArray.h"

FStructuredBufferRHIRef FVulkanDynamicRHI::RHICreateStructuredBuffer(uint32 InStride, uint32 InSize, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	return new FVulkanResourceMultiBuffer(Device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, InSize, InUsage | BUF_StructuredBuffer, InStride, CreateInfo);
}

void* FVulkanDynamicRHI::LockStructuredBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	FVulkanResourceMultiBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	return StructuredBuffer->Lock(false, LockMode, Size, Offset);
}

void FVulkanDynamicRHI::UnlockStructuredBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBufferRHI)
{
	FVulkanResourceMultiBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	StructuredBuffer->Unlock(false);
}
