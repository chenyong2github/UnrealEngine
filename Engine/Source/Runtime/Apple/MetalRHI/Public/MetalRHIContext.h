// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// Metal RHI public headers.
#include <Metal/Metal.h>
#include "MetalState.h"
#include "MetalResources.h"
#include "MetalViewport.h"

class FMetalContext;

/** The interface RHI command context. */
class FMetalRHICommandContext : public IRHICommandContext
{
public:
	FMetalRHICommandContext(class FMetalProfiler* InProfiler, FMetalContext* WrapContext);
	virtual ~FMetalRHICommandContext();

	/** Get the internal context */
	FORCEINLINE FMetalContext& GetInternalContext() const { return *Context; }
	
	/** Get the profiler pointer */
	FORCEINLINE class FMetalProfiler* GetProfiler() const { return Profiler; }

	/**
	 *Sets the current compute shader.  Mostly for compliance with platforms
	 *that require shader setting before resource binding.
	 */
	virtual void RHISetComputeShader(FRHIComputeShader* ComputeShader) override;
	
	virtual void RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState) override;
	
	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) final override;
	
	virtual void RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
	
	virtual void RHIAutomaticCacheFlushAfterComputeShader(bool bEnable) final override;
	
	virtual void RHIFlushComputeShaderCache() final override;
	
	// Useful when used with geometry shader (emit polygons to different viewports), otherwise SetViewPort() is simpler
	// @param Count >0
	// @param Data must not be 0
	virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) final override;
	
	/** Clears a UAV to the multi-component value provided. */
	virtual void RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values) final override;
	virtual void RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values) final override;
	
	virtual void RHICopyTexture(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FRHICopyTextureInfo& CopyInfo) final override;
	
	/**
	 * Resolves from one texture to another.
	 * @param SourceTexture - texture to resolve from, 0 is silenty ignored
	 * @param DestTexture - texture to resolve to, 0 is silenty ignored
	 * @param ResolveParams - optional resolve params
	 */
	virtual void RHICopyToResolveTarget(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FResolveParams& ResolveParams) final override;
	
	virtual void RHIBeginRenderQuery(FRHIRenderQuery* RenderQuery) final override;
	
	virtual void RHIEndRenderQuery(FRHIRenderQuery* RenderQuery) final override;
	
	void RHIBeginOcclusionQueryBatch(uint32 NumQueriesInBatch);
	
	void RHIEndOcclusionQueryBatch();
	
	virtual void RHISubmitCommandsHint() override;

	virtual void RHIDiscardRenderTargets(bool Depth, bool Stencil, uint32 ColorBitMask) final override;
	
	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIBeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI) override;
	
	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIEndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync) override;
	
	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIBeginFrame() override;
	
	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIEndFrame() override;
	
	/**
		* Signals the beginning of scene rendering. The RHI makes certain caching assumptions between
		* calls to BeginScene/EndScene. Currently the only restriction is that you can't update texture
		* references.
		*/
	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIBeginScene() override;
	
	/**
		* Signals the end of scene rendering. See RHIBeginScene.
		*/
	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIEndScene() override;
	
	virtual void RHISetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBuffer, uint32 Offset) final override;

	// @param MinX including like Win32 RECT
	// @param MinY including like Win32 RECT
	// @param MaxX excluding like Win32 RECT
	// @param MaxY excluding like Win32 RECT
	virtual void RHISetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ) final override;

	virtual void RHISetStereoViewport(float LeftMinX, float RightMinX, float LeftMinY, float RightMinY, float MinZ, float LeftMaxX, float RightMaxX, float LeftMaxY, float RightMaxY, float MaxZ) final override;
	
	// @param MinX including like Win32 RECT
	// @param MinY including like Win32 RECT
	// @param MaxX excluding like Win32 RECT
	// @param MaxY excluding like Win32 RECT
	virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) final override;
	
	virtual void RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState) final override;

	virtual void RHISetGlobalUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers) final override;

	/** Set the shader resource view of a surface. */
	virtual void RHISetShaderTexture(FRHIGraphicsShader* Shader, uint32 TextureIndex, FRHITexture* NewTexture) final override;
	
	/** Set the shader resource view of a surface.  This is used for binding TextureMS parameter types that need a multi sampled view. */
	virtual void RHISetShaderTexture(FRHIComputeShader* PixelShader, uint32 TextureIndex, FRHITexture* NewTexture) final override;
	
	/**
	 * Sets sampler state.
	 * @param ComputeShader		The compute shader to set the sampler for.
	 * @param SamplerIndex		The index of the sampler.
	 * @param NewState			The new sampler state.
	 */
	virtual void RHISetShaderSampler(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHISamplerState* NewState) final override;
	
	/**
	 * Sets sampler state.
	 * @param Shader			The shader to set the sampler for.
	 * @param SamplerIndex		The index of the sampler.
	 * @param NewState			The new sampler state.
	 */
	virtual void RHISetShaderSampler(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHISamplerState* NewState) final override;
	
	/** Sets a pixel shader UAV parameter. */
	virtual void RHISetUAVParameter(FRHIPixelShader* PixelShaderRHI, uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI) final override;
	
	/** Sets a compute shader UAV parameter. */
	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV) final override;
	
	/** Sets a compute shader UAV parameter and initial count */
	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount) final override;
	
	virtual void RHISetShaderResourceViewParameter(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override;
	
	virtual void RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override;
	
	virtual void RHISetShaderUniformBuffer(FRHIGraphicsShader* Shader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override;
	
	virtual void RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override;
	
	virtual void RHISetShaderParameter(FRHIGraphicsShader* Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	
	virtual void RHISetShaderParameter(FRHIComputeShader* ComputeShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	
	virtual void RHISetStencilRef(uint32 StencilRef) final override;

	virtual void RHISetBlendFactor(const FLinearColor& BlendFactor) final override;

	virtual void RHISetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets, const FRHIDepthRenderTargetView* NewDepthStencilTarget) final override;
	
	virtual void RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo) final override;
	
	virtual void RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) final override;
	
	virtual void RHIDrawPrimitiveIndirect(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
	
	virtual void RHIDrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances) final override;
	
	// @param NumPrimitives need to be >0
	virtual void RHIDrawIndexedPrimitive(FRHIIndexBuffer* IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances) final override;
	
	virtual void RHIDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBuffer, FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;

	/**
	* Sets Depth Bounds Testing with the given min/max depth.
	* @param MinDepth	The minimum depth for depth bounds test
	* @param MaxDepth	The maximum depth for depth bounds test.
	*					The valid values for fMinDepth and fMaxDepth are such that 0 <= fMinDepth <= fMaxDepth <= 1
	*/
	virtual void RHISetDepthBounds(float MinDepth, float MaxDepth) final override;

	virtual void RHIPushEvent(const TCHAR* Name, FColor Color) final override;
	
	virtual void RHIPopEvent() final override;
	
	virtual void RHIUpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture) final override;
	
	/**
	 * Explicitly transition a UAV from readable -> writable by the GPU or vice versa.
	 * Also explicitly states which pipeline the UAV can be used on next.  For example, if a Compute job just wrote this UAV for a Pixel shader to read
	 * you would do EResourceTransitionAccess::Readable and EResourceTransitionPipeline::EComputeToGfx
	 *
	 * @param TransitionType - direction of the transition
	 * @param EResourceTransitionPipeline - How this UAV is transitioning between Gfx and Compute, if at all.
	 * @param InUAVs - array of UAV objects to transition
	 * @param NumUAVs - number of UAVs to transition
	 * @param WriteComputeFence - Optional ComputeFence to write as part of this transition
	 */
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 NumUAVs, FRHIComputeFence* WriteComputeFence) final override;
	
	virtual void RHICopyToStagingBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIStagingBuffer* DestinationStagingBufferRHI, uint32 Offset, uint32 NumBytes) final override;
	virtual void RHIWriteGPUFence(FRHIGPUFence* FenceRHI) final override;

	/**
	 * Explicitly transition a texture resource from readable -> writable by the GPU or vice versa.
	 * We know rendertargets are only used as rendered targets on the Gfx pipeline, so these transitions are assumed to be implemented such
	 * Gfx->Gfx and Gfx->Compute pipeline transitions are both handled by this call by the RHI implementation.  Hence, no pipeline parameter on this call.
	 *
	 * @param TransitionType - direction of the transition
	 * @param InTextures - array of texture objects to transition
	 * @param NumTextures - number of textures to transition
	 */
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, FRHITexture** InTextures, int32 NumTextures) final override;
	
	virtual void RHITransitionResources(FExclusiveDepthStencil DepthStencilMode, FRHITexture* DepthTexture) final override
	{
		IRHICommandContext::RHITransitionResources(DepthStencilMode, DepthTexture);
	}

	/**
	 * Compute queue will wait for the fence to be written before continuing.
	 */
	virtual void RHIWaitComputeFence(FRHIComputeFence* InFence) final override;

	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName) final override;

	virtual void RHIEndRenderPass() final override;
	
	virtual void RHIBeginComputePass(const TCHAR* InName) final override;
	
	virtual void RHIEndComputePass() final override;

