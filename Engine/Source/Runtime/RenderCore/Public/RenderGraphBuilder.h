// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "RenderGraphEvent.h"
#include "RenderGraphResources.h"
#include "ShaderParameterMacros.h"

/** Flags to annotate passes. */
enum class ERDGPassFlags
{
	None = 0,

	/** Pass uses compute only */
	Compute = 1 << 0,

	//#todo-rco: Remove this when we can do split/per mip layout transitions.
	/** Hint to some RHIs this pass will be generating mips to optimize transitions. */
	GenerateMips = 1 << 1,
};

ENUM_CLASS_FLAGS(ERDGPassFlags)

// TODO(RDG): Bulk rename across codebase.
using ERenderGraphPassFlags = ERDGPassFlags;

// TODO(RDG): remove from global scope?
struct RENDERCORE_API FShaderParameterStructRef
{
	const void*						Contents;
	const FRHIUniformBufferLayout*	Layout;

	template<typename MemberType>
	MemberType* GetMemberPtrAtOffset(uint16 Offset) const
	{
		return reinterpret_cast<MemberType*>(((uint8*)Contents) + Offset);
	}
};

/** 
 * Base class of a render graph pass
 */
class RENDERCORE_API FRDGPass
{
public:
	FRDGPass(
		FRDGEventName&& InName,
		FShaderParameterStructRef InParameterStruct,
		ERDGPassFlags InPassFlags)
		: Name(static_cast<FRDGEventName&&>(InName))
		, ParameterStruct(InParameterStruct)
		, PassFlags(InPassFlags)
	{
		if (IsCompute())
		{
			ensureMsgf(ParameterStruct.Layout->NumRenderTargets() == 0, TEXT("Pass %s was declared as ERDGPassFlags::Compute yet has RenderTargets in its ResourceTable"), GetName());
		}
	}

	virtual ~FRDGPass() {}

	virtual void Execute(FRHICommandListImmediate& RHICmdList) const = 0;

	const TCHAR* GetName() const
	{
		return Name.GetTCHAR();
	}

	ERDGPassFlags GetFlags() const
	{
		return PassFlags;
	}

	bool IsCompute() const
	{
		return (PassFlags & ERDGPassFlags::Compute) == ERDGPassFlags::Compute;
	}
	
	FShaderParameterStructRef GetParameters() const
	{
		return ParameterStruct;
	}

	const FRDGEventScope* GetParentScope() const
	{
		return ParentScope;
	}

protected:
	const FRDGEventName Name;
	const FRDGEventScope* ParentScope = nullptr;
	const FShaderParameterStructRef ParameterStruct;
	const ERDGPassFlags PassFlags;

	friend class FRDGBuilder;
};

/** 
 * Render graph pass with lambda execute function
 */
template <typename ParameterStructType, typename ExecuteLambdaType>
struct TRDGLambdaPass final : public FRDGPass
{
	TRDGLambdaPass(
		FRDGEventName&& InName,
		FShaderParameterStructRef InParameterStruct,
		ERDGPassFlags InPassFlags,
		ExecuteLambdaType&& InExecuteLambda)
		: FRDGPass(static_cast<FRDGEventName&&>(InName), InParameterStruct, InPassFlags)
		, ExecuteLambda(static_cast<ExecuteLambdaType&&>(InExecuteLambda))
	{ }

	~TRDGLambdaPass()
	{
		// Manually call the destructor of the pass parameter, to make sure RHI references are released since the pass parameters are allocated on FMemStack.
		// TODO(RDG): this may lead to RHI resource leaks if a struct allocated in FMemStack does not actually get used through FRDGBuilder::AddPass().
		ParameterStructType* Struct = reinterpret_cast<ParameterStructType*>(const_cast<void*>(FRDGPass::ParameterStruct.Contents));
		Struct->~ParameterStructType();
	}

	virtual void Execute(FRHICommandListImmediate& RHICmdList) const override
	{
		ExecuteLambda(RHICmdList);
	}

	ExecuteLambdaType ExecuteLambda;
};

/** 
 * Builds the per-frame render graph.
 * Resources must be created from the builder before they can be bound to Pass ResourceTables.
 * These resources are descriptors only until the graph is executed, where RHI resources are allocated as needed.
 */
