// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AGXRHIPrivate.h"
#include "AGXCommandEncoder.h"
#include "AGXState.h"

class FAGXCommandList;
class FAGXCommandQueue;

class FAGXRenderPass
{
public:
#pragma mark - Public C++ Boilerplate -

	/** Default constructor */
	FAGXRenderPass(FAGXCommandList& CmdList, FAGXStateCache& StateCache);
	
	/** Destructor */
	~FAGXRenderPass(void);
	
#pragma mark -
	void SetDispatchType(mtlpp::DispatchType Type);
	
    void Begin(bool const bParallelBegin = false);
	
    void BeginParallelRenderPass(mtlpp::RenderPassDescriptor RenderPass, uint32 NumParallelContextsInPass);

    void BeginRenderPass(mtlpp::RenderPassDescriptor RenderPass);

    void RestartRenderPass(mtlpp::RenderPassDescriptor RenderPass);
    
    void DrawPrimitive(uint32 PrimitiveType, uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances);
    
    void DrawPrimitiveIndirect(uint32 PrimitiveType, FAGXVertexBuffer* VertexBuffer, uint32 ArgumentOffset);
    
    void DrawIndexedPrimitive(FAGXBuffer const& IndexBuffer, uint32 IndexStride, uint32 PrimitiveType, int32 BaseVertexIndex, uint32 FirstInstance,
                         uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances);
    
    void DrawIndexedIndirect(FAGXIndexBuffer* IndexBufferRHI, uint32 PrimitiveType, FAGXStructuredBuffer* VertexBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances);
    
    void DrawIndexedPrimitiveIndirect(uint32 PrimitiveType,FAGXIndexBuffer* IndexBufferRHI,FAGXVertexBuffer* VertexBufferRHI,uint32 ArgumentOffset);
	
    void Dispatch(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ);
    
    void DispatchIndirect(FAGXVertexBuffer* ArgumentBufferRHI, uint32 ArgumentOffset);
    
    void EndRenderPass(void);
    
    void CopyFromTextureToBuffer(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXBuffer const& toBuffer, uint32 destinationOffset, uint32 destinationBytesPerRow, uint32 destinationBytesPerImage, mtlpp::BlitOption options);
    
    void CopyFromBufferToTexture(FAGXBuffer const& Buffer, uint32 sourceOffset, uint32 sourceBytesPerRow, uint32 sourceBytesPerImage, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin, mtlpp::BlitOption options);
    
    void CopyFromTextureToTexture(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin);
	
	void CopyFromBufferToBuffer(FAGXBuffer const& SourceBuffer, NSUInteger SourceOffset, FAGXBuffer const& DestinationBuffer, NSUInteger DestinationOffset, NSUInteger Size);
	
	void PresentTexture(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin);
    
    void SynchronizeTexture(FAGXTexture const& Texture, uint32 Slice, uint32 Level);
    
	void SynchroniseResource(mtlpp::Resource const& Resource);
    
	void FillBuffer(FAGXBuffer const& Buffer, ns::Range Range, uint8 Value);
	
	bool AsyncCopyFromBufferToTexture(FAGXBuffer const& Buffer, uint32 sourceOffset, uint32 sourceBytesPerRow, uint32 sourceBytesPerImage, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin, mtlpp::BlitOption options);
	
	bool AsyncCopyFromTextureToTexture(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin);
	
	bool CanAsyncCopyToBuffer(FAGXBuffer const& DestinationBuffer);
	
	void AsyncCopyFromBufferToBuffer(FAGXBuffer const& SourceBuffer, NSUInteger SourceOffset, FAGXBuffer const& DestinationBuffer, NSUInteger DestinationOffset, NSUInteger Size);
	
	FAGXBuffer AllocateTemporyBufferForCopy(FAGXBuffer const& DestinationBuffer, NSUInteger Size, NSUInteger Align);
	
	void AsyncGenerateMipmapsForTexture(FAGXTexture const& Texture);
	
