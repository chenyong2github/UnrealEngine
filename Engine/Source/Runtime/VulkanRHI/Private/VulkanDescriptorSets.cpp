// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanDescriptorSets.cpp: Vulkan descriptor set RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanDescriptorSets.h"


int32 GVulkanBindlessMaxSamplerDescriptorCount = 2048;
static FAutoConsoleVariableRef CVarVulkanBindlessMaxSamplerDescriptorCount(
	TEXT("r.Vulkan.Bindless.MaxSamplerDescriptorCount"),
	GVulkanBindlessMaxSamplerDescriptorCount,
	TEXT("Maximum bindless sampler descriptor count"),
	ECVF_ReadOnly
);

int32 GVulkanBindlessMaxSampledImageDescriptorCount = 256 * 1024;
static FAutoConsoleVariableRef CVarVulkanBindlessMaxSampledImageCount(
	TEXT("r.Vulkan.Bindless.MaxResourceSampledImageCount"),
	GVulkanBindlessMaxSampledImageDescriptorCount,
	TEXT("Maximum bindless Sampled Image descriptor count"),
	ECVF_ReadOnly
);

int32 GVulkanBindlessMaxStorageImageDescriptorCount = 64 * 1024;
static FAutoConsoleVariableRef CVarVulkanBindlessMaxStorageImageCount(
	TEXT("r.Vulkan.Bindless.MaxResourceStorageImageCount"),
	GVulkanBindlessMaxStorageImageDescriptorCount,
	TEXT("Maximum bindless Storage Image descriptor count"),
	ECVF_ReadOnly
);

int32 GVulkanBindlessMaxUniformTexelBufferDescriptorCount = 64 * 1024;
static FAutoConsoleVariableRef CVarVulkanBindlessMaxUniformTexelBufferCount(
	TEXT("r.Vulkan.Bindless.MaxResourceUniformTexelBufferCount"),
	GVulkanBindlessMaxUniformTexelBufferDescriptorCount,
	TEXT("Maximum bindless Uniform Texel Buffer descriptor count"),
	ECVF_ReadOnly
);

int32 GVulkanBindlessMaxStorageTexelBufferDescriptorCount = 64 * 1024;
static FAutoConsoleVariableRef CVarVulkanBindlessMaxStorageTexelBufferCount(
	TEXT("r.Vulkan.Bindless.MaxResourceStorageTexelBufferCount"),
	GVulkanBindlessMaxStorageTexelBufferDescriptorCount,
	TEXT("Maximum bindless Storage Texel Buffer descriptor count"),
	ECVF_ReadOnly
);

int32 GVulkanBindlessMaxUniformBufferDescriptorCount = 2 * 1024 * 1024;
static FAutoConsoleVariableRef CVarVulkanBindlessMaxUniformBufferCount(
	TEXT("r.Vulkan.Bindless.MaxResourceUniformBufferCount"),
	GVulkanBindlessMaxUniformBufferDescriptorCount,
	TEXT("Maximum bindless Uniform Buffer descriptor count"),
	ECVF_ReadOnly
);

int32 GVulkanBindlessMaxStorageBufferDescriptorCount = 64 * 1024;
static FAutoConsoleVariableRef CVarVulkanBindlessMaxStorageBufferCount(
	TEXT("r.Vulkan.Bindless.MaxResourceStorageBufferCount"),
	GVulkanBindlessMaxStorageBufferDescriptorCount,
	TEXT("Maximum bindless Storage Buffer descriptor count"),
	ECVF_ReadOnly
);

int32 GVulkanBindlessMaxAccelerationStructureDescriptorCount = 64 * 1024;
static FAutoConsoleVariableRef CVarVulkanBindlessMaxAccelerationStructureCount(
	TEXT("r.Vulkan.Bindless.MaxResourceAccelerationStructureCount"),
	GVulkanBindlessMaxAccelerationStructureDescriptorCount,
	TEXT("Maximum bindless Acceleration Structure descriptor count"),
	ECVF_ReadOnly
);


int32 GVulkanBindlessMaxUniformBuffersPerStage = 32;
static FAutoConsoleVariableRef CVarVulkanBindlessMaxUniformBuffersPerStage(
	TEXT("r.Vulkan.Bindless.MaxUniformBuffersPerStage"),
	GVulkanBindlessMaxUniformBuffersPerStage,
	TEXT("Maximum Uniform Buffers per shader stage"),
	ECVF_ReadOnly
);


static inline uint32 GetInitialDescriptorCount(VkDescriptorType DescriptorType)
{
	switch (DescriptorType)
	{
	case VK_DESCRIPTOR_TYPE_SAMPLER:                    return GVulkanBindlessMaxSamplerDescriptorCount;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:              return GVulkanBindlessMaxSampledImageDescriptorCount;
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:              return GVulkanBindlessMaxStorageImageDescriptorCount;
	case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:       return GVulkanBindlessMaxUniformTexelBufferDescriptorCount;
	case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:       return GVulkanBindlessMaxStorageTexelBufferDescriptorCount;
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:             return GVulkanBindlessMaxUniformBufferDescriptorCount;
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:             return GVulkanBindlessMaxStorageBufferDescriptorCount;
	case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: return GVulkanBindlessMaxAccelerationStructureDescriptorCount;
	default: checkNoEntry();
	}
	return 0;
}


