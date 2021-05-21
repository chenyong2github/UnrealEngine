// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHIContext.h: Interface for RHI Contexts
=============================================================================*/

#pragma once

#include "Misc/AssertionMacros.h"
#include "Math/Color.h"
#include "Math/IntPoint.h"
#include "Math/IntRect.h"
#include "Math/Box2D.h"
#include "Math/PerspectiveMatrix.h"
#include "Math/TranslationMatrix.h"
#include "Math/ScaleMatrix.h"
#include "Math/Float16Color.h"
#include "Modules/ModuleInterface.h"

class FRHIDepthRenderTargetView;
class FRHIRenderTargetView;
class FRHISetRenderTargetsInfo;
struct FResolveParams;
struct FViewportBounds;
struct FRayTracingGeometryInstance;
struct FRayTracingShaderBindings;
struct FRayTracingGeometrySegment;
struct FAccelerationStructureBuildParams;
struct FRayTracingLocalShaderBindings;
enum class EAsyncComputeBudget;
enum class EResourceTransitionPipeline;

#define VALIDATE_UNIFORM_BUFFER_GLOBAL_BINDINGS (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)

/** A list of global uniform buffer bindings. */
class FUniformBufferStaticBindings
{
public:
	FUniformBufferStaticBindings() = default;

	template <typename... TArgs>
	FUniformBufferStaticBindings(TArgs... Args)
	{
		std::initializer_list<FRHIUniformBuffer*> InitList = { Args... };

		for (FRHIUniformBuffer* Buffer : InitList)
		{
			AddUniformBuffer(Buffer);
		}
	}

	inline void AddUniformBuffer(FRHIUniformBuffer* UniformBuffer)
	{
		checkf(UniformBuffer, TEXT("Attemped to assign a null uniform buffer to the global uniform buffer bindings."));
		const FRHIUniformBufferLayout& Layout = UniformBuffer->GetLayout();
		const FUniformBufferStaticSlot Slot = Layout.StaticSlot;
		checkf(IsUniformBufferStaticSlotValid(Slot), TEXT("Attempted to set a global uniform buffer %s with an invalid slot."), *Layout.GetDebugName());

#if VALIDATE_UNIFORM_BUFFER_GLOBAL_BINDINGS
		ensureMsgf(INDEX_NONE == Slots.Find(Slot), TEXT("Uniform Buffer %s was added twice to the binding array."), *Layout.GetDebugName());
#endif

		Slots.Add(Slot);
		UniformBuffers.Add(UniformBuffer);
	}

	inline void TryAddUniformBuffer(FRHIUniformBuffer* UniformBuffer)
	{
		if (UniformBuffer)
		{
			AddUniformBuffer(UniformBuffer);
		}
	}

	int32 GetUniformBufferCount() const
	{
		return UniformBuffers.Num();
	}

	FRHIUniformBuffer* GetUniformBuffer(int32 Index) const
	{
		return UniformBuffers[Index];
	}

	FUniformBufferStaticSlot GetSlot(int32 Index) const
	{
		return Slots[Index];
	}

private:
	static const uint32 InlineUniformBufferCount = 8;
	TArray<FUniformBufferStaticSlot, TInlineAllocator<InlineUniformBufferCount>> Slots;
	TArray<FRHIUniformBuffer*, TInlineAllocator<InlineUniformBufferCount>> UniformBuffers;
};

/** Parameters for RHITransferTextures, used to copy memory between GPUs */
struct FTransferTextureParams
{
	FTransferTextureParams() {}

	FTransferTextureParams(FRHITexture2D* InTexture, const FIntRect& InRect, uint32 InSrcGPUIndex, uint32 InDestGPUIndex, bool InPullData, bool InLockStepGPUs)
		: Texture(InTexture), Min(InRect.Min.X, InRect.Min.Y, 0), Max(InRect.Max.X, InRect.Max.Y, 1), SrcGPUIndex(InSrcGPUIndex), DestGPUIndex(InDestGPUIndex), bPullData(InPullData), bLockStepGPUs(InLockStepGPUs)
	{
	}

