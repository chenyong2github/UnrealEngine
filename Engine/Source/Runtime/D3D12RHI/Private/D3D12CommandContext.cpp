// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12CommandContext.cpp: RHI  Command Context implementation.
=============================================================================*/

#include "D3D12RHIPrivate.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
	#include "amd_ags.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// Aggressive batching saves ~0.1ms on the RHI thread, reduces executecommandlist calls by around 25%
int32 GCommandListBatchingMode = CLB_AggressiveBatching;

extern int32 GEnableGapRecorder;
extern bool GGapRecorderActiveOnBeginFrame;

static FAutoConsoleVariableRef CVarCommandListBatchingMode(
	TEXT("D3D12.CommandListBatchingMode"),
	GCommandListBatchingMode,
	TEXT("Changes how command lists are batched and submitted to the GPU."),
	ECVF_RenderThreadSafe
);

int32 MaxCommandsPerCommandList = 10000;
static FAutoConsoleVariableRef CVarMaxCommandsPerCommandList(
	TEXT("D3D12.MaxCommandsPerCommandList"),
	MaxCommandsPerCommandList,
	TEXT("Flush command list to GPU after certain amount of enqueued commands (draw, dispatch, copy, ...) (default value 10000)"),
	ECVF_RenderThreadSafe
);

