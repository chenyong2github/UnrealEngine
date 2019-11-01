// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "RenderGraphEvent.h"
#include "RenderGraphResources.h"
#include "RenderGraphPass.h"
#include "RenderGraphValidation.h"
#include "ShaderParameterMacros.h"

/** Builds the per-frame render graph.
 *  Resources must be created from the builder before they can be bound to Pass ResourceTables.
 *  These resources are descriptors only until the graph is executed, where RHI resources are allocated as needed.
 */
class RENDERCORE_API FRDGBuilder
{
public:
	/** A RHI cmd list is required, if using the immediate mode. */
	FRDGBuilder(FRHICommandListImmediate& InRHICmdList);
	FRDGBuilder(const FRDGBuilder&) = delete;

	/** Register a external texture to be tracked by the render graph. */
	FRDGTextureRef RegisterExternalTexture(
		const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		const TCHAR* Name = TEXT("External"),
		ERDGResourceFlags Flags = ERDGResourceFlags::None);

	/** Variant of RegisterExternalTexture which will returns null (rather than assert) if the external texture is null. */
	inline FRDGTextureRef TryRegisterExternalTexture(
		const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		const TCHAR* Name = TEXT("External"),
		ERDGResourceFlags Flags = ERDGResourceFlags::None)
	{
		return ExternalPooledTexture ? RegisterExternalTexture(ExternalPooledTexture, Name, Flags) : nullptr;
	}

	/** Register a external buffer to be tracked by the render graph. */
	FRDGBufferRef RegisterExternalBuffer(
		const TRefCountPtr<FPooledRDGBuffer>& ExternalPooledBuffer,
		const TCHAR* Name = TEXT("External"),
		ERDGResourceFlags Flags = ERDGResourceFlags::None);

	/** Variant of RegisterExternalBuffer which will return null (rather than assert) if the external buffer is null. */
	inline FRDGBufferRef TryRegisterExternalBuffer(
		const TRefCountPtr<FPooledRDGBuffer>& ExternalPooledBuffer,
		const TCHAR* Name = TEXT("External"),
		ERDGResourceFlags Flags = ERDGResourceFlags::None)
	{
		return ExternalPooledBuffer ? RegisterExternalBuffer(ExternalPooledBuffer, Name, Flags) : nullptr;
	}

	/** Create graph tracked resource from a descriptor with a debug name.
	 *
	 *  The debug name is the name used for GPU debugging tools, but also for the VisualizeTexture/Vis command.
	 */
	FRDGTextureRef CreateTexture(
		const FPooledRenderTargetDesc& Desc,
		const TCHAR* Name,
		ERDGResourceFlags Flags = ERDGResourceFlags::None);

	/** Create graph tracked resource from a descriptor with a debug name.
	 *
	 *  The debug name is the name used for GPU debugging tools, but also for the VisualizeTexture/Vis command.
	 */
	FRDGBufferRef CreateBuffer(
		const FRDGBufferDesc& Desc,
		const TCHAR* Name,
		ERDGResourceFlags Flags = ERDGResourceFlags::None);

	/** Create graph tracked SRV for a texture from a descriptor. */
	FRDGTextureSRVRef CreateSRV(const FRDGTextureSRVDesc& Desc);

	/** Create graph tracked SRV for a buffer from a descriptor. */
	FRDGBufferSRVRef CreateSRV(const FRDGBufferSRVDesc& Desc);

	FRDGBufferSRVRef CreateSRV(FRDGBufferRef Buffer, EPixelFormat Format)
	{
		return CreateSRV(FRDGBufferSRVDesc(Buffer, Format));
	}

	/** Create graph tracked UAV for a texture from a descriptor. */
	FRDGTextureUAVRef CreateUAV(const FRDGTextureUAVDesc& Desc);

	FRDGTextureUAVRef CreateUAV(FRDGTextureRef Texture)
	{
		return CreateUAV(FRDGTextureUAVDesc(Texture));
	}

	/** Create graph tracked UAV for a buffer from a descriptor. */
	FRDGBufferUAVRef CreateUAV(const FRDGBufferUAVDesc& Desc);

