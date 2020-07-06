// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "RenderGraphEvent.h"
#include "RenderGraphResources.h"
#include "RenderGraphPass.h"
#include "RenderGraphValidation.h"
#include "ShaderParameterMacros.h"

class FRDGLogFile;

/** Builds the per-frame render graph.
 *  Resources must be created from the builder before they can be bound to Pass ResourceTables.
 *  These resources are descriptors only until the graph is executed, where RHI resources are allocated as needed.
 */
class RENDERCORE_API FRDGBuilder
{
public:
	static const EResourceTransitionAccess kDefaultAccessInitial = EResourceTransitionAccess::Unknown;
	static const EResourceTransitionAccess kDefaultAccessFinal = EResourceTransitionAccess::EReadable;

	FRDGBuilder(
		FRHICommandListImmediate& InRHICmdList,
		FRDGEventName DebugName = {},
		ERDGBuilderFlags Flags = ERDGBuilderFlags::None);
	FRDGBuilder(const FRDGBuilder&) = delete;

	/** Register a external texture to be tracked by the render graph. */
	FRDGTextureRef RegisterExternalTexture(
		const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		const TCHAR* Name,
		ERDGParentResourceFlags Flags,
		EResourceTransitionAccess AccessInitial = kDefaultAccessInitial,
		EResourceTransitionAccess AccessFinal = kDefaultAccessFinal);

	/** Helper variant which elides the initial state and flags. */
	FRDGTextureRef RegisterExternalTexture(
		const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		const TCHAR* Name = TEXT("External"),
		EResourceTransitionAccess AccessFinal = kDefaultAccessFinal)
	{
		return RegisterExternalTexture(ExternalPooledTexture, Name, ERDGParentResourceFlags::None, kDefaultAccessInitial, AccessFinal);
	}

	/** Variants of RegisterExternalTexture which will returns null (rather than assert) if the external texture is null. */
	FRDGTextureRef TryRegisterExternalTexture(
		const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		const TCHAR* Name,
		ERDGParentResourceFlags Flags,
		EResourceTransitionAccess AccessInitial = kDefaultAccessInitial,
		EResourceTransitionAccess AccessFinal = kDefaultAccessFinal)
	{
		return ExternalPooledTexture ? RegisterExternalTexture(ExternalPooledTexture, Name, Flags, AccessInitial, AccessFinal) : nullptr;
	}

	FRDGTextureRef TryRegisterExternalTexture(
		const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		const TCHAR* Name = TEXT("External"),
		EResourceTransitionAccess AccessFinal = kDefaultAccessFinal)
	{
		return ExternalPooledTexture ? RegisterExternalTexture(ExternalPooledTexture, Name, ERDGParentResourceFlags::None, kDefaultAccessInitial, AccessFinal) : nullptr;
	}

	/** Register a external buffer to be tracked by the render graph. */
	FRDGBufferRef RegisterExternalBuffer(
		const TRefCountPtr<FPooledRDGBuffer>& ExternalPooledBuffer,
		const TCHAR* Name,
		ERDGParentResourceFlags Flags,
		EResourceTransitionAccess AccessInitial = kDefaultAccessInitial,
		EResourceTransitionAccess AccessFinal = kDefaultAccessFinal);

	/** Helper variant which elides the initial state and flags. */
	FRDGBufferRef RegisterExternalBuffer(
		const TRefCountPtr<FPooledRDGBuffer>& ExternalPooledBuffer,
		const TCHAR* Name = TEXT("External"),
		EResourceTransitionAccess AccessFinal = kDefaultAccessFinal)
	{
		return RegisterExternalBuffer(ExternalPooledBuffer, Name, ERDGParentResourceFlags::None, kDefaultAccessInitial, AccessFinal);
	}

	/** Variants of RegisterExternalBuffer which will return null (rather than assert) if the external buffer is null. */
	FRDGBufferRef TryRegisterExternalBuffer(
		const TRefCountPtr<FPooledRDGBuffer>& ExternalPooledBuffer,
		const TCHAR* Name,
		ERDGParentResourceFlags Flags,
		EResourceTransitionAccess AccessInitial = kDefaultAccessInitial,
		EResourceTransitionAccess AccessFinal = kDefaultAccessFinal)
	{
		return ExternalPooledBuffer ? RegisterExternalBuffer(ExternalPooledBuffer, Name, Flags, AccessInitial, AccessFinal) : nullptr;
	}

