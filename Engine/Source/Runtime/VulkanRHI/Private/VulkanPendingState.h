// Copyright Epic Games, Inc. All Rights Reserved..

/*=============================================================================
	VulkanPendingState.h: Private VulkanPendingState definitions.
=============================================================================*/

#pragma once

// Dependencies
#include "VulkanConfiguration.h"
#include "VulkanState.h"
#include "VulkanResources.h"
#include "VulkanUtil.h"
#include "VulkanViewport.h"
#include "VulkanDynamicRHI.h"
#include "VulkanPipeline.h"
#include "VulkanPipelineState.h"

// All the current compute pipeline states in use
class FVulkanPendingComputeState : public VulkanRHI::FDeviceChild
{
public:
	FVulkanPendingComputeState(FVulkanDevice* InDevice, FVulkanCommandListContext& InContext)
		: VulkanRHI::FDeviceChild(InDevice)
		, Context(InContext)
	{
	}

	~FVulkanPendingComputeState();

	void Reset()
	{
		CurrentPipeline = nullptr;
		CurrentState = nullptr;
	}

	void SetComputePipeline(FVulkanComputePipeline* InComputePipeline)
	{
		if (InComputePipeline != CurrentPipeline)
		{
			CurrentPipeline = InComputePipeline;
			FVulkanComputePipelineDescriptorState** Found = PipelineStates.Find(InComputePipeline);
			if (Found)
			{
				CurrentState = *Found;
				check(CurrentState->ComputePipeline == InComputePipeline);
			}
			else
			{
				CurrentState = new FVulkanComputePipelineDescriptorState(Device, InComputePipeline);
				PipelineStates.Add(CurrentPipeline, CurrentState);
			}

			CurrentState->Reset();
		}
	}

	void PrepareForDispatch(FVulkanCmdBuffer* CmdBuffer);

	inline const FVulkanComputeShader* GetCurrentShader() const
	{
		return CurrentPipeline ? CurrentPipeline->GetShader() : nullptr;
	}

	void SetUAVForUBResource(uint32 DescriptorSet, uint32 BindingIndex, FVulkanUnorderedAccessView* UAV);

	inline void SetUAVForStage(uint32 UAVIndex, FVulkanUnorderedAccessView* UAV)
	{
		const FVulkanComputePipelineDescriptorInfo& DescriptorInfo = CurrentState->GetComputePipelineDescriptorInfo();
		uint8 DescriptorSet;
		uint32 BindingIndex;
		if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, UAVIndex, DescriptorSet, BindingIndex))
		{
			return;
		}

		SetUAVForUBResource(DescriptorSet, BindingIndex, UAV);
	}

	inline void SetTextureForStage(uint32 TextureIndex, const FVulkanTextureBase* TextureBase, VkImageLayout Layout)
	{
		const FVulkanComputePipelineDescriptorInfo& DescriptorInfo = CurrentState->GetComputePipelineDescriptorInfo();
		uint8 DescriptorSet;
		uint32 BindingIndex;
		if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, TextureIndex, DescriptorSet, BindingIndex))
		{
			return;
		}

		CurrentState->SetTexture(DescriptorSet, BindingIndex, TextureBase, Layout);
	}

	inline void SetSamplerStateForStage(uint32 SamplerIndex, FVulkanSamplerState* Sampler)
	{
		const FVulkanComputePipelineDescriptorInfo& DescriptorInfo = CurrentState->GetComputePipelineDescriptorInfo();
		uint8 DescriptorSet;
		uint32 BindingIndex;
		if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, SamplerIndex, DescriptorSet, BindingIndex))
		{
			return;
		}

		CurrentState->SetSamplerState(DescriptorSet, BindingIndex, Sampler);
	}

	inline void SetTextureForUBResource(int32 DescriptorSet, uint32 BindingIndex, const FVulkanTextureBase* TextureBase, VkImageLayout Layout)
	{
		CurrentState->SetTexture(DescriptorSet, BindingIndex, TextureBase, Layout);
	}

	void SetSRVForUBResource(uint32 DescriptorSet, uint32 BindingIndex, FVulkanShaderResourceView* SRV);

	inline void SetSRVForStage(uint32 SRVIndex, FVulkanShaderResourceView* SRV)
	{
		const FVulkanComputePipelineDescriptorInfo& DescriptorInfo = CurrentState->GetComputePipelineDescriptorInfo();
		uint8 DescriptorSet;
		uint32 BindingIndex;
		if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, SRVIndex, DescriptorSet, BindingIndex))
		{
			return;
		}

		SetSRVForUBResource(DescriptorSet, BindingIndex, SRV);
	}

	inline void SetPackedGlobalShaderParameter(uint32 BufferIndex, uint32 Offset, uint32 NumBytes, const void* NewValue)
	{
		CurrentState->SetPackedGlobalShaderParameter(BufferIndex, Offset, NumBytes, NewValue);
	}

	inline void SetUniformBufferConstantData(uint32 BindingIndex, const TArray<uint8>& ConstantData, const FVulkanUniformBuffer* SrcBuffer)
	{
		CurrentState->SetUniformBufferConstantData(BindingIndex, ConstantData, SrcBuffer);
	}

	inline void SetSamplerStateForUBResource(uint32 DescriptorSet, uint32 BindingIndex, FVulkanSamplerState* Sampler)
	{
		CurrentState->SetSamplerState(DescriptorSet, BindingIndex, Sampler);
	}

	void NotifyDeletedPipeline(FVulkanComputePipeline* Pipeline)
	{
		PipelineStates.Remove(Pipeline);
	}