	// The texture which must be must be allocated on both GPUs 
	FTexture2DRHIRef Texture;
	// The min rect of the texture region to copy
	FIntVector Min;
	// The max rect of the texture region to copy
	FIntVector Max;
	// The GPU index where the data will be read from.
	uint32 SrcGPUIndex;
	// The GPU index where the data will be written to.
	uint32 DestGPUIndex;
	// Whether the data is read by the dest GPU, or written by the src GPU (not allowed if the texture is a backbuffer)
	bool bPullData = true;
	// Whether the GPUs must handshake before and after the transfer. Required if the texture rect is being written to in several render passes.
	// Otherwise, minimal synchronization will be used.
	bool bLockStepGPUs = true;
};

/** Context that is capable of doing Compute work.  Can be async or compute on the gfx pipe. */
class IRHIComputeContext
{
public:
	virtual ~IRHIComputeContext()
	{
	}

	/**
	*Sets the current compute shader.
	*/
	virtual void RHISetComputeShader(FRHIComputeShader* ComputeShader) = 0;

	virtual void RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState)
	{
		if (ComputePipelineState)
		{
			FRHIComputePipelineStateFallback* FallbackState = static_cast<FRHIComputePipelineStateFallback*>(ComputePipelineState);
			RHISetComputeShader(FallbackState->GetComputeShader());
		}
	}

	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) = 0;

	virtual void RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) = 0;

	virtual void RHISetAsyncComputeBudget(EAsyncComputeBudget Budget) {}

	virtual void RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions) = 0;

	virtual void RHIEndTransitions(TArrayView<const FRHITransition*> Transitions) = 0;

	/**
	* Clears a UAV to the multi-channel floating point value provided. Should only be called on UAVs with a floating point format, or on structured buffers.
	* Structured buffers are treated as a regular R32_UINT buffer during the clear operation, and the Values.X component is copied directly into the buffer without any format conversion. (Y,Z,W) of Values is ignored.
	* Typed floating point buffers undergo standard format conversion during the write operation. The conversion is determined by the format of the UAV.
	*
	* @param UnorderedAccessViewRHI		The UAV to clear.
	* @param Values						The values to clear the UAV to, one component per channel (XYZW = RGBA). Channels not supported by the UAV are ignored.
	*
	*/
	virtual void RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values) = 0;

	/**
	* Clears a UAV to the multi-component unsigned integer value provided. Should only be called on UAVs with an integer format, or on structured buffers.
	* Structured buffers are treated as a regular R32_UINT buffer during the clear operation, and the Values.X component is copied directly into the buffer without any format conversion. (Y,Z,W) of Values is ignored.
	* Typed integer buffers undergo standard format conversion during the write operation. The conversion is determined by the format of the UAV.
	*
	* @param UnorderedAccessViewRHI		The UAV to clear.
	* @param Values						The values to clear the UAV to, one component per channel (XYZW = RGBA). Channels not supported by the UAV are ignored.
	*
	*/
	virtual void RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values) = 0;

	virtual void RHIBeginUAVOverlap() {}
	virtual void RHIEndUAVOverlap() {}

	virtual void RHIBeginUAVOverlap(TArrayView<FRHIUnorderedAccessView* const> UAVs) {}
	virtual void RHIEndUAVOverlap(TArrayView<FRHIUnorderedAccessView* const> UAVs) {}

	/** Set the shader resource view of a surface.  This is used for binding TextureMS parameter types that need a multi sampled view. */
	virtual void RHISetShaderTexture(FRHIComputeShader* PixelShader, uint32 TextureIndex, FRHITexture* NewTexture) = 0;

	/**
	* Sets sampler state.
	* @param ComputeShader		The compute shader to set the sampler for.
	* @param SamplerIndex		The index of the sampler.
	* @param NewState			The new sampler state.
	*/
	virtual void RHISetShaderSampler(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHISamplerState* NewState) = 0;

	/**
	* Sets a compute shader UAV parameter.
	* @param ComputeShader	The compute shader to set the UAV for.
	* @param UAVIndex		The index of the UAVIndex.
	* @param UAV			The new UAV.
	*/
	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV) = 0;

	/**
	* Sets a compute shader counted UAV parameter and initial count
	* @param ComputeShader	The compute shader to set the UAV for.
	* @param UAVIndex		The index of the UAVIndex.
	* @param UAV			The new UAV.
	* @param InitialCount	The initial number of items in the UAV.
	*/
	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount) = 0;

	virtual void RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) = 0;

	virtual void RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) = 0;

	virtual void RHISetShaderParameter(FRHIComputeShader* ComputeShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) = 0;

	virtual void RHISetGlobalUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers)
	{
		checkNoEntry();
	}

	virtual void RHIPushEvent(const TCHAR* Name, FColor Color) = 0;

	virtual void RHIPopEvent() = 0;

	/**
	* Submit the current command buffer to the GPU if possible.
	*/
	virtual void RHISubmitCommandsHint() = 0;

	/**
	 * Some RHI implementations (OpenGL) cache render state internally
	 * Signal to RHI that cached state is no longer valid
	 */
	virtual void RHIInvalidateCachedState() {}

	/**
	 * Performs a copy of the data in 'SourceBuffer' to 'DestinationStagingBuffer.' This will occur inline on the GPU timeline. This is a mechanism to perform nonblocking readback of a buffer at a point in time.
	 * @param SourceBuffer The source vertex buffer that will be inlined copied.
	 * @param DestinationStagingBuffer The the host-visible destination buffer
	 * @param Offset The start of the data in 'SourceBuffer'
	 * @param NumBytes The number of bytes to copy out of 'SourceBuffer'
	 */
	virtual void RHICopyToStagingBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIStagingBuffer* DestinationStagingBufferRHI, uint32 InOffset, uint32 InNumBytes)
	{
		check(false);
	}

	/**
	 * Write the fence in the GPU timeline. The fence can then be tested on the CPU to know if the previous GPU commands are completed.
	 * @param Fence 
	 */
	virtual void RHIWriteGPUFence(FRHIGPUFence* FenceRHI)
	{
		check(false);
	}

	virtual void RHISetGPUMask(FRHIGPUMask GPUMask)
	{
		ensure(GPUMask == FRHIGPUMask::GPU0());
	}

