// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanDescriptorSets.cpp: Vulkan descriptor set RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanDescriptorSets.h"


int32 GBindlessMaxResourceDescriptorCount = 1000 * 1000;
static FAutoConsoleVariableRef CVarBindlessResourceDescriptorCount(
	TEXT("r.Vulkan.Bindless.MaxResourceDescriptorCount"),
	GBindlessMaxResourceDescriptorCount,
	TEXT("Maximum bindless resource descriptor count"),
	ECVF_ReadOnly
);

int32 GBindlessMaxSamplerDescriptorCount = 2048;
static FAutoConsoleVariableRef CVarBindlessSamplerDescriptorCount(
	TEXT("r.Vulkan.Bindless.MaxSamplerDescriptorCount"),
	GBindlessMaxSamplerDescriptorCount,
	TEXT("Maximum bindless sampler descriptor count"),
	ECVF_ReadOnly
);


FVulkanBindlessDescriptorManager::FVulkanBindlessDescriptorManager(FVulkanDevice* InDevice)
	: VulkanRHI::FDeviceChild(InDevice)
	, bBindlessResourcesAllowed(RHIGetBindlessResourcesConfiguration(GMaxRHIShaderPlatform) != ERHIBindlessConfiguration::Disabled)
	, bBindlessSamplersAllowed(RHIGetBindlessSamplersConfiguration(GMaxRHIShaderPlatform) != ERHIBindlessConfiguration::Disabled)
{

}

FVulkanBindlessDescriptorManager::~FVulkanBindlessDescriptorManager()
{
	VulkanRHI::vkDestroyPipelineLayout(Device->GetInstanceHandle(), BindlessPipelineLayout, VULKAN_CPU_ALLOCATOR);

	VulkanRHI::vkDestroyDescriptorSetLayout(Device->GetInstanceHandle(), EmptyDescriptorSetLayout, VULKAN_CPU_ALLOCATOR);
	VulkanRHI::vkDestroyDescriptorSetLayout(Device->GetInstanceHandle(), SamplerDescriptorSetLayout, VULKAN_CPU_ALLOCATOR);
	VulkanRHI::vkDestroyDescriptorSetLayout(Device->GetInstanceHandle(), ResourceDescriptorSetLayout, VULKAN_CPU_ALLOCATOR);

	VulkanRHI::vkDestroyDescriptorPool(Device->GetInstanceHandle(), DescriptorPool, VULKAN_CPU_ALLOCATOR);
}