protected:
	FVulkanComputePipeline* CurrentPipeline = nullptr;
	FVulkanComputePipelineDescriptorState* CurrentState = nullptr;

	TMap<FVulkanComputePipeline*, FVulkanComputePipelineDescriptorState*> PipelineStates;

	FVulkanCommandListContext& Context;

	friend class FVulkanCommandListContext;
};

// All the current gfx pipeline states in use
class FVulkanPendingGfxState : public VulkanRHI::FDeviceChild
{
public:
	FVulkanPendingGfxState(FVulkanDevice* InDevice, FVulkanCommandListContext& InContext)
		: VulkanRHI::FDeviceChild(InDevice)
		, Context(InContext)
	{
		Reset();
	}

	~FVulkanPendingGfxState();

	void Reset()
	{
		FMemory::Memzero(Scissor);
		FMemory::Memzero(Viewport);
		StencilRef = 0;
		bScissorEnable = false;

		CurrentPipeline = nullptr;
		CurrentState = nullptr;
		bDirtyVertexStreams = true;

		PrimitiveType = PT_Num;

		//#todo-rco: Would this cause issues?
		//FMemory::Memzero(PendingStreams);
	}

	const uint64 GetCurrentShaderKey(EShaderFrequency Frequency) const
	{
		return (CurrentPipeline ? CurrentPipeline->GetShaderKey(Frequency) : 0);
	}

	const uint64 GetCurrentShaderKey(ShaderStage::EStage Stage) const
	{
		return GetCurrentShaderKey(ShaderStage::GetFrequencyForGfxStage(Stage));
	}