#if WITH_MGPU
	virtual void RHIWaitForTemporalEffect(const FName& InEffectName)
	{
		/* empty default implementation */
	}

	virtual void RHIBroadcastTemporalEffect(const FName& InEffectName, const TArrayView<FRHITexture*> InTextures)
	{
		/* empty default implementation */
	}

	virtual void RHIBroadcastTemporalEffect(const FName& InEffectName, const TArrayView<FRHIVertexBuffer*> InBuffers)
	{
		/* empty default implementation */
	}
#endif // WITH_MGPU

	/**
	 * Synchronizes the content of a texture resource between two GPUs using a copy operation.
	 * @param Params - the parameters for each texture region copied between GPUse.
	 */
	virtual void RHITransferTextures(const TArrayView<const FTransferTextureParams> Params)
	{
		/* empty default implementation */
	}


	virtual void RHIBuildAccelerationStructure(FRHIRayTracingGeometry* Geometry)
	{
		checkNoEntry();
	}

	virtual void RHIBuildAccelerationStructures(const TArrayView<const FAccelerationStructureBuildParams> Params)
	{
		checkNoEntry();
	}

	virtual void RHIBuildAccelerationStructure(FRHIRayTracingScene* Scene)
	{
		checkNoEntry();
	}
#if ENABLE_RHI_VALIDATION

	RHIValidation::FTracker* Tracker = nullptr;
	IRHIComputeContext* WrappingContext = nullptr;

	// Always returns the platform RHI context, even when the validation RHI is active.
	virtual IRHIComputeContext& GetLowestLevelContext() { return *this; }

	// Returns the validation RHI context if the validation RHI is active, otherwise returns the platform RHI context.
	virtual IRHIComputeContext& GetHighestLevelContext()
	{
		return WrappingContext ? *WrappingContext : *this;
	}

#else

	// Fast implementations when the RHI validation layer is disabled.
	inline IRHIComputeContext& GetLowestLevelContext() { return *this; }
	inline IRHIComputeContext& GetHighestLevelContext() { return *this; }

#endif

	virtual void* RHIGetNativeCommandBuffer() { return nullptr; }
	virtual void RHIPostExternalCommandsReset() { }
};

enum class EAccelerationStructureBuildMode
{
	// Perform a full acceleration structure build.
	Build,

	// Update existing acceleration structure, based on new vertex positions.
	// Index buffer must not change between initial build and update operations.
	// Only valid when geometry was created with FRayTracingGeometryInitializer::bAllowUpdate = true.
	Update,
};

struct FAccelerationStructureBuildParams
{
	FRayTracingGeometryRHIRef Geometry;
	EAccelerationStructureBuildMode BuildMode = EAccelerationStructureBuildMode::Build;

	// Optional array of geometry segments that can be used to change per-segment vertex buffers.
	// Only fields related to vertex buffer are used. If empty, then geometry vertex buffers are not changed.
	TArrayView<const FRayTracingGeometrySegment> Segments;
};