protected:
	static TGlobalResource<TBoundShaderStateHistory<10000>> BoundShaderStateHistory;
	
	/** Context implementation details. */
	FMetalContext* Context;
	
	/** Occlusion query batch fence */
	TSharedPtr<FMetalCommandBufferFence, ESPMode::ThreadSafe> CommandBufferFence;

	/** Profiling implementation details. */
	class FMetalProfiler* Profiler;
	
	/** Some local variables to track the pending primitive information used in RHIEnd*UP functions */
	FMetalBuffer PendingVertexBuffer;
	uint32 PendingVertexDataStride;
	
	FMetalBuffer PendingIndexBuffer;
	uint32 PendingIndexDataStride;
	
	uint32 PendingPrimitiveType;
	uint32 PendingNumPrimitives;

	template <typename TRHIShader>
	void ApplyGlobalUniformBuffers(TRHIShader* Shader)
	{
		if (Shader)
		{
			::ApplyGlobalUniformBuffers(this, Shader, Shader->StaticSlots, Shader->Bindings.ShaderResourceTable.ResourceTableLayoutHashes, GlobalUniformBuffers);
		}
	}

	TArray<FRHIUniformBuffer*> GlobalUniformBuffers;

private:
	void RHIClearMRT(bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);
};

class FMetalRHIComputeContext : public FMetalRHICommandContext
{
public:
	FMetalRHIComputeContext(class FMetalProfiler* InProfiler, FMetalContext* WrapContext);
	virtual ~FMetalRHIComputeContext();
	
	virtual void RHISetAsyncComputeBudget(EAsyncComputeBudget Budget) final override;
	virtual void RHISetComputeShader(FRHIComputeShader* ComputeShader) final override;
	virtual void RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState) final override;
	virtual void RHISubmitCommandsHint() final override;
};

class FMetalRHIImmediateCommandContext : public FMetalRHICommandContext
{
public:
	FMetalRHIImmediateCommandContext(class FMetalProfiler* InProfiler, FMetalContext* WrapContext);

	// FRHICommandContext API accessible only on the immediate device context
	virtual void RHIBeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI) final override;
	virtual void RHIEndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync) final override;
	virtual void RHIBeginFrame() final override;
	virtual void RHIEndFrame() final override;
	virtual void RHIBeginScene() final override;
	virtual void RHIEndScene() final override;
	
protected:
	friend class FMetalDynamicRHI;
};