	void SetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ)
	{
		FMemory::Memzero(Viewport);

		Viewport.x = MinX;
		Viewport.y = MinY;
		Viewport.width = MaxX - MinX;
		Viewport.height = MaxY - MinY;
		Viewport.minDepth = MinZ;
		if (MinZ == MaxZ)
		{
			// Engine pases in some cases MaxZ as 0.0
			Viewport.maxDepth = MinZ + 1.0f;
		}
		else
		{
			Viewport.maxDepth = MaxZ;
		}

		SetScissorRect((uint32)MinX, (uint32)MinY, (uint32)(MaxX - MinX), (uint32)(MaxY - MinY));
		bScissorEnable = false;
	}

	inline void SetScissor(bool bInEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY)
	{
		if (bInEnable)
		{
			SetScissorRect(MinX, MinY, MaxX - MinX, MaxY - MinY);
		}
		else
		{
			SetScissorRect(Viewport.x, Viewport.y, Viewport.width, Viewport.height);
		}

		bScissorEnable = bInEnable;
	}

	inline void SetScissorRect(uint32 MinX, uint32 MinY, uint32 Width, uint32 Height)
	{
		FMemory::Memzero(Scissor);

		Scissor.offset.x = MinX;
		Scissor.offset.y = MinY;
		Scissor.extent.width = Width;
		Scissor.extent.height = Height;
	}

	inline void SetStreamSource(uint32 StreamIndex, VkBuffer VertexBuffer, uint32 Offset)
	{
		PendingStreams[StreamIndex].Stream = VertexBuffer;
		PendingStreams[StreamIndex].BufferOffset = Offset;
		bDirtyVertexStreams = true;
	}

	inline void Bind(VkCommandBuffer CmdBuffer)
	{
		CurrentPipeline->Bind(CmdBuffer);
	}

	inline void SetTextureForStage(ShaderStage::EStage Stage, uint32 ParameterIndex, const FVulkanTextureBase* TextureBase, VkImageLayout Layout)
	{
		const FVulkanGfxPipelineDescriptorInfo& DescriptorInfo = CurrentState->GetGfxPipelineDescriptorInfo();
		uint8 DescriptorSet;
		uint32 BindingIndex;
		if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, Stage, ParameterIndex, DescriptorSet, BindingIndex))
		{
			return;
		}

		CurrentState->SetTexture(DescriptorSet, BindingIndex, TextureBase, Layout);
	}

	inline void SetTextureForUBResource(uint8 DescriptorSet, uint32 BindingIndex, const FVulkanTextureBase* TextureBase, VkImageLayout Layout)
	{
		CurrentState->SetTexture(DescriptorSet, BindingIndex, TextureBase, Layout);
	}

	inline void SetUniformBufferConstantData(ShaderStage::EStage Stage, uint32 BindingIndex, const TArray<uint8>& ConstantData, const FVulkanUniformBuffer* SrcBuffer)
	{
		CurrentState->SetUniformBufferConstantData(Stage, BindingIndex, ConstantData, SrcBuffer);
	}

	template<bool bDynamic>
	inline void SetUniformBuffer(uint8 DescriptorSet, uint32 BindingIndex, const FVulkanRealUniformBuffer* UniformBuffer)
	{
		CurrentState->SetUniformBuffer<bDynamic>(DescriptorSet, BindingIndex, UniformBuffer);
	}

	void SetUAVForUBResource(uint8 DescriptorSet, uint32 BindingIndex, FVulkanUnorderedAccessView* UAV);

	inline void SetUAVForStage(ShaderStage::EStage Stage, uint32 ParameterIndex, FVulkanUnorderedAccessView* UAV)
	{
		const FVulkanGfxPipelineDescriptorInfo& DescriptorInfo = CurrentState->GetGfxPipelineDescriptorInfo();
		uint8 DescriptorSet;
		uint32 BindingIndex;
		if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, Stage, ParameterIndex, DescriptorSet, BindingIndex))
		{
			return;
		}

		SetUAVForUBResource(DescriptorSet, BindingIndex, UAV);
	}

	void SetSRVForUBResource(uint8 DescriptorSet, uint32 BindingIndex, FVulkanShaderResourceView* SRV);

	inline void SetSRVForStage(ShaderStage::EStage Stage, uint32 ParameterIndex, FVulkanShaderResourceView* SRV)
	{
		const FVulkanGfxPipelineDescriptorInfo& DescriptorInfo = CurrentState->GetGfxPipelineDescriptorInfo();
		uint8 DescriptorSet;
		uint32 BindingIndex;
		if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, Stage, ParameterIndex, DescriptorSet, BindingIndex))
		{
			return;
		}

		SetSRVForUBResource(DescriptorSet, BindingIndex, SRV);
	}

	inline void SetSamplerStateForStage(ShaderStage::EStage Stage, uint32 ParameterIndex, FVulkanSamplerState* Sampler)
	{
		const FVulkanGfxPipelineDescriptorInfo& DescriptorInfo = CurrentState->GetGfxPipelineDescriptorInfo();
		uint8 DescriptorSet;
		uint32 BindingIndex;
		if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, Stage, ParameterIndex, DescriptorSet, BindingIndex))
		{
			return;
		}

		CurrentState->SetSamplerState(DescriptorSet, BindingIndex, Sampler);
	}

	inline void SetSamplerStateForUBResource(uint32 DescriptorSet, uint32 BindingIndex, FVulkanSamplerState* Sampler)
	{
		CurrentState->SetSamplerState(DescriptorSet, BindingIndex, Sampler);
	}

	inline void SetPackedGlobalShaderParameter(ShaderStage::EStage Stage, uint32 BufferIndex, uint32 Offset, uint32 NumBytes, const void* NewValue)
	{
		const FVulkanGfxPipelineDescriptorInfo& DescriptorInfo = CurrentState->GetGfxPipelineDescriptorInfo();
		CurrentState->SetPackedGlobalShaderParameter(Stage, BufferIndex, Offset, NumBytes, NewValue);
	}

	void PrepareForDraw(FVulkanCmdBuffer* CmdBuffer);

	bool SetGfxPipeline(FVulkanRHIGraphicsPipelineState* InGfxPipeline, bool bForceReset);

	inline void UpdateDynamicStates(FVulkanCmdBuffer* Cmd)
	{
		InternalUpdateDynamicStates(Cmd);
	}

	inline void SetStencilRef(uint32 InStencilRef)
	{
		if (InStencilRef != StencilRef)
		{
			StencilRef = InStencilRef;
		}
	}

	void NotifyDeletedPipeline(FVulkanRHIGraphicsPipelineState* Pipeline)
	{
		PipelineStates.Remove(Pipeline);
	}

	inline void MarkNeedsDynamicStates()
	{
	}

protected:
	VkViewport Viewport;
	VkRect2D Scissor;

	EPrimitiveType PrimitiveType = PT_Num;
	uint32 StencilRef;
	bool bScissorEnable;

	bool bNeedToClear;

	FVulkanRHIGraphicsPipelineState* CurrentPipeline;
	FVulkanGraphicsPipelineDescriptorState* CurrentState;

	TMap<FVulkanRHIGraphicsPipelineState*, FVulkanGraphicsPipelineDescriptorState*> PipelineStates;

	struct FVertexStream
	{
		FVertexStream() :
			Stream(VK_NULL_HANDLE),
			BufferOffset(0)
		{
		}

		VkBuffer Stream;
		uint32 BufferOffset;
	};
	FVertexStream PendingStreams[MaxVertexElementCount];
	bool bDirtyVertexStreams;

	void InternalUpdateDynamicStates(FVulkanCmdBuffer* Cmd);
	void UpdateInputAttachments(FVulkanFramebuffer* Framebuffer);

	FVulkanCommandListContext& Context;

	friend class FVulkanCommandListContext;
};
