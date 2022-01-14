// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "AGXViewport.h"
#include "AGXCommandEncoder.h"
#include "AGXCommandQueue.h"
#include "AGXCommandList.h"
#include "AGXRenderPass.h"
#include "AGXBuffer.h"
#include "AGXCaptureManager.h"
#include "AGXFrameAllocator.h"
#if PLATFORM_IOS
#include "IOS/IOSView.h"
#endif
#include "Containers/LockFreeList.h"

#define NUM_SAFE_FRAMES 4

class FAGXRHICommandContext;
class FAGXPipelineStateCacheManager;
class FAGXQueryBufferPool;
class FAGXRHIBuffer;

class FAGXContext
{
	friend class FAGXCommandContextContainer;
public:
	FAGXContext(FAGXCommandQueue& Queue, bool bIsImmediate);
	virtual ~FAGXContext();
	
	FAGXCommandQueue& GetCommandQueue();
	FAGXCommandList& GetCommandList();
	mtlpp::CommandBuffer const& GetCurrentCommandBuffer() const;
	mtlpp::CommandBuffer& AGXRHI_API GetCurrentCommandBuffer();
	FAGXStateCache& GetCurrentState() { return StateCache; }
	FAGXRenderPass& GetCurrentRenderPass() { return RenderPass; }
	
	void InsertCommandBufferFence(FAGXCommandBufferFence& Fence, mtlpp::CommandBufferHandler Handler = nil);

	/**
	 * Do anything necessary to prepare for any kind of draw call 
	 * @param PrimitiveType The UnrealEngine primitive type for the draw call, needed to compile the correct render pipeline.
	 * @returns True if the preparation completed and the draw call can be encoded, false to skip.
	 */
	bool PrepareToDraw(uint32 PrimitiveType);
	
	/**
	 * Set the color, depth and stencil render targets, and then make the new command buffer/encoder
	 */
	void SetRenderPassInfo(const FRHIRenderPassInfo& RenderTargetsInfo, bool bRestart = false);
	
	/**
	 * Allocate from a dynamic ring buffer - by default align to the allowed alignment for offset field when setting buffers
	 */
	FAGXBuffer AllocateFromRingBuffer(uint32 Size, uint32 Alignment=0);

	TSharedRef<FAGXQueryBufferPool, ESPMode::ThreadSafe> GetQueryBufferPool()
	{
		return QueryBuffer.ToSharedRef();
	}

    void SubmitCommandsHint(uint32 const bFlags = EAGXSubmitFlagsCreateCommandBuffer);
	void SubmitCommandBufferAndWait();
	void ResetRenderCommandEncoder();
	
	void DrawPrimitive(uint32 PrimitiveType, uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances);
	
	void DrawPrimitiveIndirect(uint32 PrimitiveType, FAGXVertexBuffer* VertexBuffer, uint32 ArgumentOffset);
	
	void DrawIndexedPrimitive(FAGXBuffer const& IndexBuffer, uint32 IndexStride, mtlpp::IndexType IndexType, uint32 PrimitiveType, int32 BaseVertexIndex, uint32 FirstInstance,
							  uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances);
	
	void DrawIndexedIndirect(FAGXIndexBuffer* IndexBufferRHI, uint32 PrimitiveType, FAGXStructuredBuffer* VertexBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances);
	
	void DrawIndexedPrimitiveIndirect(uint32 PrimitiveType,FAGXIndexBuffer* IndexBufferRHI,FAGXVertexBuffer* VertexBufferRHI,uint32 ArgumentOffset);
	
	void DrawPatches(uint32 PrimitiveType, FAGXBuffer const& IndexBuffer, uint32 IndexBufferStride, int32 BaseVertexIndex, uint32 FirstInstance, uint32 StartIndex,
					 uint32 NumPrimitives, uint32 NumInstances);
	
	void CopyFromTextureToBuffer(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXBuffer const& toBuffer, uint32 destinationOffset, uint32 destinationBytesPerRow, uint32 destinationBytesPerImage, mtlpp::BlitOption options);
	
	void CopyFromBufferToTexture(FAGXBuffer const& Buffer, uint32 sourceOffset, uint32 sourceBytesPerRow, uint32 sourceBytesPerImage, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin, mtlpp::BlitOption options);
	
	void CopyFromTextureToTexture(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin);
	
	void CopyFromBufferToBuffer(FAGXBuffer const& SourceBuffer, NSUInteger SourceOffset, FAGXBuffer const& DestinationBuffer, NSUInteger DestinationOffset, NSUInteger Size);
	
    bool AsyncCopyFromBufferToTexture(FAGXBuffer const& Buffer, uint32 sourceOffset, uint32 sourceBytesPerRow, uint32 sourceBytesPerImage, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin, mtlpp::BlitOption options);
    