	FRDGBufferRef TryRegisterExternalBuffer(
		const TRefCountPtr<FPooledRDGBuffer>& ExternalPooledBuffer,
		const TCHAR* Name = TEXT("External"),
		EResourceTransitionAccess AccessFinal = kDefaultAccessFinal)
	{
		return ExternalPooledBuffer ? RegisterExternalBuffer(ExternalPooledBuffer, Name, ERDGParentResourceFlags::None, kDefaultAccessInitial, AccessFinal) : nullptr;
	}

	/** Create graph tracked resource from a descriptor with a debug name.
	 *
	 *  The debug name is the name used for GPU debugging tools, but also for the VisualizeTexture/Vis command.
	 */
	FRDGTextureRef CreateTexture(
		const FPooledRenderTargetDesc& Desc,
		const TCHAR* Name,
		ERDGParentResourceFlags Flags = ERDGParentResourceFlags::None);

	/** Create graph tracked resource from a descriptor with a debug name.
	 *
	 *  The debug name is the name used for GPU debugging tools, but also for the VisualizeTexture/Vis command.
	 */
	FRDGBufferRef CreateBuffer(
		const FRDGBufferDesc& Desc,
		const TCHAR* Name,
		ERDGParentResourceFlags Flags = ERDGParentResourceFlags::None);

	/** Create graph tracked SRV for a texture from a descriptor. */
	FRDGTextureSRVRef CreateSRV(const FRDGTextureSRVDesc& Desc);

	/** Create graph tracked SRV for a buffer from a descriptor. */
	FRDGBufferSRVRef CreateSRV(const FRDGBufferSRVDesc& Desc);

	FRDGBufferSRVRef CreateSRV(FRDGBufferRef Buffer, EPixelFormat Format)
	{
		return CreateSRV(FRDGBufferSRVDesc(Buffer, Format));
	}

	/** Create graph tracked UAV for a texture from a descriptor. */
	FRDGTextureUAVRef CreateUAV(const FRDGTextureUAVDesc& Desc, ERDGChildResourceFlags Flags = ERDGChildResourceFlags::None);

	FRDGTextureUAVRef CreateUAV(FRDGTextureRef Texture, ERDGChildResourceFlags Flags = ERDGChildResourceFlags::None)
	{
		return CreateUAV(FRDGTextureUAVDesc(Texture), Flags);
	}

	/** Create graph tracked UAV for a buffer from a descriptor. */
	FRDGBufferUAVRef CreateUAV(const FRDGBufferUAVDesc& Desc, ERDGChildResourceFlags Flags = ERDGChildResourceFlags::None);

	FRDGBufferUAVRef CreateUAV(FRDGBufferRef Buffer, EPixelFormat Format, ERDGChildResourceFlags Flags = ERDGChildResourceFlags::None)
	{
		return CreateUAV(FRDGBufferUAVDesc(Buffer, Format), Flags);
	}

	/** Allocates parameter struct specifically to survive through the life time of the render graph. */
	template< typename ParameterStructType >
	ParameterStructType* AllocParameters();

	/** Adds a hard coded lambda pass to the graph.
	 *
	 * The Name of the pass should be generated with enough information to identify it's purpose and GPU cost, to be clear
	 * for GPU profiling tools.
	 *
	 * Caution: The pass parameter will be validated, and should not longer be modified after this call, since the pass may be executed
	 * right away with the immediate debugging mode.
	 */
	template<typename ParameterStructType, typename ExecuteLambdaType>
	FRDGPassRef AddPass(
		FRDGEventName&& Name,
		ParameterStructType* ParameterStruct,
		ERDGPassFlags Flags,
		ExecuteLambdaType&& ExecuteLambda);

	/** Adds an externally created pass to the render graph. This is useful if the pass parameter structure is generated at runtime.
	 *
	 *  Caution: You are on your own to have correct memory lifetime of the FRDGPass.
	 */
	void AddExternalPass(FRDGPass* NewPass)
	{
		AddPass(NewPass);
	}

#if WITH_MGPU
	void SetNameForTemporalEffect(FName InNameForTemporalEffect)
	{
		NameForTemporalEffect = InNameForTemporalEffect;
	}
#endif

	UE_DEPRECATED(4.26, "QueueTextureExtraction with bTransitionToRead is deprecated; use the EResourceTransitionAccess variant instead.")
	void QueueTextureExtraction(FRDGTextureRef Texture, TRefCountPtr<IPooledRenderTarget>* OutTexturePtr, bool bTransitionToRead);