// We don't yet have a way to auto-detect that the Radeon Developer Panel is running
// with profiling enabled, so for now, we have to manually toggle this console var.
// It needs to be set before device creation, so it's read only.
int32 GEmitRgpFrameMarkers = 0;
static FAutoConsoleVariableRef CVarEmitRgpFrameMarkers(
	TEXT("D3D12.EmitRgpFrameMarkers"),
	GEmitRgpFrameMarkers,
	TEXT("Enables/Disables frame markers for AMD's RGP tool."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
	);

FD3D12CommandContextBase::FD3D12CommandContextBase(class FD3D12Adapter* InParentAdapter, FRHIGPUMask InGPUMask, bool InIsDefaultContext, bool InIsAsyncComputeContext)
	: FD3D12AdapterChild(InParentAdapter)
	, GPUMask(InGPUMask)
	, bTrackingEvents(false)
	, bIsDefaultContext(InIsDefaultContext)
	, bIsAsyncComputeContext(InIsAsyncComputeContext)
{
}

static D3D12_RESOURCE_STATES GetValidResourceStates(D3D12_COMMAND_LIST_TYPE CommandListType)
{
	// For reasons, we can't just list the allowed states, we have to list the disallowed states.
	// For reference on allowed/disallowed states, see:
	//    https://microsoft.github.io/DirectX-Specs/d3d/CPUEfficiency.html#state-support-by-command-list-type
	
	const D3D12_RESOURCE_STATES DisallowedDirectStates =
		static_cast<D3D12_RESOURCE_STATES>(0);

	const D3D12_RESOURCE_STATES DisallowedComputeStates =
		DisallowedDirectStates |
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
		D3D12_RESOURCE_STATE_INDEX_BUFFER |
		D3D12_RESOURCE_STATE_RENDER_TARGET |
		D3D12_RESOURCE_STATE_DEPTH_WRITE |
		D3D12_RESOURCE_STATE_DEPTH_READ |
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_STREAM_OUT |
		D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT |
		D3D12_RESOURCE_STATE_RESOLVE_DEST |
		D3D12_RESOURCE_STATE_RESOLVE_SOURCE;

	const D3D12_RESOURCE_STATES DisallowedCopyStates =
		DisallowedComputeStates |
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;


	if (CommandListType == D3D12_COMMAND_LIST_TYPE_COPY)
	{
		return ~DisallowedCopyStates;
	}

	if (CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
		return ~DisallowedComputeStates;
	}

	return ~DisallowedDirectStates;
}

FD3D12CommandContext::FD3D12CommandContext(FD3D12Device* InParent, bool InIsDefaultContext, bool InIsAsyncComputeContext) :
	FD3D12CommandContextBase(InParent->GetParentAdapter(), InParent->GetGPUMask(), InIsDefaultContext, InIsAsyncComputeContext),
	FD3D12DeviceChild(InParent),
	ConstantsAllocator(InParent, InParent->GetGPUMask()),
	CommandListHandle(),
	CommandAllocator(nullptr),
	CommandAllocatorManager(InParent, InIsAsyncComputeContext ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT),
	StateCache(InParent->GetGPUMask()),
	OwningRHI(*InParent->GetOwningRHI()),
	CurrentDepthStencilTarget(nullptr),
	CurrentDepthTexture(nullptr),
	NumSimultaneousRenderTargets(0),
	CurrentDSVAccessType(FExclusiveDepthStencil::DepthWrite_StencilWrite),
	bOuterOcclusionQuerySubmitted(false),
	bDiscardSharedGraphicsConstants(false),
	bDiscardSharedComputeConstants(false),
	bUsingTessellation(false),
#if PLATFORM_SUPPORTS_VARIABLE_RATE_SHADING
	VRSCombiners{ D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH } ,
	VRSShadingRate(D3D12_SHADING_RATE_1X1),
#endif
	SkipFastClearEliminateState(D3D12_RESOURCE_STATES(0)),
	ValidResourceStates(GetValidResourceStates(InIsAsyncComputeContext ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT)),
#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
	bNeedFlushTextureCache(false),
#endif
	VSConstantBuffer(InParent, ConstantsAllocator),
	HSConstantBuffer(InParent, ConstantsAllocator),
	DSConstantBuffer(InParent, ConstantsAllocator),
	PSConstantBuffer(InParent, ConstantsAllocator),
	GSConstantBuffer(InParent, ConstantsAllocator),
	CSConstantBuffer(InParent, ConstantsAllocator)
{
	FMemory::Memzero(DirtyUniformBuffers);
	FMemory::Memzero(BoundUniformBuffers);
	for (int i = 0; i < UE_ARRAY_COUNT(BoundUniformBufferRefs); i++)
	{
		for (int j = 0; j < UE_ARRAY_COUNT(BoundUniformBufferRefs[i]); j++)
		{
			BoundUniformBufferRefs[i][j] = NULL;
		}
	}
	FMemory::Memzero(CurrentRenderTargets);
	StateCache.Init(GetParentDevice(), this, nullptr);
	GlobalUniformBuffers.AddZeroed(FUniformBufferStaticSlotRegistry::Get().GetSlotCount());
}

FD3D12CommandContext::~FD3D12CommandContext()
{
	ClearState();
}


/** Write out the event stack to the bread crumb resource if available */
void FD3D12CommandContext::WriteGPUEventStackToBreadCrumbData(bool bBeginEvent)
{
	// Only in Windows for now, could be made available on Xbox as well
#if PLATFORM_WINDOWS
	// Write directly to command list if breadcrumb resource is available
	FD3D12Resource* BreadCrumbResource = CommandListHandle.GetCommandListManager()->GetBreadCrumbResource();
	ID3D12GraphicsCommandList2* CommandList2 = CommandListHandle.GraphicsCommandList2();
	if (BreadCrumbResource && CommandList2)
	{
		// Find the max parameter count from the resource
		int MaxParameterCount = BreadCrumbResource->GetDesc().Width / sizeof(uint32);

		// allocate the parameters on the stack if smaller than 4K
		int ParameterCount = GPUEventStack.Num() < (MaxParameterCount - 2) ? GPUEventStack.Num() + 2 : MaxParameterCount;
		size_t MemSize = ParameterCount * (sizeof(D3D12_WRITEBUFFERIMMEDIATE_PARAMETER) + sizeof(D3D12_WRITEBUFFERIMMEDIATE_PARAMETER));
		const bool bAllocateOnStack = (MemSize < 4096);
		void* Mem = bAllocateOnStack ? FMemory_Alloca(MemSize) : FMemory::Malloc(MemSize);

		if (Mem)
		{
			D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* Parameters = (D3D12_WRITEBUFFERIMMEDIATE_PARAMETER*)Mem;
			D3D12_WRITEBUFFERIMMEDIATE_MODE* Modes = (D3D12_WRITEBUFFERIMMEDIATE_MODE*)(Parameters + ParameterCount);
			for (int i = 0; i < ParameterCount; ++i)
			{
				Parameters[i].Dest = BreadCrumbResource->GetGPUVirtualAddress() + 4 * i;
				Modes[i] = D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN;

				// Write event stack count first
				if (i == 0)
				{
					Parameters[i].Value = GPUEventStack.Num();
				}
				// Then if it's the begin or end event
				else if (i == 1)
				{
					Parameters[i].Value = bBeginEvent ? 1 : 0;
				}
				// Otherwise the actual stack value
				else
				{
					Parameters[i].Value = GPUEventStack[i - 2];
				}
			}
			CommandList2->WriteBufferImmediate(ParameterCount, Parameters, Modes);
		}

		if (!bAllocateOnStack)
		{
			FMemory::Free(Mem);
		}
	}
#endif // PLATFORM_WINDOWS
}


void FD3D12CommandContext::RHIPushEvent(const TCHAR* Name, FColor Color)
{
	D3D12RHI::FD3DGPUProfiler& GPUProfiler = GetParentDevice()->GetGPUProfiler();

	// forward event to profiler if it's the default context
	if (IsDefaultContext())
	{
		GPUProfiler.PushEvent(Name, Color);
	}

	// If we are tracking GPU crashes then retrieve the hash of the name and track in the command list somewhere
	if (GPUProfiler.bTrackingGPUCrashData)
	{
		// Get the CRC of the event (handle case when depth is too big)
		const TCHAR* EventName = (GPUProfiler.GPUCrashDataDepth < 0 || GPUEventStack.Num() < GPUProfiler.GPUCrashDataDepth) ? Name : *D3D12RHI::FD3DGPUProfiler::EventDeepString;
		uint32 CRC = GPUProfiler.GetOrAddEventStringHash(Name);

		GPUEventStack.Push(CRC);
		WriteGPUEventStackToBreadCrumbData(true);

#if NV_AFTERMATH
		// Only track aftermath for default context?
		if (IsDefaultContext() && GDX12NVAfterMathEnabled && GDX12NVAfterMathMarkers)
			GFSDK_Aftermath_SetEventMarker(CommandListHandle.AftermathCommandContext(), &GPUEventStack[0], GPUEventStack.Num() * sizeof(uint32));
#endif // NV_AFTERMATH		
	}

#if PLATFORM_WINDOWS
	AGSContext* const AmdAgsContext = OwningRHI.GetAmdAgsContext();
	if (GEmitRgpFrameMarkers && AmdAgsContext)
	{
		agsDriverExtensionsDX12_PushMarker(AmdAgsContext, CommandListHandle.GraphicsCommandList(), TCHAR_TO_ANSI(Name));
	}
#endif

#if USE_PIX
	if (FD3D12DynamicRHI::GetD3DRHI()->IsPixEventEnabled())
	{
		PIXBeginEvent(CommandListHandle.GraphicsCommandList(), PIX_COLOR(Color.R, Color.G, Color.B), Name);
	}
#endif // USE_PIX
}

void FD3D12CommandContext::RHIPopEvent()
{
	D3D12RHI::FD3DGPUProfiler& GPUProfiler = GetParentDevice()->GetGPUProfiler();
	
	if (IsDefaultContext())
	{
		GPUProfiler.PopEvent();
	}
		
	if (GPUProfiler.bTrackingGPUCrashData)
	{
		WriteGPUEventStackToBreadCrumbData(false);

		// need to look for unbalanced push/pop
		if (GPUEventStack.Num() > 0)
		{
			GPUEventStack.Pop(false);
		}
	}

#if PLATFORM_WINDOWS
	AGSContext* const AmdAgsContext = OwningRHI.GetAmdAgsContext();
	if (GEmitRgpFrameMarkers && AmdAgsContext)
	{
		agsDriverExtensionsDX12_PopMarker(AmdAgsContext, CommandListHandle.GraphicsCommandList());
	}
#endif

#if USE_PIX
	if (FD3D12DynamicRHI::GetD3DRHI()->IsPixEventEnabled())
	{
		PIXEndEvent(CommandListHandle.GraphicsCommandList());
	}
#endif
}

FD3D12CommandListManager& FD3D12CommandContext::GetCommandListManager()
{
	return bIsAsyncComputeContext ? GetParentDevice()->GetAsyncCommandListManager() : GetParentDevice()->GetCommandListManager();
}

void FD3D12CommandContext::ConditionalObtainCommandAllocator()
{
	if (CommandAllocator == nullptr)
	{
		// Obtain a command allocator if the context doesn't already have one.
		// This will check necessary fence values to ensure the returned command allocator isn't being used by the GPU, then reset it.
		CommandAllocator = CommandAllocatorManager.ObtainCommandAllocator();
	}
}

void FD3D12CommandContext::ReleaseCommandAllocator()
{
	if (CommandAllocator != nullptr)
	{
		// Release the command allocator so it can be reused.
		CommandAllocatorManager.ReleaseCommandAllocator(CommandAllocator);
		CommandAllocator = nullptr;
	}
}

void FD3D12CommandContext::OpenCommandList()
{
	// Conditionally get a new command allocator.
	// Each command context uses a new allocator for all command lists within a "frame".
	ConditionalObtainCommandAllocator();

	// Get a new command list
	CommandListHandle = GetCommandListManager().ObtainCommandList(*CommandAllocator);
	CommandListHandle.SetCurrentOwningContext(this);

	// Notify the descriptor cache about the new command list
	// This will set the descriptor cache's current heaps on the new command list.
	StateCache.GetDescriptorCache()->SetCurrentCommandList(CommandListHandle);

	// Go through the state and find bits that differ from command list defaults.
	// Mark state as dirty so next time ApplyState is called, it will set all state on this new command list
	StateCache.DirtyStateForNewCommandList();

	bIsDoingQuery = false;

	numDraws = 0;
	numDispatches = 0;
	numClears = 0;
	numBarriers = 0;
	numPendingBarriers = 0;
	numCopies = 0;
	numInitialResourceCopies = 0;
	otherWorkCounter = 0;
}

void FD3D12CommandContext::CloseCommandList()
{
	CommandListHandle.Close();
}

FD3D12CommandListHandle FD3D12CommandContext::FlushCommands(bool WaitForCompletion, EFlushCommandsExtraAction ExtraAction)
{
	// We should only be flushing the default context
	check(IsDefaultContext());

	// We should be in a query anymore
	check(!bIsDoingQuery);

	bool bHasProfileGPUAction = false;
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	// Only graphics command list supports ID3D12GraphicsCommandList::EndQuery currently
	if (!bIsAsyncComputeContext)
	{
		if (ExtraAction == FCEA_StartProfilingGPU)
		{
			GetCommandListManager().StartTrackingCommandListTime();
			bHasProfileGPUAction = true;
		}
		else if (ExtraAction == FCEA_EndProfilingGPU)
		{
			GetCommandListManager().EndTrackingCommandListTime();
			bHasProfileGPUAction = true;
		}
	}
#endif

	FD3D12Device* Device = GetParentDevice();
	const bool bIsCommandListOpen = !CommandListHandle.IsClosed();
	const bool bHasPendingWork = Device->PendingCommandLists.Num() > 0 && (CommandListHandle.GetCommandListType() == D3D12_COMMAND_LIST_TYPE_DIRECT);
	const bool bHasDoneWork = HasDoneWork() || bHasPendingWork;
	const bool bOpenNewCmdList = WaitForCompletion || bHasDoneWork || bHasProfileGPUAction;

	// Only submit a command list if it does meaningful work or the flush is expected to wait for completion.
	if (bOpenNewCmdList)
	{
		// Close the current command list
		if (bIsCommandListOpen)
		{
			CloseCommandList();
		}

		if (bHasPendingWork)
		{
			// Submit all pending command lists and the current command list if it was still open
			if (bIsCommandListOpen)
			{
				Device->PendingCommandLists.Add(CommandListHandle);				
			}
			else
			{
				// otherwise it should be already part of the pending list
				check(Device->PendingCommandLists.Contains(CommandListHandle));

				// This use case should only happen when force flush is called from the rendering thread using the FScopedRHIThreadStaller
				// which could insert a flush in between pending command lists
				check(IsInRenderingThread());
			}
			GetCommandListManager().ExecuteCommandLists(Device->PendingCommandLists, WaitForCompletion);
			Device->PendingCommandLists.Reset();
		}
		else
		{
			// If there are no pending command lists then we assyme that the current command list is open
			check(bIsCommandListOpen);

			// Just submit the current command list
			CommandListHandle.Execute(WaitForCompletion);
		}

		if (bIsCommandListOpen)
		{
			// Get a new command list to replace the one we submitted for execution. 
			// Restore the state from the previous command list.
			OpenCommandList();
		}
	}

	return CommandListHandle;
}

void FD3D12CommandContext::ConditionalFlushCommandList()
{
	// Flush command list of reached maximum amount of commands which can be done in a single
	// command list - too many can cause TDRs
	// (can't flush when query is open!)
	if (IsDefaultContext() && !bIsDoingQuery && MaxCommandsPerCommandList > 0 && GetTotalWorkCount() > (uint32)MaxCommandsPerCommandList)
	{
		UE_LOG(LogD3D12RHI, Warning, TEXT("Force flushing command list to GPU because too many commands have been enqueued already (%d commands)"), GetTotalWorkCount());
		FlushCommands();
	}
}

void FD3D12CommandContext::Finish(TArray<FD3D12CommandListHandle>& CommandLists)
{
	CloseCommandList();

	if (HasDoneWork())
	{
		CommandLists.Add(CommandListHandle);
	}
	else
	{
		// Release the unused command list.
		GetCommandListManager().ReleaseCommandList(CommandListHandle);
	}

	// The context is done with this command list handle.
	CommandListHandle = FD3D12CommandListHandle();
}

void FD3D12CommandContextBase::RHIBeginFrame()
{
	bTrackingEvents = false;

	RHIPrivateBeginFrame();

#if D3D12_SUBMISSION_GAP_RECORDER
	typedef typename FD3D12CommandContext::EFlushCommandsExtraAction EFlushCmdsAction;
	constexpr bool bWaitForCommands = false;
	constexpr EFlushCmdsAction FlushAction = EFlushCmdsAction::FCEA_StartProfilingGPU;

	int32 CurrentSlotIdx = ParentAdapter->GetDevice(0)->GetCmdListExecTimeQueryHeap()->GetNextFreeIdx();
	ParentAdapter->SubmissionGapRecorder.SetStartFrameSlotIdx(CurrentSlotIdx);
#endif

	for (uint32 GPUIndex : GPUMask)
	{
		FD3D12Device* Device = ParentAdapter->GetDevice(GPUIndex);

		bTrackingEvents |= bIsDefaultContext && Device->GetGPUProfiler().bTrackingEvents;

#if D3D12_SUBMISSION_GAP_RECORDER
		if (GEnableGapRecorder && !GTriggerGPUProfile)
		{
			Device->GetDefaultCommandContext().FlushCommands(bWaitForCommands, FlushAction);
			GGapRecorderActiveOnBeginFrame = true;
		}
#endif

		// Resolve the last frame's timestamp queries
		FD3D12CommandContext* ContextAtIndex = GetContext(GPUIndex);
		if (ensure(ContextAtIndex))
		{
			Device->GetTimestampQueryHeap()->EndQueryBatchAndResolveQueryData(*ContextAtIndex);
#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
			uint64 TimeStampFrequency = 0;
			VERIFYD3D12RESULT(Device->GetCommandListManager().GetTimestampFrequency(&TimeStampFrequency));
			Device->GetBackBufferWriteBarrierTracker()->ResolveBatches(TimeStampFrequency, false);
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
		}

		FD3D12GlobalOnlineSamplerHeap& SamplerHeap = Device->GetGlobalSamplerHeap();
		if (SamplerHeap.DescriptorTablesDirty())
		{
			//Rearrange the set for better look-up performance
			SamplerHeap.GetUniqueDescriptorTables().Compact();
			SET_DWORD_STAT(STAT_NumReuseableSamplerOnlineDescriptorTables, SamplerHeap.GetUniqueDescriptorTables().Num());
			SET_DWORD_STAT(STAT_NumReuseableSamplerOnlineDescriptors, SamplerHeap.GetNextSlotIndex());
		}

		const uint32 NumContexts = Device->GetNumContexts();
		for (uint32 i = 0; i < NumContexts; ++i)
		{
			Device->GetCommandContext(i).StateCache.GetDescriptorCache()->BeginFrame();
		}

		const uint32 NumAsyncContexts = Device->GetNumAsyncComputeContexts();
		for (uint32 i = 0; i < NumAsyncContexts; ++i)
		{
			Device->GetAsyncComputeContext(i).StateCache.GetDescriptorCache()->BeginFrame();
		}

		SamplerHeap.ToggleDescriptorTablesDirtyFlag(false);

		Device->GetGPUProfiler().BeginFrame(ParentAdapter->GetOwningRHI());
	}
}

void FD3D12CommandContext::ClearState()
{
	StateCache.ClearState();

	bDiscardSharedGraphicsConstants = false;
	bDiscardSharedComputeConstants = false;

	FMemory::Memzero(BoundUniformBuffers, sizeof(BoundUniformBuffers));
	FMemory::Memzero(DirtyUniformBuffers, sizeof(DirtyUniformBuffers));

	for (int i = 0; i < UE_ARRAY_COUNT(BoundUniformBufferRefs); i++)
	{
		for (int j = 0; j < UE_ARRAY_COUNT(BoundUniformBufferRefs[i]); j++)
		{
			BoundUniformBufferRefs[i][j] = NULL;
		}
	}

	if (!bIsAsyncComputeContext)
	{
		FMemory::Memzero(CurrentRenderTargets, sizeof(CurrentRenderTargets));
		NumSimultaneousRenderTargets = 0;

		CurrentDepthStencilTarget = nullptr;
		CurrentDepthTexture = nullptr;

		CurrentDSVAccessType = FExclusiveDepthStencil::DepthWrite_StencilWrite;

		bUsingTessellation = false;
	}
}

void FD3D12CommandContext::ConditionalClearShaderResource(FD3D12ResourceLocation* Resource)
{
	check(Resource);
	StateCache.ClearShaderResourceViews<SF_Vertex>(Resource);
	StateCache.ClearShaderResourceViews<SF_Hull>(Resource);
	StateCache.ClearShaderResourceViews<SF_Domain>(Resource);
	StateCache.ClearShaderResourceViews<SF_Pixel>(Resource);
	StateCache.ClearShaderResourceViews<SF_Geometry>(Resource);
	StateCache.ClearShaderResourceViews<SF_Compute>(Resource);
}

void FD3D12CommandContext::ClearAllShaderResources()
{
	StateCache.ClearSRVs();
}

void FD3D12CommandContextBase::RHIEndFrame()
{
#if D3D12_SUBMISSION_GAP_RECORDER
	typedef typename FD3D12CommandContext::EFlushCommandsExtraAction EFlushCmdsAction;
	constexpr bool bWaitForCommands = false;
	constexpr EFlushCmdsAction FlushAction = EFlushCmdsAction::FCEA_EndProfilingGPU;
#endif

	FD3D12Device* Device = ParentAdapter->GetDevice(0);

#if D3D12_SUBMISSION_GAP_RECORDER
	if (GEnableGapRecorder && GGapRecorderActiveOnBeginFrame)
	{
		Device->GetDefaultCommandContext().FlushCommands(bWaitForCommands, FlushAction);
	}
#endif

	ParentAdapter->EndFrame();

	for (uint32 GPUIndex : GPUMask)
	{
		Device = ParentAdapter->GetDevice(GPUIndex);

		FD3D12CommandContext& DefaultContext = Device->GetDefaultCommandContext();
		DefaultContext.CommandListHandle.FlushResourceBarriers();

#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
		Device->GetBackBufferWriteBarrierTracker()->EndBatch(DefaultContext);
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING

		DefaultContext.ReleaseCommandAllocator();
		DefaultContext.ClearState();
		DefaultContext.FlushCommands();

		if (GEnableAsyncCompute)
		{
			FD3D12CommandContext& DefaultAsyncComputeContext = Device->GetDefaultAsyncComputeContext();
			DefaultAsyncComputeContext.ReleaseCommandAllocator();
			DefaultAsyncComputeContext.ClearState();
		}

		const uint32 NumContexts = Device->GetNumContexts();
		for (uint32 i = 0; i < NumContexts; ++i)
		{
			Device->GetCommandContext(i).EndFrame();
		}

		const uint32 NumAsyncContexts = Device->GetNumAsyncComputeContexts();
		for (uint32 i = 0; i < NumAsyncContexts; ++i)
		{
			Device->GetAsyncComputeContext(i).EndFrame();
		}

		Device->GetTextureAllocator().CleanUpAllocations();

		// Only delete free blocks when not used in the last 2 frames, to make sure we are not allocating and releasing
		// the same blocks every frame.
		uint64 BufferPoolDeletionFrameLag = 2;
		Device->GetDefaultBufferAllocator().CleanupFreeBlocks(BufferPoolDeletionFrameLag);

		uint64 FastAllocatorDeletionFrameLag = 10;
		Device->GetDefaultFastAllocator().CleanupPages(FastAllocatorDeletionFrameLag);
	}


	for (uint32 GPUIndex : GPUMask)
	{
		Device = ParentAdapter->GetDevice(GPUIndex);
		Device->GetCommandListManager().ReleaseResourceBarrierCommandListAllocator();
	}

	UpdateMemoryStats();

	// Stop Timing at the very last moment
	D3D12RHI::FD3DGPUProfiler& GPUProfiler = Device->GetGPUProfiler();
	GPUProfiler.EndFrame(ParentAdapter->GetOwningRHI());

#if WITH_MGPU
	// Multi-GPU support : For now, set GGPUFrameTime to GPU 0's frame time to be
	// consistent with code that calls RHIGetGPUFrameCycles and is not MGPU-aware.
	// Perhaps we should change it to get the max frame time of all GPUs?
	// Only use this code if running on a multi GPU configuration.
	GGPUFrameTime = GPUProfiler.GetGPUFrameCycles(0);
#endif
}

void FD3D12CommandContextBase::SignalTransitionFences(TArrayView<const FRHITransition*> Transitions)
{
	bool bSubmitted = false;
	for (const FRHITransition* Transition : Transitions)
	{
		const auto* Data = Transition->GetPrivateData<FD3D12TransitionData>();
		const auto& Fence = Data->Fence;
		if (Fence)
		{
			if (!bSubmitted)
			{
				RHISubmitCommandsHint();
				bSubmitted = true;
			}
			Fence->Signal(bIsAsyncComputeContext ? ED3D12CommandQueueType::Async : ED3D12CommandQueueType::Default);
		}
	}
}

void FD3D12CommandContextBase::WaitForTransitionFences(TArrayView<const FRHITransition*> Transitions)
{
	bool bSubmitted = false;
	for (const FRHITransition* Transition : Transitions)
	{
		const auto* Data = Transition->GetPrivateData<FD3D12TransitionData>();
		const auto& Fence = Data->Fence;
		if (Fence)
		{
			if (!bSubmitted)
			{
				RHISubmitCommandsHint();
				bSubmitted = true;
			}
			Fence->GpuWait(bIsAsyncComputeContext ? ED3D12CommandQueueType::Async : ED3D12CommandQueueType::Default, Fence->GetLastSignaledFence());
		}
	}
}

void FD3D12CommandContextBase::UpdateMemoryStats()
{
#if PLATFORM_WINDOWS && STATS
	DXGI_QUERY_VIDEO_MEMORY_INFO LocalVideoMemoryInfo;
	ParentAdapter->GetLocalVideoMemoryInfo(&LocalVideoMemoryInfo);

	const int64 Budget = LocalVideoMemoryInfo.Budget;
	const int64 AvailableSpace = Budget - int64(LocalVideoMemoryInfo.CurrentUsage);
	SET_MEMORY_STAT(STAT_D3D12UsedVideoMemory, LocalVideoMemoryInfo.CurrentUsage);
	SET_MEMORY_STAT(STAT_D3D12AvailableVideoMemory, AvailableSpace);
	SET_MEMORY_STAT(STAT_D3D12TotalVideoMemory, Budget);

	uint64 MaxTexAllocWastage = 0;
	for (uint32 GPUIndex : GPUMask)
	{
		FD3D12Device* Device = ParentAdapter->GetDevice(GPUIndex);

#if D3D12RHI_SEGREGATED_TEXTURE_ALLOC && D3D12RHI_SEGLIST_ALLOC_TRACK_WASTAGE
		uint64 TotalAllocated;
		uint64 TotalUnused;
		Device->GetTextureAllocator().GetMemoryStats(TotalAllocated, TotalUnused);
		MaxTexAllocWastage = FMath::Max(MaxTexAllocWastage, TotalUnused);
		SET_MEMORY_STAT(STAT_D3D12TextureAllocatorWastage, MaxTexAllocWastage);
#endif

		Device->GetDefaultBufferAllocator().UpdateMemoryStats();
	}		
#endif
}

void FD3D12CommandContext::RHIBeginScene()
{
}

void FD3D12CommandContext::RHIEndScene()
{
}

#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
void FD3D12CommandContext::RHIBackBufferWaitTrackingBeginFrame(uint64 FrameToken, bool bDeferred)
{
	GetParentDevice()->GetBackBufferWriteBarrierTracker()->BeginBatch(FrameToken, bDeferred);
}
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING

#if D3D12_SUPPORTS_PARALLEL_RHI_EXECUTE

//todo recycle these to avoid alloc

class FD3D12CommandContextContainer : public IRHICommandContextContainer
{
	FD3D12Adapter* Adapter;

	FD3D12CommandContext* CmdContext;
	FD3D12CommandContextRedirector* CmdContextRedirector;
	FRHIGPUMask GPUMask;

	TArray<FD3D12CommandListHandle> CommandLists;

public:

	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void* RawMemory);

	FD3D12CommandContextContainer(FD3D12Adapter* InAdapter, FRHIGPUMask InGPUMask)
		: Adapter(InAdapter)
		, CmdContext(nullptr)
		, CmdContextRedirector(nullptr)
		, GPUMask(InGPUMask)
	{
		CommandLists.Reserve(16);

		// Currently, there is only support for single index or full broadcast.
		ensure(GPUMask.HasSingleIndex() || GPUMask == FRHIGPUMask::All());
	}

	virtual ~FD3D12CommandContextContainer() override
	{
	}

	virtual IRHICommandContext* GetContext() override
	{
		check(!CmdContext && !CmdContextRedirector);

		if (GPUMask.HasSingleIndex())
		{
			FD3D12Device* Device = Adapter->GetDevice(GPUMask.ToIndex());

			CmdContext = Device->ObtainCommandContext();
			check(!CmdContext->CommandListHandle);

			// Clear state and then open the new command list to
			// minimize what state is marked dirty.
			CmdContext->ClearState();
			CmdContext->OpenCommandList();

			return CmdContext;
		}
		else
		{
			CmdContextRedirector = new FD3D12CommandContextRedirector(Adapter, false, false);
			CmdContextRedirector->SetPhysicalGPUMask(GPUMask);

			for (uint32 GPUIndex : GPUMask)
			{
				FD3D12Device* Device = Adapter->GetDevice(GPUIndex);

				CmdContext = Device->ObtainCommandContext();
				check(!CmdContext->CommandListHandle);
				CmdContext->OpenCommandList();
				CmdContext->ClearState();

				CmdContextRedirector->SetPhysicalContext(CmdContext);
				CmdContext = nullptr;
			}
			return CmdContextRedirector;
		}

	}

	virtual void FinishContext() override
	{
		// We never "Finish" the default context. It gets submitted when FlushCommands() is called.
		check(!CmdContext || !CmdContext->IsDefaultContext());

		if (CmdContext)
		{
			CmdContext->Finish(CommandLists);
			CmdContext->GetParentDevice()->ReleaseCommandContext(CmdContext);
			CmdContext = nullptr;
		}

		if (CmdContextRedirector)
		{
			for (uint32 GPUIndex : GPUMask)
			{
				CmdContext = CmdContextRedirector->GetContext(GPUIndex);
				CmdContext->Finish(CommandLists);
				CmdContext->GetParentDevice()->ReleaseCommandContext(CmdContext);
				CmdContext = nullptr;
			}
			delete CmdContextRedirector;
			CmdContextRedirector = nullptr;
		}
	}

	virtual void SubmitAndFreeContextContainer(int32 Index, int32 Num) override
	{
		if (Index == 0)
		{
			check((IsInRenderingThread() || IsInRHIThread()));

			for (uint32 GPUIndex : GPUMask)
			{
				FD3D12Device* Device = Adapter->GetDevice(GPUIndex);
				check(Device);

				FD3D12CommandContext& DefaultContext = Device->GetDefaultCommandContext();

				// Don't really submit the default context yet, just start a new command list.
				// Close the command list, add it to the pending command lists, then open a new command list (with the previous state restored).
				DefaultContext.CloseCommandList();

				Device->PendingCommandLists.Add(DefaultContext.CommandListHandle);

				// Note: we open the command list later after any possible flush.
			}
		}

		// Add the current lists for execution (now or possibly later depending on the command list batching mode).
		for (FD3D12CommandListHandle& CommandList : CommandLists)
		{
			FD3D12Device* Device = Adapter->GetDevice(CommandList.GetGPUIndex());
			check(Device);
			check(CommandList.GetCommandListType() == D3D12_COMMAND_LIST_TYPE_DIRECT);

			Device->PendingCommandLists.Add(CommandList);
		}
		CommandLists.Reset();

		for (uint32 GPUIndex : GPUMask)
		{
			FD3D12Device* Device = Adapter->GetDevice(GPUIndex);
			check(Device);

			if (Index == (Num - 1))
			{
				// Determine if we should flush:
				// 1) If the GPU is starving (i.e. we are CPU bound).
				// 2) If we want to submit at the end of a batch.
				const bool bFlush = (GCommandListBatchingMode == CLB_NormalBatching) || Device->IsGPUIdle();
				const bool bHasPendingWork = Device->PendingCommandLists.Num() > 0;
				if (bFlush && bHasPendingWork)
				{
					Device->GetCommandListManager().ExecuteCommandLists(Device->PendingCommandLists);
					Device->PendingCommandLists.Reset();
				}

				// Open a new command list.
				Device->GetDefaultCommandContext().OpenCommandList();
			}
		}
		delete this;
	}
};

