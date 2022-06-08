// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <Metal/Metal.h>
#include "AGXBuffer.h"
#include "AGXProfiler.h"
#include "command_buffer.hpp"

class FAGXCommandList;
class FAGXCommandQueue;
class FAGXGraphicsPipelineState;
struct FAGXCommandBufferFence;

/**
 * Enumeration for submission hints to avoid unclear bool values.
 */
enum EAGXSubmitFlags
{
	/** No submission flags. */
	EAGXSubmitFlagsNone = 0,
	/** Create the next command buffer. */
	EAGXSubmitFlagsCreateCommandBuffer = 1 << 0,
	/** Wait on the submitted command buffer. */
	EAGXSubmitFlagsWaitOnCommandBuffer = 1 << 1,
	/** Break a single logical command-buffer into parts to keep the GPU active. */
	EAGXSubmitFlagsBreakCommandBuffer = 1 << 2,
	/** Submit the prologue command-buffer only, leave the current command-buffer active.  */
	EAGXSubmitFlagsAsyncCommandBuffer = 1 << 3,
	/** Submit and reset all the cached encoder state.  */
	EAGXSubmitFlagsResetState = 1 << 4,
	/** Force submission even if the command-buffer is empty. */
	EAGXSubmitFlagsForce = 1 << 5,
	/** Indicates this is the final command buffer in a frame. */
	EAGXSubmitFlagsLastCommandBuffer = 1 << 6,
};

/**
 * EAGXCommandEncoderType:
 *   EAGXCommandEncoderCurrent: The primary encoder that is used for draw calls & dispatches
 *   EAGXCommandEncoderPrologue: A secondary encoder that is used for blits & dispatches that setup resources & state for the current encoder.
 */
enum EAGXCommandEncoderType
{
	EAGXCommandEncoderCurrent,
	EAGXCommandEncoderPrologue
};

/**
 * FAGXCommandEncoder:
 *	Wraps the details of switching between different command encoders on the command-buffer, allowing for restoration of the render encoder if needed.
 * 	UE expects the API to serialise commands in-order, but Metal expects applications to work with command-buffers directly so we need to implement
 *	the RHI semantics by switching between encoder types. This class hides the ugly details. Eventually it might be possible to move some of the operations
 *	into pre- & post- command-buffers so that we avoid encoder switches but that will take changes to the RHI and high-level code too, so it won't happen soon.
 */
class FAGXCommandEncoder
{
public:
	/** Default constructor */
	FAGXCommandEncoder(FAGXCommandList& CmdList, EAGXCommandEncoderType Type);
	
	/** Destructor */
	~FAGXCommandEncoder();
	
	/** Reset cached state for reuse */
	void Reset();
	
	/** Reset cached state for reuse while in rendering */
	void ResetLive();
	
	/**
	 * Start encoding to CommandBuffer. It is an error to call this with any outstanding command encoders or current command buffer.
	 * Instead call EndEncoding & CommitCommandBuffer before calling this.
	 */
	void StartCommandBuffer();
	
	/**
	 * Commit the existing command buffer if there is one & optionally waiting for completion, if there isn't a current command buffer this is a no-op.
	 * @param Flags Flags to control commit behaviour.
 	 */
	void CommitCommandBuffer(uint32 Flags);

	/** @returns the current command buffer */
	mtlpp::CommandBuffer& GetCommandBuffer() { return CommandBuffer; }

	/** @returns the current command buffer */
	mtlpp::CommandBuffer const& GetCommandBuffer() const { return CommandBuffer; }
	
	/** @returns the monotonically incremented command buffer index */
	uint32 GetCommandBufferIndex() const { return CmdBufIndex; }

	/** @returns True if and only if there is an active parallel render command encoder, otherwise false. */
	bool IsParallelRenderCommandEncoderActive() const;
	
	/** @returns True if and only if there is an active render command encoder, otherwise false. */
	bool IsRenderCommandEncoderActive() const;
	
	/** @returns True if and only if there is an active compute command encoder, otherwise false. */
	bool IsComputeCommandEncoderActive() const;
	