	/** Queue a texture extraction. This will set *OutTexturePtr with the internal pooled render target at the Execute().
	 *
	 *  Note: even when the render graph uses the immediate debugging mode (executing passes as they get added), the texture extractions
	 *  will still happen in the Execute(), to ensure there is no bug caused in code outside the render graph on whether this mode is used or not.
	 */
	void QueueTextureExtraction(
		FRDGTextureRef Texture,
		TRefCountPtr<IPooledRenderTarget>* OutTexturePtr,
		EResourceTransitionAccess AccessFinal = EResourceTransitionAccess::EReadable);

	/** Queue a buffer extraction. This will set *OutBufferPtr with the internal pooled buffer at the Execute().
	 *
	 *  Note: even when the render graph uses the immediate debugging mode (executing passes as they get added), the buffer extractions
	 *  will still happen in the Execute(), to ensure there is no bug caused in code outside the render graph on whether this mode is used or not.
	 */
	void QueueBufferExtraction(
		FRDGBufferRef Buffer,
		TRefCountPtr<FPooledRDGBuffer>* OutBufferPtr,
		EResourceTransitionAccess AccessFinal = EResourceTransitionAccess::EReadable);

	/** Flag a texture that is produced by a pass but never used or extracted to not emit an 'unused' warning. */
	void RemoveUnusedTextureWarning(FRDGTextureRef Texture);

	/** Flag a buffer that is produced by a pass but never used or extracted to not emit an 'unused' warning. */
	void RemoveUnusedBufferWarning(FRDGBufferRef Buffer);

	/** Begins / ends a named event scope. These scopes are visible in most external GPU profilers.
	 *  Prefer to use RDG_EVENT_SCOPE instead of calling this manually.
	 */
	void BeginEventScope(FRDGEventName&& ScopeName);
	void EndEventScope();

	/** Begins / ends a stat scope. Stat scopes are visible via 'stat GPU'. Must be accompanied by a
	 *  respective EndStatScope call. Prefer to use RDG_GPU_STAT_SCOPE instead of calling this manually.
	 */
	void BeginStatScope(const FName& Name, const FName& StatName);
	void EndStatScope();

	/** Executes the queued passes, managing setting of render targets (RHI RenderPasses), resource transitions and queued texture extraction. */
	void Execute();

	/** Per-frame update of the render graph resource pool. */
	static void TickPoolElements();

	/** The RHI command list used for the render graph. */
	FRHICommandListImmediate& RHICmdList;

	/** Memory stack use for allocating RDG resource and passes. */
	FMemStackBase& MemStack;

private:
	using FPassBitArray = TBitArray<TInlineAllocator<16, SceneRenderingAllocator>>;

	FRHIAsyncComputeCommandListImmediate& RHICmdListAsyncCompute;

	const FRDGEventName BuilderName;
	const ERDGBuilderFlags BuilderFlags = ERDGBuilderFlags::None;

	ERDGPassFlags OverridePassFlags(const TCHAR* PassName, ERDGPassFlags Flags, bool bAsyncComputeSupported);

	FORCEINLINE FRDGPassHandle GetProloguePassHandle() const
	{
		return Passes.Begin();
	}

	FORCEINLINE FRDGPassHandle GetEpiloguePassHandle() const
	{
		return Passes.Last();
	}

	FORCEINLINE bool IsCulled(FRDGPassHandle Handle) const
	{
		return PassesToCull[Handle.GetIndex()];
	}

	/** Registry of passes added to the graph. */
	FRDGPassRegistry Passes;

	/** Registry of resources created through the graph. */
	FRDGResourceRegistry Resources;

	/** Bit array of culled passes. */
	FPassBitArray PassesToCull;

	/** Tracks resources with RHI allocations. */
	TSet<FRDGTexture*, DefaultKeyFuncs<FRDGTexture*>, SceneRenderingSetAllocator> AllocatedTextures;
	TSet<FRDGBuffer*, DefaultKeyFuncs<FRDGBuffer*>, SceneRenderingSetAllocator> AllocatedBuffers;

	/** Tracks external resources to their registered render graph counterparts for de-duplication. */
	TMap<const IPooledRenderTarget*, FRDGTexture*, SceneRenderingSetAllocator> ExternalTextures;
	TMap<const FPooledRDGBuffer*, FRDGBuffer*, SceneRenderingSetAllocator> ExternalBuffers;