static inline VkMemoryPropertyFlags GetDescriptorBufferMemoryType(FVulkanDevice* Device)
{
	if (Device->HasUnifiedMemory() || (FVulkanPlatform::SupportsDeviceLocalHostVisibleWithNoPenalty(Device->GetVendorId()) &&
		Device->GetDeviceMemoryManager().SupportsMemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))
	{
		return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}
	else
	{
		return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}
}





FVulkanBindlessDescriptorManager::FVulkanBindlessDescriptorManager(FVulkanDevice* InDevice)
	: VulkanRHI::FDeviceChild(InDevice)
	, bIsSupported(VerifySupport(InDevice))
{
	const bool bFullyDisabled = (RHIGetBindlessResourcesConfiguration(GMaxRHIShaderPlatform) == ERHIBindlessConfiguration::Disabled) &&
		(RHIGetBindlessSamplersConfiguration(GMaxRHIShaderPlatform) == ERHIBindlessConfiguration::Disabled);
	const bool bFullyEnabled = (RHIGetBindlessResourcesConfiguration(GMaxRHIShaderPlatform) == ERHIBindlessConfiguration::AllShaders) &&
		(RHIGetBindlessSamplersConfiguration(GMaxRHIShaderPlatform) == ERHIBindlessConfiguration::AllShaders);
	checkf(bFullyDisabled || bFullyEnabled, TEXT("Bindless must be fully enabled or fully disabled in Vulkan because of the way it uses descriptor buffers."));
}

FVulkanBindlessDescriptorManager::~FVulkanBindlessDescriptorManager()
{
	check(BindlessPipelineLayout == VK_NULL_HANDLE);
}

void FVulkanBindlessDescriptorManager::Deinit()
{
}

bool FVulkanBindlessDescriptorManager::VerifySupport(FVulkanDevice* Device)
{
	return false;
}

void FVulkanBindlessDescriptorManager::Init()
{
}

void FVulkanBindlessDescriptorManager::BindDescriptorBuffers(VkCommandBuffer CommandBuffer, VkPipelineStageFlags SupportedStages)
{
}

void FVulkanBindlessDescriptorManager::RegisterUniformBuffers(VkCommandBuffer CommandBuffer, VkPipelineBindPoint BindPoint, ShaderStage::EStage Stage, const FVulkanDescriptorSetWriter& SetWriter)
{
}


FRHIDescriptorHandle FVulkanBindlessDescriptorManager::RegisterSampler(VkSampler VulkanSampler)
{
	return FRHIDescriptorHandle();
}

FRHIDescriptorHandle FVulkanBindlessDescriptorManager::RegisterImage(VkImageView ImageView, VkDescriptorType DescriptorType, bool bIsDepthStencil)
{
	return FRHIDescriptorHandle();
}

FRHIDescriptorHandle FVulkanBindlessDescriptorManager::RegisterBuffer(VkBuffer VulkanBuffer, VkDeviceSize BufferOffset, VkDeviceSize BufferSize, VkDescriptorType DescriptorType)
{
	return FRHIDescriptorHandle();
}

FRHIDescriptorHandle FVulkanBindlessDescriptorManager::RegisterTexelBuffer(const VkBufferViewCreateInfo& ViewInfo, VkDescriptorType DescriptorType)
{
	return FRHIDescriptorHandle();
}

FRHIDescriptorHandle FVulkanBindlessDescriptorManager::RegisterAccelerationStructure(VkAccelerationStructureKHR AccelerationStructure)
{
	return FRHIDescriptorHandle();
}


uint32 FVulkanBindlessDescriptorManager::GetFreeResourceIndex(FVulkanBindlessDescriptorManager::BindlessSetState& State)
{
	{
		FScopeLock ScopeLock(&State.FreeListCS);
		if ((State.FreeListHead != MAX_uint32) && (State.PeakDescriptorCount >= State.MaxDescriptorCount)) // todo-jn: temp
		{
			const uint32 FreeIndex = State.FreeListHead;
			const uint32 ByteOffset = State.FreeListHead * State.DescriptorSize;
			uint32* NextSlot = (uint32*)(&State.DebugDescriptors[ByteOffset]);
			State.FreeListHead = *NextSlot;
			return FreeIndex;
		}
	}

	const uint32 ResourceIndex = State.PeakDescriptorCount++;
	checkf(ResourceIndex < State.MaxDescriptorCount, TEXT("You need to grow the resource array size for [%s]!"), VK_TYPE_TO_STRING(VkDescriptorType, State.DescriptorType));
	return ResourceIndex;
}

void FVulkanBindlessDescriptorManager::Unregister(FRHIDescriptorHandle DescriptorHandle)
{
}

void FVulkanBindlessDescriptorManager::CopyDescriptor(VkCommandBuffer CommandBuffer, FRHIDescriptorHandle DstHandle, const FRHIDescriptorHandle SrcHandle)
{
}
