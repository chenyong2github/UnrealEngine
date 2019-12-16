// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanContext.h: Class to generate Vulkan command buffers from RHI CommandLists
=============================================================================*/

#pragma once 

#include "VulkanResources.h"

class FVulkanDevice;
class FVulkanCommandBufferManager;
class FVulkanPendingGfxState;
class FVulkanPendingComputeState;
class FVulkanQueue;
class FVulkanOcclusionQueryPool;

struct FInputAttachmentData;

class FTransitionAndLayoutManagerData
{
public:
	void TempCopy(const FTransitionAndLayoutManagerData& In)
	{
		Framebuffers = In.Framebuffers;
		RenderPasses = In.RenderPasses;
		Layouts = In.Layouts;
	}

protected:
	TMap<uint32, FVulkanRenderPass*> RenderPasses;
	struct FFramebufferList
	{
		TArray<FVulkanFramebuffer*> Framebuffer;
	};
	TMap<uint32, FFramebufferList*> Framebuffers;
	TMap<VkImage, VkImageLayout> Layouts;
};

class FTransitionAndLayoutManager : public FTransitionAndLayoutManagerData
{
	using FFramebufferList = FTransitionAndLayoutManagerData::FFramebufferList;
public:
	FTransitionAndLayoutManager()
		: CurrentRenderPass(nullptr)
		, CurrentFramebuffer(nullptr)
	{
	}

	void Destroy(FVulkanDevice& InDevice, FTransitionAndLayoutManager* Immediate);

	FVulkanFramebuffer* GetOrCreateFramebuffer(FVulkanDevice& InDevice, const FRHISetRenderTargetsInfo& RenderTargetsInfo, const FVulkanRenderTargetLayout& RTLayout, FVulkanRenderPass* RenderPass);
	FVulkanRenderPass* GetOrCreateRenderPass(FVulkanDevice& InDevice, const FVulkanRenderTargetLayout& RTLayout)
	{
		uint32 RenderPassHash = RTLayout.GetRenderPassFullHash();
		FVulkanRenderPass** FoundRenderPass = nullptr;
		{
			FScopeLock Lock(&RenderPassesCS);
			FoundRenderPass = RenderPasses.Find(RenderPassHash);
		}
		if (FoundRenderPass)
		{
			return *FoundRenderPass;
		}

		FVulkanRenderPass* RenderPass = new FVulkanRenderPass(InDevice, RTLayout);
		{
			FScopeLock Lock(&RenderPassesCS);
			RenderPasses.Add(RenderPassHash, RenderPass);
		}
		return RenderPass;
	}

	void BeginEmulatedRenderPass(FVulkanCommandListContext& Context, FVulkanDevice& InDevice, FVulkanCmdBuffer* CmdBuffer, const FRHISetRenderTargetsInfo& RenderTargetsInfo, const FVulkanRenderTargetLayout& RTLayout, FVulkanRenderPass* RenderPass, FVulkanFramebuffer* Framebuffer);
	void EndEmulatedRenderPass(FVulkanCmdBuffer* CmdBuffer);

	void BeginRealRenderPass(FVulkanCommandListContext& Context, FVulkanDevice& InDevice, FVulkanCmdBuffer* CmdBuffer, const FRHIRenderPassInfo& RPInfo, const FVulkanRenderTargetLayout& RTLayout, FVulkanRenderPass* RenderPass, FVulkanFramebuffer* Framebuffer);
	void EndRealRenderPass(FVulkanCmdBuffer* CmdBuffer);

	struct FGenerateMipsInfo
	{
		int32 NumRenderTargets = 0;

		bool bInsideGenerateMips;
		bool bLastMip;
		int32 CurrentSlice;
		int32 CurrentMip;

		struct
		{
			// Per face/slice array of mip layouts
			TArray<TArray<VkImageLayout>> Layouts;
			VkImage CurrentImage;
		} Target[MaxSimultaneousRenderTargets];