void* FD3D12CommandContextContainer::operator new(size_t Size)
{
	return FMemory::Malloc(Size);
}

void FD3D12CommandContextContainer::operator delete(void* RawMemory)
{
	FMemory::Free(RawMemory);
}

IRHICommandContextContainer* FD3D12DynamicRHI::RHIGetCommandContextContainer(int32 Index, int32 Num)
{
	return new FD3D12CommandContextContainer(&GetAdapter(), FRHIGPUMask::All());
}

#if WITH_MGPU
IRHICommandContextContainer* FD3D12DynamicRHI::RHIGetCommandContextContainer(int32 Index, int32 Num, FRHIGPUMask GPUMask)
{
	return new FD3D12CommandContextContainer(&GetAdapter(), GPUMask);
}
#endif // WITH_MGPU



#endif // D3D12_SUPPORTS_PARALLEL_RHI_EXECUTE

void FD3D12DynamicRHI::RHICreateTransition(FRHITransition* Transition, ERHIPipeline SrcPipelines, ERHIPipeline DstPipelines, ERHICreateTransitionFlags CreateFlags, TArrayView<const FRHITransitionInfo> Infos)
{
	checkf(FMath::IsPowerOfTwo(uint32(SrcPipelines)) && FMath::IsPowerOfTwo(uint32(DstPipelines)), TEXT("Support for multi-pipe resources is not yet implemented."));

	// Construct the data in-place on the transition instance
	FD3D12TransitionData* Data = new (Transition->GetPrivateData<FD3D12TransitionData>()) FD3D12TransitionData;

	Data->SrcPipelines = SrcPipelines;
	Data->DstPipelines = DstPipelines;
	Data->CreateFlags = CreateFlags;

	const bool bCrossPipeline = SrcPipelines != DstPipelines;

	if (bCrossPipeline && !EnumHasAnyFlags(Data->CreateFlags, ERHICreateTransitionFlags::NoFence))
	{
		const FName Name = SrcPipelines == ERHIPipeline::Graphics ? L"<Graphics To AsyncCompute>" : L"<AsyncCompute To Graphics>";

		Data->Fence = new FD3D12Fence(&GetAdapter(), FRHIGPUMask::All(), Name);
		Data->Fence->CreateFence();
	}

	Data->bCrossPipeline = bCrossPipeline;
	Data->Infos.Append(Infos.GetData(), Infos.Num());
}