class RENDERCORE_API FRDGBuilder
{
public:
	/** A RHI cmd list is required, if using the immediate mode. */
	FRDGBuilder(FRHICommandListImmediate& InRHICmdList)
		: RHICmdList(InRHICmdList)
		, MemStack(FMemStack::Get())
		, EventScopeStack(RHICmdList)
	{}

	~FRDGBuilder();

	/** Register a external texture to be tracked by the render graph. */
	FORCEINLINE_DEBUGGABLE FRDGTextureRef RegisterExternalTexture(const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture, const TCHAR* DebugName = TEXT("External"))
	{
		#if RDG_ENABLE_DEBUG
		{
			ensureMsgf(ExternalPooledTexture.IsValid(), TEXT("Attempted to register NULL external texture: %s"), DebugName);
			checkf(DebugName, TEXT("Externally allocated texture requires a debug name when registering them to render graph."));
		}
		#endif
		FRDGTexture* OutTexture = AllocateForRHILifeTime<FRDGTexture>(DebugName, ExternalPooledTexture->GetDesc(), ERDGResourceFlags::None);
		OutTexture->PooledRenderTarget = ExternalPooledTexture;
		OutTexture->CachedRHI.Texture = ExternalPooledTexture->GetRenderTargetItem().ShaderResourceTexture;
		AllocatedTextures.Add(OutTexture, ExternalPooledTexture);
		#if RDG_ENABLE_DEBUG
		{
			OutTexture->bHasEverBeenProduced = true;
			Resources.Add(OutTexture);
		}
		#endif
		return OutTexture;
	}

	/** Register a external buffer to be tracked by the render graph. */
	FORCEINLINE_DEBUGGABLE FRDGBufferRef RegisterExternalBuffer(const TRefCountPtr<FPooledRDGBuffer>& ExternalPooledBuffer, const TCHAR* Name = TEXT("External"))
	{
#if RDG_ENABLE_DEBUG
		{
			ensureMsgf(ExternalPooledBuffer.IsValid(), TEXT("Attempted to register NULL external buffer: %s"), Name);
		}
#endif
		FRDGBuffer* OutBuffer = AllocateForRHILifeTime<FRDGBuffer>(Name, ExternalPooledBuffer->Desc, ERDGResourceFlags::None);
		OutBuffer->PooledBuffer = ExternalPooledBuffer;
		AllocatedBuffers.Add(OutBuffer, ExternalPooledBuffer);
#if RDG_ENABLE_DEBUG
		{
			OutBuffer->bHasEverBeenProduced = true;
			Resources.Add(OutBuffer);
		}
#endif
		return OutBuffer;
	}


	/** Create graph tracked resource from a descriptor with a debug name.
	 *
	 * The debug name is the name used for GPU debugging tools, but also for the VisualizeTexture/Vis command.
	 */
	FORCEINLINE_DEBUGGABLE FRDGTextureRef CreateTexture(const FPooledRenderTargetDesc& Desc, const TCHAR* DebugName, ERDGResourceFlags Flags = ERDGResourceFlags::None)
	{
		#if RDG_ENABLE_DEBUG
		{
			ensureMsgf(!bHasExecuted, TEXT("Render graph texture %s needs to be created before the builder execution."), DebugName);
			checkf(DebugName, TEXT("Creating a render graph texture requires a valid debug name."));
			checkf(Desc.Format != PF_Unknown, TEXT("Illegal to create texture %s with an invalid pixel format."), DebugName);
		}
		#endif
		FRDGTexture* Texture = AllocateForRHILifeTime<FRDGTexture>(DebugName, Desc, Flags);
		#if RDG_ENABLE_DEBUG
			Resources.Add(Texture);
		#endif
		return Texture;
	}

	/** Create graph tracked resource from a descriptor with a debug name.
	 *
	 * The debug name is the name used for GPU debugging tools, but also for the VisualizeTexture/Vis command.
	 */
	FORCEINLINE_DEBUGGABLE FRDGBufferRef CreateBuffer(const FRDGBufferDesc& Desc, const TCHAR* DebugName, ERDGResourceFlags Flags = ERDGResourceFlags::None)
	{
		#if RDG_ENABLE_DEBUG
		{
			ensureMsgf(!bHasExecuted, TEXT("Render graph buffer %s needs to be created before the builder execution."), DebugName);
			checkf(DebugName, TEXT("Creating a render graph buffer requires a valid debug name."));
		}
		#endif
		FRDGBuffer* Buffer = AllocateForRHILifeTime<FRDGBuffer>(DebugName, Desc, Flags);
		#if RDG_ENABLE_DEBUG
			Resources.Add(Buffer);
		#endif
		return Buffer;
	}

