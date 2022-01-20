// Copyright Epic Games, Inc. All Rights Reserved.

#include "VulkanRHIPrivate.h"
#include "VulkanTransientResourceAllocator.h"


FVulkanTransientHeap::FVulkanTransientHeap(const FInitializer& Initializer, FVulkanDevice* InDevice)
	: FRHITransientHeap(Initializer)
	, FDeviceChild(InDevice)
	, VulkanBuffer(VK_NULL_HANDLE)
{
	const VkBufferUsageFlags BufferUsageFlags =
#if VULKAN_RHI_RAYTRACING
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
#endif
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	// :TODO: VK_KHR_maintenance4...
	{
		VkBufferCreateInfo BufferCreateInfo;
		ZeroVulkanStruct(BufferCreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
		BufferCreateInfo.size = Initializer.Size;
		BufferCreateInfo.usage = BufferUsageFlags;

		const VkDevice VulkanDevice = InDevice->GetInstanceHandle();

		VERIFYVULKANRESULT(VulkanRHI::vkCreateBuffer(VulkanDevice, &BufferCreateInfo, VULKAN_CPU_ALLOCATOR, &VulkanBuffer));
		VulkanRHI::vkGetBufferMemoryRequirements(VulkanDevice, VulkanBuffer, &MemoryRequirements);

		// Find the alignment that works for everyone
		const uint32 MinBufferAlignment = FMemoryManager::CalculateBufferAlignment(*InDevice, BufferCreateInfo.usage);
		MemoryRequirements.alignment = FMath::Max<VkDeviceSize>(Initializer.Alignment, MemoryRequirements.alignment);
		MemoryRequirements.alignment = FMath::Max<VkDeviceSize>(MinBufferAlignment, MemoryRequirements.alignment);
	}

	VkMemoryPropertyFlags BufferMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	const bool bUnifiedMem = InDevice->HasUnifiedMemory();
	if (bUnifiedMem)
	{
		BufferMemFlags |= (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	if (!InDevice->GetMemoryManager().AllocateBufferMemory(InternalAllocation, nullptr, MemoryRequirements, BufferMemFlags, EVulkanAllocationMetaBufferOther, false, __FILE__, __LINE__))
	{
		InDevice->GetMemoryManager().HandleOOM();
	}

	InternalAllocation.BindBuffer(InDevice, VulkanBuffer);
}

FVulkanTransientHeap::~FVulkanTransientHeap()
{
	Device->GetMemoryManager().FreeVulkanAllocation(InternalAllocation);
	Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Buffer, VulkanBuffer);
	VulkanBuffer = VK_NULL_HANDLE;
}

VkDeviceMemory FVulkanTransientHeap::GetMemoryHandle()
{
	return InternalAllocation.GetDeviceMemoryHandle(Device);
}

FVulkanAllocation FVulkanTransientHeap::GetVulkanAllocation(const FRHITransientHeapAllocation& HeapAllocation)
{
	FVulkanTransientHeap* Heap = static_cast<FVulkanTransientHeap*>(HeapAllocation.Heap);
	check(Heap);

	FVulkanAllocation TransientAlloc;
	TransientAlloc.Reference(Heap->InternalAllocation);
	TransientAlloc.VulkanHandle = (uint64)Heap->VulkanBuffer;
	TransientAlloc.Offset += HeapAllocation.Offset;
	TransientAlloc.Size = HeapAllocation.Size;
	check((TransientAlloc.Offset + TransientAlloc.Size) <= Heap->InternalAllocation.Size);
	return TransientAlloc;
}

FVulkanTransientHeapCache* FVulkanTransientHeapCache::Create(FVulkanDevice* InDevice)
{
	FRHITransientHeapCache::FInitializer Initializer = FRHITransientHeapCache::FInitializer::CreateDefault();

	// Respect a minimum alignment
	Initializer.HeapAlignment = FMath::Max((uint32)InDevice->GetLimits().bufferImageGranularity, 256u);

	// Mix resource types onto the same heap.
	Initializer.bSupportsAllHeapFlags = true;

	return new FVulkanTransientHeapCache(Initializer, InDevice);
}

FVulkanTransientHeapCache::FVulkanTransientHeapCache(const FRHITransientHeapCache::FInitializer& Initializer, FVulkanDevice* InDevice)
	: FRHITransientHeapCache(Initializer)
	, FDeviceChild(InDevice)
{
}

FRHITransientHeap* FVulkanTransientHeapCache::CreateHeap(const FRHITransientHeap::FInitializer& HeapInitializer)
{
	return new FVulkanTransientHeap(HeapInitializer, Device);
}


FVulkanTransientResourceAllocator::FVulkanTransientResourceAllocator(FVulkanTransientHeapCache& InHeapCache)
	: FRHITransientResourceHeapAllocator(InHeapCache)
	, FDeviceChild(InHeapCache.GetParent())
{
}

FRHITransientTexture* FVulkanTransientResourceAllocator::CreateTexture(const FRHITextureCreateInfo& InCreateInfo, const TCHAR* InDebugName, uint32 InPassIndex)
{
	uint32 ReqAlign = 1;
	uint64 ReqSize = GVulkanRHI->RHICalcTexturePlatformSize(InCreateInfo, ReqAlign);

	return CreateTextureInternal(InCreateInfo, InDebugName, InPassIndex, ReqSize, ReqAlign,
		[&](const FRHITransientHeap::FResourceInitializer& Initializer)
	{
		ERHIAccess InitialState = ERHIAccess::UAVMask;
		if (EnumHasAnyFlags(InCreateInfo.Flags, TexCreate_RenderTargetable | TexCreate_ResolveTargetable))
		{
			InitialState = ERHIAccess::RTV;
		}
		else if (EnumHasAnyFlags(InCreateInfo.Flags, TexCreate_DepthStencilTargetable))
		{
			InitialState = ERHIAccess::DSVWrite;
		}

		FRHIResourceCreateInfo ResourceCreateInfo(InDebugName, InCreateInfo.ClearValue);
		FRHITexture* Texture = GVulkanRHI->CreateTexture(InCreateInfo, ResourceCreateInfo, InitialState, &Initializer.Allocation);
		return new FRHITransientTexture(Texture, 0/*GpuVirtualAddress*/, Initializer.Hash, ReqSize, ERHITransientAllocationType::Heap, InCreateInfo);
	});
}

FRHITransientBuffer* FVulkanTransientResourceAllocator::CreateBuffer(const FRHIBufferCreateInfo& InCreateInfo, const TCHAR* InDebugName, uint32 InPassIndex)
{
	const VkBufferUsageFlags VulkanBufferUsage = FVulkanResourceMultiBuffer::UEToVKBufferUsageFlags(Device, InCreateInfo.Usage, (InCreateInfo.Size == 0));
	const uint32 Alignment = FMemoryManager::CalculateBufferAlignment(*Device, VulkanBufferUsage);
	uint64 Size = Align(InCreateInfo.Size, Alignment) * FVulkanResourceMultiBuffer::GetNumBuffersFromUsage(InCreateInfo.Usage);

	return CreateBufferInternal(InCreateInfo, InDebugName, InPassIndex, Size, Alignment,
		[&](const FRHITransientHeap::FResourceInitializer& Initializer)
	{
		FRHIResourceCreateInfo ResourceCreateInfo(InDebugName);
		FRHIBuffer* Buffer = GVulkanRHI->CreateBuffer(InCreateInfo, ResourceCreateInfo, &Initializer.Allocation);
		return new FRHITransientBuffer(Buffer, 0/*GpuVirtualAddress*/, Initializer.Hash, Size, ERHITransientAllocationType::Heap, InCreateInfo);
	});
}