void FD3D12DynamicRHI::RHIReleaseTransition(FRHITransition* Transition)
{
	// Destruct the transition data
	Transition->GetPrivateData<FD3D12TransitionData>()->~FD3D12TransitionData();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FD3D12CommandContextRedirector
//
//////////////////////////////////////////////////////////////////////////////////////////////////////////

FD3D12CommandContextRedirector::FD3D12CommandContextRedirector(class FD3D12Adapter* InParent, bool InIsDefaultContext, bool InIsAsyncComputeContext)
	: FD3D12CommandContextBase(InParent, FRHIGPUMask::All(), InIsDefaultContext, InIsAsyncComputeContext)
{
	FMemory::Memzero(PhysicalContexts, sizeof(PhysicalContexts[0]) * MAX_NUM_GPUS);
}

void FD3D12CommandContextRedirector::RHIMultiGPULockstep(FRHIGPUMask InGPUMask)
{
#if WITH_MGPU
	FD3D12Fence* GPUFence = GetParentAdapter()->GetStagingFence();
	GPUFence->Signal(ED3D12CommandQueueType::Default);

	// Then everyone waits for completion of everyone one else.
	for (uint32 GPUIndex : InGPUMask)
	{
		for (uint32 GPUIndex2 : InGPUMask)
		{
			if (GPUIndex != GPUIndex2)
			{
				GPUFence->GpuWait(GPUIndex2, ED3D12CommandQueueType::Default, GPUFence->GetLastSignaledFence(), GPUIndex);
			}
		}
	}
#endif // WITH_MGPU
}

void FD3D12CommandContextRedirector::RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions)
{
	ContextRedirect(RHIBeginTransitionsWithoutFencing(Transitions));
	SignalTransitionFences(Transitions);
}