	/** Create graph tracked SRV for a texture from a descriptor. */
	FORCEINLINE_DEBUGGABLE FRDGTextureSRVRef CreateSRV(const FRDGTextureSRVDesc& Desc)
	{
		check(Desc.Texture);
		#if RDG_ENABLE_DEBUG
		{
			ensureMsgf(!bHasExecuted, TEXT("Render graph SRV %s needs to be created before the builder execution."), Desc.Texture->Name);
			ensureMsgf(Desc.Texture->Desc.TargetableFlags & TexCreate_ShaderResource, TEXT("Attempted to create SRV from texture %s which was not created with TexCreate_ShaderResource"), Desc.Texture->Name);
		}
		#endif
		
		FRDGTextureSRV* SRV = AllocateForRHILifeTime<FRDGTextureSRV>(Desc.Texture->Name, Desc);
		#if RDG_ENABLE_DEBUG
			Resources.Add(SRV);
		#endif
		return SRV;
	}

	/** Create graph tracked SRV for a buffer from a descriptor. */
	FORCEINLINE_DEBUGGABLE FRDGBufferSRVRef CreateSRV(const FRDGBufferSRVDesc& Desc)
	{
		check(Desc.Buffer);
		#if RDG_ENABLE_DEBUG
		{
			ensureMsgf(!bHasExecuted, TEXT("Render graph SRV %s needs to be created before the builder execution."), Desc.Buffer->Name);
		}
		#endif
		
		FRDGBufferSRV* SRV = AllocateForRHILifeTime<FRDGBufferSRV>(Desc.Buffer->Name, Desc);
		#if RDG_ENABLE_DEBUG
			Resources.Add(SRV);
		#endif
		return SRV;
	}

	FORCEINLINE_DEBUGGABLE FRDGBufferSRVRef CreateSRV(FRDGBufferRef Buffer, EPixelFormat Format)
	{
		return CreateSRV(FRDGBufferSRVDesc(Buffer, Format));
	}

	/** Create graph tracked UAV for a texture from a descriptor. */
	FORCEINLINE_DEBUGGABLE FRDGTextureUAVRef CreateUAV(const FRDGTextureUAVDesc& Desc)
	{
		check(Desc.Texture);
		#if RDG_ENABLE_DEBUG
		{
			ensureMsgf(!bHasExecuted, TEXT("Render graph UAV %s needs to be created before the builder execution."), Desc.Texture->Name);
			ensureMsgf(Desc.Texture->Desc.TargetableFlags & TexCreate_UAV, TEXT("Attempted to create UAV from texture %s which was not created with TexCreate_UAV"), Desc.Texture->Name);
		}
		#endif
		
		FRDGTextureUAV* UAV = AllocateForRHILifeTime<FRDGTextureUAV>(Desc.Texture->Name, Desc);
		#if RDG_ENABLE_DEBUG
			Resources.Add(UAV);
		#endif
		return UAV;
	}

	FORCEINLINE_DEBUGGABLE FRDGTextureUAVRef CreateUAV(FRDGTextureRef Texture)
	{
		return CreateUAV( FRDGTextureUAVDesc(Texture) );
	}

	/** Create graph tracked UAV for a buffer from a descriptor. */
	FORCEINLINE_DEBUGGABLE FRDGBufferUAVRef CreateUAV(const FRDGBufferUAVDesc& Desc)
	{
		check(Desc.Buffer);
		#if RDG_ENABLE_DEBUG
		{
			ensureMsgf(!bHasExecuted, TEXT("Render graph UAV %s needs to be created before the builder execution."), Desc.Buffer->Name);
		}
		#endif
		
		FRDGBufferUAV* UAV = AllocateForRHILifeTime<FRDGBufferUAV>(Desc.Buffer->Name, Desc);
		#if RDG_ENABLE_DEBUG
			Resources.Add(UAV);
		#endif
		return UAV;
	}

	FORCEINLINE_DEBUGGABLE FRDGBufferUAVRef CreateUAV(FRDGBufferRef Buffer, EPixelFormat Format)
	{
		return CreateUAV( FRDGBufferUAVDesc(Buffer, Format) );
	}

