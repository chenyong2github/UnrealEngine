// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanVertexBuffer.cpp: Vulkan vertex buffer RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanLLM.h"

FVertexBufferRHIRef FVulkanDynamicRHI::RHICreateVertexBuffer(uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanBuffers);
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FVulkanResourceMultiBuffer(nullptr, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, 0, 0, CreateInfo);
	}
	FVulkanResourceMultiBuffer* VertexBuffer = new FVulkanResourceMultiBuffer(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Size, InUsage | BUF_VertexBuffer, 0, CreateInfo);
	return VertexBuffer;
}

void FVulkanDynamicRHI::RHICopyVertexBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIVertexBuffer* DestBufferRHI)
{
	VULKAN_SIGNAL_UNIMPLEMENTED();
}
