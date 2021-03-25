// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanShaders.cpp: Vulkan shader RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"
#include "GlobalShader.h"
#include "Serialization/MemoryReader.h"
#include "VulkanLLM.h"

TAutoConsoleVariable<int32> GDynamicGlobalUBs(
	TEXT("r.Vulkan.DynamicGlobalUBs"),
	1,
	TEXT("2 to treat ALL uniform buffers as dynamic\n")\
	TEXT("1 to treat global/packed uniform buffers as dynamic [default]\n")\
	TEXT("0 to treat them as regular"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);


static TAutoConsoleVariable<int32> GDescriptorSetLayoutMode(
	TEXT("r.Vulkan.DescriptorSetLayoutMode"),
	0,
	TEXT("0 to not change layouts (eg Set 0 = Vertex, 1 = Pixel, etc\n")\
	TEXT("1 to use a new set for common Uniform Buffers\n")\
	TEXT("2 to collapse all sets into Set 0\n"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

FVulkanShaderFactory::~FVulkanShaderFactory()
{
	for (auto& Map : ShaderMap)
	{
		Map.Empty();
	}
}

template <typename ShaderType> 
ShaderType* FVulkanShaderFactory::CreateShader(TArrayView<const uint8> Code, FVulkanDevice* Device)
{
	uint32 ShaderCodeLen = Code.Num();
	uint32 ShaderCodeCRC = FCrc::MemCrc32(Code.GetData(), Code.Num());
	uint64 ShaderKey = ((uint64)ShaderCodeLen | ((uint64)ShaderCodeCRC << 32));

	ShaderType* RetShader = LookupShader<ShaderType>(ShaderKey);
	if (RetShader == nullptr)
	{
		RetShader = new ShaderType(Device);
		RetShader->Setup(Code, ShaderKey);
			
		FRWScopeLock ScopedLock(Lock, SLT_Write);
		ShaderMap[ShaderType::StaticFrequency].Add(ShaderKey, RetShader);
	}
	return RetShader;
}

void FVulkanShaderFactory::LookupShaders(const uint64 InShaderKeys[ShaderStage::NumStages], FVulkanShader* OutShaders[ShaderStage::NumStages]) const
{
	FRWScopeLock ScopedLock(Lock, SLT_ReadOnly);
	
	for (int32 Idx = 0; Idx < ShaderStage::NumStages; ++Idx)
	{
		uint64 ShaderKey = InShaderKeys[Idx];
		if (ShaderKey)
		{
			EShaderFrequency ShaderFrequency = ShaderStage::GetFrequencyForGfxStage((ShaderStage::EStage)Idx);
			FVulkanShader* const * FoundShaderPtr = ShaderMap[ShaderFrequency].Find(ShaderKey);
			if (FoundShaderPtr)
			{
				OutShaders[Idx] = *FoundShaderPtr;
			}
		}
	}
}

void FVulkanShaderFactory::OnDeleteShader(const FVulkanShader& Shader)
{
	FRWScopeLock ScopedLock(Lock, SLT_Write);
	uint64 ShaderKey = Shader.GetShaderKey(); 
	ShaderMap[Shader.Frequency].Remove(ShaderKey);
}

void FVulkanShader::Setup(TArrayView<const uint8> InShaderHeaderAndCode, uint64 InShaderKey)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanShaders);
	check(Device);

	ShaderKey = InShaderKey;

	FMemoryReaderView Ar(InShaderHeaderAndCode, true);

	Ar << CodeHeader;

	Ar << Spirv;

	checkf(Spirv.Num() != 0, TEXT("Empty SPIR-V! %s"), *CodeHeader.DebugName);

	SpirvSize = Spirv.Num() * sizeof(uint32);

	if (CodeHeader.bHasRealUBs)
	{
		check(CodeHeader.UniformBufferSpirvInfos.Num() == CodeHeader.UniformBuffers.Num());
	}
	else
	{
		checkSlow(CodeHeader.UniformBufferSpirvInfos.Num() == 0);
	}
	check(CodeHeader.GlobalSpirvInfos.Num() == CodeHeader.Globals.Num());

	StaticSlots.Reserve(CodeHeader.UniformBuffers.Num());

	for (const FVulkanShaderHeader::FUniformBufferInfo& UBInfo : CodeHeader.UniformBuffers)
	{
		if (const FShaderParametersMetadata* Metadata = FindUniformBufferStructByLayoutHash(UBInfo.LayoutHash))
		{
			StaticSlots.Add(Metadata->GetLayout().StaticSlot);
		}
		else
		{
			StaticSlots.Add(MAX_UNIFORM_BUFFER_STATIC_SLOTS);
		}
	}

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	// main_00000000_00000000
	ANSICHAR EntryPoint[24];
	GetEntryPoint(EntryPoint, 24);
	DebugEntryPoint = EntryPoint;
#endif
}

static VkShaderModule CreateShaderModule(FVulkanDevice* Device, const TArray<uint32>& Spirv)
{
	VkShaderModule ShaderModule;
	VkShaderModuleCreateInfo ModuleCreateInfo;
	ZeroVulkanStruct(ModuleCreateInfo, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
	//ModuleCreateInfo.flags = 0;

	ModuleCreateInfo.codeSize = Spirv.Num() * sizeof(uint32);
	ModuleCreateInfo.pCode = Spirv.GetData();

#if VULKAN_SUPPORTS_VALIDATION_CACHE
	VkShaderModuleValidationCacheCreateInfoEXT ValidationInfo;
	if (Device->GetOptionalExtensions().HasEXTValidationCache)
	{
		ZeroVulkanStruct(ValidationInfo, VK_STRUCTURE_TYPE_SHADER_MODULE_VALIDATION_CACHE_CREATE_INFO_EXT);
		ValidationInfo.validationCache = Device->GetValidationCache();
		ModuleCreateInfo.pNext = &ValidationInfo;
	}
#endif

	VERIFYVULKANRESULT(VulkanRHI::vkCreateShaderModule(Device->GetInstanceHandle(), &ModuleCreateInfo, VULKAN_CPU_ALLOCATOR, &ShaderModule));
	return ShaderModule;
}

/*
 *  Replace all subpassInput declarations with subpassInputMS
 *  Replace all subpassLoad(Input) with subpassLoad(Input, 0)
 */
static void PatchSpirvInputAttachments(TArray<uint32>& InSpirv)
{
	const uint32 kHeaderLength = 5;
	const uint32 kOpTypeImage = 25;
	const uint32 kDimSubpassData = 6;
	const uint32 kOpImageRead = 98;
	const uint32 kOpLoad = 61;
	const uint32 kOpConstant = 43;
	const uint32 kOpTypeInt = 21;

	const uint32 Len = InSpirv.Num();
	// Make sure we at least have a header
	if (Len < kHeaderLength)
	{
		return;
	}

	TArray<uint32> OutSpirv;
	OutSpirv.Reserve(Len + 2);
	// Copy header
	OutSpirv.Append(&InSpirv[0], kHeaderLength);

	uint32 IntegerType = 0;
	uint32 Constant0 = 0;
	TArray<uint32, TInlineAllocator<4>> SubpassDataImages;
	
	for (uint32 Pos = kHeaderLength; Pos < Len;)
	{
		uint32* SpirvData = &InSpirv[Pos];
		const uint32 InstLen =	SpirvData[0] >> 16;
		const uint32 Opcode =	SpirvData[0] & 0x0000ffffu;
		bool bSkip = false;

		if (Opcode == kOpTypeInt && SpirvData[3] == 1)
		{
			// signed int
			IntegerType = SpirvData[1];
		}
		else if (Opcode == kOpConstant && SpirvData[1] == IntegerType && SpirvData[3] == 0)
		{
			// const signed int == 0
			Constant0 = SpirvData[2];
		}
		else if (Opcode == kOpTypeImage && SpirvData[3] == kDimSubpassData)
		{
			SpirvData[6] = 1; // mark as multisampled
			SubpassDataImages.Add(SpirvData[1]);
		}
		else if (Opcode == kOpLoad && SubpassDataImages.Contains(SpirvData[1]))
		{
			// pointers to our image
			SubpassDataImages.Add(SpirvData[2]);
		}
		else if (Opcode == kOpImageRead && SubpassDataImages.Contains(SpirvData[3]))
		{
			// const int 0, must be present as it's used for coord operand in image sampling
			check(Constant0 != 0);

			OutSpirv.Add((7u << 16) | kOpImageRead); // new instruction with 7 operands
			OutSpirv.Append(&SpirvData[1], 4); // copy existing operands
			OutSpirv.Add(0x40);			// Sample operand
			OutSpirv.Add(Constant0);	// Sample number
			bSkip = true;
		}

		if (!bSkip)
		{
			OutSpirv.Append(&SpirvData[0], InstLen);
		}
		Pos += InstLen;
	}

	Swap(InSpirv, OutSpirv);
}

bool FVulkanShader::NeedsSpirvInputAttachmentPatching(const FGfxPipelineDesc& Desc) const
{
	return (Desc.RasterizationSamples > 1 && CodeHeader.InputAttachments.Num() > 0);
}

VkShaderModule FVulkanShader::CreateHandle(const FGfxPipelineDesc& Desc, const FVulkanLayout* Layout, uint32 LayoutHash)
{
	Layout->PatchSpirvBindings(Spirv, Frequency, CodeHeader, StageFlag);
	if (NeedsSpirvInputAttachmentPatching(Desc))
	{
		PatchSpirvInputAttachments(Spirv);
	}

	VkShaderModule Module = CreateShaderModule(Device, Spirv);
	ShaderModules.Add(LayoutHash, Module);
	return Module;
}

VkShaderModule FVulkanShader::CreateHandle(const FVulkanLayout* Layout, uint32 LayoutHash)
{
	Layout->PatchSpirvBindings(Spirv, Frequency, CodeHeader, StageFlag);
	VkShaderModule Module = CreateShaderModule(Device, Spirv);
	ShaderModules.Add(LayoutHash, Module);
	return Module;
}

FVulkanShader::~FVulkanShader()
{
	PurgeShaderModules();
	Device->GetShaderFactory().OnDeleteShader(*this);
}

void FVulkanShader::PurgeShaderModules()
{
	for (const auto& Pair : ShaderModules)
	{
		VkShaderModule ShaderModule = Pair.Value;
		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::ShaderModule, ShaderModule);
	}
	ShaderModules.Empty(0);
}

void FVulkanLayout::PatchSpirvBindings(TArray<uint32>& Spirv, EShaderFrequency Frequency, const FVulkanShaderHeader& CodeHeader, VkShaderStageFlagBits InStageFlag) const
{
	//#todo-rco: Do we need an actual copy of the SPIR-V?
	ShaderStage::EStage Stage = ShaderStage::GetStageForFrequency(Frequency);
	const FDescriptorSetRemappingInfo::FStageInfo& StageInfo = DescriptorSetLayout.RemappingInfo.StageInfos[Stage];
	if (CodeHeader.bHasRealUBs)
	{
		checkSlow(StageInfo.UniformBuffers.Num() == CodeHeader.UniformBufferSpirvInfos.Num());
		for (int32 Index = 0; Index < CodeHeader.UniformBufferSpirvInfos.Num(); ++Index)
		{
			if (StageInfo.UniformBuffers[Index].bHasConstantData)
			{
				const uint32 OffsetDescriptorSet = CodeHeader.UniformBufferSpirvInfos[Index].DescriptorSetOffset;
				const uint32 OffsetBindingIndex = CodeHeader.UniformBufferSpirvInfos[Index].BindingIndexOffset;
				check(OffsetDescriptorSet != UINT32_MAX && OffsetBindingIndex != UINT32_MAX);
				uint16 NewDescriptorSet = StageInfo.UniformBuffers[Index].Remapping.NewDescriptorSet;
				Spirv[OffsetDescriptorSet] = NewDescriptorSet;
				uint16 NewBindingIndex = StageInfo.UniformBuffers[Index].Remapping.NewBindingIndex;
				Spirv[OffsetBindingIndex] = NewBindingIndex;
			}
		}
	}

	checkSlow(StageInfo.Globals.Num() == CodeHeader.GlobalSpirvInfos.Num());
	for (int32 Index = 0; Index < CodeHeader.GlobalSpirvInfos.Num(); ++Index)
	{
		const uint32 OffsetDescriptorSet = CodeHeader.GlobalSpirvInfos[Index].DescriptorSetOffset;
		const uint32 OffsetBindingIndex = CodeHeader.GlobalSpirvInfos[Index].BindingIndexOffset;
		check(OffsetDescriptorSet != UINT32_MAX && OffsetBindingIndex != UINT32_MAX);
		uint16 NewDescriptorSet = StageInfo.Globals[Index].NewDescriptorSet;
		Spirv[OffsetDescriptorSet] = NewDescriptorSet;
		uint16 NewBindingIndex = StageInfo.Globals[Index].NewBindingIndex;
		Spirv[OffsetBindingIndex] = NewBindingIndex;
	}

	checkSlow(StageInfo.PackedUBBindingIndices.Num() == CodeHeader.PackedUBs.Num());
	for (int32 Index = 0; Index < CodeHeader.PackedUBs.Num(); ++Index)
	{
		const uint32 OffsetDescriptorSet = CodeHeader.PackedUBs[Index].SPIRVDescriptorSetOffset;
		const uint32 OffsetBindingIndex = CodeHeader.PackedUBs[Index].SPIRVBindingIndexOffset;
		check(OffsetDescriptorSet != UINT32_MAX && OffsetBindingIndex != UINT32_MAX);
		Spirv[OffsetDescriptorSet] = StageInfo.PackedUBDescriptorSet;
		Spirv[OffsetBindingIndex] = StageInfo.PackedUBBindingIndices[Index];
	}
}

FVertexShaderRHIRef FVulkanDynamicRHI::RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return Device->GetShaderFactory().CreateShader<FVulkanVertexShader>(Code, Device);
}

