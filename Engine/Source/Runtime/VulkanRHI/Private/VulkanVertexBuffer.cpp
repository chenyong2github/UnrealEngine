// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanVertexBuffer.cpp: Vulkan vertex buffer RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanLLM.h"

FVertexBufferRHIRef FVulkanDynamicRHI::RHICreateVertexBuffer(uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanVertexBuffers);
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FVulkanResourceMultiBuffer(nullptr, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, 0, 0, CreateInfo);
	}
	FVulkanResourceMultiBuffer* VertexBuffer = new FVulkanResourceMultiBuffer(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Size, InUsage | BUF_VertexBuffer, 0, CreateInfo);
	return VertexBuffer;
}

void* FVulkanDynamicRHI::LockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanVertexBuffers);
	FVulkanResourceMultiBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	return VertexBuffer->Lock(false, LockMode, Size, Offset);
}

#if VULKAN_BUFFER_LOCK_THREADSAFE
void* FVulkanDynamicRHI::LockVertexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
{
	return this->RHILockVertexBuffer(VertexBufferRHI, Offset, SizeRHI, LockMode);
}
#endif

void FVulkanDynamicRHI::UnlockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanVertexBuffers);
	FVulkanResourceMultiBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	VertexBuffer->Unlock(false);
}

#if VULKAN_BUFFER_LOCK_THREADSAFE
void FVulkanDynamicRHI::UnlockVertexBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI)
{
	return this->RHIUnlockVertexBuffer(VertexBufferRHI);
}
#endif

void FVulkanDynamicRHI::RHICopyVertexBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIVertexBuffer* DestBufferRHI)
{
	VULKAN_SIGNAL_UNIMPLEMENTED();
}