    bool AsyncCopyFromTextureToTexture(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin);
	
	bool CanAsyncCopyToBuffer(FAGXBuffer const& DestinationBuffer);
	
    void AsyncCopyFromBufferToBuffer(FAGXBuffer const& SourceBuffer, NSUInteger SourceOffset, FAGXBuffer const& DestinationBuffer, NSUInteger DestinationOffset, NSUInteger Size);
	
    void AsyncGenerateMipmapsForTexture(FAGXTexture const& Texture);
    
	void SubmitAsyncCommands(mtlpp::CommandBufferHandler ScheduledHandler, mtlpp::CommandBufferHandler CompletionHandler, bool bWait);
	
	void SynchronizeTexture(FAGXTexture const& Texture, uint32 Slice, uint32 Level);
	
	void SynchroniseResource(mtlpp::Resource const& Resource);
	
	void FillBuffer(FAGXBuffer const& Buffer, ns::Range Range, uint8 Value);

	void Dispatch(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ);
	void DispatchIndirect(FAGXVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset);

	void StartTiming(class FAGXEventNode* EventNode);
	void EndTiming(class FAGXEventNode* EventNode);

#if ENABLE_METAL_GPUPROFILE
	static void MakeCurrent(FAGXContext* Context);
	static FAGXContext* GetCurrentContext();
#endif
	
	void InitFrame(bool bImmediateContext, uint32 Index, uint32 Num);
	void FinishFrame(bool bImmediateContext);

	// Track Write->Read transitions for TBDR Fragment->Verex fencing
	void TransitionResource(FRHIUnorderedAccessView* InResource);
	void TransitionResource(FRHITexture* InResource);

	template<typename T>
	void TransitionRHIResource(T* InResource);

protected:
	/** The wrapper around the device command-queue for creating & committing command buffers to */
	FAGXCommandQueue& CommandQueue;
	
	/** The wrapper around commabd buffers for ensuring correct parallel execution order */
	FAGXCommandList CommandList;
	
	/** The cache of all tracked & accessible state. */
	FAGXStateCache StateCache;
	
	/** The render pass handler that actually encodes our commands. */
	FAGXRenderPass RenderPass;
	
	/** A sempahore used to ensure that wait for previous frames to complete if more are in flight than we permit */
	dispatch_semaphore_t CommandBufferSemaphore;
	
	/** A pool of buffers for writing visibility query results. */
	TSharedPtr<FAGXQueryBufferPool, ESPMode::ThreadSafe> QueryBuffer;
	
#if ENABLE_METAL_GPUPROFILE
	/** the slot to store a per-thread context ref */
	static uint32 CurrentContextTLSSlot;
#endif
	
	/** Total number of parallel contexts that constitute the current pass. */
	int32 NumParallelContextsInPass;
	
	/** Whether the validation layer is enabled */
	bool bValidationEnabled;
};

template<typename T>
void FAGXContext::TransitionRHIResource(T* InResource)
{
	auto Resource = ResourceCast(InResource);
	if (Resource->GetCurrentBufferOrNil())
	{
		RenderPass.TransitionResources(Resource->GetCurrentBuffer());
	}
}

class FAGXDeviceContext : public FAGXContext
{
public:
	static FAGXDeviceContext* CreateDeviceContext();
	virtual ~FAGXDeviceContext();
	
	void Init(void);
	
	inline bool SupportsFeature(EAGXFeatures InFeature) { return CommandQueue.SupportsFeature(InFeature); }
	
	inline FAGXResourceHeap& GetResourceHeap(void) { return Heap; }
	
	FAGXTexture CreateTexture(FAGXSurface* Surface, mtlpp::TextureDescriptor Descriptor);
	FAGXBuffer CreatePooledBuffer(FAGXPooledBufferArgs const& Args);
	void ReleaseBuffer(FAGXBuffer& Buf);
	void ReleaseObject(id Object);
	void ReleaseTexture(FAGXSurface* Surface, FAGXTexture& Texture);
	void ReleaseTexture(FAGXTexture& Texture);
	
	void BeginFrame();
	void FlushFreeList();
	void ClearFreeList();
	void DrainHeap();
	void EndFrame();
	
	/** RHIBeginScene helper */
	void BeginScene();
	/** RHIEndScene helper */
	void EndScene();
	
	void BeginDrawingViewport(FAGXViewport* Viewport);
	void EndDrawingViewport(FAGXViewport* Viewport, bool bPresent, bool bLockToVsync);
	
	/** Take a parallel FAGXContext from the free-list or allocate a new one if required */
	FAGXRHICommandContext* AcquireContext(int32 NewIndex, int32 NewNum);
	
