// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanUniformBuffer.cpp: Vulkan Constant buffer implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanContext.h"
#include "VulkanLLM.h"

static int32 GVulkanAllowUniformUpload = 0;
static FAutoConsoleVariableRef CVarVulkanAllowUniformUpload(
	TEXT("r.Vulkan.AllowUniformUpload"),
	GVulkanAllowUniformUpload,
	TEXT("Allow Uniform Buffer uploads outside of renderpasses\n")
	TEXT(" 0: Disabled, buffers are always reallocated\n")
	TEXT(" 1: Enabled, buffers are uploaded outside renderpasses"),
	ECVF_Default
);

enum
{
#if PLATFORM_DESKTOP
	PackedUniformsRingBufferSize = 16 * 1024 * 1024,
#else
	PackedUniformsRingBufferSize = 8 * 1024 * 1024,
#endif
};

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
constexpr EUniformBufferValidation UniformBufferValidation = EUniformBufferValidation::ValidateResources;
#else
constexpr EUniformBufferValidation UniformBufferValidation = EUniformBufferValidation::None;
#endif

static void ValidateUniformBufferResource(const FRHIUniformBufferLayout& InLayout, int32 Index, FRHIResource* Resource, EUniformBufferValidation Validation)
{
	// Allow null SRV's in uniform buffers for feature levels that don't support SRV's in shaders
	if (!(GMaxRHIFeatureLevel <= ERHIFeatureLevel::ES3_1
		&& (InLayout.Resources[Index].MemberType == UBMT_SRV || InLayout.Resources[Index].MemberType == UBMT_RDG_TEXTURE_SRV || InLayout.Resources[Index].MemberType == UBMT_RDG_BUFFER_SRV))
		&& Validation == EUniformBufferValidation::ValidateResources)
	{
		checkf(Resource, TEXT("Invalid resource entry creating uniform buffer, %s.Resources[%u], ResourceType 0x%x."), *InLayout.GetDebugName().ToString(), Index, (uint8)InLayout.Resources[Index].MemberType);
	}
}

/*-----------------------------------------------------------------------------
	Uniform buffer RHI object
-----------------------------------------------------------------------------*/

static FRHIResourceCreateInfo GEmptyCreateInfo;

static inline EBufferUsageFlags UniformBufferToBufferUsage(EUniformBufferUsage Usage)
{
	switch (Usage)
	{
	default:
		ensure(0);
		// fall through...
	case UniformBuffer_SingleDraw:
		return BUF_Volatile;
	case UniformBuffer_SingleFrame:
		return BUF_Volatile;
	case UniformBuffer_MultiFrame:
		return BUF_Static;
	}
}

FVulkanUniformBuffer::FVulkanUniformBuffer(const FRHIUniformBufferLayout& InLayout, const void* Contents, EUniformBufferUsage InUsage, EUniformBufferValidation Validation)
	: FRHIUniformBuffer(InLayout)
{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
	SCOPE_CYCLE_COUNTER(STAT_VulkanUniformBufferCreateTime);
#endif

	// Verify the correctness of our thought pattern how the resources are delivered
	//	- If we have at least one resource, we also expect ResourceOffset to have an offset
	//	- Meaning, there is always a uniform buffer with a size specified larged than 0 bytes
	check(InLayout.Resources.Num() > 0 || InLayout.ConstantBufferSize > 0);

	// Setup resource table
	const uint32 NumResources = InLayout.Resources.Num();
	if (NumResources > 0)
	{
		// Transfer the resource table to an internal resource-array
		ResourceTable.Empty(NumResources);
		ResourceTable.AddZeroed(NumResources);
		for (uint32 Index = 0; Index < NumResources; Index++)
		{
			FRHIResource* Resource = *(FRHIResource**)((uint8*)Contents + InLayout.Resources[Index].MemberOffset);
			ValidateUniformBufferResource(InLayout, (int32)Index, Resource, Validation);
			ResourceTable[Index] = Resource;
		}
	}
}

