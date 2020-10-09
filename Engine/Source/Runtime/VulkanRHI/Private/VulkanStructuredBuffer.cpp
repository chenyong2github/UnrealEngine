// Copyright Epic Games, Inc. All Rights Reserved.


#include "VulkanRHIPrivate.h"
#include "Containers/ResourceArray.h"

FStructuredBufferRHIRef FVulkanDynamicRHI::RHICreateStructuredBuffer(uint32 InStride, uint32 InSize, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	return new FVulkanResourceMultiBuffer(Device, InSize, InUsage | BUF_StructuredBuffer, InStride, CreateInfo, nullptr);
}