	/** Release a parallel FAGXContext back into the free-list */
	void ReleaseContext(FAGXRHICommandContext* Context);
	
	/** Returns the number of concurrent contexts encoding commands, including the device context. */
	uint32 GetNumActiveContexts(void) const;

	void BeginParallelRenderCommandEncoding(uint32 Num);
	void SetParallelRenderPassDescriptor(FRHIRenderPassInfo const& TargetInfo);
	mtlpp::RenderCommandEncoder GetParallelRenderCommandEncoder(uint32 Index, mtlpp::ParallelRenderCommandEncoder& ParallelEncoder, mtlpp::CommandBuffer& CommandBuffer);
	void EndParallelRenderCommandEncoding(void);
	
	/** Get the index of the bound Metal device in the global list of rendering devices. */
	uint32 GetDeviceIndex(void) const;
	
	FAGXFrameAllocator* GetTransferAllocator()
	{
		return TransferBufferAllocator;
	}
    
    FAGXFrameAllocator* GetUniformAllocator()
    {
        return UniformBufferAllocator;
    }
    
    uint32 GetFrameNumberRHIThread()
    {
        return FrameNumberRHIThread;
    }
	
	void NewLock(FAGXRHIBuffer* Buffer, FAGXFrameAllocator::AllocationEntry& Allocation);
	FAGXFrameAllocator::AllocationEntry FetchAndRemoveLock(FAGXRHIBuffer* Buffer);
	
#if METAL_DEBUG_OPTIONS
    void AddActiveBuffer(FAGXBuffer const& Buffer);
    void RemoveActiveBuffer(FAGXBuffer const& Buffer);
	bool ValidateIsInactiveBuffer(FAGXBuffer const& Buffer);
	void ScribbleBuffer(FAGXBuffer& Buffer);
#endif
	
private:
	FAGXDeviceContext(uint32 DeviceIndex, FAGXCommandQueue* Queue);
	
private:
	/** The index into the GPU device list for the selected Metal device */
	uint32 DeviceIndex;
	
	/** Dynamic memory heap */
	FAGXResourceHeap Heap;
	
	/** GPU Frame Capture Manager */
	FAGXCaptureManager CaptureManager;
	
	/** Free lists for releasing objects only once it is safe to do so */
	TSet<FAGXBuffer> UsedBuffers;
	TSet<FAGXTexture> UsedTextures;
	TSet<id> ObjectFreeList;
	struct FAGXDelayedFreeList
	{
		bool IsComplete() const;
		TArray<mtlpp::CommandBufferFence> Fences;
		TSet<FAGXBuffer> UsedBuffers;
		TSet<FAGXTexture> UsedTextures;
		TSet<id> ObjectFreeList;
#if METAL_DEBUG_OPTIONS
		int32 DeferCount;
#endif
	};
	TArray<FAGXDelayedFreeList*> DelayedFreeLists;
	
//	TSet<FAGXUniformBuffer*> UniformBuffers;
    FAGXFrameAllocator* UniformBufferAllocator;
	FAGXFrameAllocator* TransferBufferAllocator;
	
	TMap<FAGXRHIBuffer*, FAGXFrameAllocator::AllocationEntry> OutstandingLocks;
	
#if METAL_DEBUG_OPTIONS
    FCriticalSection ActiveBuffersMutex;
    
    /** These are the active buffers that cannot be CPU modified */
    TMap<id<MTLBuffer>, TArray<NSRange>> ActiveBuffers;
#endif
	
	/** Free-list of contexts for parallel encoding */
	TLockFreePointerListLIFO<FAGXRHICommandContext> ParallelContexts;
	
	/** Critical section for FreeList */
	FCriticalSection FreeListMutex;
	
	/** Event for coordinating pausing of render thread to keep inline with the ios display link. */
	FEvent* FrameReadyEvent;
	
	/** Internal frame counter, incremented on each call to RHIBeginScene. */
	uint32 SceneFrameCounter;
	
	/** Internal frame counter, used to ensure that we only drain the buffer pool one after each frame within RHIEndFrame. */
	uint32 FrameCounter;
	
	/** Bitfield of supported Metal features with varying availability depending on OS/device */
	uint32 Features;
	
	/** Count of concurrent contexts encoding commands. */
	int32 ActiveContexts;
	
	/** Count of concurrent parallel contexts encoding commands. */
	int32 ActiveParallelContexts;
	
	/** Whether we presented this frame - only used to track when to introduce debug markers */
	bool bPresented;
	
	/** PSO cache manager */
	FAGXPipelineStateCacheManager* PSOManager;

    /** Thread index owned by the RHI Thread. Monotonically increases every call to EndFrame() */
    uint32 FrameNumberRHIThread;
};