FPixelShaderRHIRef FVulkanDynamicRHI::RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return Device->GetShaderFactory().CreateShader<FVulkanPixelShader>(Code, Device);
}

FHullShaderRHIRef FVulkanDynamicRHI::RHICreateHullShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	return Device->GetShaderFactory().CreateShader<FVulkanHullShader>(Code, Device);
}

FDomainShaderRHIRef FVulkanDynamicRHI::RHICreateDomainShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	return Device->GetShaderFactory().CreateShader<FVulkanDomainShader>(Code, Device);
}

FGeometryShaderRHIRef FVulkanDynamicRHI::RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	return Device->GetShaderFactory().CreateShader<FVulkanGeometryShader>(Code, Device);
}

FComputeShaderRHIRef FVulkanDynamicRHI::RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	return Device->GetShaderFactory().CreateShader<FVulkanComputeShader>(Code, Device);
}


FVulkanLayout::FVulkanLayout(FVulkanDevice* InDevice)
	: VulkanRHI::FDeviceChild(InDevice)
	, DescriptorSetLayout(Device)
	, PipelineLayout(VK_NULL_HANDLE)
{
}

FVulkanLayout::~FVulkanLayout()
{
	if (PipelineLayout != VK_NULL_HANDLE)
	{
		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::PipelineLayout, PipelineLayout);
		PipelineLayout = VK_NULL_HANDLE;
	}
}