void FVulkanUniformBuffer::UpdateResourceTable(const FRHIUniformBufferLayout& InLayout, const void* Contents, int32 ResourceNum)
{
	check(ResourceTable.Num() == ResourceNum);
	for (int32 ResourceIndex = 0; ResourceIndex < ResourceNum; ++ResourceIndex)
	{
		FRHIResource* Resource = *(FRHIResource**)((uint8*)Contents + InLayout.Resources[ResourceIndex].MemberOffset);
		ValidateUniformBufferResource(InLayout, ResourceIndex, Resource, UniformBufferValidation);
		ResourceTable[ResourceIndex] = Resource;
	}
}

void FVulkanUniformBuffer::UpdateResourceTable(FRHIResource** Resources, int32 ResourceNum)
{
	check(ResourceTable.Num() == ResourceNum);

	for (int32 ResourceIndex = 0; ResourceIndex < ResourceNum; ++ResourceIndex)
	{
		ResourceTable[ResourceIndex] = Resources[ResourceIndex];
	}
}


FVulkanEmulatedUniformBuffer::FVulkanEmulatedUniformBuffer(const FRHIUniformBufferLayout& InLayout, const void* Contents, EUniformBufferUsage InUsage, EUniformBufferValidation Validation)
	: FVulkanUniformBuffer(InLayout, Contents, InUsage, Validation)
{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
	SCOPE_CYCLE_COUNTER(STAT_VulkanUniformBufferCreateTime);
#endif

	// Contents might be null but size > 0 as the data doesn't need a CPU copy
	if (InLayout.ConstantBufferSize)
	{
		// Create uniform buffer, which is stored on the CPU, the buffer is uploaded to a correct GPU buffer in UpdateDescriptorSets()
		ConstantData.AddUninitialized(InLayout.ConstantBufferSize);
		if (Contents)
		{
			FMemory::Memcpy(ConstantData.GetData(), Contents, InLayout.ConstantBufferSize);
		}
	}

	// Ancestor's constructor will set up the Resource table, so nothing else to do here
}

void FVulkanEmulatedUniformBuffer::UpdateConstantData(const void* Contents, int32 ContentsSize)
{
	checkSlow(ConstantData.Num() * sizeof(ConstantData[0]) == ContentsSize);
	if (ContentsSize > 0)
	{
		FMemory::Memcpy(ConstantData.GetData(), Contents, ContentsSize);
	}
}


FVulkanRealUniformBuffer::FVulkanRealUniformBuffer(FVulkanDevice& Device, const FRHIUniformBufferLayout& InLayout, const void* Contents, EUniformBufferUsage InUsage, EUniformBufferValidation Validation)
	: FVulkanUniformBuffer(InLayout, Contents, InUsage, Validation)
	, Device(&Device)
{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
	SCOPE_CYCLE_COUNTER(STAT_VulkanUniformBufferCreateTime);
#endif

	if (InLayout.ConstantBufferSize > 0)
	{
		VulkanRHI::FResourceHeapManager& ResourceMgr = Device.GetResourceHeapManager();

		// Set it directly as there is no previous one
		UBAllocation = ResourceMgr.AllocUniformBuffer(InLayout.ConstantBufferSize, Contents);
	}

	// Ancestor's constructor will set up the Resource table, so nothing else to do here
}

FVulkanRealUniformBuffer::~FVulkanRealUniformBuffer()
{
	if (UBAllocation)
	{
		Device->GetResourceHeapManager().ReleaseUniformBuffer(UBAllocation);
	}
}

FUniformBufferRHIRef FVulkanDynamicRHI::RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanUniformBuffers);

	static TConsoleVariableData<int32>* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Vulkan.UseRealUBs"));
	const bool bHasRealUBs = FVulkanPlatform::UseRealUBsOptimization(CVar && CVar->GetValueOnAnyThread() > 0);
	if (bHasRealUBs)
	{
		return new FVulkanRealUniformBuffer(*Device, Layout, Contents, Usage, Validation);
	}
	else
	{
		// Parts of the buffer are later on copied for each shader stage into the packed uniform buffer
		return new FVulkanEmulatedUniformBuffer(Layout, Contents, Usage, Validation);
	}
}