	/** The epilogue and prologue passes are sentinels that are used to simplify graph logic around barriers
	 *  and traversal. The prologue pass is used exclusively for barriers before the graph executes, while the
	 *  epilogue pass is used for resource extraction barriers--a property that also makes it the main root of
	 *  the graph for culling purposes. The epilogue pass is added to the very end of the pass array for traversal
	 *  purposes. The prologue does not need to participate in any graph traversal behavior.
	 */
	FRDGPass* ProloguePass = nullptr;
	FRDGPass* EpiloguePass = nullptr;

	/** Array of all deferred access to internal textures. */
	struct FDeferredInternalTextureQuery
	{
		FRDGTexture* Texture = nullptr;
		TRefCountPtr<IPooledRenderTarget>* OutTexturePtr = nullptr;
	};
	TArray<FDeferredInternalTextureQuery, SceneRenderingAllocator> DeferredInternalTextureQueries;

	/** Array of all deferred access to internal buffers. */
	struct FDeferredInternalBufferQuery
	{
		FRDGBuffer* Buffer = nullptr;
		TRefCountPtr<FPooledRDGBuffer>* OutBufferPtr = nullptr;
	};
	TArray<FDeferredInternalBufferQuery, SceneRenderingAllocator> DeferredInternalBufferQueries;

	IF_RDG_SCOPES(FRDGScopeStacksByPipeline ScopeStacks);

	/** Tracks the total number of textures and buffers created from the graph. */
	uint16 TextureCount = 0;
	uint16 BufferCount = 0;

#if RDG_ENABLE_DEBUG
	FRDGUserValidation UserValidation;
	FRDGBarrierValidation BarrierValidation;
	FRDGLogFile LogFile;

	/** Tracks whether we are in a scope of adding passes to the builder. Used to avoid recursion. */
	bool bInDebugPassScope = false;
#endif

#if WITH_MGPU
	/** Name for the temporal effect used to synchronize multi-frame resources. */
	FName NameForTemporalEffect;

	/** Whether we performed the wait for the temporal effect yet. */
	bool bWaitedForTemporalEffect = false;
#endif

	template <typename T, typename... TArgs>
	T* Allocate(TArgs&&... Args);

	template <typename TRDGResource, typename... TArgs>
	TRDGResource* AllocateResource(TArgs&&... Args);

	void Compile();
	void Clear();

	void BeginResourceRHI(FRDGPass* Pass, FRDGTexture* Texture);
	void BeginResourceRHI(FRDGPass* Pass, FRDGTextureSRV* SRV);
	void BeginResourceRHI(FRDGPass* Pass, FRDGTextureUAV* UAV);
	void BeginResourceRHI(FRDGPass* Pass, FRDGBuffer* Buffer);
	void BeginResourceRHI(FRDGPass* Pass, FRDGBufferSRV* SRV);
	void BeginResourceRHI(FRDGPass* Pass, FRDGBufferUAV* UAV);

	void EndResourceRHI(FRDGPass* Pass, FRDGTexture* Texture, const FRDGTextureSubresourceRange Range);
	void EndResourceRHI(FRDGPass* Pass, FRDGBuffer* Buffer);

	void ReleaseResourceRHI(FRDGTextureRef Texture);
	void ReleaseResourceRHI(FRDGBufferRef Buffer);

	void AddPassDependencyInternal(FRDGPassHandle ProducerHandle, FRDGPassHandle ConsumerHandle);

	FRDGPassRef AddPass(FRDGPass* Pass);
	FRDGPassHandle AddPassInternal(FRDGPass* Pass);
	void ExecutePassPrologue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass);
	void ExecutePassEpilogue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass);
	void ExecutePass(FRDGPass* Pass);

	void CollectPassBarriers(FRDGPassHandle PassHandle);

	void AddTransition(FRDGTextureRef Texture, const FRDGTextureState& StateBefore, const FRDGTextureState& StateAfter);
	void AddTransition(FRDGBufferRef Buffer, FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter);

	void AddTransitionInternal(
		FRDGParentResource* Resource,
		FRDGSubresourceState StateBefore,
		FRDGSubresourceState StateAfter,
		const FRHITransitionInfo& TransitionInfo);

	void ResolveState(const FRDGPass* Pass, FRDGTextureRef Texture);
	void ResolveState(const FRDGPass* Pass, FRDGBufferRef Buffer);

	FRHIRenderPassInfo GetRenderPassInfo(const FRDGPass* Pass) const;

#if RDG_ENABLE_DEBUG
	void VisualizePassOutputs(const FRDGPass* Pass);
	void ClobberPassOutputs(const FRDGPass* Pass);
#endif
};

#include "RenderGraphBuilder.inl"