	/** @returns True if and only if there is an active blit command encoder, otherwise false. */
	bool IsBlitCommandEncoderActive() const;
	
	/**
	 * True iff the command-encoder submits immediately to the command-queue, false if it performs any buffering.
	 * @returns True iff the command-list submits immediately to the command-queue, false if it performs any buffering.
	 */
	bool IsImmediate() const;
	
	/**
	 * True iff the command-encoder encodes only to a child of a parallel render command encoder, false if it is standalone.
	 * @returns True iff the command-encoder encodes only to a child of a parallel render command encoder, false if it is standalone.
	 */
	bool IsParallel() const;

	/** @returns True if and only if there is valid render pass descriptor set on the encoder, otherwise false. */
	bool IsRenderPassDescriptorValid() const;
	
	/** @returns The current render pass descriptor. */
	MTLRenderPassDescriptor const* GetRenderPassDescriptor() const;
	
	/** @returns The active render command encoder or nil if there isn't one. */
	id<MTLParallelRenderCommandEncoder> GetParallelRenderCommandEncoder() const;
	
	/** @returns The child render command encoder of the current parallel render encoder for Index. */
	id<MTLRenderCommandEncoder> GetChildRenderCommandEncoder(uint32 Index) const;

	/** @returns The active render command encoder or nil if there isn't one. */
	id<MTLRenderCommandEncoder> GetRenderCommandEncoder() const;
	
	/** @returns The active compute command encoder or nil if there isn't one. */
	id<MTLComputeCommandEncoder> GetComputeCommandEncoder() const;
	
	/** @returns The active blit command encoder or nil if there isn't one. */
	id<MTLBlitCommandEncoder> GetBlitCommandEncoder() const;
	
	/** @returns The number of encoded passes in the command buffer. */
	uint32 NumEncodedPasses() const { return EncoderNum; }
	
	/**
 	 * Begins encoding rendering commands into the current command buffer. No other encoder may be active & the MTLRenderPassDescriptor must previously have been set.
	 * @param NumChildren The number of child render-encoders to create. 
	 */
	void BeginParallelRenderCommandEncoding(uint32 NumChildren);

	/**
 	 * Begins encoding rendering commands into the current command buffer. No other encoder may be active & the MTLRenderPassDescriptor must previously have been set.
	 */
	void BeginRenderCommandEncoding();
	
	/** Begins encoding compute commands into the current command buffer. No other encoder may be active. */
	void BeginComputeCommandEncoding(MTLDispatchType Type = MTLDispatchTypeSerial);
	
	/** Begins encoding blit commands into the current command buffer. No other encoder may be active. */
	void BeginBlitCommandEncoding();
	
	/** Declare that all command generation from this encoder is complete, and detach from the MTLCommandBuffer if there is an encoder active or does nothing if there isn't. */
	void EndEncoding();
	
	/** Initialises a fence for the current command-buffer optionally adding a command-buffer completion handler to the command-buffer */
	void InsertCommandBufferFence(FAGXCommandBufferFence& Fence, mtlpp::CommandBufferHandler Handler);
	
	/** Adds a command-buffer completion handler to the command-buffer */
	void AddCompletionHandler(mtlpp::CommandBufferHandler Handler);
	
	/*
	 * Inserts a debug string into the command buffer.  This does not change any API behavior, but can be useful when debugging.
	 * @param string The name of the signpost. 
	 */
	void InsertDebugSignpost(ns::String const& String);
	
	/*
	 * Push a new named string onto a stack of string labels.
	 * @param string The name of the debug group. 
	 */
	void PushDebugGroup(ns::String const& String);
	
	/* Pop the latest named string off of the stack. */
	void PopDebugGroup();
	
#if ENABLE_METAL_GPUPROFILE
	/* Get the command-buffer stats object. */
	FAGXCommandBufferStats* GetCommandBufferStats();
#endif

	/**
	 * Set the render pass descriptor - no encoder may be active when this function is called.
	 * @param RenderPass The render pass descriptor to set. May be nil.
	 */
	void SetRenderPassDescriptor(MTLRenderPassDescriptor* InRenderPassDesc);
	