void FVulkanLayout::Compile(FVulkanDescriptorSetLayoutMap& DSetLayoutMap)
{
	check(PipelineLayout == VK_NULL_HANDLE);

	DescriptorSetLayout.Compile(DSetLayoutMap);

	const TArray<VkDescriptorSetLayout>& LayoutHandles = DescriptorSetLayout.GetHandles();

	VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo;
	ZeroVulkanStruct(PipelineLayoutCreateInfo, VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
	PipelineLayoutCreateInfo.setLayoutCount = LayoutHandles.Num();
	PipelineLayoutCreateInfo.pSetLayouts = LayoutHandles.GetData();
	//PipelineLayoutCreateInfo.pushConstantRangeCount = 0;

	VERIFYVULKANRESULT(VulkanRHI::vkCreatePipelineLayout(Device->GetInstanceHandle(), &PipelineLayoutCreateInfo, VULKAN_CPU_ALLOCATOR, &PipelineLayout));
}

bool FVulkanGfxLayout::UsesInputAttachment(FVulkanShaderHeader::EAttachmentType AttachmentType) const
{
	const TArray<FInputAttachmentData>& InputAttachmentData = GfxPipelineDescriptorInfo.GetInputAttachmentData();
	for (const FInputAttachmentData& Input : InputAttachmentData)
	{
		if (Input.Type == AttachmentType)
		{
			return true;
		}
	}

	return false;
}

uint32 FVulkanDescriptorSetWriter::SetupDescriptorWrites(
	const TArray<VkDescriptorType>& Types, FVulkanHashableDescriptorInfo* InHashableDescriptorInfos,
	VkWriteDescriptorSet* InWriteDescriptors, VkDescriptorImageInfo* InImageInfo, VkDescriptorBufferInfo* InBufferInfo, uint8* InBindingToDynamicOffsetMap,
	const FVulkanSamplerState& DefaultSampler, const FVulkanTextureView& DefaultImageView)
{
	HashableDescriptorInfos = InHashableDescriptorInfos;
	WriteDescriptors = InWriteDescriptors;
	NumWrites = Types.Num();

	BindingToDynamicOffsetMap = InBindingToDynamicOffsetMap;

	BufferViewReferences.Empty(NumWrites);
	BufferViewReferences.AddDefaulted(NumWrites);
	InitWrittenMasks(NumWrites);

	uint32 DynamicOffsetIndex = 0;

	for (int32 Index = 0; Index < Types.Num(); ++Index)
	{
		InWriteDescriptors->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		InWriteDescriptors->dstBinding = Index;
		InWriteDescriptors->descriptorCount = 1;
		InWriteDescriptors->descriptorType = Types[Index];

		switch (Types[Index])
		{
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			BindingToDynamicOffsetMap[Index] = DynamicOffsetIndex;
			++DynamicOffsetIndex;
			InWriteDescriptors->pBufferInfo = InBufferInfo++;
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			InWriteDescriptors->pBufferInfo = InBufferInfo++;
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			SetWrittenBase(Index); //samplers have a default setting, don't assert on those yet.
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			// Texture.Load() still requires a default sampler...
			if (InHashableDescriptorInfos) // UseVulkanDescriptorCache()
			{
				InHashableDescriptorInfos[Index].Image.SamplerId = DefaultSampler.SamplerId;
				InHashableDescriptorInfos[Index].Image.ImageViewId = DefaultImageView.ViewId;
				InHashableDescriptorInfos[Index].Image.ImageLayout = static_cast<uint32>(VK_IMAGE_LAYOUT_GENERAL);
			}
			InImageInfo->sampler = DefaultSampler.Sampler;
			InImageInfo->imageView = DefaultImageView.View;
			InImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			InWriteDescriptors->pImageInfo = InImageInfo++;
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			break;
		default:
			checkf(0, TEXT("Unsupported descriptor type %d"), (int32)Types[Index]);
			break;
		}
		++InWriteDescriptors;
	}

	return DynamicOffsetIndex;
}

void FVulkanDescriptorSetsLayoutInfo::ProcessBindingsForStage(VkShaderStageFlagBits StageFlags, ShaderStage::EStage DescSetStage, const FVulkanShaderHeader& CodeHeader, FUniformBufferGatherInfo& OutUBGatherInfo) const
{
	const bool bMoveCommonUBsToExtraSet = GDescriptorSetLayoutMode.GetValueOnAnyThread() == 1 || GDescriptorSetLayoutMode.GetValueOnAnyThread() == 2;

	// Find all common UBs from different stages
	for (const FVulkanShaderHeader::FUniformBufferInfo& UBInfo : CodeHeader.UniformBuffers)
	{
		if (bMoveCommonUBsToExtraSet)
		{
			VkShaderStageFlags* Found = OutUBGatherInfo.CommonUBLayoutsToStageMap.Find(UBInfo.LayoutHash);
			if (Found)
			{
				*Found = *Found | StageFlags;
			}
			else
			{
				//#todo-rco: Only process constant data part of the UB
				Found = (UBInfo.ConstantDataOriginalBindingIndex == UINT16_MAX) ? nullptr : OutUBGatherInfo.UBLayoutsToUsedStageMap.Find(UBInfo.LayoutHash);
				if (Found)
				{
					// Move from per stage to common UBs
					VkShaderStageFlags PrevStage = (VkShaderStageFlags)0;
					bool bFound = OutUBGatherInfo.UBLayoutsToUsedStageMap.RemoveAndCopyValue(UBInfo.LayoutHash, PrevStage);
					check(bFound);
					check(OutUBGatherInfo.CommonUBLayoutsToStageMap.Find(UBInfo.LayoutHash) == nullptr);
					OutUBGatherInfo.CommonUBLayoutsToStageMap.Add(UBInfo.LayoutHash, PrevStage | (VkShaderStageFlags)StageFlags);
				}
				else
				{
					OutUBGatherInfo.UBLayoutsToUsedStageMap.Add(UBInfo.LayoutHash, (VkShaderStageFlags)StageFlags);
				}
			}
		}
		else
		{
			OutUBGatherInfo.UBLayoutsToUsedStageMap.Add(UBInfo.LayoutHash, (VkShaderStageFlags)StageFlags);
		}
	}

	OutUBGatherInfo.CodeHeaders[DescSetStage] = &CodeHeader;
}

template<bool bIsCompute>
void FVulkanDescriptorSetsLayoutInfo::FinalizeBindings(const FUniformBufferGatherInfo& UBGatherInfo, const TArrayView<FRHISamplerState*>& ImmutableSamplers)
{
	checkSlow(RemappingInfo.IsEmpty());

	TMap<uint32, FDescriptorSetRemappingInfo::FUBRemappingInfo> AlreadyProcessedUBs;

	// We'll be reusing this struct
	VkDescriptorSetLayoutBinding Binding;
	FMemory::Memzero(Binding);
	Binding.descriptorCount = 1;

	const bool bConvertAllUBsToDynamic = (GDynamicGlobalUBs.GetValueOnAnyThread() > 1);
	const bool bConvertPackedUBsToDynamic = bConvertAllUBsToDynamic || (GDynamicGlobalUBs.GetValueOnAnyThread() == 1);
	const bool bConsolidateAllIntoOneSet = GDescriptorSetLayoutMode.GetValueOnAnyThread() == 2;

	uint8	DescriptorStageToSetMapping[ShaderStage::NumStages];
	FMemory::Memset(DescriptorStageToSetMapping, UINT8_MAX);

	const bool bMoveCommonUBsToExtraSet = (UBGatherInfo.CommonUBLayoutsToStageMap.Num() > 0) || bConsolidateAllIntoOneSet;
	const uint32 CommonUBDescriptorSet = bMoveCommonUBsToExtraSet ? RemappingInfo.SetInfos.AddDefaulted() : UINT32_MAX;

	auto FindOrAddDescriptorSet = [&](int32 Stage) -> uint8
	{
		if (bConsolidateAllIntoOneSet)
		{
			return 0;
		}

		if (DescriptorStageToSetMapping[Stage] == UINT8_MAX)
		{
			uint32 NewSet = RemappingInfo.SetInfos.AddDefaulted();
			DescriptorStageToSetMapping[Stage] = (uint8)NewSet;
			return NewSet;
		}

		return DescriptorStageToSetMapping[Stage];
	};

	int32 CurrentImmutableSampler = 0;
	for (int32 Stage = 0; Stage < (bIsCompute ? 1 : ShaderStage::NumStages); ++Stage)
	{
		if (const FVulkanShaderHeader* ShaderHeader = UBGatherInfo.CodeHeaders[Stage])
		{
			VkShaderStageFlags StageFlags = UEFrequencyToVKStageBit(bIsCompute ? SF_Compute : ShaderStage::GetFrequencyForGfxStage((ShaderStage::EStage)Stage));
			Binding.stageFlags = StageFlags;

			RemappingInfo.StageInfos[Stage].PackedUBBindingIndices.Reserve(ShaderHeader->PackedUBs.Num());
			for (int32 Index = 0; Index < ShaderHeader->PackedUBs.Num(); ++Index)
			{
				int32 DescriptorSet = FindOrAddDescriptorSet(Stage);
				VkDescriptorType Type = bConvertPackedUBsToDynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				uint32 NewBindingIndex = RemappingInfo.AddPackedUB(Stage, Index, DescriptorSet, Type);

				Binding.binding = NewBindingIndex;
				Binding.descriptorType = Type;
				AddDescriptor(DescriptorSet, Binding);
			}

			if (ShaderHeader->bHasRealUBs)
			{
				RemappingInfo.StageInfos[Stage].UniformBuffers.Reserve(ShaderHeader->UniformBuffers.Num());
				for (int32 Index = 0; Index < ShaderHeader->UniformBuffers.Num(); ++Index)
				{
					VkDescriptorType Type = bConvertAllUBsToDynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					// Here we might mess up with the stageFlags, so reset them every loop
					Binding.stageFlags = StageFlags;
					Binding.descriptorType = Type;
					const FVulkanShaderHeader::FUniformBufferInfo& UBInfo = ShaderHeader->UniformBuffers[Index];
					const uint32 LayoutHash = UBInfo.LayoutHash;
					const bool bUBHasConstantData = UBInfo.ConstantDataOriginalBindingIndex != UINT16_MAX;
					if (bUBHasConstantData)
					{
						bool bProcessRegularUB = true;
						const VkShaderStageFlags* FoundFlags = bMoveCommonUBsToExtraSet ? UBGatherInfo.CommonUBLayoutsToStageMap.Find(LayoutHash) : nullptr;
						if (FoundFlags)
						{
							if (const FDescriptorSetRemappingInfo::FUBRemappingInfo* UBRemapInfo = AlreadyProcessedUBs.Find(LayoutHash))
							{
								RemappingInfo.AddRedundantUB(Stage, Index, UBRemapInfo);
							}
							else
							{
								//#todo-rco: Only process constant data part of the UB
								check(bUBHasConstantData);

								Binding.stageFlags = *FoundFlags;
								uint32 NewBindingIndex;
								AlreadyProcessedUBs.Add(LayoutHash, RemappingInfo.AddUBWithData(Stage, Index, CommonUBDescriptorSet, Type, NewBindingIndex));
								Binding.binding = NewBindingIndex;

								AddDescriptor(CommonUBDescriptorSet, Binding);
							}
							bProcessRegularUB = false;
						}

						if (bProcessRegularUB)
						{
							int32 DescriptorSet = FindOrAddDescriptorSet(Stage);
							uint32 NewBindingIndex;
							RemappingInfo.AddUBWithData(Stage, Index, DescriptorSet, Type, NewBindingIndex);
							Binding.binding = NewBindingIndex;

							AddDescriptor(FindOrAddDescriptorSet(Stage), Binding);
						}
					}
					else
					{
						RemappingInfo.AddUBResourceOnly(Stage, Index);
					}
				}
			}

			RemappingInfo.StageInfos[Stage].Globals.Reserve(ShaderHeader->Globals.Num());
			Binding.stageFlags = StageFlags;
			for (int32 Index = 0; Index < ShaderHeader->Globals.Num(); ++Index)
			{
				const FVulkanShaderHeader::FGlobalInfo& GlobalInfo = ShaderHeader->Globals[Index];
				int32 DescriptorSet = FindOrAddDescriptorSet(Stage);
				VkDescriptorType Type = ShaderHeader->GlobalDescriptorTypes[GlobalInfo.TypeIndex];
				uint16 CombinedSamplerStateAlias = GlobalInfo.CombinedSamplerStateAliasIndex;
				uint32 NewBindingIndex = RemappingInfo.AddGlobal(Stage, Index, DescriptorSet, Type, CombinedSamplerStateAlias);
				Binding.binding = NewBindingIndex;
				Binding.descriptorType = Type;
				if (CombinedSamplerStateAlias == UINT16_MAX)
				{
					if (GlobalInfo.bImmutableSampler)
					{
						if (CurrentImmutableSampler < ImmutableSamplers.Num())
						{
							FVulkanSamplerState* SamplerState = ResourceCast(ImmutableSamplers[CurrentImmutableSampler]);
							if (SamplerState && SamplerState->Sampler != VK_NULL_HANDLE)
							{
								Binding.pImmutableSamplers = &SamplerState->Sampler;
							}
							++CurrentImmutableSampler;
						}
					}

					AddDescriptor(DescriptorSet, Binding);
				}

				Binding.pImmutableSamplers = nullptr;
			}

			if (ShaderHeader->InputAttachments.Num())
			{
				int32 DescriptorSet = FindOrAddDescriptorSet(Stage);
				check(Stage == ShaderStage::Pixel);
				for (int32 SrcIndex = 0; SrcIndex < ShaderHeader->InputAttachments.Num(); ++SrcIndex)
				{
					int32 OriginalGlobalIndex = ShaderHeader->InputAttachments[SrcIndex].GlobalIndex;
					const FVulkanShaderHeader::FGlobalInfo& OriginalGlobalInfo = ShaderHeader->Globals[OriginalGlobalIndex];
					check(ShaderHeader->GlobalDescriptorTypes[OriginalGlobalInfo.TypeIndex] == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
					int32 RemappingIndex = RemappingInfo.InputAttachmentData.AddDefaulted();
					FInputAttachmentData& AttachmentData = RemappingInfo.InputAttachmentData[RemappingIndex];
					AttachmentData.BindingIndex = RemappingInfo.StageInfos[Stage].Globals[OriginalGlobalIndex].NewBindingIndex;
					AttachmentData.DescriptorSet = (uint8)DescriptorSet;
					AttachmentData.Type = ShaderHeader->InputAttachments[SrcIndex].Type;
				}
			}
		}
	}

	CompileTypesUsageID();
	GenerateHash(ImmutableSamplers);

	// If we are consolidating and no uniforms are present in the shader, then strip the empty set data
	if (bConsolidateAllIntoOneSet)
	{
		for (int32 Index = 0; Index < RemappingInfo.SetInfos.Num(); ++Index)
		{
			if (RemappingInfo.SetInfos[Index].Types.Num() == 0)
			{
				RemappingInfo.SetInfos.RemoveAt(Index);
			}
		}
		check(RemappingInfo.SetInfos.Num() <= 1);
	}
	else
	{
		for (int32 Index = 0; Index < RemappingInfo.SetInfos.Num(); ++Index)
		{
			check(RemappingInfo.SetInfos[Index].Types.Num() > 0);
		}
	}	
}

void FVulkanComputePipelineDescriptorInfo::Initialize(const FDescriptorSetRemappingInfo& InRemappingInfo)
{
	check(!bInitialized);

	RemappingGlobalInfos = InRemappingInfo.StageInfos[0].Globals.GetData();
	RemappingUBInfos = InRemappingInfo.StageInfos[0].UniformBuffers.GetData();
	RemappingPackedUBInfos = InRemappingInfo.StageInfos[0].PackedUBBindingIndices.GetData();

	RemappingInfo = &InRemappingInfo;

	for (int32 Index = 0; Index < InRemappingInfo.SetInfos.Num(); ++Index)
	{
		const FDescriptorSetRemappingInfo::FSetInfo& SetInfo = InRemappingInfo.SetInfos[Index];
		if (SetInfo.Types.Num() > 0)
		{
			check(Index < sizeof(HasDescriptorsInSetMask) * 8);
			HasDescriptorsInSetMask = HasDescriptorsInSetMask | (1 << Index);
		}
		else
		{
			ensure(0);
		}
	}

	bInitialized = true;
}

void FVulkanGfxPipelineDescriptorInfo::Initialize(const FDescriptorSetRemappingInfo& InRemappingInfo)
{
	check(!bInitialized);

	for (int32 StageIndex = 0; StageIndex < ShaderStage::NumStages; ++StageIndex)
	{
		//#todo-rco: Enable this!
		RemappingUBInfos[StageIndex] = InRemappingInfo.StageInfos[StageIndex].UniformBuffers.GetData();
		RemappingGlobalInfos[StageIndex] = InRemappingInfo.StageInfos[StageIndex].Globals.GetData();
		RemappingPackedUBInfos[StageIndex] = InRemappingInfo.StageInfos[StageIndex].PackedUBBindingIndices.GetData();
	}

	RemappingInfo = &InRemappingInfo;

	for (int32 Index = 0; Index < InRemappingInfo.SetInfos.Num(); ++Index)
	{
		const FDescriptorSetRemappingInfo::FSetInfo& SetInfo = InRemappingInfo.SetInfos[Index];
		if (SetInfo.Types.Num() > 0)
		{
			check(Index < sizeof(HasDescriptorsInSetMask) * 8);
			HasDescriptorsInSetMask = HasDescriptorsInSetMask | (1 << Index);
		}
		else
		{
			ensure(0);
		}
	}

	bInitialized = true;
}


FVulkanBoundShaderState::FVulkanBoundShaderState(FRHIVertexDeclaration* InVertexDeclarationRHI, FRHIVertexShader* InVertexShaderRHI,
	FRHIPixelShader* InPixelShaderRHI, FRHIHullShader* InHullShaderRHI,
	FRHIDomainShader* InDomainShaderRHI, FRHIGeometryShader* InGeometryShaderRHI)
	: CacheLink(InVertexDeclarationRHI, InVertexShaderRHI, InPixelShaderRHI, InHullShaderRHI, InDomainShaderRHI, InGeometryShaderRHI, this)
{
	CacheLink.AddToCache();
}

FVulkanBoundShaderState::~FVulkanBoundShaderState()
{
	CacheLink.RemoveFromCache();
}

FBoundShaderStateRHIRef FVulkanDynamicRHI::RHICreateBoundShaderState(
	FRHIVertexDeclaration* VertexDeclarationRHI,
	FRHIVertexShader* VertexShaderRHI,
	FRHIHullShader* HullShaderRHI,
	FRHIDomainShader* DomainShaderRHI,
	FRHIPixelShader* PixelShaderRHI,
	FRHIGeometryShader* GeometryShaderRHI
	)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanShaders);
	FBoundShaderStateRHIRef CachedBoundShaderState = GetCachedBoundShaderState_Threadsafe(
		VertexDeclarationRHI,
		VertexShaderRHI,
		PixelShaderRHI,
		HullShaderRHI,
		DomainShaderRHI,
		GeometryShaderRHI
	);
	if (CachedBoundShaderState.GetReference())
	{
		// If we've already created a bound shader state with these parameters, reuse it.
		return CachedBoundShaderState;
	}

	return new FVulkanBoundShaderState(VertexDeclarationRHI, VertexShaderRHI, PixelShaderRHI, HullShaderRHI, DomainShaderRHI, GeometryShaderRHI);
}


template void FVulkanDescriptorSetsLayoutInfo::FinalizeBindings<true>(const FUniformBufferGatherInfo& UBGatherInfo, const TArrayView<FRHISamplerState*>& ImmutableSamplers);
template void FVulkanDescriptorSetsLayoutInfo::FinalizeBindings<false>(const FUniformBufferGatherInfo& UBGatherInfo, const TArrayView<FRHISamplerState*>& ImmutableSamplers);