		FGenerateMipsInfo()
		{
			Reset();
		}

		void Reset()
		{
			NumRenderTargets = 0;
			bInsideGenerateMips = false;
			bLastMip = false;
			CurrentSlice = -1;
			CurrentMip = -1;
			for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
			{
				Target[Index].Layouts.Reset(0);
				Target[Index].CurrentImage = VK_NULL_HANDLE;
			}
		}
	} GenerateMipsInfo;

	bool bInsideRealRenderPass = false;

	FVulkanRenderPass* CurrentRenderPass;
	FVulkanFramebuffer* CurrentFramebuffer;

	FCriticalSection RenderPassesCS;

	void NotifyDeletedRenderTarget(FVulkanDevice& InDevice, VkImage Image);

	inline void NotifyDeletedImage(VkImage Image)
	{
		Layouts.Remove(Image);
	}

	VkImageLayout FindLayoutChecked(VkImage Image) const
	{
		return Layouts.FindChecked(Image);
	}



	VkImageLayout FindOrAddLayout(VkImage Image, VkImageLayout LayoutIfNotFound);
	VULKANRHI_API VkImageLayout& FindOrAddLayoutRW(VkImage Image, VkImageLayout LayoutIfNotFound);
	void TransitionResource(FVulkanCmdBuffer* CmdBuffer, FVulkanSurface& Surface, VulkanRHI::EImageLayoutBarrier DestLayout);
};

class FVulkanCommandListContext : public IRHICommandContext
{
public:
	FVulkanCommandListContext(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, FVulkanQueue* InQueue, FVulkanCommandListContext* InImmediate);
	virtual ~FVulkanCommandListContext();

	inline bool IsImmediate() const
	{
		return Immediate == nullptr;
	}