	FRDGBufferUAVRef CreateUAV(FRDGBufferRef Buffer, EPixelFormat Format)
	{
		return CreateUAV(FRDGBufferUAVDesc(Buffer, Format));
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
	 * TODO(RDG): Verify with hashing.
	 */
	template<typename ParameterStructType, typename ExecuteLambdaType>
	void AddPass(
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
		AddPassInternal(NewPass);
	}

#if WITH_MGPU
	void SetNameForTemporalEffect(FName InNameForTemporalEffect)
	{
		NameForTemporalEffect = InNameForTemporalEffect;
	}
#endif

	/** Queue a texture extraction. This will set *OutTexturePtr with the internal pooled render target at the Execute().
	 *
	 *  Note: even when the render graph uses the immediate debugging mode (executing passes as they get added), the texture extractions
	 *  will still happen in the Execute(), to ensure there is no bug caused in code outside the render graph on whether this mode is used or not.
	 */
	void QueueTextureExtraction(FRDGTextureRef Texture, TRefCountPtr<IPooledRenderTarget>* OutTexturePtr, bool bTransitionToRead = true);

	/** Queue a buffer extraction. This will set *OutBufferPtr with the internal pooled buffer at the Execute().
	 *
	 *  Note: even when the render graph uses the immediate debugging mode (executing passes as they get added), the buffer extractions
	 *  will still happen in the Execute(), to ensure there is no bug caused in code outside the render graph on whether this mode is used or not.
	 */
	void QueueBufferExtraction(FRDGBufferRef Buffer, TRefCountPtr<FPooledRDGBuffer>* OutBufferPtr, FRDGResourceState::EAccess DestinationAccess, FRDGResourceState::EPipeline DestinationPipeline);

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
	/** Array of all pass created */
	TArray<FRDGPass*, SceneRenderingAllocator> Passes;

	/** Keep the references over the pooled render target, since FRDGTexture is allocated on MemStack. */
	TMap<FRDGTexture*, TRefCountPtr<IPooledRenderTarget>, SceneRenderingSetAllocator> AllocatedTextures;

	/** Keep the references over the pooled render target, since FRDGTexture is allocated on MemStack. */
	TMap<FRDGBuffer*, TRefCountPtr<FPooledRDGBuffer>, SceneRenderingSetAllocator> AllocatedBuffers;

	/** Tracks external resources to their registered render graph counterparts for de-duplication. */
	TMap<const IPooledRenderTarget*, FRDGTexture*, SceneRenderingSetAllocator> ExternalTextures;
	TMap<const FPooledRDGBuffer*, FRDGBuffer*, SceneRenderingSetAllocator> ExternalBuffers;

	/** Array of all deferred access to internal textures. */
	struct FDeferredInternalTextureQuery
	{
		FRDGTexture* Texture;
		TRefCountPtr<IPooledRenderTarget>* OutTexturePtr;
		bool bTransitionToRead;
	};
	TArray<FDeferredInternalTextureQuery, SceneRenderingAllocator> DeferredInternalTextureQueries;

	/** Array of all deferred access to internal buffers. */
	struct FDeferredInternalBufferQuery
	{
		FRDGBuffer* Buffer;
		TRefCountPtr<FPooledRDGBuffer>* OutBufferPtr;
		FRDGResourceState::EAccess DestinationAccess;
		FRDGResourceState::EPipeline DestinationPipeline;
	};
	TArray<FDeferredInternalBufferQuery, SceneRenderingAllocator> DeferredInternalBufferQueries;

	FRDGEventScopeStack EventScopeStack;
	FRDGStatScopeStack StatScopeStack;

#if RDG_ENABLE_DEBUG
	FRDGUserValidation Validation;

	/** Tracks whether we are in a scope of adding passes to the builder. Used to avoid recursion. */
	bool bInDebugPassScope = false;
#endif

#if WITH_MGPU
	/** Name for the temporal effect used to synchronize multi-frame resources. */
	FName NameForTemporalEffect;
#endif

	void VisualizePassOutputs(const FRDGPass* Pass);
	void ClobberPassOutputs(const FRDGPass* Pass);

	void WalkGraphDependencies();
	
	template<class Type, class ...ConstructorParameterTypes>
	Type* AllocateForRHILifeTime(ConstructorParameterTypes&&... ConstructorParameters);

	void AddPassInternal(FRDGPass* Pass);

	void AllocateRHITextureIfNeeded(FRDGTexture* Texture);
	void AllocateRHITextureSRVIfNeeded(FRDGTextureSRV* SRV);
	void AllocateRHITextureUAVIfNeeded(FRDGTextureUAV* UAV);
	void AllocateRHIBufferIfNeeded(FRDGBuffer* Buffer);
	void AllocateRHIBufferSRVIfNeeded(FRDGBufferSRV* SRV);
	void AllocateRHIBufferUAVIfNeeded(FRDGBufferUAV* UAV);

	void ExecutePass(const FRDGPass* Pass);
	void PrepareResourcesForExecute(const FRDGPass* Pass, struct FRHIRenderPassInfo* OutRPInfo, bool* bOutHasGraphicsOutputs);

	void ReleaseRHITextureIfUnreferenced(FRDGTexture* Texture);
	void ReleaseRHIBufferIfUnreferenced(FRDGBuffer* Buffer);
	void ReleaseUnreferencedResources(const FRDGPass* Pass);

	void ProcessDeferredInternalResourceQueries();
	void DestructPasses();
};

#include "RenderGraphBuilder.inl"