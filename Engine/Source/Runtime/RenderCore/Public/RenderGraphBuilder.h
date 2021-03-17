// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "RenderGraphEvent.h"
#include "RenderGraphResources.h"
#include "RenderGraphPass.h"
#include "RenderGraphTrace.h"
#include "RenderGraphValidation.h"
#include "RenderGraphBlackboard.h"
#include "ShaderParameterMacros.h"
#include "ProfilingDebugging/CsvProfiler.h"

class FRDGLogFile;

/** Use the render graph builder to build up a graph of passes and then call Execute() to process them. Resource barriers
 *  and lifetimes are derived from _RDG_ parameters in the pass parameter struct provided to each AddPass call. The resulting
 *  graph is compiled, culled, and executed in Execute(). The builder should be created on the stack and executed prior to
 *  destruction.
 */
class RENDERCORE_API FRDGBuilder
	: FRDGAllocatorScope
{
public:
	FRDGBuilder(FRHICommandListImmediate& InRHICmdList, FRDGEventName InName = {});
	FRDGBuilder(const FRDGBuilder&) = delete;

	/** Finds an RDG texture associated with the external texture, or returns null if none is found. */
	FRDGTextureRef FindExternalTexture(FRHITexture* Texture) const;
	FRDGTextureRef FindExternalTexture(IPooledRenderTarget* ExternalPooledTexture, ERenderTargetTexture Texture) const;

	/** Registers a external pooled render target texture to be tracked by the render graph. The pooled render target may contain two RHI
	 *  textures--one MSAA and one non-MSAA resolve texture. In most cases they are both the same pointer. RDG textures are 1-to-1 with an
	 *  RHI texture, so two RDG textures must be registered at most. Use ERenderTargetTexture to select which RHI texture on the pooled
	 *  render target to register. The name of the registered RDG texture is pulled from the pooled render target.
	 */
	FRDGTextureRef RegisterExternalTexture(
		const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		ERenderTargetTexture Texture = ERenderTargetTexture::ShaderResource,
		ERDGTextureFlags Flags = ERDGTextureFlags::None);

	/** Register an external texture with a custom name. The name is only used if the texture has not already been registered. */
	FRDGTextureRef RegisterExternalTexture(
		const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		const TCHAR* NameIfNotRegistered,
		ERenderTargetTexture RenderTargetTexture = ERenderTargetTexture::ShaderResource,
		ERDGTextureFlags Flags = ERDGTextureFlags::None);

	/** Register a external buffer to be tracked by the render graph. */
	FRDGBufferRef RegisterExternalBuffer(const TRefCountPtr<FRDGPooledBuffer>& ExternalPooledBuffer, ERDGBufferFlags Flags = ERDGBufferFlags::None);
	FRDGBufferRef RegisterExternalBuffer(const TRefCountPtr<FRDGPooledBuffer>& ExternalPooledBuffer, ERDGBufferFlags Flags, ERHIAccess AccessFinal);

	/** Register an external buffer with a custom name. The name is only used if the buffer has not already been registered. */
	FRDGBufferRef RegisterExternalBuffer(
		const TRefCountPtr<FRDGPooledBuffer>& ExternalPooledBuffer,
		const TCHAR* NameIfNotRegistered,
		ERDGBufferFlags Flags = ERDGBufferFlags::None);

	/** Create graph tracked texture from a descriptor. The CPU memory is guaranteed to be valid through execution of
	 *  the graph, at which point it is released. The underlying RHI texture lifetime is only guaranteed for passes which
	 *  declare the texture in the pass parameter struct. The name is the name used for GPU debugging tools and the the
	 *  VisualizeTexture/Vis command.
	 */
	FRDGTextureRef CreateTexture(const FRDGTextureDesc& Desc, const TCHAR* Name, ERDGTextureFlags Flags = ERDGTextureFlags::None);

	/** Create graph tracked buffer from a descriptor. The CPU memory is guaranteed to be valid through execution of
	 *  the graph, at which point it is released. The underlying RHI buffer lifetime is only guaranteed for passes which
	 *  declare the buffer in the pass parameter struct. The name is the name used for GPU debugging tools.
	 */
	FRDGBufferRef CreateBuffer(const FRDGBufferDesc& Desc, const TCHAR* Name, ERDGBufferFlags Flags = ERDGBufferFlags::None);

	/** Create graph tracked SRV for a texture from a descriptor. */
	FRDGTextureSRVRef CreateSRV(const FRDGTextureSRVDesc& Desc);

	/** Create graph tracked SRV for a buffer from a descriptor. */
	FRDGBufferSRVRef CreateSRV(const FRDGBufferSRVDesc& Desc);

	FORCEINLINE FRDGBufferSRVRef CreateSRV(FRDGBufferRef Buffer, EPixelFormat Format)
	{
		return CreateSRV(FRDGBufferSRVDesc(Buffer, Format));
	}

	/** Create graph tracked UAV for a texture from a descriptor. */
	FRDGTextureUAVRef CreateUAV(const FRDGTextureUAVDesc& Desc, ERDGUnorderedAccessViewFlags Flags = ERDGUnorderedAccessViewFlags::None);

	FORCEINLINE FRDGTextureUAVRef CreateUAV(FRDGTextureRef Texture, ERDGUnorderedAccessViewFlags Flags = ERDGUnorderedAccessViewFlags::None)
	{
		return CreateUAV(FRDGTextureUAVDesc(Texture), Flags);
	}

	/** Create graph tracked UAV for a buffer from a descriptor. */
	FRDGBufferUAVRef CreateUAV(const FRDGBufferUAVDesc& Desc, ERDGUnorderedAccessViewFlags Flags = ERDGUnorderedAccessViewFlags::None);

	FORCEINLINE FRDGBufferUAVRef CreateUAV(FRDGBufferRef Buffer, EPixelFormat Format, ERDGUnorderedAccessViewFlags Flags = ERDGUnorderedAccessViewFlags::None)
	{
		return CreateUAV(FRDGBufferUAVDesc(Buffer, Format), Flags);
	}

	/** Creates a graph tracked uniform buffer which can be attached to passes. These uniform buffers require some care
	 *  because they will bulk transition all resources. The graph will only transition resources which are not also
	 *  bound for write access by the pass.
	 */
	template <typename ParameterStructType>
	TRDGUniformBufferRef<ParameterStructType> CreateUniformBuffer(const ParameterStructType* ParameterStruct);

	//////////////////////////////////////////////////////////////////////////
	// Allocation Methods

	/** Allocates raw memory using an allocator tied to the lifetime of the graph. */
	void* Alloc(uint32 SizeInBytes, uint32 AlignInBytes);

	/** Allocates POD memory using an allocator tied to the lifetime of the graph. Does not construct / destruct. */
	template <typename PODType>
	PODType* AllocPOD();

	/** Allocates a C++ object using an allocator tied to the lifetime of the graph. Will destruct the object. */
	template <typename ObjectType, typename... TArgs>
	ObjectType* AllocObject(TArgs&&... Args);

	/** Allocates a parameter struct with a lifetime tied to graph execution. */
	template <typename ParameterStructType>
	ParameterStructType* AllocParameters();

	//////////////////////////////////////////////////////////////////////////

	/** Adds a lambda pass to the graph with an accompanied pass parameter struct.
	 *
	 *  RDG resources declared in the struct (via _RDG parameter macros) are safe to access in the lambda. The pass parameter struct
	 *  should be allocated by AllocParameters(), and once passed in, should not be mutated. It is safe to provide the same parameter
	 *  struct to multiple passes, so long as it is kept immutable. The lambda is deferred until execution unless the immediate debug
	 *  mode is enabled. All lambda captures should assume deferral of execution.
	 *
	 *  The lambda must include a single RHI command list as its parameter. The exact type of command list depends on the workload.
	 *  For example, use FRHIComputeCommandList& for Compute / AsyncCompute workloads. Raster passes should use FRHICommandList&.
	 *  Prefer not to use FRHICommandListImmediate& unless actually required.
	 *
	 *  Declare the type of GPU workload (i.e. Copy, Compute / AsyncCompute, Graphics) to the pass via the Flags argument. This is
	 *  used to determine async compute regions, render pass setup / merging, RHI transition accesses, etc. Other flags exist for
	 *  specialized purposes, like forcing a pass to never be culled (NeverCull). See ERDGPassFlags for more info.
	 *
	 *  The pass name is used by debugging / profiling tools.
	 */
	template <typename ParameterStructType, typename ExecuteLambdaType>
	FRDGPassRef AddPass(FRDGEventName&& Name, const ParameterStructType* ParameterStruct, ERDGPassFlags Flags, ExecuteLambdaType&& ExecuteLambda);

	/** Adds a lambda pass to the graph with a runtime-generated parameter struct. */
	template <typename ExecuteLambdaType>
	FRDGPassRef AddPass(FRDGEventName&& Name, const FShaderParametersMetadata* ParametersMetadata, const void* ParameterStruct, ERDGPassFlags Flags, ExecuteLambdaType&& ExecuteLambda);

	/** Adds a lambda pass to the graph without any parameters. This useful for deferring RHI work onto the graph timeline,
	 *  or incrementally porting code to use the graph system. NeverCull and SkipRenderPass (if Raster) are implicitly added
	 *  to Flags. AsyncCompute is not allowed. It is never permitted to access a created (i.e. not externally registered) RDG
	 *  resource outside of passes it is registered with, as the RHI lifetime is not guaranteed.
	 */
	template <typename ExecuteLambdaType>
	FRDGPassRef AddPass(FRDGEventName&& Name, ERDGPassFlags Flags, ExecuteLambdaType&& ExecuteLambda);

#if WITH_MGPU
	void SetNameForTemporalEffect(FName InNameForTemporalEffect)
	{
		NameForTemporalEffect = InNameForTemporalEffect;
	}
#endif

	/** Sets the current command list stat for all subsequent passes. */
	void SetCommandListStat(TStatId StatId);

	/** Queues a pooled render target extraction to happen at the end of graph execution. For graph-created textures, this extends
	 *  the lifetime of the GPU resource until execution, at which point the pointer is filled. If specified, the texture is transitioned
	 *  to the AccessFinal state, or kDefaultAccessFinal otherwise.
	 */
	void QueueTextureExtraction(FRDGTextureRef Texture, TRefCountPtr<IPooledRenderTarget>* OutPooledTexturePtr);
	void QueueTextureExtraction(FRDGTextureRef Texture, TRefCountPtr<IPooledRenderTarget>* OutPooledTexturePtr, ERHIAccess AccessFinal);

	/** Queues a pooled buffer extraction to happen at the end of graph execution. For graph-created buffers, this extends the lifetime
	 *  of the GPU resource until execution, at which point the pointer is filled. If specified, the buffer is transitioned to the
	 *  AccessFinal state, or kDefaultAccessFinal otherwise.
	 */
	void QueueBufferExtraction(FRDGBufferRef Buffer, TRefCountPtr<FRDGPooledBuffer>* OutPooledBufferPtr);
	void QueueBufferExtraction(FRDGBufferRef Buffer, TRefCountPtr<FRDGPooledBuffer>* OutPooledBufferPtr, ERHIAccess AccessFinal);

	/** For graph-created resources, this forces immediate allocation of the underlying pooled resource, effectively promoting it
	 *  to an external resource. This will increase memory pressure, but allows for querying the pooled resource with GetPooled{Texture, Buffer}.
	 *  This is primarily used as an aid for porting code incrementally to RDG.
	 */
	void PreallocateTexture(FRDGTextureRef Texture);
	void PreallocateBuffer(FRDGBufferRef Buffer);

	/** Performs an immediate query for the underlying pooled texture. This is only allowed for registered or preallocated textures. */
	const TRefCountPtr<IPooledRenderTarget>& GetPooledTexture(FRDGTextureRef Texture) const;

	/** Performs an immediate query for the underlying pooled buffer. This is only allowed for registered or preallocated buffers. */
	const TRefCountPtr<FRDGPooledBuffer>& GetPooledBuffer(FRDGBufferRef Buffer) const;

	/** Sets the access to transition to after execution. Only valid on external or extracted textures. Overwrites any previously set final access. */
	void SetTextureAccessFinal(FRDGTextureRef Texture, ERHIAccess Access);

	/** Sets the access to transition to after execution. Only valid on external or extracted buffers. Overwrites any previously set final access. */
	void SetBufferAccessFinal(FRDGBufferRef Buffer, ERHIAccess Access);

	/** Flag a texture that is produced by a pass but never used or extracted to not emit an 'unused' warning. */
	void RemoveUnusedTextureWarning(FRDGTextureRef Texture);

	/** Flag a buffer that is produced by a pass but never used or extracted to not emit an 'unused' warning. */
	void RemoveUnusedBufferWarning(FRDGBufferRef Buffer);

	/** Manually begins a new GPU event scope. */
	void BeginEventScope(FRDGEventName&& Name);

	/** Manually ends the current GPU event scope. */
	void EndEventScope();

	/** Executes the queued passes, managing setting of render targets (RHI RenderPasses), resource transitions and queued texture extraction. */
	void Execute();

	/** Per-frame update of the render graph resource pool. */
	static void TickPoolElements();

	/** The RHI command list used for the render graph. */
	FRHICommandListImmediate& RHICmdList;

	/** The blackboard used to hold common data tied to the graph lifetime. */
	FRDGBlackboard Blackboard;

private:
	static const ERHIAccess kDefaultAccessInitial = ERHIAccess::Unknown;
	static const ERHIAccess kDefaultAccessFinal = ERHIAccess::SRVMask;
	static const char* const kDefaultUnaccountedCSVStat;

	FRHIAsyncComputeCommandListImmediate& RHICmdListAsyncCompute;

	const FRDGEventName BuilderName;

	template <typename ParameterStructType, typename ExecuteLambdaType>
	FRDGPassRef AddPassInternal(
		FRDGEventName&& Name,
		const FShaderParametersMetadata* ParametersMetadata,
		const ParameterStructType* ParameterStruct,
		ERDGPassFlags Flags,
		ExecuteLambdaType&& ExecuteLambda);

	static ERDGPassFlags OverridePassFlags(const TCHAR* PassName, ERDGPassFlags Flags, bool bAsyncComputeSupported);

	/** Returns the graph prologue pass handle. */
	FORCEINLINE FRDGPassHandle GetProloguePassHandle() const
	{
		return Passes.Begin();
	}

	/** Returns the graph epilogue pass handle. */
	FORCEINLINE FRDGPassHandle GetEpiloguePassHandle() const
	{
		checkf(EpiloguePass, TEXT("The handle is not valid until the epilogue has been added to the graph during execution."));
		return Passes.Last();
	}

	/** Barrier location controls where the barrier is 'Ended' relative to the pass lambda being executed.
	 *  Most barrier locations are done in the prologue prior to the executing lambda. But certain cases
	 *  like an aliasing discard operation need to be done *after* the pass being invoked. Therefore, when
	 *  adding a transition the user can specify where to place the barrier.
	 */
	enum class EBarrierLocation
	{
		/** The barrier occurs in the prologue of the pass (before execution). */
		Prologue,

		/** The barrier occurs in the epilogue of the pass (after execution). */
		Epilogue
	};

	/** Prologue and Epilogue barrier passes are used to plan transitions around RHI render pass merging,
	 *  as it is illegal to issue a barrier during a render pass. If passes [A, B, C] are merged together,
	 *  'A' becomes 'B's prologue pass and 'C' becomes 'A's epilogue pass. This way, any transitions that
	 *  need to happen before the merged pass (i.e. in the prologue) are done in A. Any transitions after
	 *  the render pass merge are done in C.
	 */
	FRDGPassHandle GetEpilogueBarrierPassHandle(FRDGPassHandle Handle)
	{
		return Passes[Handle]->EpilogueBarrierPass;
	}

	FRDGPassHandle GetPrologueBarrierPassHandle(FRDGPassHandle Handle)
	{
		return Passes[Handle]->PrologueBarrierPass;
	}

	FRDGPass* GetEpilogueBarrierPass(FRDGPassHandle Handle)
	{
		return Passes[GetEpilogueBarrierPassHandle(Handle)];
	}

	FRDGPass* GetPrologueBarrierPass(FRDGPassHandle Handle)
	{
		return Passes[GetPrologueBarrierPassHandle(Handle)];
	}

	/** Ends the barrier batch in the prologue of the provided pass. */
	void AddToPrologueBarriersToEnd(FRDGPassHandle Handle, FRDGBarrierBatchBegin& BarriersToBegin)
	{
		FRDGPass* Pass = GetPrologueBarrierPass(Handle);
		Pass->GetPrologueBarriersToEnd(Allocator).AddDependency(&BarriersToBegin);
	}

	/** Ends the barrier batch in the epilogue of the provided pass. */
	void AddToEpilogueBarriersToEnd(FRDGPassHandle Handle, FRDGBarrierBatchBegin& BarriersToBegin)
	{
		FRDGPass* Pass = GetEpilogueBarrierPass(Handle);
		Pass->GetEpilogueBarriersToEnd(Allocator).AddDependency(&BarriersToBegin);
	}

	/** Utility function to add an immediate barrier dependency in the prologue of the provided pass. */
	template <typename FunctionType>
	void AddToPrologueBarriers(FRDGPassHandle PassHandle, FunctionType Function)
	{
		FRDGPass* Pass = GetPrologueBarrierPass(PassHandle);
		FRDGBarrierBatchBegin& BarriersToBegin = Pass->GetPrologueBarriersToBegin(Allocator);
		Function(BarriersToBegin);
		Pass->GetPrologueBarriersToEnd(Allocator).AddDependency(&BarriersToBegin);
	}

	/** Utility function to add an immediate barrier dependency in the epilogue of the provided pass. */
	template <typename FunctionType>
	void AddToEpilogueBarriers(FRDGPassHandle PassHandle, FunctionType Function)
	{
		FRDGPass* Pass = GetEpilogueBarrierPass(PassHandle);
		FRDGBarrierBatchBegin& BarriersToBegin = Pass->GetEpilogueBarriersToBeginFor(Allocator, Pass->GetPipeline());
		Function(BarriersToBegin);
		Pass->GetEpilogueBarriersToEnd(Allocator).AddDependency(&BarriersToBegin);
	}

	/** Utility function to add an immediate barrier dependency in either the prologue or epilogue of the provided pass, depending on BarrierLocation. */
	template <typename FunctionType>
	void AddToBarriers(FRDGPassHandle PassHandle, EBarrierLocation BarrierLocation, FunctionType Function)
	{
		if (BarrierLocation == EBarrierLocation::Prologue)
		{
			AddToPrologueBarriers(PassHandle, Function);
		}
		else
		{
			AddToEpilogueBarriers(PassHandle, Function);
		}
	}

	/** Registry of graph objects. */
	FRDGPassRegistry Passes;
	FRDGTextureRegistry Textures;
	FRDGBufferRegistry Buffers;
	FRDGViewRegistry Views;
	FRDGUniformBufferRegistry UniformBuffers;

	/** Passes that have been culled from the graph. */
	FRDGPassBitArray PassesToCull;

	/** Passes that don't have any parameters. */
	FRDGPassBitArray PassesWithEmptyParameters;

	/** Tracks external resources to their registered render graph counterparts for de-duplication. */
	TSortedMap<FRHITexture*, FRDGTexture*, FRDGArrayAllocator> ExternalTextures;
	TSortedMap<const FRDGPooledBuffer*, FRDGBuffer*, FRDGArrayAllocator> ExternalBuffers;

	/** Map of barrier batches begun from more than one pipe. */
	TMap<FRDGBarrierBatchBeginId, FRDGBarrierBatchBegin*, FRDGSetAllocator> BarrierBatchMap;

	/** The epilogue and prologue passes are sentinels that are used to simplify graph logic around barriers
	 *  and traversal. The prologue pass is used exclusively for barriers before the graph executes, while the
	 *  epilogue pass is used for resource extraction barriers--a property that also makes it the main root of
	 *  the graph for culling purposes. The epilogue pass is added to the very end of the pass array for traversal
	 *  purposes. The prologue does not need to participate in any graph traversal behavior.
	 */
	FRDGPass* ProloguePass = nullptr;
	FRDGPass* EpiloguePass = nullptr;

	/** Array of all requested resource extractions from the graph. */
	TArray<TPair<FRDGTextureRef, TRefCountPtr<IPooledRenderTarget>*>, FRDGArrayAllocator> ExtractedTextures;
	TArray<TPair<FRDGBufferRef, TRefCountPtr<FRDGPooledBuffer>*>, FRDGArrayAllocator> ExtractedBuffers;

	/** Texture state used for intermediate operations. Held here to avoid re-allocating. */
	FRDGTextureTransientSubresourceStateIndirect ScratchTextureState;

	/** Current scope's async compute budget. This is passed on to every pass created. */
	EAsyncComputeBudget AsyncComputeBudgetScope = EAsyncComputeBudget::EAll_4;

	IF_RDG_CPU_SCOPES(FRDGCPUScopeStacks CPUScopeStacks);
	IF_RDG_GPU_SCOPES(FRDGGPUScopeStacksByPipeline GPUScopeStacks);

	IF_RDG_ENABLE_TRACE(FRDGTrace Trace);

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

	/** Tracks whether all passes / resources have been added to the graph. */
	bool bSetupComplete = false;

	IF_RDG_CMDLIST_STATS(TStatId CommandListStat);

	class FTransientResourceAllocator
	{
	public:
		FTransientResourceAllocator(FRHICommandListImmediate& InRHICmdList)
			: RHICmdList(InRHICmdList)
		{}

		~FTransientResourceAllocator();

		IRHITransientResourceAllocator* GetOrCreate();
		IRHITransientResourceAllocator* Get() const { return Allocator; }
		IRHITransientResourceAllocator* operator->() const { check(Allocator);  return Allocator; }
		operator bool() const { return Allocator != nullptr; }

	private:
		FRHICommandListImmediate& RHICmdList;
		IRHITransientResourceAllocator* Allocator = nullptr;
	};

	void Compile();
	void Clear();

	void BeginResourceRHI(FRDGUniformBuffer* UniformBuffer);
	void BeginResourceRHI(FTransientResourceAllocator*, FRDGPassHandle, FRDGTexture* Texture);
	void BeginResourceRHI(FTransientResourceAllocator*, FRDGPassHandle, FRDGTextureSRV* SRV);
	void BeginResourceRHI(FTransientResourceAllocator*, FRDGPassHandle, FRDGTextureUAV* UAV);
	void BeginResourceRHI(FTransientResourceAllocator*, FRDGPassHandle, FRDGBuffer* Buffer);
	void BeginResourceRHI(FTransientResourceAllocator*, FRDGPassHandle, FRDGBufferSRV* SRV);
	void BeginResourceRHI(FTransientResourceAllocator*, FRDGPassHandle, FRDGBufferUAV* UAV);

	void EndResourceRHI(FTransientResourceAllocator&, FRDGPassHandle, FRDGTexture* Texture, uint32 ReferenceCount);
	void EndResourceRHI(FTransientResourceAllocator&, FRDGPassHandle, FRDGBuffer* Buffer, uint32 ReferenceCount);

	void SetupPassInternal(FRDGPass* Pass, FRDGPassHandle PassHandle, ERHIPipeline PassPipeline);
	void SetupPass(FRDGPass* Pass);
	void SetupEmptyPass(FRDGPass* Pass);
	void ExecutePass(FRDGPass* Pass);

	void ExecutePassPrologue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass);
	void ExecutePassEpilogue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass);

	void CollectPassResources(FTransientResourceAllocator&, FRDGPassHandle PassHandle);
	void CollectPassBarriers(FRDGPassHandle PassHandle);

	void AddPassDependency(FRDGPassHandle ProducerHandle, FRDGPassHandle ConsumerHandle);

	void AddEpilogueTransition(FRDGTextureRef Texture);
	void AddEpilogueTransition(FRDGBufferRef Buffer);

	void AddTransition(
		FRDGPassHandle PassHandle,
		FRDGTextureRef Texture,
		const FRDGTextureTransientSubresourceStateIndirect& StateAfter,
		EBarrierLocation BarrierLocation = EBarrierLocation::Prologue);

	void AddTransition(
		FRDGPassHandle PassHandle,
		FRDGBufferRef Buffer,
		FRDGSubresourceState StateAfter,
		EBarrierLocation BarrierLocation = EBarrierLocation::Prologue);

	void AddTransition(
		FRDGParentResource* Resource,
		FRDGSubresourceState StateBefore,
		FRDGSubresourceState StateAfter,
		EBarrierLocation BarrierLocation,
		const FRHITransitionInfo& TransitionInfo);

	bool IsTransient(FRDGTextureRef Texture) const;
	bool IsTransient(FRDGBufferRef Buffer) const;
	bool IsTransientInternal(FRDGParentResourceRef Resource) const;

	void AddAcquireResourceRHI(FRDGTexture* Texture, FRDGPassHandle PassHandle);
	void AddAcquireResourceRHI(FRDGBuffer* Buffer, FRDGPassHandle PassHandle);
	void AddDiscardResourceRHI(FRDGTexture* Texture, FRDGPassHandle PassHandle);
	void AddDiscardResourceRHI(FRDGBuffer* Buffer, FRDGPassHandle PassHandle);

	FRHIRenderPassInfo GetRenderPassInfo(const FRDGPass* Pass) const;

	FRDGSubresourceState* AllocSubresource(const FRDGSubresourceState& Other);