	/**
	 * Set the render pass store actions, call after SetRenderPassDescriptor but before EndEncoding.
	 * @param ColorStore The store actions for color targets.
	 * @param DepthStore The store actions for the depth buffer - use MTLStoreActionUnknown if no depth-buffer bound.
	 * @param StencilStore The store actions for the stencil buffer - use MTLStoreActionUnknown if no stencil-buffer bound.
	 */
	void SetRenderPassStoreActions(MTLStoreAction const* ColorStore, MTLStoreAction DepthStore, MTLStoreAction StencilStore);
	
	/*
	 * Sets the current render pipeline state object.
	 * @param PipelineState The pipeline state to set. Must not be nil.
	 */
	void SetRenderPipelineState(FAGXShaderPipeline* const PipelineState);
	
	/*
	 * Set the viewport, which is used to transform vertexes from normalized device coordinates to window coordinates.  Fragments that lie outside of the viewport are clipped, and optionally clamped for fragments outside of znear/zfar.
	 * @param Viewport The array of viewport dimensions to use.
	 * @param NumActive The number of active viewport dimensions to use.
	 */
	void SetViewport(MTLViewport const Viewport[], uint32 NumActive);
	
	/*
	 * The winding order of front-facing primitives.
	 * @param FrontFacingWinding The front face winding.
	 */
	void SetFrontFacingWinding(MTLWinding FrontFacingWinding);
	
	/*
	 * Controls if primitives are culled when front facing, back facing, or not culled at all.
	 * @param CullMode The cull mode.
	 */
	void SetCullMode(MTLCullMode CullMode);
	
	/*
	 * Depth Bias.
	 * @param DepthBias The depth-bias value.
	 * @param SlopeScale The slope-scale to apply.
	 * @param Clamp The value to clamp to.
	 */
	void SetDepthBias(float DepthBias, float SlopeScale, float Clamp);
	
	/*
	 * Specifies a rectangle for a fragment scissor test.  All fragments outside of this rectangle are discarded.
	 * @param Rect The array of scissor rect dimensions.
	 * @param NumActive The number of active scissor rect dimensions.
	 */
	void SetScissorRect(MTLScissorRect const Rect[], uint32 NumActive);
	
	/*
	 * Set how to rasterize triangle and triangle strip primitives.
	 * @param FillMode The fill mode.
	 */
	void SetTriangleFillMode(MTLTriangleFillMode InFillMode);

	/*
	 * Set wether to clip or clamp triangles based on depth.
	 * @param FillMode The fill mode.
	 */
	void SetDepthClipMode(MTLDepthClipMode InDepthClipMode);
	
	/*
	 * Set the constant blend color used across all blending on all render targets
	 * @param Red The value for the red channel in 0-1.
	 * @param Green The value for the green channel in 0-1.
	 * @param Blue The value for the blue channel in 0-1.
	 * @param Alpha The value for the alpha channel in 0-1.
	 */
	void SetBlendColor(float Red, float Green, float Blue, float Alpha);
	
	/*
	 * Set the DepthStencil state object.
	 * @param DepthStencilState The depth-stencil state, must not be nil.
	 */
	void SetDepthStencilState(id<MTLDepthStencilState> DepthStencilState);
	
	/*
	 * Set the stencil reference value for both the back and front stencil buffers.
	 * @param ReferenceValue The stencil ref value to use.
	 */
	void SetStencilReferenceValue(uint32 ReferenceValue);
	
	/*
	 * Monitor if samples pass the depth and stencil tests.
	 * @param Mode Controls if the counter is disabled or moniters passing samples.
	 * @param Offset The offset relative to the occlusion query buffer provided when the command encoder was created.  offset must be a multiple of 8.
	 */
	void SetVisibilityResultMode(MTLVisibilityResultMode Mode, NSUInteger Offset);
	