    void Submit(EAGXSubmitFlags SubmissionFlags);
    
    void End(void);
	
	void InsertCommandBufferFence(FAGXCommandBufferFence& Fence, mtlpp::CommandBufferHandler Handler);
	
	void AddCompletionHandler(mtlpp::CommandBufferHandler Handler);
	
	void AddAsyncCommandBufferHandlers(mtlpp::CommandBufferHandler Scheduled, mtlpp::CommandBufferHandler Completion);
	
	void TransitionResources(mtlpp::Resource const& Resource);

#pragma mark - Public Debug Support -
	
    /*
     * Inserts a debug compute encoder into the command buffer. This is how we generate a timestamp when no encoder exists.
     */
    void InsertDebugEncoder();
	
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
	void PopDebugGroup(void);
    
#pragma mark - Public Accessors -
	
	/*
	 * Get the current internal command buffer.
	 * @returns The current command buffer.
	 */
	mtlpp::CommandBuffer const& GetCurrentCommandBuffer(void) const;
	mtlpp::CommandBuffer& GetCurrentCommandBuffer(void);
	
    /*
     * Get the internal current command-encoder.
     * @returns The current command encoder.
     */
	inline FAGXCommandEncoder& GetCurrentCommandEncoder(void) { return CurrentEncoder; }
	
	/*
	 * Get the internal ring-buffer used for temporary allocations.
	 * @returns The temporary allocation buffer for the command-pass.
	 */
	FAGXSubBufferRing& GetRingBuffer(void);
	
	/*
	 * Attempts to shrink the ring-buffers so we don't keep very large allocations when we don't need them.
	 */
	void ShrinkRingBuffers(void);
	
	/*
	 * Whether the render-pass is within a parallel rendering pass.
	 * @returns True if and only if within a parallel rendering pass, otherwise false.
	 */
	bool IsWithinParallelPass(void);

	/*
	 * Get a child render command-encoder and the parent parallel command-encoder when within a parallel pass.
	 * @returns A valid render command encoder or nil.
	 */
    mtlpp::RenderCommandEncoder GetParallelRenderCommandEncoder(uint32 Index, mtlpp::ParallelRenderCommandEncoder& ParallelEncoder);
	
	void InsertTextureBarrier();
	
private:
    void ConditionalSwitchToRender(void);
    void ConditionalSwitchToCompute(void);
	void ConditionalSwitchToBlit(void);
	void ConditionalSwitchToAsyncBlit(void);
	void ConditionalSwitchToAsyncCompute(void);
	
    void PrepareToRender(uint32 PrimType);
    void PrepareToDispatch(void);
	void PrepareToAsyncDispatch(void);

    void CommitRenderResourceTables(void);
    void CommitDispatchResourceTables(void);
	void CommitAsyncDispatchResourceTables(void);
    
    void ConditionalSubmit();
	
	uint32 GetEncoderIndex(void) const;
	uint32 GetCommandBufferIndex(void) const;
	
private:
	FAGXCommandList& CmdList;
    FAGXStateCache& State;
    
    // Which of the buffers/textures/sampler slots are bound
    // The state cache is responsible for ensuring we bind the correct 
	FAGXTextureMask BoundTextures[EAGXShaderStages::Num];
    uint32 BoundBuffers[EAGXShaderStages::Num];
    uint16 BoundSamplers[EAGXShaderStages::Num];
    
    FAGXCommandEncoder CurrentEncoder;
    FAGXCommandEncoder PrologueEncoder;
	
	// To ensure that buffer uploads aren't overwritten before they are used track what is in flight
	// Disjoint ranges *are* permitted!
	TMap<id<MTLBuffer>, TArray<NSRange>> OutstandingBufferUploads;

    mtlpp::RenderPassDescriptor RenderPassDesc;
    
	mtlpp::DispatchType ComputeDispatchType;
    uint32 NumOutstandingOps;
    bool bWithinRenderPass;
};