struct FCopyBufferRegionParams
{
	FRHIVertexBuffer* DestBuffer;
	uint64 DstOffset;
	FRHIVertexBuffer* SourceBuffer;
	uint64 SrcOffset;
	uint64 NumBytes;
};

/** The interface RHI command context. Sometimes the RHI handles these. On platforms that can processes command lists in parallel, it is a separate object. */
class IRHICommandContext : public IRHIComputeContext
{
public:
	virtual ~IRHICommandContext()
	{
	}

	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) = 0;

	virtual void RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) = 0;

	// Useful when used with geometry shader (emit polygons to different viewports), otherwise SetViewPort() is simpler
	// @param Count >0
	// @param Data must not be 0
	virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) = 0;

	/**
	* Resolves from one texture to another.
	* @param SourceTexture - texture to resolve from, 0 is silently ignored
	* @param DestTexture - texture to resolve to, 0 is silently ignored
	* @param ResolveParams - optional resolve params
	* @param Fence - optional fence, will be set once copy is completed by GPU
	*/
	virtual void RHICopyToResolveTarget(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FResolveParams& ResolveParams) = 0;

	/**
	* Rebuilds the depth target HTILE meta data (on supported platforms).
	* @param DepthTexture - the depth surface to resummarize.
	*/
	virtual void RHIResummarizeHTile(FRHITexture2D* DepthTexture)
	{
		/* empty default implementation */
	}

	virtual void RHIBeginRenderQuery(FRHIRenderQuery* RenderQuery) = 0;

	virtual void RHIEndRenderQuery(FRHIRenderQuery* RenderQuery) = 0;

	virtual void RHICalibrateTimers()
	{
		/* empty default implementation */
	}

	virtual void RHICalibrateTimers(FRHITimestampCalibrationQuery* CalibrationQuery)
	{
		/* empty default implementation */
	}

	// Used for OpenGL to check and see if any occlusion queries can be read back on the RHI thread. If they aren't ready when we need them, then we end up stalling.
	virtual void RHIPollOcclusionQueries()
	{
		/* empty default implementation */
	}

	// Not all RHIs need this (Mobile specific)
	virtual void RHIDiscardRenderTargets(bool Depth, bool Stencil, uint32 ColorBitMask) {}

	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIBeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI) = 0;

	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIEndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync) = 0;

	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIBeginFrame() = 0;

	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIEndFrame() = 0;

	/**
	* Signals the beginning of scene rendering. The RHI makes certain caching assumptions between
	* calls to BeginScene/EndScene. Currently the only restriction is that you can't update texture
	* references.
	*/
	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIBeginScene() = 0;

	/**
	* Signals the end of scene rendering. See RHIBeginScene.
	*/
	// This method is queued with an RHIThread, otherwise it will flush after it is queued; without an RHI thread there is no benefit to queuing this frame advance commands
	virtual void RHIEndScene() = 0;

	/**
	* Signals the beginning and ending of rendering to a resource to be used in the next frame on a multiGPU system
	*/
	virtual void RHIBeginUpdateMultiFrameResource(FRHITexture* Texture)
	{
		/* empty default implementation */
	}

	virtual void RHIEndUpdateMultiFrameResource(FRHITexture* Texture)
	{
		/* empty default implementation */
	}

	virtual void RHIBeginUpdateMultiFrameResource(FRHIUnorderedAccessView* UAV)
	{
		/* empty default implementation */
	}

	virtual void RHIEndUpdateMultiFrameResource(FRHIUnorderedAccessView* UAV)
	{
		/* empty default implementation */
	}

	virtual void RHISetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBuffer, uint32 Offset) = 0;

	// @param MinX including like Win32 RECT
	// @param MinY including like Win32 RECT
	// @param MaxX excluding like Win32 RECT
	// @param MaxY excluding like Win32 RECT
	virtual void RHISetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ) = 0;

	virtual void RHISetStereoViewport(float LeftMinX, float RightMinX, float LeftMinY, float RightMinY, float MinZ, float LeftMaxX, float RightMaxX, float LeftMaxY, float RightMaxY, float MaxZ)
	{
		/* empty default implementation */
	}

	// @param MinX including like Win32 RECT
	// @param MinY including like Win32 RECT
	// @param MaxX excluding like Win32 RECT
	// @param MaxY excluding like Win32 RECT
	virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) = 0;

	virtual void RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState, bool bApplyAdditionalState) = 0;

	/** Set the shader resource view of a surface. */
	virtual void RHISetShaderTexture(FRHIGraphicsShader* Shader, uint32 TextureIndex, FRHITexture* NewTexture) = 0;

	/** Set the shader resource view of a surface.  This is used for binding TextureMS parameter types that need a multi sampled view. */
	virtual void RHISetShaderTexture(FRHIComputeShader* PixelShader, uint32 TextureIndex, FRHITexture* NewTexture) = 0;

	/**
	* Sets sampler state.
	* @param ComputeShader		The compute shader to set the sampler for.
	* @param SamplerIndex		The index of the sampler.
	* @param NewState			The new sampler state.
	*/
	virtual void RHISetShaderSampler(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHISamplerState* NewState) = 0;

	/**
	* Sets sampler state.
	* @param Shader				The shader to set the sampler for.
	* @param SamplerIndex		The index of the sampler.
	* @param NewState			The new sampler state.
	*/
	virtual void RHISetShaderSampler(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHISamplerState* NewState) = 0;

	/**
	* Sets a pixel shader UAV parameter.
	* @param PixelShader		The pixel shader to set the UAV for.
	* @param UAVIndex		The index of the UAVIndex.
	* @param UAV			The new UAV.
	*/
	virtual void RHISetUAVParameter(FRHIPixelShader* PixelShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV) = 0;


	/**
	* Sets a compute shader UAV parameter.
	* @param ComputeShader	The compute shader to set the UAV for.
	* @param UAVIndex		The index of the UAVIndex.
	* @param UAV			The new UAV.
	*/
	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV) = 0;

	/**
	* Sets a compute shader counted UAV parameter and initial count
	* @param ComputeShader	The compute shader to set the UAV for.
	* @param UAVIndex		The index of the UAVIndex.
	* @param UAV			The new UAV.
	* @param InitialCount	The initial number of items in the UAV.
	*/
	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount) = 0;

	virtual void RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) = 0;

	virtual void RHISetShaderResourceViewParameter(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) = 0;

	virtual void RHISetShaderUniformBuffer(FRHIGraphicsShader* Shader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) = 0;

	virtual void RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) = 0;

	virtual void RHISetShaderParameter(FRHIGraphicsShader* Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) = 0;

	virtual void RHISetShaderParameter(FRHIComputeShader* ComputeShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) = 0;

	virtual void RHISetStencilRef(uint32 StencilRef) {}

	virtual void RHISetBlendFactor(const FLinearColor& BlendFactor) {}

	virtual void RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) = 0;

	virtual void RHIDrawPrimitiveIndirect(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) = 0;

	virtual void RHIDrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances) = 0;

	// @param NumPrimitives need to be >0 
	virtual void RHIDrawIndexedPrimitive(FRHIIndexBuffer* IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances) = 0;

	virtual void RHIDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBuffer, FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) = 0;

	/**
	* Sets Depth Bounds range with the given min/max depth.
	* @param MinDepth	The minimum depth for depth bounds test
	* @param MaxDepth	The maximum depth for depth bounds test.
	*					The valid values for fMinDepth and fMaxDepth are such that 0 <= fMinDepth <= fMaxDepth <= 1
	*/
	virtual void RHISetDepthBounds(float MinDepth, float MaxDepth) = 0;

	virtual void RHISetShadingRate(EVRSShadingRate ShadingRate, EVRSRateCombiner Combiner)
	{
		/* empty default implementation */
	} 
	
	UE_DEPRECATED(4.27, "RHISetShadingRateImage is deprecated. Bind the shading rate image as part of the RHISetRenderTargetsInfo struct.")
	virtual void RHISetShadingRateImage(FRHITexture* RateImageTexture, EVRSRateCombiner Combiner)
	{
		/* empty default implementation */
	}

	virtual void RHIUpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture) = 0;

	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName) = 0;

	virtual void RHIEndRenderPass() = 0;

	/** 
	* Enable/begin recording for late-latching (for ulta-late uniform buffer patching for VR devices)
	* @param RHICmdList		The command list
	* @param FrameNumber	Frame number this is applied to.
	*/
	virtual void RHIBeginLateLatching(int32 FrameNumber)
	{
		/* empty default implementation */
	}

	/**
	* End recording for late-latching (for ulta-late uniform buffer patching for VR devices)
	*/
	virtual void RHIEndLateLatching()
	{
		/* empty default implementation */
	}

	virtual void RHINextSubpass()
	{
	}

	virtual void RHICopyTexture(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FRHICopyTextureInfo& CopyInfo)
	{
		const bool bIsCube = SourceTexture->GetTextureCube() != nullptr;
		const bool bAllCubeFaces = bIsCube && (CopyInfo.NumSlices % 6) == 0;
		const int32 NumArraySlices = bAllCubeFaces ? CopyInfo.NumSlices / 6 : CopyInfo.NumSlices;
		const int32 NumFaces = bAllCubeFaces ? 6 : 1;
		for (int32 ArrayIndex = 0; ArrayIndex < NumArraySlices; ++ArrayIndex)
		{
			int32 SourceArrayIndex = CopyInfo.SourceSliceIndex + ArrayIndex;
			int32 DestArrayIndex = CopyInfo.DestSliceIndex + ArrayIndex;
			for (int32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
			{
				FResolveParams ResolveParams(FResolveRect(0, 0, 0, 0),
					bIsCube ? (ECubeFace)FaceIndex : CubeFace_PosX,
					CopyInfo.SourceMipIndex,
					SourceArrayIndex,
					DestArrayIndex,
					FResolveRect(0, 0, 0, 0)
				);
				if (CopyInfo.Size != FIntVector::ZeroValue)
				{
					ResolveParams.Rect = FResolveRect(CopyInfo.SourcePosition.X, CopyInfo.SourcePosition.Y, CopyInfo.SourcePosition.X + CopyInfo.Size.X, CopyInfo.SourcePosition.Y + CopyInfo.Size.Y);
					ResolveParams.DestRect = FResolveRect(CopyInfo.DestPosition.X, CopyInfo.DestPosition.Y, CopyInfo.DestPosition.X + CopyInfo.Size.X, CopyInfo.DestPosition.Y + CopyInfo.Size.Y);
				}
				RHICopyToResolveTarget(SourceTexture, DestTexture, ResolveParams);
			}
		}
	}

	virtual void RHICopyBufferRegion(FRHIVertexBuffer* DestBuffer, uint64 DstOffset, FRHIVertexBuffer* SourceBuffer, uint64 SrcOffset, uint64 NumBytes)
	{
		checkNoEntry();
	}

#if RHI_RAYTRACING
	virtual void RHICopyBufferRegions(const TArrayView<const FCopyBufferRegionParams> Params)
	{
		checkNoEntry();
	}
#endif

	virtual void RHIClearRayTracingBindings(FRHIRayTracingScene* Scene)
	{
		checkNoEntry();
	}

	virtual void RHIBuildAccelerationStructures(const TArrayView<const FAccelerationStructureBuildParams> Params)
	{
		checkNoEntry();
	}

	virtual void RHIBuildAccelerationStructure(FRHIRayTracingGeometry* Geometry) final override
	{
		FAccelerationStructureBuildParams Params;
		Params.Geometry = Geometry;
		Params.BuildMode = EAccelerationStructureBuildMode::Build;

		RHIBuildAccelerationStructures(MakeArrayView(&Params, 1));
	}

	virtual void RHIBuildAccelerationStructure(FRHIRayTracingScene* Scene)
	{
		checkNoEntry();
	}

	virtual void RHIRayTraceOcclusion(FRHIRayTracingScene* Scene,
		FRHIShaderResourceView* Rays,
		FRHIUnorderedAccessView* Output,
		uint32 NumRays)
	{
		checkNoEntry();
	}

	virtual void RHIRayTraceIntersection(FRHIRayTracingScene* Scene,
		FRHIShaderResourceView* Rays,
		FRHIUnorderedAccessView* Output,
		uint32 NumRays)
	{
		checkNoEntry();
	}

	virtual void RHIRayTraceDispatch(FRHIRayTracingPipelineState* RayTracingPipelineState, FRHIRayTracingShader* RayGenShader,
		FRHIRayTracingScene* Scene,
		const FRayTracingShaderBindings& GlobalResourceBindings,
		uint32 Width, uint32 Height)
	{
		checkNoEntry();
	}

	virtual void RHISetRayTracingHitGroups(FRHIRayTracingScene* Scene, FRHIRayTracingPipelineState* Pipeline, uint32 NumBindings, const FRayTracingLocalShaderBindings* Bindings)
	{
		checkNoEntry();
	}

	virtual void RHISetRayTracingHitGroup(
		FRHIRayTracingScene* Scene, uint32 InstanceIndex, uint32 SegmentIndex, uint32 ShaderSlot,
		FRHIRayTracingPipelineState* Pipeline, uint32 HitGroupIndex,
		uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
		uint32 LooseParameterDataSize, const void* LooseParameterData,
		uint32 UserData)
	{
		checkNoEntry();
	}

	virtual void RHISetRayTracingCallableShader(
		FRHIRayTracingScene* Scene, uint32 ShaderSlotInScene,
		FRHIRayTracingPipelineState* Pipeline, uint32 ShaderIndexInPipeline,
		uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
		uint32 UserData)
	{
		checkNoEntry();
	}

	virtual void RHISetRayTracingMissShader(
		FRHIRayTracingScene* Scene, uint32 ShaderSlotInScene,
		FRHIRayTracingPipelineState* Pipeline, uint32 ShaderIndexInPipeline,
		uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
		uint32 UserData)
	{
		checkNoEntry();
	}

#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
	virtual void RHIBackBufferWaitTrackingBeginFrame(uint64 FrameToken, bool bDeferred)
	{
		checkNoEntry();
	}
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING

#if PLATFORM_REQUIRES_UAV_TO_RTV_TEXTURE_CACHE_FLUSH_WORKAROUND
	virtual void RHIFlushTextureCacheBOP(FRHITexture* Texture)
	{
		checkNoEntry();
	}
#endif // #if PLATFORM_REQUIRES_UAV_TO_RTV_TEXTURE_CACHE_FLUSH_WORKAROUND

	protected:
		FRHIRenderPassInfo RenderPassInfo;
};