	/*
	 * Set a global buffer for the specified shader frequency at the given bind point index.
	 * @param FunctionType The shader function to modify.
	 * @param Buffer The buffer to bind or nil to clear.
	 * @param Offset The offset in the buffer or 0 when Buffer is nil.
	 * @param Length The data length - caller is responsible for accounting for non-zero Offset.
	 * @param Index The index to modify.
	 * @param Usage The resource usage mask.
	 * @param Format The Pixel format to reinterpret the resource as.
	 */
	void SetShaderBuffer(MTLFunctionType FunctionType, FAGXBuffer const& Buffer, NSUInteger Offset, NSUInteger Length, NSUInteger Index, MTLResourceUsage Usage, EPixelFormat Format = PF_Unknown, NSUInteger ElementRowPitch = 0);
	
	/*
	 * Set an FAGXBufferData to the specified shader frequency at the given bind point index.
	 * @param FunctionType The shader function to modify.
	 * @param Data The data to bind or nullptr to clear.
	 * @param Offset The offset in the buffer or 0 when Buffer is nil.
	 * @param Index The index to modify.
	 * @param Format The pixel format to reinterpret the resource as.
	 */
	void SetShaderData(MTLFunctionType FunctionType, FAGXBufferData* Data, NSUInteger Offset, NSUInteger Index, EPixelFormat Format = PF_Unknown, NSUInteger ElementRowPitch = 0);
	
	/*
	 * Set bytes to the specified shader frequency at the given bind point index.
	 * @param FunctionType The shader function to modify.
	 * @param Bytes The data to bind or nullptr to clear.
	 * @param Length The length of the buffer or 0 when Bytes is nil.
	 * @param Index The index to modify.
	 */
	void SetShaderBytes(MTLFunctionType FunctionType, uint8 const* Bytes, NSUInteger Length, NSUInteger Index);
	
	/*
	 * Set a global texture for the specified shader frequency at the given bind point index.
	 * @param FunctionType The shader function to modify.
	 * @param Texture The texture to bind or nil to clear.
	 * @param Index The index to modify.
	 * @param Usage The resource usage mask.
	 */
	void SetShaderTexture(MTLFunctionType FunctionType, FAGXTexture const& Texture, NSUInteger Index, MTLResourceUsage Usage);
	
	/*
	 * Set a global sampler for the specified shader frequency at the given bind point index.
	 * @param FunctionType The shader function to modify.
	 * @param Sampler The sampler state to bind or nil to clear.
	 * @param Index The index to modify.
	 */
	void SetShaderSamplerState(MTLFunctionType FunctionType, id<MTLSamplerState> Sampler, NSUInteger Index);
	
	/*
	 * Set the shader side-table data for FunctionType at Index.
	 * @param FunctionType The shader function to modify.
	 * @param Index The index to bind data to.
	 */
	void SetShaderSideTable(MTLFunctionType FunctionType, NSUInteger Index);
	
	/*
	 * Transition resource so that we can barrier fragment->vertex stages.
	 * @param Resource The resource we are going to make readable having been written.
	 */
	void TransitionResources(mtlpp::Resource const& Resource);
	
	/*
	 * Set the compute pipeline state that will be used.
	 * @param State The state to set - must not be nil.
	 */
	void SetComputePipelineState(FAGXShaderPipeline* State);

	/*
	 * Get the internal ring-buffer used for temporary allocations.
	 * @returns The temporary allocation buffer for this command-encoder.
	 */
	FAGXSubBufferRing& GetRingBuffer();
	
	/*
	 * Returns True if the Resource has been bound to a command encoder, otherwise false.  History will be cleared after a commit operation
	 * @returns True if the Resource has been bound to a command encoder, otherwise false.
	 */
	bool HasTextureBindingHistory(FAGXTexture const& Texture) const;
	bool HasBufferBindingHistory(FAGXBuffer const& Buffer) const;
	
private:
	/*
	 * Set the offset for the buffer bound on the specified shader frequency at the given bind point index.
	 * @param FunctionType The shader function to modify.
	 * @param Offset The offset in the buffer or 0 when Buffer is nil.
	 * @param Length The data length - caller is responsible for accounting for non-zero Offset.
	 * @param Index The index to modify.
	 */
	void SetShaderBufferOffset(MTLFunctionType FunctionType, NSUInteger Offset, NSUInteger Length, NSUInteger Index);
	
