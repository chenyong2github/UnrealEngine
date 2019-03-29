// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ValidationContext.h: Public Validation Context definitions.
=============================================================================*/

#pragma once

#include "RHIResources.h"
#include "RHIContext.h"
#include "DynamicRHI.h"


class FValidationRHI;

class FValidationContext : public IRHICommandContext
{
public:
	FValidationContext(FValidationRHI* InRHI);

	/**
	* Compute queue will wait for the fence to be written before continuing.
	*/
	virtual void RHIWaitComputeFence(FComputeFenceRHIParamRef InFence) override
	{
		RHIContext->RHIWaitComputeFence(InFence);
	}

	/**
	*Sets the current compute shader.  Mostly for compliance with platforms
	*that require shader setting before resource binding.
	*/
	virtual void RHISetComputeShader(FComputeShaderRHIParamRef Shader) override
	{
		State.bComputeShaderSet = true;
		State.bGfxPSOSet = false;
		RHIContext->RHISetComputeShader(Shader);
	}

	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) override
	{
		RHIContext->RHIDispatchComputeShader(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
	}

	virtual void RHIDispatchIndirectComputeShader(FVertexBufferRHIParamRef ArgumentBuffer, uint32 ArgumentOffset) override
	{
		RHIContext->RHIDispatchIndirectComputeShader(ArgumentBuffer, ArgumentOffset);
	}

	virtual void RHISetAsyncComputeBudget(EAsyncComputeBudget Budget) override
	{
		RHIContext->RHISetAsyncComputeBudget(Budget);
	}

	virtual void RHIAutomaticCacheFlushAfterComputeShader(bool bEnable) override
	{
		RHIContext->RHIAutomaticCacheFlushAfterComputeShader(bEnable);
	}

	virtual void RHIFlushComputeShaderCache() override
	{
		RHIContext->RHIFlushComputeShaderCache();
	}

	// Useful when used with geometry shader (emit polygons to different viewports), otherwise SetViewPort() is simpler
	// @param Count >0
	// @param Data must not be 0
	virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) override
	{
		RHIContext->RHISetMultipleViewports(Count, Data);
	}

	/** Clears a UAV to the multi-component value provided. */
	virtual void RHIClearTinyUAV(FUnorderedAccessViewRHIParamRef UnorderedAccessViewRHI, const uint32* Values) override
	{
		RHIContext->RHIClearTinyUAV(UnorderedAccessViewRHI, Values);
	}

	/**
	* Resolves from one texture to another.
	* @param SourceTexture - texture to resolve from, 0 is silenty ignored
	* @param DestTexture - texture to resolve to, 0 is silenty ignored
	* @param ResolveParams - optional resolve params
	*/
	virtual void RHICopyToResolveTarget(FTextureRHIParamRef SourceTexture, FTextureRHIParamRef DestTexture, const FResolveParams& ResolveParams) override
	{
		RHIContext->RHICopyToResolveTarget(SourceTexture, DestTexture, ResolveParams);
	}

	/**
	* Explicitly transition a texture resource from readable -> writable by the GPU or vice versa.
	* We know rendertargets are only used as rendered targets on the Gfx pipeline, so these transitions are assumed to be implemented such
	* Gfx->Gfx and Gfx->Compute pipeline transitions are both handled by this call by the RHI implementation.  Hence, no pipeline parameter on this call.
	*
	* @param TransitionType - direction of the transition
	* @param InTextures - array of texture objects to transition
	* @param NumTextures - number of textures to transition
	*/
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, FTextureRHIParamRef* InTextures, int32 NumTextures) override
	{
		RHIContext->RHITransitionResources(TransitionType, InTextures, NumTextures);
	}

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
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FUnorderedAccessViewRHIParamRef* InUAVs, int32 NumUAVs, FComputeFenceRHIParamRef WriteComputeFence) override
	{
		RHIContext->RHITransitionResources(TransitionType, TransitionPipeline, InUAVs, NumUAVs, WriteComputeFence);
	}

	virtual void RHIBeginRenderQuery(FRenderQueryRHIParamRef RenderQuery) override
	{
		RHIContext->RHIBeginRenderQuery(RenderQuery);
	}

	virtual void RHIEndRenderQuery(FRenderQueryRHIParamRef RenderQuery) override
	{
		RHIContext->RHIEndRenderQuery(RenderQuery);
	}

	virtual void RHISubmitCommandsHint() override
	{
		ensureMsgf(!State.bInsideBeginRenderPass, TEXT("Submitting inside a RenderPass is not efficient!"));
		RHIContext->RHISubmitCommandsHint();
	}

	// Used for OpenGL to check and see if any occlusion queries can be read back on the RHI thread. If they aren't ready when we need them, then we end up stalling.
	virtual void RHIPollOcclusionQueries() override
	{
		RHIContext->RHIPollOcclusionQueries();
	}

	// Not all RHIs need this (Mobile specific)
	virtual void RHIDiscardRenderTargets(bool bDepth, bool bStencil, uint32 ColorBitMask) override
	{
		RHIContext->RHIDiscardRenderTargets(bDepth, bStencil, ColorBitMask);
	}

	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIBeginDrawingViewport(FViewportRHIParamRef Viewport, FTextureRHIParamRef RenderTargetRHI) override
	{
		RHIContext->RHIBeginDrawingViewport(Viewport, RenderTargetRHI);
	}

	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIEndDrawingViewport(FViewportRHIParamRef Viewport, bool bPresent, bool bLockToVsync) override
	{
		RHIContext->RHIEndDrawingViewport(Viewport, bPresent, bLockToVsync);
	}

	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIBeginFrame() override
	{
		State.Reset();
		RHIContext->RHIBeginFrame();
	}

	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIEndFrame() override
	{
		RHIContext->RHIEndFrame();
	}

	/**
	* Signals the beginning of scene rendering. The RHI makes certain caching assumptions between
	* calls to BeginScene/EndScene. Currently the only restriction is that you can't update texture
	* references.
	*/
	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIBeginScene() override
	{
		RHIContext->RHIBeginScene();
	}

	/**
	* Signals the end of scene rendering. See RHIBeginScene.
	*/
	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIEndScene() override
	{
		RHIContext->RHIEndScene();
	}

	/**
	* Signals the beginning and ending of rendering to a resource to be used in the next frame on a multiGPU system
	*/
	virtual void RHIBeginUpdateMultiFrameResource(FTextureRHIParamRef Texture) override
	{
		RHIContext->RHIBeginUpdateMultiFrameResource(Texture);
	}

	virtual void RHIEndUpdateMultiFrameResource(FTextureRHIParamRef Texture) override
	{
		RHIContext->RHIEndUpdateMultiFrameResource(Texture);
	}

	virtual void RHIBeginUpdateMultiFrameResource(FUnorderedAccessViewRHIParamRef UAV) override
	{
		RHIContext->RHIBeginUpdateMultiFrameResource(UAV);
	}

	virtual void RHIEndUpdateMultiFrameResource(FUnorderedAccessViewRHIParamRef UAV) override
	{
		RHIContext->RHIEndUpdateMultiFrameResource(UAV);
	}

	virtual void RHISetStreamSource(uint32 StreamIndex, FVertexBufferRHIParamRef VertexBuffer, uint32 Offset) override
	{
		//#todo-rco: Decide if this is needed or not...
		//checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set-up the vertex streams!"));

		checkf(State.bInsideBeginRenderPass, TEXT("A RenderPass has to be set to set-up the vertex streams!"));
		RHIContext->RHISetStreamSource(StreamIndex, VertexBuffer, Offset);
	}

	// @param MinX including like Win32 RECT
	// @param MinY including like Win32 RECT
	// @param MaxX excluding like Win32 RECT
	// @param MaxY excluding like Win32 RECT
	virtual void RHISetViewport(uint32 MinX, uint32 MinY, float MinZ, uint32 MaxX, uint32 MaxY, float MaxZ) override
	{
		RHIContext->RHISetViewport(MinX, MinY, MinZ, MaxX, MaxY, MaxZ);
	}

	virtual void RHISetStereoViewport(uint32 LeftMinX, uint32 RightMinX, uint32 LeftMinY, uint32 RightMinY, float MinZ, uint32 LeftMaxX, uint32 RightMaxX, uint32 LeftMaxY, uint32 RightMaxY, float MaxZ) override
	{
		RHIContext->RHISetStereoViewport(LeftMinX, RightMinX, LeftMinY, RightMinY, MinZ, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, MaxZ);
	}

	// @param MinX including like Win32 RECT
	// @param MinY including like Win32 RECT
	// @param MaxX excluding like Win32 RECT
	// @param MaxY excluding like Win32 RECT
	virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) override
	{
		RHIContext->RHISetScissorRect(bEnable, MinX, MinY, MaxX, MaxY);
	}

	virtual void RHISetGraphicsPipelineState(FGraphicsPipelineStateRHIParamRef GraphicsState) override
	{
		checkf(State.bInsideBeginRenderPass, TEXT("Graphics PSOs can only be set inside a RenderPass!"));
		State.bGfxPSOSet = true;
		State.bComputeShaderSet = false;
		RHIContext->RHISetGraphicsPipelineState(GraphicsState);
	}

	/** Set the shader resource view of a surface.  This is used for binding TextureMS parameter types that need a multi sampled view. */
	virtual void RHISetShaderTexture(FVertexShaderRHIParamRef Shader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderTexture(Shader, TextureIndex, NewTexture);
	}

	/** Set the shader resource view of a surface.  This is used for binding TextureMS parameter types that need a multi sampled view. */
	virtual void RHISetShaderTexture(FHullShaderRHIParamRef Shader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderTexture(Shader, TextureIndex, NewTexture);
	}

	/** Set the shader resource view of a surface.  This is used for binding TextureMS parameter types that need a multi sampled view. */
	virtual void RHISetShaderTexture(FDomainShaderRHIParamRef Shader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderTexture(Shader, TextureIndex, NewTexture);
	}

	/** Set the shader resource view of a surface.  This is used for binding TextureMS parameter types that need a multi sampled view. */
	virtual void RHISetShaderTexture(FGeometryShaderRHIParamRef Shader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderTexture(Shader, TextureIndex, NewTexture);
	}

	/** Set the shader resource view of a surface.  This is used for binding TextureMS parameter types that need a multi sampled view. */
	virtual void RHISetShaderTexture(FPixelShaderRHIParamRef Shader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderTexture(Shader, TextureIndex, NewTexture);
	}

	/** Set the shader resource view of a surface.  This is used for binding TextureMS parameter types that need a multi sampled view. */
	virtual void RHISetShaderTexture(FComputeShaderRHIParamRef Shader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) override
	{
		checkf(State.bComputeShaderSet, TEXT("A Compute shader has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderTexture(Shader, TextureIndex, NewTexture);
	}

	/**
	* Sets sampler state.
	* @param Shader	The geometry shader to set the sampler for.
	* @param SamplerIndex		The index of the sampler.
	* @param NewState			The new sampler state.
	*/
	virtual void RHISetShaderSampler(FComputeShaderRHIParamRef Shader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) override
	{
		checkf(State.bComputeShaderSet, TEXT("A Compute shader has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderSampler(Shader, SamplerIndex, NewState);
	}

	/**
	* Sets sampler state.
	* @param Shader	The geometry shader to set the sampler for.
	* @param SamplerIndex		The index of the sampler.
	* @param NewState			The new sampler state.
	*/
	virtual void RHISetShaderSampler(FVertexShaderRHIParamRef Shader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderSampler(Shader, SamplerIndex, NewState);
	}


	/**
	* Sets sampler state.
	* @param Shader	The geometry shader to set the sampler for.
	* @param SamplerIndex		The index of the sampler.
	* @param NewState			The new sampler state.
	*/
	virtual void RHISetShaderSampler(FGeometryShaderRHIParamRef Shader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderSampler(Shader, SamplerIndex, NewState);
	}

	/**
	* Sets sampler state.
	* @param Shader	The geometry shader to set the sampler for.
	* @param SamplerIndex		The index of the sampler.
	* @param NewState			The new sampler state.
	*/
	virtual void RHISetShaderSampler(FDomainShaderRHIParamRef Shader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderSampler(Shader, SamplerIndex, NewState);
	}

	/**
	* Sets sampler state.
	* @param Shader	The geometry shader to set the sampler for.
	* @param SamplerIndex		The index of the sampler.
	* @param NewState			The new sampler state.
	*/
	virtual void RHISetShaderSampler(FHullShaderRHIParamRef Shader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderSampler(Shader, SamplerIndex, NewState);
	}

	/**
	* Sets sampler state.
	* @param Shader	The geometry shader to set the sampler for.
	* @param SamplerIndex		The index of the sampler.
	* @param NewState			The new sampler state.
	*/
	virtual void RHISetShaderSampler(FPixelShaderRHIParamRef Shader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderSampler(Shader, SamplerIndex, NewState);
	}

	/**
	* Sets a compute shader UAV parameter.
	* @param Shader	The compute shader to set the UAV for.
	* @param UAVIndex		The index of the UAVIndex.
	* @param UAV			The new UAV.
	*/
	virtual void RHISetUAVParameter(FComputeShaderRHIParamRef Shader, uint32 UAVIndex, FUnorderedAccessViewRHIParamRef UAV) override
	{
		checkf(State.bComputeShaderSet, TEXT("A Compute shader has to be set to set resources into a shader!"));
		RHIContext->RHISetUAVParameter(Shader, UAVIndex, UAV);
	}

	/**
	* Sets a compute shader counted UAV parameter and initial count
	* @param Shader	The compute shader to set the UAV for.
	* @param UAVIndex		The index of the UAVIndex.
	* @param UAV			The new UAV.
	* @param InitialCount	The initial number of items in the UAV.
	*/
	virtual void RHISetUAVParameter(FComputeShaderRHIParamRef Shader, uint32 UAVIndex, FUnorderedAccessViewRHIParamRef UAV, uint32 InitialCount) override
	{
		checkf(State.bComputeShaderSet, TEXT("A Compute shader has to be set to set resources into a shader!"));
		RHIContext->RHISetUAVParameter(Shader, UAVIndex, UAV, InitialCount);
	}

	virtual void RHISetShaderResourceViewParameter(FPixelShaderRHIParamRef Shader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderResourceViewParameter(Shader, SamplerIndex, SRV);
	}

	virtual void RHISetShaderResourceViewParameter(FVertexShaderRHIParamRef Shader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderResourceViewParameter(Shader, SamplerIndex, SRV);
	}

	virtual void RHISetShaderResourceViewParameter(FComputeShaderRHIParamRef Shader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) override
	{
		checkf(State.bComputeShaderSet, TEXT("A Compute shader has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderResourceViewParameter(Shader, SamplerIndex, SRV);
	}

	virtual void RHISetShaderResourceViewParameter(FHullShaderRHIParamRef Shader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderResourceViewParameter(Shader, SamplerIndex, SRV);
	}

	virtual void RHISetShaderResourceViewParameter(FDomainShaderRHIParamRef Shader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderResourceViewParameter(Shader, SamplerIndex, SRV);
	}

	virtual void RHISetShaderResourceViewParameter(FGeometryShaderRHIParamRef Shader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderResourceViewParameter(Shader, SamplerIndex, SRV);
	}

	virtual void RHISetShaderUniformBuffer(FVertexShaderRHIParamRef Shader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderUniformBuffer(Shader, BufferIndex, Buffer);
	}

	virtual void RHISetShaderUniformBuffer(FHullShaderRHIParamRef Shader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderUniformBuffer(Shader, BufferIndex, Buffer);
	}

	virtual void RHISetShaderUniformBuffer(FDomainShaderRHIParamRef Shader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderUniformBuffer(Shader, BufferIndex, Buffer);
	}

	virtual void RHISetShaderUniformBuffer(FGeometryShaderRHIParamRef Shader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderUniformBuffer(Shader, BufferIndex, Buffer);
	}

	virtual void RHISetShaderUniformBuffer(FPixelShaderRHIParamRef Shader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderUniformBuffer(Shader, BufferIndex, Buffer);
	}

	virtual void RHISetShaderUniformBuffer(FComputeShaderRHIParamRef Shader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) override
	{
		checkf(State.bComputeShaderSet, TEXT("A Compute shader has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderUniformBuffer(Shader, BufferIndex, Buffer);
	}

	virtual void RHISetShaderParameter(FVertexShaderRHIParamRef Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderParameter(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
	}

	virtual void RHISetShaderParameter(FPixelShaderRHIParamRef Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderParameter(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
	}

	virtual void RHISetShaderParameter(FHullShaderRHIParamRef Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderParameter(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
	}

	virtual void RHISetShaderParameter(FDomainShaderRHIParamRef Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderParameter(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
	}

	virtual void RHISetShaderParameter(FGeometryShaderRHIParamRef Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderParameter(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
	}

	virtual void RHISetShaderParameter(FComputeShaderRHIParamRef Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) override
	{
		checkf(State.bComputeShaderSet, TEXT("A Compute shader has to be set to set resources into a shader!"));
		RHIContext->RHISetShaderParameter(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
	}

	virtual void RHISetStencilRef(uint32 StencilRef) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to change stencil ref!"));
		RHIContext->RHISetStencilRef(StencilRef);
	}

	virtual void RHISetBlendFactor(const FLinearColor& BlendFactor) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to change blend factor!"));
		RHIContext->RHISetBlendFactor(BlendFactor);
	}

	virtual void RHISetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets, const FRHIDepthRenderTargetView* NewDepthStencilTarget, uint32 NumUAVs, const FUnorderedAccessViewRHIParamRef* UAVs) override
	{
		RHIContext->RHISetRenderTargets(NumSimultaneousRenderTargets, NewRenderTargets, NewDepthStencilTarget, NumUAVs, UAVs);
	}

	virtual void RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo) override
	{
		RHIContext->RHISetRenderTargetsAndClear(RenderTargetsInfo);
	}

	// Bind the clear state of the currently set rendertargets.  This is used by platforms which
	// need the state of the target when finalizing a hardware clear or a resource transition to SRV
	// The explicit bind is needed to support parallel rendering (propagate state between contexts).
	virtual void RHIBindClearMRTValues(bool bClearColor, bool bClearDepth, bool bClearStencil) override
	{
		RHIContext->RHIBindClearMRTValues(bClearColor, bClearDepth, bClearStencil);
	}

	virtual void RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to draw!"));
		RHIContext->RHIDrawPrimitive(BaseVertexIndex, NumPrimitives, NumInstances);
	}

	virtual void RHIDrawPrimitiveIndirect(FVertexBufferRHIParamRef ArgumentBuffer, uint32 ArgumentOffset) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to draw!"));
		RHIContext->RHIDrawPrimitiveIndirect(ArgumentBuffer, ArgumentOffset);
	}

	virtual void RHIDrawIndexedIndirect(FIndexBufferRHIParamRef IndexBufferRHI, FStructuredBufferRHIParamRef ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to draw!"));
		RHIContext->RHIDrawIndexedIndirect(IndexBufferRHI, ArgumentsBufferRHI, DrawArgumentsIndex, NumInstances);
	}

	// @param NumPrimitives need to be >0 
	virtual void RHIDrawIndexedPrimitive(FIndexBufferRHIParamRef IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to draw!"));
		RHIContext->RHIDrawIndexedPrimitive(IndexBuffer, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances);
	}

	virtual void RHIDrawIndexedPrimitiveIndirect(FIndexBufferRHIParamRef IndexBuffer, FVertexBufferRHIParamRef ArgumentBuffer, uint32 ArgumentOffset) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to draw!"));
		RHIContext->RHIDrawIndexedPrimitiveIndirect(IndexBuffer, ArgumentBuffer, ArgumentOffset);
	}

	/**
	* Sets Depth Bounds range with the given min/max depth.
	* @param MinDepth	The minimum depth for depth bounds test
	* @param MaxDepth	The maximum depth for depth bounds test.
	*					The valid values for fMinDepth and fMaxDepth are such that 0 <= fMinDepth <= fMaxDepth <= 1
	*/
	virtual void RHISetDepthBounds(float MinDepth, float MaxDepth) override
	{
		checkf(State.bGfxPSOSet, TEXT("A Graphics PSO has to be set to set resources into a shader!"));
		RHIContext->RHISetDepthBounds(MinDepth, MaxDepth);
	}

	virtual void RHIPushEvent(const TCHAR* Name, FColor Color) override
	{
		RHIContext->RHIPushEvent(Name, Color);
	}

	virtual void RHIPopEvent() override
	{
		RHIContext->RHIPopEvent();
	}

	virtual void RHIUpdateTextureReference(FTextureReferenceRHIParamRef TextureRef, FTextureRHIParamRef NewTexture) override
	{
		RHIContext->RHIUpdateTextureReference(TextureRef, NewTexture);
	}

	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName) override
	{
		checkf(!State.bInsideBeginRenderPass, TEXT("Trying to begin RenderPass '%s', but already inside '%s'!"), *State.RenderPassName, InName);
		checkf(InName, TEXT("RenderPass should have a name!"));
		State.bInsideBeginRenderPass = true;
		State.RenderPassInfo = InInfo;
		State.RenderPassName = InName;
		State.bGfxPSOSet = false;
		RHIContext->RHIBeginRenderPass(InInfo, InName);
	}

	virtual void RHIEndRenderPass() override
	{
		checkf(State.bInsideBeginRenderPass, TEXT("Trying to end a RenderPass but not inside one!"));
		RHIContext->RHIEndRenderPass();
		State.bInsideBeginRenderPass = false;
		State.PreviousRenderPassName = State.RenderPassName;
		State.bGfxPSOSet = false;
	}

	virtual void RHIBeginComputePass(const TCHAR* InName) override
	{
		checkf(InName && *InName, TEXT("ComputePass should have a name!"));
		checkf(!State.bInsideBeginRenderPass, TEXT("Can't begin a compute pass from inside RenderPass '%s'"), *State.RenderPassName);
		checkf(!State.bInsideComputePass, TEXT("Can't begin a compute pass inside from inside ComputePass '%s'"), *State.ComputePassName);
		State.bInsideComputePass = true;
		State.ComputePassName = InName;
		RHIContext->RHIBeginComputePass(InName);
	}

	virtual void RHIEndComputePass() override
	{
		checkf(State.bInsideComputePass, TEXT("Can't end a compute pass without a Begin!"), *State.ComputePassName);
		RHIContext->RHIEndComputePass();
		State.bInsideComputePass = false;
	}

	virtual void RHICopyTexture(FTextureRHIParamRef SourceTexture, FTextureRHIParamRef DestTexture, const FRHICopyTextureInfo& CopyInfo) override
	{
		ensureMsgf(!State.bInsideBeginRenderPass, TEXT("Copying inside a RenderPass is not efficient!"));
		RHIContext->RHICopyTexture(SourceTexture, DestTexture, CopyInfo);
	}

	virtual void RHIBuildAccelerationStructure(FRayTracingGeometryRHIParamRef Geometry) override
	{
		RHIContext->RHIBuildAccelerationStructure(Geometry);
	}

	virtual void RHIUpdateAccelerationStructures(const TArrayView<const FAccelerationStructureUpdateParams> Params) override
	{
		RHIContext->RHIUpdateAccelerationStructures(Params);
	}

	virtual void RHIBuildAccelerationStructures(const TArrayView<const FAccelerationStructureUpdateParams> Params) override
	{
		RHIContext->RHIBuildAccelerationStructures(Params);
	}

	virtual void RHIBuildAccelerationStructure(FRayTracingSceneRHIParamRef Scene) override
	{
		RHIContext->RHIBuildAccelerationStructure(Scene);
	}

	virtual void RHIRayTraceOcclusion(FRayTracingSceneRHIParamRef Scene,
		FShaderResourceViewRHIParamRef Rays,
		FUnorderedAccessViewRHIParamRef Output,
		uint32 NumRays) override
	{
		RHIContext->RHIRayTraceOcclusion(Scene, Rays, Output, NumRays);
	}

	virtual void RHIRayTraceIntersection(FRayTracingSceneRHIParamRef Scene,
		FShaderResourceViewRHIParamRef Rays,
		FUnorderedAccessViewRHIParamRef Output,
		uint32 NumRays) override
	{
		RHIContext->RHIRayTraceIntersection(Scene, Rays, Output, NumRays);
	}

	virtual void RHIRayTraceDispatch(FRayTracingPipelineStateRHIParamRef RayTracingPipelineState, FRHIRayTracingShader* RayGenShader,
		FRayTracingSceneRHIParamRef Scene,
		const FRayTracingShaderBindings& GlobalResourceBindings,
		uint32 Width, uint32 Height) override
	{
		RHIContext->RHIRayTraceDispatch(RayTracingPipelineState, RayGenShader, Scene, GlobalResourceBindings, Width, Height);
	}

	virtual void RHISetRayTracingHitGroup(
		FRayTracingSceneRHIParamRef Scene, uint32 InstanceIndex, uint32 SegmentIndex, uint32 ShaderSlot,
		FRayTracingPipelineStateRHIParamRef Pipeline, uint32 HitGroupIndex,
		uint32 NumUniformBuffers, const FUniformBufferRHIParamRef* UniformBuffers,
		uint32 UserData) override
	{
		RHIContext->RHISetRayTracingHitGroup(Scene, InstanceIndex, SegmentIndex, ShaderSlot, Pipeline, HitGroupIndex, NumUniformBuffers, UniformBuffers, UserData);
	}

	IRHICommandContext* RHIContext;
	FValidationRHI*		RHI;

protected:
	struct FState
	{
		bool bInsideBeginRenderPass;
		FRHIRenderPassInfo RenderPassInfo;
		FString RenderPassName;
		FString PreviousRenderPassName;
		bool bInsideComputePass;
		FString ComputePassName;
		bool bGfxPSOSet;
		bool bComputeShaderSet;

		void Reset();
	};
	FState State;

	friend class FValidationRHI;
};