template <bool bRealUBs>
inline void FVulkanDynamicRHI::UpdateUniformBuffer(FVulkanUniformBuffer* UniformBuffer, const void* Contents)
{
	SCOPE_CYCLE_COUNTER(STAT_VulkanUpdateUniformBuffers);
	const FRHIUniformBufferLayout& Layout = UniformBuffer->GetLayout();

	const int32 ConstantBufferSize = Layout.ConstantBufferSize;
	const int32 NumResources = Layout.Resources.Num();

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	FVulkanRealUniformBuffer* RealUniformBuffer = bRealUBs ? (FVulkanRealUniformBuffer*)UniformBuffer : nullptr;
	FVulkanEmulatedUniformBuffer* EmulatedUniformBuffer = bRealUBs ? nullptr : (FVulkanEmulatedUniformBuffer*)UniformBuffer;

	FBufferSuballocation* NewUBAlloc = nullptr;
	bool bIsInRenderPass = RHICmdList.IsInsideRenderPass();
	bool bUseUpload = GVulkanAllowUniformUpload && !bIsInRenderPass; //inside renderpasses, a rename is enforced.

	if (bRealUBs && !bUseUpload)
	{
		NewUBAlloc = nullptr;
		if (ConstantBufferSize > 0)
		{
			SCOPE_CYCLE_COUNTER(STAT_VulkanUpdateUniformBuffersRename);
			NewUBAlloc = Device->GetResourceHeapManager().AllocUniformBuffer(ConstantBufferSize, Contents);
		}
	}

	auto UpdateUniformBufferHelper = [](FVulkanCommandListContext& Context, FVulkanRealUniformBuffer* VulkanUniformBuffer, int32 DataSize, const void* Data)
	{
		FVulkanCmdBuffer* CmdBuffer = Context.GetCommandBufferManager()->GetActiveCmdBufferDirect();
		ensure(CmdBuffer->IsOutsideRenderPass());
		VulkanRHI::FTempFrameAllocationBuffer::FTempAllocInfo LockInfo;
		Context.GetTempFrameAllocationBuffer().Alloc(DataSize, 16, LockInfo);
		FMemory::Memcpy(LockInfo.Data, Data, DataSize);
		VkBufferCopy Region;
		Region.size = DataSize;
		Region.srcOffset = LockInfo.GetBindOffset();
		Region.dstOffset = VulkanUniformBuffer->GetOffset();
		VkBuffer UBBuffer = VulkanUniformBuffer->GetBufferAllocation()->GetHandle();
		VulkanRHI::vkCmdCopyBuffer(CmdBuffer->GetHandle(), LockInfo.GetHandle(), UBBuffer, 1, &Region);
	};

	bool bRHIBypass = RHICmdList.Bypass();
	if (bRHIBypass)
	{
		if (bRealUBs)
		{
			if (ConstantBufferSize > 0)
			{
				if(bUseUpload)
				{			
					FVulkanCommandListContext& Context = (FVulkanCommandListContext&)*Device->ImmediateContext;
					UpdateUniformBufferHelper(Context, RealUniformBuffer, ConstantBufferSize, Contents);
				}
				else
				{
					FBufferSuballocation* PrevAlloc = RealUniformBuffer->UpdateUBAllocation(NewUBAlloc);
					Device->GetResourceHeapManager().ReleaseUniformBuffer(PrevAlloc);
				}
			}
		}
		else
		{
			EmulatedUniformBuffer->UpdateConstantData(Contents, ConstantBufferSize);
		}
		UniformBuffer->UpdateResourceTable(Layout, Contents, NumResources);
	}
	else
	{
		FRHIResource** CmdListResources = nullptr;
		if (NumResources > 0)
		{
			CmdListResources = (FRHIResource**)RHICmdList.Alloc(sizeof(FRHIResource*) * NumResources, alignof(FRHIResource*));

			for (int32 ResourceIndex = 0; ResourceIndex < NumResources; ++ResourceIndex)
			{
				FRHIResource* Resource = *(FRHIResource**)((uint8*)Contents + Layout.Resources[ResourceIndex].MemberOffset);
				ValidateUniformBufferResource(Layout, ResourceIndex, Resource, UniformBufferValidation);
				CmdListResources[ResourceIndex] = Resource;
			}
		}

		if (bRealUBs)
		{
			if(bUseUpload)
			{
				void* CmdListConstantBufferData = RHICmdList.Alloc(ConstantBufferSize, 16);
				FMemory::Memcpy(CmdListConstantBufferData, Contents, ConstantBufferSize);

				RHICmdList.EnqueueLambda([UpdateUniformBufferHelper, RealUniformBuffer, CmdListResources, NumResources, ConstantBufferSize, CmdListConstantBufferData](FRHICommandList& CmdList)
				{
					FVulkanCommandListContext& Context = (FVulkanCommandListContext&)CmdList.GetContext();
					UpdateUniformBufferHelper(Context, RealUniformBuffer, ConstantBufferSize, CmdListConstantBufferData);
					RealUniformBuffer->UpdateResourceTable(CmdListResources, NumResources);
				});
			}
			else
			{
				RHICmdList.EnqueueLambda([RealUniformBuffer, NewUBAlloc, CmdListResources, NumResources](FRHICommandList& CmdList)
				{
					FBufferSuballocation* PrevAlloc = RealUniformBuffer->UpdateUBAllocation(NewUBAlloc);
					RealUniformBuffer->Device->GetResourceHeapManager().ReleaseUniformBuffer(PrevAlloc);
					RealUniformBuffer->UpdateResourceTable(CmdListResources, NumResources);
				});
			}
		}
		else
		{
			void* CmdListConstantBufferData = RHICmdList.Alloc(ConstantBufferSize, 16);
			FMemory::Memcpy(CmdListConstantBufferData, Contents, ConstantBufferSize);
			RHICmdList.EnqueueLambda([EmulatedUniformBuffer, CmdListResources, NumResources, CmdListConstantBufferData, ConstantBufferSize](FRHICommandList&)
			{
				EmulatedUniformBuffer->UpdateConstantData(CmdListConstantBufferData, ConstantBufferSize);
				EmulatedUniformBuffer->UpdateResourceTable(CmdListResources, NumResources);
			});
		}
		RHICmdList.RHIThreadFence(true);
	}
}