void FVulkanBindlessDescriptorManager::Init()
{
	MaxResourceDescriptors = GBindlessMaxResourceDescriptorCount;
	MaxSamplerDescriptors = GBindlessMaxSamplerDescriptorCount;

	// Create the empty descriptor set (used to pad empty indices)
	{
		VkDescriptorSetLayoutCreateInfo EmptyDescriptorSetLayoutCreateInfo;
		ZeroVulkanStruct(EmptyDescriptorSetLayoutCreateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
		VERIFYVULKANRESULT(VulkanRHI::vkCreateDescriptorSetLayout(Device->GetInstanceHandle(), &EmptyDescriptorSetLayoutCreateInfo, VULKAN_CPU_ALLOCATOR, &EmptyDescriptorSetLayout));
	}

	// Create the descriptor pool for bindless
	{
		const uint32 DescriptorTypeCount = 1;
		VkDescriptorPoolSize DescriptorPoolSize[DescriptorTypeCount];
		DescriptorPoolSize[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
		DescriptorPoolSize[0].descriptorCount = MaxSamplerDescriptors;
		// todo-jn: bindless: add 'real' resource descriptors

		VkDescriptorPoolCreateInfo DescriptorPoolCreateInfo;
		ZeroVulkanStruct(DescriptorPoolCreateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
		DescriptorPoolCreateInfo.maxSets = VulkanBindless::NumBindlessSets;
		DescriptorPoolCreateInfo.poolSizeCount = DescriptorTypeCount;
		DescriptorPoolCreateInfo.pPoolSizes = DescriptorPoolSize;
		DescriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

		VERIFYVULKANRESULT(VulkanRHI::vkCreateDescriptorPool(Device->GetInstanceHandle(), &DescriptorPoolCreateInfo, VULKAN_CPU_ALLOCATOR, &DescriptorPool));
	}

	// Create the sampler descriptor set layout
	{
		VkDescriptorSetLayoutBinding DescriptorSetLayoutBinding;
		DescriptorSetLayoutBinding.binding = 0;
		DescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		DescriptorSetLayoutBinding.descriptorCount = MaxSamplerDescriptors;
		DescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
		DescriptorSetLayoutBinding.pImmutableSamplers = nullptr;

		const VkDescriptorBindingFlags DescriptorBindingFlags =
			VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
			VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
		VkDescriptorSetLayoutBindingFlagsCreateInfo DescriptorSetLayoutBindingFlagsCreateInfo;
		ZeroVulkanStruct(DescriptorSetLayoutBindingFlagsCreateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
		DescriptorSetLayoutBindingFlagsCreateInfo.bindingCount = 1;
		DescriptorSetLayoutBindingFlagsCreateInfo.pBindingFlags = &DescriptorBindingFlags;

		VkDescriptorSetLayoutCreateInfo DescriptorSetLayoutCreateInfo;
		ZeroVulkanStruct(DescriptorSetLayoutCreateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
		DescriptorSetLayoutCreateInfo.pBindings = &DescriptorSetLayoutBinding;
		DescriptorSetLayoutCreateInfo.bindingCount = 1;
		DescriptorSetLayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		DescriptorSetLayoutCreateInfo.pNext = &DescriptorSetLayoutBindingFlagsCreateInfo;

		VERIFYVULKANRESULT(VulkanRHI::vkCreateDescriptorSetLayout(Device->GetInstanceHandle(), &DescriptorSetLayoutCreateInfo, VULKAN_CPU_ALLOCATOR, &SamplerDescriptorSetLayout));
	}

	// Create the sampler descriptor set
	{
		VkDescriptorSetVariableDescriptorCountAllocateInfo VariableDescriptorCountAllocateInfo;
		ZeroVulkanStruct(VariableDescriptorCountAllocateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
		VariableDescriptorCountAllocateInfo.descriptorSetCount = 1;
		VariableDescriptorCountAllocateInfo.pDescriptorCounts = &MaxSamplerDescriptors;

		VkDescriptorSetAllocateInfo DescriptorSetAllocateInfo;
		ZeroVulkanStruct(DescriptorSetAllocateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
		DescriptorSetAllocateInfo.descriptorPool = DescriptorPool;
		DescriptorSetAllocateInfo.descriptorSetCount = 1;
		DescriptorSetAllocateInfo.pSetLayouts = &SamplerDescriptorSetLayout;
		DescriptorSetAllocateInfo.pNext = &VariableDescriptorCountAllocateInfo;

		VERIFYVULKANRESULT(VulkanRHI::vkAllocateDescriptorSets(Device->GetInstanceHandle(), &DescriptorSetAllocateInfo, &DescriptorSets[VulkanBindless::BindlessSamplerSet]));
	}

	// todo-jn: bindless: Create the resource descriptor set and layout
	{
		ResourceDescriptorSetLayout = EmptyDescriptorSetLayout;
		DescriptorSets[VulkanBindless::BindlessResourceSet] = VK_NULL_HANDLE;
	}

	// Now create the basic pipeline
	{
		VkDescriptorSetLayout DescriptorSetLayouts[VulkanBindless::NumBindlessSets];
		DescriptorSetLayouts[VulkanBindless::BindlessSamplerSet] = SamplerDescriptorSetLayout;
		DescriptorSetLayouts[VulkanBindless::BindlessResourceSet] = ResourceDescriptorSetLayout;

		VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo;
		ZeroVulkanStruct(PipelineLayoutCreateInfo, VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
		PipelineLayoutCreateInfo.setLayoutCount = VulkanBindless::NumBindlessSets;
		PipelineLayoutCreateInfo.pSetLayouts = DescriptorSetLayouts;

		VERIFYVULKANRESULT(VulkanRHI::vkCreatePipelineLayout(Device->GetInstanceHandle(), &PipelineLayoutCreateInfo, VULKAN_CPU_ALLOCATOR, &BindlessPipelineLayout));
	}
}

FVulkanBindlessDescriptorManager::BindlessLayoutArray FVulkanBindlessDescriptorManager::GeneratePipelineLayout(const TArray<VkDescriptorSetLayout>& LayoutArray) const
{
	BindlessLayoutArray PatchedArray;
	PatchedArray.Add(SamplerDescriptorSetLayout);
	PatchedArray.Add(ResourceDescriptorSetLayout);
	PatchedArray.Append(LayoutArray);
	return PatchedArray;
}

void FVulkanBindlessDescriptorManager::BindDescriptorSets(VkCommandBuffer CommandBuffer, VkPipelineBindPoint BindPoint)
{
	uint32 FirstDescriptorSet = VulkanBindless::NumBindlessSets;
	uint32 NumDescriptorSets = 0;
	if (bBindlessSamplersAllowed)
	{
		FirstDescriptorSet = FMath::Min<uint32>(VulkanBindless::BindlessSamplerSet, FirstDescriptorSet);
		++NumDescriptorSets;
	}
	if (bBindlessResourcesAllowed)
	{
		FirstDescriptorSet = FMath::Min<uint32>(VulkanBindless::BindlessResourceSet, FirstDescriptorSet);
		++NumDescriptorSets;
	}

	if (NumDescriptorSets > 0)
	{
		VulkanRHI::vkCmdBindDescriptorSets(CommandBuffer, BindPoint, BindlessPipelineLayout, 0, NumDescriptorSets, &DescriptorSets[FirstDescriptorSet], 0, nullptr);
	}
}

FRHIDescriptorHandle FVulkanBindlessDescriptorManager::RegisterSampler(VkSampler VulkanSampler)
{
	const uint32 SamplerIndex = BindlessSamplerCount++;
	checkf(SamplerIndex < MaxSamplerDescriptors, TEXT("You need to grow the sampler array size!"));

	VkDescriptorImageInfo DescriptorImageInfo;
	FMemory::Memzero(DescriptorImageInfo);
	DescriptorImageInfo.sampler = VulkanSampler;

	VkWriteDescriptorSet WriteDescriptorSet;
	ZeroVulkanStruct(WriteDescriptorSet, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
	WriteDescriptorSet.dstSet = DescriptorSets[VulkanBindless::BindlessSamplerSet];
	WriteDescriptorSet.dstBinding = 0;
	WriteDescriptorSet.dstArrayElement = SamplerIndex;
	WriteDescriptorSet.descriptorCount = 1;
	WriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	WriteDescriptorSet.pImageInfo = &DescriptorImageInfo;

	VulkanRHI::vkUpdateDescriptorSets(Device->GetInstanceHandle(), 1, &WriteDescriptorSet, 0, nullptr);
	return FRHIDescriptorHandle(ERHIDescriptorHeapType::Sampler, SamplerIndex);
}