void FD3D12CommandContextRedirector::RHIEndTransitions(TArrayView<const FRHITransition*> Transitions)
{
	ContextRedirect(RHIEndTransitions(Transitions));
}

void FD3D12CommandContextRedirector::RHITransferTextures(const TArrayView<const FTransferTextureParams> Params)
{
#if WITH_MGPU
	// Note that by default it is not empty, but GPU0
	FRHIGPUMask SrcAndDestMask; 

	for (const FTransferTextureParams& Param : Params)
	{
		FD3D12CommandContext* SrcContext = PhysicalContexts[Param.SrcGPUIndex];
		FD3D12CommandContext* DestContext = PhysicalContexts[Param.DestGPUIndex];
		if (!ensure(SrcContext && DestContext))
		{
			continue;
		}

		if (Param.bLockStepGPUs)
		{
			// If it's the first time we set the mask.
			if (SrcAndDestMask != FRHIGPUMask())
			{
				SrcAndDestMask |= FRHIGPUMask::FromIndex(Param.SrcGPUIndex) | FRHIGPUMask::FromIndex(Param.DestGPUIndex);
			}
			else
			{
				SrcAndDestMask = FRHIGPUMask::FromIndex(Param.SrcGPUIndex) | FRHIGPUMask::FromIndex(Param.DestGPUIndex);
			}
		}

		FD3D12TextureBase* SrcTexture = FD3D12CommandContext::RetrieveTextureBase(Param.Texture, Param.SrcGPUIndex);
		FD3D12TextureBase* DestTexture = FD3D12CommandContext::RetrieveTextureBase(Param.Texture, Param.DestGPUIndex);

		FD3D12DynamicRHI::TransitionResource(SrcContext->CommandListHandle, SrcTexture->GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, 0);
		FD3D12DynamicRHI::TransitionResource(DestContext->CommandListHandle, DestTexture->GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, 0);

	}

	// Make sure to Submit any pending work before signaling the fence.
	// Note that this redirect to all GPUs from the context redirector.
	RHISubmitCommandsHint();

	// Submit the fence on all GPUs
	FD3D12Fence* GPUFence = GetParentAdapter()->GetStagingFence();
	if (SrcAndDestMask != FRHIGPUMask())
	{
		RHIMultiGPULockstep(SrcAndDestMask);
	}
	else
	{
		GPUFence->Signal(ED3D12CommandQueueType::Default);
		for (const FTransferTextureParams& Param : Params)
		{
			if (Param.bPullData)
			{
				// The dest waits for the source to complete before reading the data.
				GPUFence->GpuWait(Param.DestGPUIndex, ED3D12CommandQueueType::Default,  GPUFence->GetLastSignaledFence(), Param.SrcGPUIndex);
			}
		}
	}

	for (const FTransferTextureParams& Param : Params)
	{
		FD3D12CommandContext* SrcContext = PhysicalContexts[Param.SrcGPUIndex];
		FD3D12CommandContext* DestContext = PhysicalContexts[Param.DestGPUIndex];
		if (!ensure(SrcContext && DestContext))
		{
			continue;
		}

		FD3D12TextureBase* SrcTexture = FD3D12CommandContext::RetrieveTextureBase(Param.Texture, Param.SrcGPUIndex);
		FD3D12TextureBase* DestTexture = FD3D12CommandContext::RetrieveTextureBase(Param.Texture, Param.DestGPUIndex);

		ensureMsgf(
			Param.Min.X >= 0 && Param.Min.Y >= 0 && Param.Min.Z >= 0 &&
			Param.Max.X >= 0 && Param.Max.Y >= 0 && Param.Max.Z >= 0,
			TEXT("Invalid rect for texture transfer: %i, %i, %i, %i"), Param.Min.X, Param.Min.Y, Param.Min.Z, Param.Max.X, Param.Max.Y, Param.Max.Z);

		D3D12_BOX Box = { (UINT)Param.Min.X, (UINT)Param.Min.Y, (UINT)Param.Min.Z, (UINT)Param.Max.X, (UINT)Param.Max.Y, (UINT)Param.Max.Z };

		CD3DX12_TEXTURE_COPY_LOCATION SrcLocation(SrcTexture->GetResource()->GetResource(), 0);
		CD3DX12_TEXTURE_COPY_LOCATION DestLocation(DestTexture->GetResource()->GetResource(), 0);

		FD3D12CommandContext* CopyContext = Param.bPullData ? DestContext : SrcContext;
		CopyContext->CommandListHandle->CopyTextureRegion(&DestLocation, Box.left, Box.top, Box.front, &SrcLocation, &Box);
		CopyContext->numCopies++;
	}

	if (SrcAndDestMask != FRHIGPUMask())
	{
		// Complete the lockstep by ensuring the GPUs don't start doing something else before the copy completes.
		RHIMultiGPULockstep(SrcAndDestMask);
	}
	else
	{
		GPUFence->Signal(ED3D12CommandQueueType::Default);
		for (const FTransferTextureParams& Param : Params)
		{
			if (!Param.bPullData)
			{
				// The source waits for the dest to be at this place in the frame before writing the data.
				GPUFence->GpuWait(Param.SrcGPUIndex, ED3D12CommandQueueType::Default, GPUFence->GetLastSignaledFence(), Param.DestGPUIndex);
			}
		}
	}
#endif // WITH_MGPU
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FD3D12TemporalEffect
//
//////////////////////////////////////////////////////////////////////////////////////////////////////////

#if WITH_MGPU

FD3D12TemporalEffect::FD3D12TemporalEffect(FD3D12Adapter* Parent, const FName& InEffectName)
	: FD3D12AdapterChild(Parent)
	, EffectName(InEffectName)
{}

void FD3D12TemporalEffect::Init()
{
	// Create fences for each set of sibling GPUs.
	FD3D12FenceCorePool& FenceCorePool = GetParentAdapter()->GetFenceCorePool();
	const auto& SiblingMasks = AFRUtils::GetSiblingMasks();
	for (int32 MaskIndex = 0; MaskIndex < SiblingMasks.Num(); MaskIndex++)
	{
		const FRHIGPUMask GPUMask = SiblingMasks[MaskIndex];
		FD3D12FenceCore* FenceCore = FenceCorePool.ObtainFenceCore(GPUMask.GetFirstIndex());
		SetName(FenceCore->GetFence(), *FString::Printf(TEXT("%s (GPUMask 0x%x)"), *EffectName.ToString(), GPUMask.GetNative()));
		EffectFences.Emplace(GPUMask, FenceCore->FenceValueAvailableAt, FenceCore);
	}
}

void FD3D12TemporalEffect::Destroy()
{
	FD3D12FenceCorePool& FenceCorePool = GetParentAdapter()->GetFenceCorePool();
	for (auto& CrossGPUFence : EffectFences)
	{
		FenceCorePool.ReleaseFenceCore(CrossGPUFence.FenceCore, CrossGPUFence.LastSignaledFence);
	}
	EffectFences.Empty();
}

bool FD3D12TemporalEffect::ShouldWaitForPrevious(uint32 GPUIndex) const
{
	const FCrossGPUFence* CrossGPUFence = GetFenceForGPU(GPUIndex);
	check(CrossGPUFence);
	return CrossGPUFence->LastWaitedFence != CrossGPUFence->LastSignaledFence;
}

void FD3D12TemporalEffect::WaitForPrevious(uint32 GPUIndex, ED3D12CommandQueueType InQueueType)
{
	FCrossGPUFence* CrossGPUFence = GetFenceForGPU(GPUIndex);
	check(CrossGPUFence);

	ID3D12CommandQueue* CommandQueue = GetParentAdapter()->GetDevice(GPUIndex)->GetD3DCommandQueue(InQueueType);
	check(CommandQueue);

	check(CrossGPUFence->FenceCore);
	VERIFYD3D12RESULT(CommandQueue->Wait(CrossGPUFence->FenceCore->GetFence(), CrossGPUFence->LastSignaledFence));
	CrossGPUFence->LastWaitedFence = CrossGPUFence->LastSignaledFence;
}

void FD3D12TemporalEffect::SignalSyncComplete(uint32 GPUIndex, ED3D12CommandQueueType InQueueType)
{
	FCrossGPUFence* CrossGPUFence = GetFenceForGPU(GPUIndex);
	check(CrossGPUFence);

	ID3D12CommandQueue* CommandQueue = GetParentAdapter()->GetDevice(GPUIndex)->GetD3DCommandQueue(InQueueType);
	check(CommandQueue);

	check(CrossGPUFence->FenceCore);
	VERIFYD3D12RESULT(CommandQueue->Signal(CrossGPUFence->FenceCore->GetFence(), ++CrossGPUFence->LastSignaledFence));
}

#endif // WITH_MGPU