void FVulkanDynamicRHI::RHIUpdateUniformBuffer(FRHIUniformBuffer* UniformBufferRHI, const void* Contents)
{
	static TConsoleVariableData<int32>* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Vulkan.UseRealUBs"));
	const bool bHasRealUBs = FVulkanPlatform::UseRealUBsOptimization(CVar && CVar->GetValueOnAnyThread() > 0);
	FVulkanUniformBuffer* UniformBuffer = ResourceCast(UniformBufferRHI);
	if (bHasRealUBs)
	{
		UpdateUniformBuffer<true>(UniformBuffer, Contents);
	}
	else
	{
		UpdateUniformBuffer<false>(UniformBuffer, Contents);
	}
}

FVulkanUniformBufferUploader::FVulkanUniformBufferUploader(FVulkanDevice* InDevice)
	: VulkanRHI::FDeviceChild(InDevice)
	, CPUBuffer(nullptr)
{
	if (Device->HasUnifiedMemory())
	{
		CPUBuffer = new FVulkanRingBuffer(InDevice, PackedUniformsRingBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}
	else
	{
		if (FVulkanPlatform::SupportsDeviceLocalHostVisibleWithNoPenalty() &&
			InDevice->GetMemoryManager().SupportsMemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			CPUBuffer = new FVulkanRingBuffer(InDevice, PackedUniformsRingBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		}
		else
		{
			CPUBuffer = new FVulkanRingBuffer(InDevice, PackedUniformsRingBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}
	}
}

FVulkanUniformBufferUploader::~FVulkanUniformBufferUploader()
{
	delete CPUBuffer;
}


namespace VulkanRHI
{
	VulkanRHI::FBufferSuballocation* FResourceHeapManager::AllocUniformBuffer(uint32 Size, const void* Contents)
	{
		VulkanRHI::FBufferSuballocation* OutAlloc = Device->GetResourceHeapManager().AllocateBuffer(Size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, __FILE__, __LINE__);
		FMemory::Memcpy(OutAlloc->GetMappedPointer(), Contents, Size);
		OutAlloc->Flush();

		return OutAlloc;
	}

	void FResourceHeapManager::ReleaseUniformBuffer(VulkanRHI::FBufferSuballocation* UBAlloc)
	{
		checkSlow(UBAlloc);

		FScopeLock ScopeLock(&UBAllocations.CS);
		ProcessPendingUBFreesNoLock(false);
		FUBPendingFree Pending;
		Pending.Frame = GFrameNumberRenderThread;
		Pending.Allocation = UBAlloc;
		UBAllocations.PendingFree.Add(Pending);

		UBAllocations.Peak = FMath::Max(UBAllocations.Peak, (uint32)UBAllocations.PendingFree.Num());
	}

	void FResourceHeapManager::ProcessPendingUBFreesNoLock(bool bForce)
	{
		// this keeps an frame number of the first frame when we can expect to delete things, updated in the loop if any pending allocations are left
		static uint32 GFrameNumberRenderThread_WhenWeCanDelete = 0;

		if (UNLIKELY(bForce))
		{
			int32 NumAlloc = UBAllocations.PendingFree.Num();
			for (int32 Index = 0; Index < NumAlloc; ++Index)
			{
				FUBPendingFree& Alloc = UBAllocations.PendingFree[Index];
				delete Alloc.Allocation;
			}
			UBAllocations.PendingFree.Empty();

			// invalidate the value
			GFrameNumberRenderThread_WhenWeCanDelete = 0;
		}
		else
		{
			if (LIKELY(GFrameNumberRenderThread < GFrameNumberRenderThread_WhenWeCanDelete))
			{
				// too early
				return;
			}

			// making use of the fact that we always add to the end of the array, so allocations are sorted by frame ascending
			int32 OldestFrameToKeep = GFrameNumberRenderThread - VulkanRHI::NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS;
			int32 NumAlloc = UBAllocations.PendingFree.Num();
			int32 Index = 0;
			for (; Index < NumAlloc; ++Index)
			{
				FUBPendingFree& Alloc = UBAllocations.PendingFree[Index];
				if (LIKELY(Alloc.Frame < OldestFrameToKeep))
				{
					delete Alloc.Allocation;
				}
				else
				{
					// calculate when we will be able to delete the oldest allocation
					GFrameNumberRenderThread_WhenWeCanDelete = Alloc.Frame + VulkanRHI::NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS + 1;
					break;
				}
			}

			int32 ElementsLeft = NumAlloc - Index;
			if (ElementsLeft > 0 && ElementsLeft != NumAlloc)
			{
				// FUBPendingFree is POD because it is stored in a TArray
				FMemory::Memmove(UBAllocations.PendingFree.GetData(), UBAllocations.PendingFree.GetData() + Index, ElementsLeft * sizeof(FUBPendingFree));
			}
			UBAllocations.PendingFree.SetNum(NumAlloc - Index, false);
		}
	}

	void FResourceHeapManager::ProcessPendingUBFrees(bool bForce)
	{
		FScopeLock ScopeLock(&UBAllocations.CS);
		ProcessPendingUBFreesNoLock(bForce);
	}
}
