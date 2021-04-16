// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanUniformBuffer.cpp: Vulkan Constant buffer implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanContext.h"
#include "VulkanLLM.h"
#include "ShaderParameterStruct.h"

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
		for (uint32 Index = 0; Index < NumResources; ++Index)
		{
			ResourceTable[Index] = GetShaderParameterResourceRHI(Contents, InLayout.Resources[Index].MemberOffset, InLayout.Resources[Index].MemberType);
		}
	}
}

void FVulkanUniformBuffer::UpdateResourceTable(const FRHIUniformBufferLayout& InLayout, const void* Contents, int32 NumResources)
{
	check(ResourceTable.Num() == NumResources);

	for (int32 Index = 0; Index < NumResources; ++Index)
	{
		const auto Parameter = InLayout.Resources[Index];
		ResourceTable[Index] = GetShaderParameterResourceRHI(Contents, Parameter.MemberOffset, Parameter.MemberType);
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
	, PatchingFrameNumber(-1)
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
		VulkanRHI::FMemoryManager& ResourceMgr = Device.GetMemoryManager();

		// Set it directly as there is no previous one
		ResourceMgr.AllocUniformBuffer(Allocation, InLayout.ConstantBufferSize, Contents);
	}

	// Ancestor's constructor will set up the Resource table, so nothing else to do here
}

FVulkanRealUniformBuffer::~FVulkanRealUniformBuffer()
{
	Device->GetMemoryManager().FreeUniformBuffer(Allocation);
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

	FVulkanAllocation NewUBAlloc;
	bool bUseUpload = GVulkanAllowUniformUpload && !RHICmdList.IsInsideRenderPass(); //inside renderpasses, a rename is enforced.

	if (bRealUBs && !bUseUpload)
	{
		if (ConstantBufferSize > 0)
		{
			SCOPE_CYCLE_COUNTER(STAT_VulkanUpdateUniformBuffersRename);
			Device->GetMemoryManager().AllocUniformBuffer(NewUBAlloc, ConstantBufferSize, Contents);
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
		VkBuffer UBBuffer = VulkanUniformBuffer->Allocation.GetBufferHandle();
		VkBuffer LockHandle = VulkanUniformBuffer->Allocation.GetBufferHandle();

		bool bIsInRenderPass = CmdBuffer->IsInsideRenderPass();
		bool bIsUniformBarrierAdded = CmdBuffer->IsUniformBufferBarrierAdded();
		if(bIsInRenderPass || !bIsUniformBarrierAdded)
		{
			CmdBuffer->BeginUniformUpdateBarrier();
		}

		VulkanRHI::vkCmdCopyBuffer(CmdBuffer->GetHandle(), LockHandle, UBBuffer, 1, &Region);

		if(bIsInRenderPass) //when updating outside render passes, the EndUniformUpdateBarrier will be called from EndRenderPass
		{
			CmdBuffer->EndUniformUpdateBarrier();
		}
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
					FVulkanCommandListContext& Context = Device->GetImmediateContext();
					UpdateUniformBufferHelper(Context, RealUniformBuffer, ConstantBufferSize, Contents);
				}
				else
				{
					RealUniformBuffer->UpdateAllocation(NewUBAlloc);
					Device->GetMemoryManager().FreeUniformBuffer(NewUBAlloc);
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

			for (int32 Index = 0; Index < NumResources; ++Index)
			{
				CmdListResources[Index] = GetShaderParameterResourceRHI(Contents, Layout.Resources[Index].MemberOffset, Layout.Resources[Index].MemberType);
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
					FVulkanCommandListContext& Context = (FVulkanCommandListContext&)CmdList.GetContext().GetLowestLevelContext();
					UpdateUniformBufferHelper(Context, RealUniformBuffer, ConstantBufferSize, CmdListConstantBufferData);
					RealUniformBuffer->UpdateResourceTable(CmdListResources, NumResources);
				});
			}
			else
			{
				NewUBAlloc.Disown(); //this releases ownership while its put into the lambda
				RHICmdList.EnqueueLambda([RealUniformBuffer, NewUBAlloc, CmdListResources, NumResources](FRHICommandList& CmdList)
				{
					FVulkanAllocation Alloc;
					Alloc.Reference(NewUBAlloc);
					Alloc.Own(); //this takes ownership of the allocation
					RealUniformBuffer->UpdateAllocation(Alloc);
					RealUniformBuffer->Device->GetMemoryManager().FreeUniformBuffer(Alloc);
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
		if (FVulkanPlatform::SupportsDeviceLocalHostVisibleWithNoPenalty(InDevice->GetVendorId()) &&
			InDevice->GetDeviceMemoryManager().SupportsMemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			CPUBuffer = new FVulkanRingBuffer(InDevice, PackedUniformsRingBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		}
		else
		{
			CPUBuffer = new FVulkanRingBuffer(InDevice, PackedUniformsRingBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}
	}

	bEnableUniformBufferPatching = false;
	UniformBufferPatchingFrameNumber = -1;
	if (FVulkanPlatform::SupportsUniformBufferPatching())
		BufferPatchInfos.Reserve(1000);
}

void FVulkanUniformBufferUploader::ApplyUniformBufferPatching(bool bNeedAbort)
{
	int PatchCount = BufferPatchInfos.Num();

	if (bNeedAbort)
	{
		for (int i = 0; i < PatchCount; i++)
		{
			FUniformBufferPatchInfo& PatchInfo = BufferPatchInfos[i];
			FVulkanEmulatedUniformBuffer* Emulate = (FVulkanEmulatedUniformBuffer*)PatchInfo.SourceBuffer;
			if (Emulate->GetPatchingFrameNumber() > 0)
			{
				Emulate->SetPatchingFrameNumber(-1);
			}
		}
	}
	else
	{
		for (int i = 0; i < PatchCount; i++)
		{
			FUniformBufferPatchInfo& PatchInfo = BufferPatchInfos[i];
			if (ensureMsgf(PatchInfo.SourceBuffer != NULL, TEXT("PatchInfo.SourceBuffer can't be null")))
			{
				FVulkanEmulatedUniformBuffer* Emulate = (FVulkanEmulatedUniformBuffer*)PatchInfo.SourceBuffer;
				FMemory::Memcpy(PatchInfo.DestBufferAddress, Emulate->ConstantData.GetData() + PatchInfo.SourceOffsetInFloats * sizeof(float), PatchInfo.SizeInFloats * sizeof(float));
			}
		}
	}

	BufferPatchInfos.Empty(BufferPatchInfos.Num());
}

FVulkanUniformBufferUploader::~FVulkanUniformBufferUploader()
{
	delete CPUBuffer;
}