	void SetShaderBufferInternal(MTLFunctionType Function, uint32 Index);
	
	void FenceResource(mtlpp::Texture const& Resource);
	void FenceResource(mtlpp::Buffer const& Resource);
	
private:
	static const uint32 NUM_SHADER_FREQUENCIES = uint32(MTLFunctionTypeKernel) + 1;

    /** A structure of arrays for the current buffer binding settings. */
    struct FAGXBufferBindings
    {
		/** Side-table wrapper object to allow us to use Set*Bytes. */
		FAGXBufferData* SideTable;
        /** The bound buffers or nil. */
		ns::AutoReleased<FAGXBuffer> Buffers[ML_MaxBuffers];
        /** The bound buffers or nil. */
        FAGXBufferData* Bytes[ML_MaxBuffers];
        /** The bound buffer offsets or 0. */
        NSUInteger Offsets[ML_MaxBuffers];
		/** The usage mask for the bound resource or 0 */
		MTLResourceUsage Usage[ML_MaxBuffers];
		/** The bound buffer constants */
		struct FSizeConstants
		{
			union
			{
				uint32 	Length;
				uint32 	Swizzle;
			};
			uint32 Format;
			uint32 ElementRowPitch;
		};
		FSizeConstants Lengths[ML_MaxBuffers + ML_MaxTextures];
        /** A bitmask for which buffers were bound by the application where a bit value of 1 is bound and 0 is unbound. */
        uint32 Bound;
        
        void SetBufferMetaData(NSUInteger Index, NSUInteger Length, NSUInteger Format, NSUInteger ElementRowPitch)
        {
			Lengths[Index].Length = Length;
			Lengths[Index].Format = Format;
			Lengths[Index].ElementRowPitch = ElementRowPitch;
        }
        void SetTextureSwizzle(NSUInteger Index, uint8 (&Swizzle)[4])
        {
			FMemory::Memcpy(&Lengths[ML_MaxBuffers + Index].Swizzle, Swizzle, sizeof(Swizzle));
			Lengths[ML_MaxBuffers + Index].Format = 0;
			Lengths[ML_MaxBuffers + Index].ElementRowPitch = 0;
        }
	};
	
	FAGXCommandList& CommandList;

    // Cache Queue feature
    bool bSupportsMetalFeaturesSetBytes;
    
	FAGXBufferBindings ShaderBuffers[NUM_SHADER_FREQUENCIES];
	
	MTLStoreAction ColorStoreActions[MaxSimultaneousRenderTargets];
	MTLStoreAction DepthStoreAction;
	MTLStoreAction StencilStoreAction;
	
	FAGXSubBufferRing RingBuffer;
	
	MTLRenderPassDescriptor* RenderPassDesc;
	
	mtlpp::CommandBuffer CommandBuffer;
	id<MTLParallelRenderCommandEncoder> ParallelRenderCommandEncoder;
	id<MTLRenderCommandEncoder> RenderCommandEncoder;
	id<MTLComputeCommandEncoder> ComputeCommandEncoder;
	id<MTLBlitCommandEncoder> BlitCommandEncoder;
	TArray< id<MTLRenderCommandEncoder> > ChildRenderCommandEncoders;
	
#if ENABLE_METAL_GPUPROFILE
	FAGXCommandBufferStats* CommandBufferStats;
#endif

	TArray<ns::Object<mtlpp::CommandBufferHandler>> CompletionHandlers;
	NSMutableArray* DebugGroups;
    
    TSet<ns::AutoReleased<FAGXBuffer>> ActiveBuffers;
    
	TSet<ns::AutoReleased<FAGXBuffer>> BufferBindingHistory;
	TSet<ns::AutoReleased<FAGXTexture>> TextureBindingHistory;
	
	TMap<mtlpp::Resource::Type, MTLResourceUsage> ResourceUsage;
	
	uint32 EncoderNum;
	uint32 CmdBufIndex;
	EAGXCommandEncoderType Type;
};