	/** Allocates parameter struct specifically to survive through the life time of the render graph. */
	template< typename ParameterStructType >
	FORCEINLINE_DEBUGGABLE ParameterStructType* AllocParameters()
	{
		// TODO(RDG): could allocate using AllocateForRHILifeTime() to avoid the copy done when using FRHICommandList::BuildLocalUniformBuffer()
		ParameterStructType* OutParameterPtr = new(MemStack) ParameterStructType;
		FMemory::Memzero(OutParameterPtr, sizeof(ParameterStructType));
		#if RDG_ENABLE_DEBUG
		{
			AllocatedUnusedPassParameters.Add(static_cast<void *>(OutParameterPtr));
		}
		#endif
		return OutParameterPtr;
	}

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
		ExecuteLambdaType&& ExecuteLambda)
	{
		auto NewPass = new(MemStack) TRDGLambdaPass<ParameterStructType, ExecuteLambdaType>(
			static_cast<FRDGEventName&&>(Name),
			{ ParameterStruct, &ParameterStructType::FTypeInfo::GetStructMetadata()->GetLayout() },
			Flags,
			static_cast<ExecuteLambdaType&&>(ExecuteLambda) );

		AddPassInternal(NewPass);
	}

	/** Adds a procedurally created pass to the render graph.
	 *
	 * Note: You want to use this only when the layout of the pass might be procedurally generated from data driven, as opose to AddPass() that have,
	 * constant hard coded pass layout.
	 *
	 * Caution: You are on your own to have correct memory lifetime of the FRDGPass.
	 */
	FORCEINLINE_DEBUGGABLE void AddProcedurallyCreatedPass(FRDGPass* NewPass)
	{
		AddPassInternal(NewPass);
	}

	/** Queue a texture extraction. This will set *OutTexturePtr with the internal pooled render target at the Execute().
	 *
	 * Note: even when the render graph uses the immediate debugging mode (executing passes as they get added), the texture extractions
	 * will still happen in the Execute(), to ensure there is no bug caused in code outside the render graph on whether this mode is used or not.
	 */
	FORCEINLINE_DEBUGGABLE void QueueTextureExtraction(FRDGTextureRef Texture, TRefCountPtr<IPooledRenderTarget>* OutTexturePtr, bool bTransitionToRead = true)
	{
		check(Texture);
		check(OutTexturePtr);
		#if RDG_ENABLE_DEBUG
		{
			checkf(!bHasExecuted,
				TEXT("Accessing render graph internal texture %s with QueueTextureExtraction() needs to happen before the builder's execution."),
				Texture->Name);

			checkf(Texture->bHasEverBeenProduced,
				TEXT("Unable to queue the extraction of the texture %s because it has not been produced by any pass."),
				Texture->Name);
		}
		#endif
		FDeferredInternalTextureQuery Query;
		Query.Texture = Texture;
		Query.OutTexturePtr = OutTexturePtr;
		Query.bTransitionToRead = bTransitionToRead;
		DeferredInternalTextureQueries.Emplace(Query);
	}

	/** Queue a buffer extraction. This will set *OutBufferPtr with the internal pooled buffer at the Execute().
	 *
	 * Note: even when the render graph uses the immediate debugging mode (executing passes as they get added), the buffer extractions
	 * will still happen in the Execute(), to ensure there is no bug caused in code outside the render graph on whether this mode is used or not.
	 */
	FORCEINLINE_DEBUGGABLE void QueueBufferExtraction(FRDGBufferRef Buffer, TRefCountPtr<FPooledRDGBuffer>* OutBufferPtr)
	{
		check(Buffer);
		check(OutBufferPtr);
#if RDG_ENABLE_DEBUG
		{
			checkf(!bHasExecuted,
				TEXT("Accessing render graph internal buffer %s with QueueBufferExtraction() needs to happen before the builder's execution."),
				Buffer->Name);

			checkf(Buffer->bHasEverBeenProduced,
				TEXT("Unable to queue the extraction of the buffer %s because it has not been produced by any pass."),
				Buffer->Name);
		}
#endif
		FDeferredInternalBufferQuery Query;
		Query.Buffer = Buffer;
		Query.OutBufferPtr = OutBufferPtr;
		DeferredInternalBufferQueries.Emplace(Query);
	}

	/** Flag a texture that is only produced by only 1 pass, but never used or extracted, to avoid generating a warning at runtime. */
	FORCEINLINE_DEBUGGABLE void RemoveUnusedTextureWarning(FRDGTextureRef Texture)
	{
		check(Texture);
		#if RDG_ENABLE_DEBUG
		{
			checkf(!bHasExecuted,
				TEXT("Flaging texture %s with FlagUnusedTexture() needs to happen before the builder's execution."),
				Texture->Name);
			
			// Increment the number of time the texture has been accessed to avoid warning on produced but never used resources that were produced
			// only to be extracted for the graph.
			Texture->DebugPassAccessCount += 1;
		}
		#endif
	}

	/** 
	 * Executes the queued passes, managing setting of render targets (RHI RenderPasses), resource transitions and queued texture extraction.
	 */
	void Execute();

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
	};
	TArray<FDeferredInternalBufferQuery, SceneRenderingAllocator> DeferredInternalBufferQueries;

	FRDGEventScopeStack EventScopeStack;

	#if RDG_ENABLE_DEBUG
		/** Whether the Execute() has already been called. */
		bool bHasExecuted = false;

		/** Lists of all created resources */
		TArray<const FRDGResource*, SceneRenderingAllocator> Resources;

		// All recently allocated pass parameter structure, but not used by a AddPass() yet.
		TSet<const void*> AllocatedUnusedPassParameters;
	#endif

	void DebugPass(const FRDGPass* Pass);
	void ValidatePass(const FRDGPass* Pass) const;
	void CaptureAnyInterestingPassOutput(const FRDGPass* Pass);

	void WalkGraphDependencies();
	
	template<class Type, class ...ConstructorParameterTypes>
	Type* AllocateForRHILifeTime(ConstructorParameterTypes&&... ConstructorParameters)
	{
		check(IsInRenderingThread());
		// When bypassing the RHI command queuing, can allocate directly on render thread memory stack allocator, otherwise allocate
		// on the RHI's stack allocator so RHICreateUniformBuffer() can dereference render graph resources.
		if (RHICmdList.Bypass() || 1) // TODO: UE-68018
		{
			return new (MemStack) Type(Forward<ConstructorParameterTypes>(ConstructorParameters)...);
		}
		else
		{
			void* UnitializedType = RHICmdList.Alloc<Type>();
			return new (UnitializedType) Type(Forward<ConstructorParameterTypes>(ConstructorParameters)...);
		}
	}

	void AddPassInternal(FRDGPass* Pass);

	void AllocateRHITextureIfNeeded(FRDGTexture* Texture, bool bComputePass);
	void AllocateRHITextureUAVIfNeeded(FRDGTextureUAV* UAV, bool bComputePass);
	void AllocateRHIBufferSRVIfNeeded(FRDGBufferSRV* SRV, bool bComputePass);
	void AllocateRHIBufferUAVIfNeeded(FRDGBufferUAV* UAV, bool bComputePass);

	void BeginEventScope(FRDGEventName&& ScopeName);
	void EndEventScope();

	void TransitionTexture(FRDGTexture* Texture, EResourceTransitionAccess TransitionAccess, bool bRequiredCompute) const;
	void TransitionUAV(FUnorderedAccessViewRHIParamRef UAV, FRDGResource* UnderlyingResource, ERDGResourceFlags ResourceFlags, EResourceTransitionAccess TransitionAccess, bool bRequiredCompute ) const;

	void PushDrawEventStack(const FRDGPass* Pass);
	void ExecutePass(const FRDGPass* Pass);
	void AllocateAndTransitionPassResources(const FRDGPass* Pass, struct FRHIRenderPassInfo* OutRPInfo, bool* bOutHasRenderTargets);
	static void UpdateAccessGuardForPassResources(const FRDGPass* Pass, bool bAllowAccess);
	static void WarnForUselessPassDependencies(const FRDGPass* Pass);

	void ReleaseRHITextureIfPossible(FRDGTexture* Texture);
	void ReleaseRHIBufferIfPossible(FRDGBuffer* Buffer);
	void ReleaseUnecessaryResources(const FRDGPass* Pass);

	void ProcessDeferredInternalResourceQueries();
	void DestructPasses();

	friend class FRDGEventScopeGuard;

	/** To allow greater flexibility in the user code, the RHI can dereferenced RDG resource when creating uniform buffer. */
	// TODO(RDG): Make this a little more explicit in RHI code.
	static_assert(STRUCT_OFFSET(FRDGResource, CachedRHI) == 0, "FRDGResource::CachedRHI requires to be at offset 0 so the RHI can dereferenced them.");
};