FORCEINLINE FBoundShaderStateRHIRef RHICreateBoundShaderState(
	FRHIVertexDeclaration* VertexDeclaration,
	FRHIVertexShader* VertexShader,
	FRHIHullShader* HullShader,
	FRHIDomainShader* DomainShader,
	FRHIPixelShader* PixelShader,
	FRHIGeometryShader* GeometryShader
);


// Command Context for RHIs that do not support real Graphics Pipelines.
class IRHICommandContextPSOFallback : public IRHICommandContext
{
public:
	/**
	* Set bound shader state. This will set the vertex decl/shader, and pixel shader
	* @param BoundShaderState - state resource
	*/
	virtual void RHISetBoundShaderState(FRHIBoundShaderState* BoundShaderState) = 0;

	virtual void RHISetDepthStencilState(FRHIDepthStencilState* NewState, uint32 StencilRef) = 0;

	virtual void RHISetRasterizerState(FRHIRasterizerState* NewState) = 0;

	virtual void RHISetBlendState(FRHIBlendState* NewState, const FLinearColor& BlendFactor) = 0;

	virtual void RHIEnableDepthBoundsTest(bool bEnable) = 0;

	/**
	* This will set most relevant pipeline state. Legacy APIs are expected to set corresponding disjoint state as well.
	* @param GraphicsShaderState - the graphics pipeline state
	*/
	virtual void RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState, bool bApplyAdditionalState) override
	{
		FRHIGraphicsPipelineStateFallBack* FallbackGraphicsState = static_cast<FRHIGraphicsPipelineStateFallBack*>(GraphicsState);
		FGraphicsPipelineStateInitializer& PsoInit = FallbackGraphicsState->Initializer;

		RHISetBoundShaderState(
			RHICreateBoundShaderState(
				PsoInit.BoundShaderState.VertexDeclarationRHI,
				PsoInit.BoundShaderState.VertexShaderRHI,
				PsoInit.BoundShaderState.HullShaderRHI,
				PsoInit.BoundShaderState.DomainShaderRHI,
				PsoInit.BoundShaderState.PixelShaderRHI,
				PsoInit.BoundShaderState.GeometryShaderRHI
			).GetReference()
		);

		RHISetDepthStencilState(FallbackGraphicsState->Initializer.DepthStencilState, 0);
		RHISetRasterizerState(FallbackGraphicsState->Initializer.RasterizerState);
		RHISetBlendState(FallbackGraphicsState->Initializer.BlendState, FLinearColor(1.0f, 1.0f, 1.0f));
		if (GSupportsDepthBoundsTest)
		{
			RHIEnableDepthBoundsTest(FallbackGraphicsState->Initializer.bDepthBounds);
		}
	}
};