	virtual void RHISetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBuffer, uint32 Offset) final override;
	virtual void RHISetViewport(uint32 MinX, uint32 MinY, float MinZ, uint32 MaxX, uint32 MaxY, float MaxZ) final override;
	virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) final override;
	virtual void RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState) final override;
	virtual void RHISetShaderTexture(FRHIVertexShader* VertexShader, uint32 TextureIndex, FRHITexture* NewTexture) final override;
	virtual void RHISetShaderTexture(FRHIHullShader* HullShader, uint32 TextureIndex, FRHITexture* NewTexture) final override;
	virtual void RHISetShaderTexture(FRHIDomainShader* DomainShader, uint32 TextureIndex, FRHITexture* NewTexture) final override;
	virtual void RHISetShaderTexture(FRHIGeometryShader* GeometryShader, uint32 TextureIndex, FRHITexture* NewTexture) final override;
	virtual void RHISetShaderTexture(FRHIPixelShader* PixelShader, uint32 TextureIndex, FRHITexture* NewTexture) final override;
	virtual void RHISetShaderTexture(FRHIComputeShader* PixelShader, uint32 TextureIndex, FRHITexture* NewTexture) final override;
	virtual void RHISetShaderSampler(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHISamplerState* NewState) final override;
	virtual void RHISetShaderSampler(FRHIVertexShader* VertexShader, uint32 SamplerIndex, FRHISamplerState* NewState) final override;
	virtual void RHISetShaderSampler(FRHIGeometryShader* GeometryShader, uint32 SamplerIndex, FRHISamplerState* NewState) final override;
	virtual void RHISetShaderSampler(FRHIDomainShader* DomainShader, uint32 SamplerIndex, FRHISamplerState* NewState) final override;
	virtual void RHISetShaderSampler(FRHIHullShader* HullShader, uint32 SamplerIndex, FRHISamplerState* NewState) final override;
	virtual void RHISetShaderSampler(FRHIPixelShader* PixelShader, uint32 SamplerIndex, FRHISamplerState* NewState) final override;
	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV) final override;
	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount) final override;
	virtual void RHISetShaderResourceViewParameter(FRHIPixelShader* PixelShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FRHIVertexShader* VertexShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FRHIHullShader* HullShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FRHIDomainShader* DomainShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FRHIGeometryShader* GeometryShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override;
	virtual void RHISetShaderUniformBuffer(FRHIVertexShader* VertexShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FRHIHullShader* HullShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FRHIDomainShader* DomainShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FRHIGeometryShader* GeometryShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FRHIPixelShader* PixelShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override;
	virtual void RHISetShaderParameter(FRHIVertexShader* VertexShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FRHIHullShader* HullShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FRHIDomainShader* DomainShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FRHIGeometryShader* GeometryShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FRHIPixelShader* PixelShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FRHIComputeShader* ComputeShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetStencilRef(uint32 StencilRef) final override;
	virtual void RHISetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets, const FRHIDepthRenderTargetView* NewDepthStencilTarget, uint32 NumUAVs, FRHIUnorderedAccessView* const* UAVs) final override;
	virtual void RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo) final override;
	virtual void RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) final override;
	virtual void RHIDrawPrimitiveIndirect(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHIDrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances) final override;
	virtual void RHIDrawIndexedPrimitive(FRHIIndexBuffer* IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances) final override;
	virtual void RHIDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBuffer, FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHISetDepthBounds(float MinDepth, float MaxDepth) final override;
	virtual void RHIPushEvent(const TCHAR* Name, FColor Color) final override;
	virtual void RHIPopEvent() final override;

	virtual void RHISetComputeShader(FRHIComputeShader* ComputeShader) final override;
	virtual void RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState) final override;
	virtual void RHIWaitComputeFence(FRHIComputeFence* InFence) final override;
	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) final override;
	virtual void RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHIAutomaticCacheFlushAfterComputeShader(bool bEnable) final override;

	virtual void RHIFlushComputeShaderCache() final override;
	virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) final override;
	virtual void RHIClearTinyUAV(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const uint32* Values) final override;
	virtual void RHICopyToResolveTarget(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FResolveParams& ResolveParams) final override;
	virtual void RHICopyTexture(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FRHICopyTextureInfo& CopyInfo) final override;
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, FRHITexture** InRenderTargets, int32 NumTextures) final override;
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 NumUAVs, FRHIComputeFence* WriteComputeFence) final override;
	virtual void RHITransitionResources(FExclusiveDepthStencil DepthStencilMode, FRHITexture* DepthTexture) final override;
	virtual void RHICopyToStagingBuffer(FRHIVertexBuffer* SourceBuffer, FRHIStagingBuffer* DestinationStagingBuffer, uint32 Offset, uint32 NumBytes) final override;
	virtual void RHIWriteGPUFence(FRHIGPUFence* Fence) final override;

	// Render time measurement
	virtual void RHIBeginRenderQuery(FRHIRenderQuery* RenderQuery) final override;
	virtual void RHIEndRenderQuery(FRHIRenderQuery* RenderQuery) final override;

	virtual void RHIUpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture) final override;

	virtual void RHISubmitCommandsHint() final override;

	virtual void RHIBeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI) final override;
	virtual void RHIEndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync) final override;

	virtual void RHIBeginFrame() final override;
	virtual void RHIEndFrame() final override;

	virtual void RHIBeginScene() final override;
	virtual void RHIEndScene() final override;

	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName) final override;
	virtual void RHIEndRenderPass() final override;
	virtual void RHINextSubpass() final override;

	inline FVulkanCommandBufferManager* GetCommandBufferManager()
	{
		return CommandBufferManager;
	}

	inline VulkanRHI::FTempFrameAllocationBuffer& GetTempFrameAllocationBuffer()
	{
		return TempFrameAllocationBuffer;
	}

	inline FVulkanPendingGfxState* GetPendingGfxState()
	{
		return PendingGfxState;
	}

	inline FVulkanPendingComputeState* GetPendingComputeState()
	{
		return PendingComputeState;
	}

	inline void NotifyDeletedRenderTarget(VkImage Image)
	{
		TransitionAndLayoutManager.NotifyDeletedRenderTarget(*Device, Image);
	}

	inline void NotifyDeletedImage(VkImage Image)
	{
		TransitionAndLayoutManager.NotifyDeletedImage(Image);
	}

	inline FVulkanRenderPass* GetCurrentRenderPass()
	{
		return TransitionAndLayoutManager.CurrentRenderPass;
	}

	inline uint64 GetFrameCounter() const
	{
		return FrameCounter;
	}

	inline FVulkanUniformBufferUploader* GetUniformBufferUploader()
	{
		return UniformBufferUploader;
	}

	inline FVulkanQueue* GetQueue()
	{
		return Queue;
	}

	void WriteBeginTimestamp(FVulkanCmdBuffer* CmdBuffer);
	void WriteEndTimestamp(FVulkanCmdBuffer* CmdBuffer);

	void ReadAndCalculateGPUFrameTime();
	
	inline FVulkanGPUProfiler& GetGPUProfiler()
	{
		return GpuProfiler;
	}

	inline FVulkanDevice* GetDevice() const
	{
		return Device;
	}
	void EndRenderQueryInternal(FVulkanCmdBuffer* CmdBuffer, FVulkanRenderQuery* Query);

	inline VkImageLayout FindLayout(VkImage Image)
	{
		return TransitionAndLayoutManager.FindLayoutChecked(Image);
	}

	inline VkImageLayout GetLayoutForDescriptor(const FVulkanSurface& Surface) const
	{
#if PLATFORM_ANDROID && !PLATFORM_LUMIN
		// Workaround clang bug; don't use IsDepthOrStencilAspect() directly
		VkImageAspectFlags AspectFlags = Surface.GetFullAspectMask();
		if ((AspectFlags & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 || (AspectFlags & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
		{
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}
#else
		if (Surface.IsDepthOrStencilAspect())
		{
#if VULKAN_SUPPORTS_MAINTENANCE_LAYER2
			// If the spec gets lenient, we could remove this search since then 
			// Images in VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL_KHR could be used with 
			// descriptor write of VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
			if (Device->GetOptionalExtensions().HasKHRMaintenance2)
			{
				return TransitionAndLayoutManager.FindLayoutChecked(Surface.Image);
			}
#else
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
#endif
		}
#endif

		return TransitionAndLayoutManager.FindLayoutChecked(Surface.Image);
	}

	inline VkImageLayout FindOrAddLayout(VkImage Image, VkImageLayout NewLayout)
	{
		return TransitionAndLayoutManager.FindOrAddLayout(Image, NewLayout);
	}

	inline VkImageLayout& FindOrAddLayoutRW(VkImage Image, VkImageLayout NewLayout)
	{
		return TransitionAndLayoutManager.FindOrAddLayoutRW(Image, NewLayout);
	}

	void PrepareParallelFromBase(const FVulkanCommandListContext& BaseContext);

	void* Hotfix = nullptr;

protected:
	FVulkanDynamicRHI* RHI;
	FVulkanCommandListContext* Immediate;
	FVulkanDevice* Device;
	FVulkanQueue* Queue;
	bool bSubmitAtNextSafePoint;
	bool bAutomaticFlushAfterComputeShader;
	bool bUniformBufferUploadRenderPassDirty = true;
	FVulkanUniformBufferUploader* UniformBufferUploader;

	void BeginOcclusionQueryBatch(FVulkanCmdBuffer* CmdBuffer, uint32 NumQueriesInBatch);
	void EndOcclusionQueryBatch(FVulkanCmdBuffer* CmdBuffer);

	void SetShaderUniformBuffer(ShaderStage::EStage Stage, const FVulkanUniformBuffer* UniformBuffer, int32 ParameterIndex, const FVulkanShader* Shader);

	VulkanRHI::FTempFrameAllocationBuffer TempFrameAllocationBuffer;

	TArray<FString> EventStack;

	FVulkanCommandBufferManager* CommandBufferManager;

	FTransitionAndLayoutManager TransitionAndLayoutManager;


	struct FPendingTransition
	{
		EResourceTransitionAccess TransitionType;

		// Only one of a) Textures or b) UAVs is active at a time
		TArray<FRHITexture*, TInlineAllocator<MaxSimultaneousRenderTargets + 1>> Textures;	// a

		TArray<FRHIUnorderedAccessView*, TInlineAllocator<4>> UAVs;	// b
		FRHIComputeFence* WriteComputeFenceRHI = nullptr;			// b
		EResourceTransitionPipeline TransitionPipeline;				// b

		bool GatherBarriers(FTransitionAndLayoutManager& TransitionAndLayoutManager, TArray<VkBufferMemoryBarrier>& OutBufferBarriers, 
			TArray<VkImageMemoryBarrier>& OutImageBarriers) const;
	};

	void TransitionResources(const FPendingTransition& PendingTransition);
	static void TransitionUAVResourcesTransferringOwnership(FVulkanCommandListContext& GfxContext, FVulkanCommandListContext& ComputeContext, 
		EResourceTransitionPipeline Pipeline, const TArray<VkBufferMemoryBarrier>& BufferBarriers, const TArray<VkImageMemoryBarrier>& ImageBarriers);

	FVulkanOcclusionQueryPool* CurrentOcclusionQueryPool = nullptr;

	// List of UAVs which need setting for pixel shaders. D3D treats UAVs like rendertargets so the RHI doesn't make SetUAV calls at the right time
	struct FPendingPixelUAV
	{
		FVulkanUnorderedAccessView* UAV;
		uint32 BindIndex;
	};
	TArray<FPendingPixelUAV> PendingPixelUAVs;

	FVulkanPendingGfxState* PendingGfxState;
	FVulkanPendingComputeState* PendingComputeState;

	void PrepareForCPURead();
	void RequestSubmitCurrentCommands();

	void InternalClearMRT(FVulkanCmdBuffer* CmdBuffer, bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);

public:
	inline FTransitionAndLayoutManager& GetTransitionAndLayoutManager()
	{
		return TransitionAndLayoutManager;
	}

	FVulkanRenderPass* PrepareRenderPassForPSOCreation(const FGraphicsPipelineStateInitializer& Initializer);
	FVulkanRenderPass* PrepareRenderPassForPSOCreation(const FVulkanRenderTargetLayout& Initializer);

private:
	void RHIClearMRT(bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);

	inline bool SafePointSubmit()
	{
		if (bSubmitAtNextSafePoint)
		{
			InternalSubmitActiveCmdBuffer();
			bSubmitAtNextSafePoint = false;
			return true;
		}

		return false;
	}

	void InternalSubmitActiveCmdBuffer();
	void FlushAfterComputeShader();

	friend class FVulkanDevice;
	friend class FVulkanDynamicRHI;

	// Number of times EndFrame() has been called on this context
	uint64 FrameCounter;

	FVulkanGPUProfiler GpuProfiler;
	FVulkanGPUTiming* FrameTiming;

	friend struct FVulkanCommandContextContainer;
};

class FVulkanCommandListContextImmediate : public FVulkanCommandListContext
{
public:
	FVulkanCommandListContextImmediate(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, FVulkanQueue* InQueue);
};


struct FVulkanCommandContextContainer : public IRHICommandContextContainer, public VulkanRHI::FDeviceChild
{
	FVulkanCommandListContext* CmdContext;

	FVulkanCommandContextContainer(FVulkanDevice* InDevice);

	virtual IRHICommandContext* GetContext() override final;
	virtual void FinishContext() override final;
	virtual void SubmitAndFreeContextContainer(int32 Index, int32 Num) override final;

	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void* RawMemory);

private:
	friend class FVulkanDevice;
};

inline FVulkanCommandListContextImmediate& FVulkanDevice::GetImmediateContext()
{
	return *ImmediateContext;
}