#if RDG_ENABLE_DEBUG
	void VisualizePassOutputs(const FRDGPass* Pass);
	void ClobberPassOutputs(const FRDGPass* Pass);
#endif

	friend FRDGTrace;
	friend FRDGEventScopeGuard;
	friend FRDGGPUStatScopeGuard;
	friend FRDGAsyncComputeBudgetScopeGuard;
	friend FRDGScopedCsvStatExclusive;
	friend FRDGScopedCsvStatExclusiveConditional;
};

class FRDGAsyncComputeBudgetScopeGuard final
{
public:
	FRDGAsyncComputeBudgetScopeGuard(FRDGBuilder& InGraphBuilder, EAsyncComputeBudget InAsyncComputeBudget)
		: GraphBuilder(InGraphBuilder)
		, AsyncComputeBudgetRestore(GraphBuilder.AsyncComputeBudgetScope)
	{
		GraphBuilder.AsyncComputeBudgetScope = InAsyncComputeBudget;
	}

	~FRDGAsyncComputeBudgetScopeGuard()
	{
		GraphBuilder.AsyncComputeBudgetScope = AsyncComputeBudgetRestore;
	}

private:
	FRDGBuilder& GraphBuilder;
	const EAsyncComputeBudget AsyncComputeBudgetRestore;
};

#define RDG_ASYNC_COMPUTE_BUDGET_SCOPE(GraphBuilder, AsyncComputeBudget) \
	FRDGAsyncComputeBudgetScopeGuard PREPROCESSOR_JOIN(FRDGAsyncComputeBudgetScope, __LINE__)(GraphBuilder, AsyncComputeBudget)

#if WITH_MGPU
	#define RDG_GPU_MASK_SCOPE(GraphBuilder, GPUMask) SCOPED_GPU_MASK(GraphBuilder.RHICmdList, GPUMask)
#else
	#define RDG_GPU_MASK_SCOPE(GraphBuilder, GPUMask)
#endif

#include "RenderGraphBuilder.inl"