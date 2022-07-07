// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RHIPrivate.h"
#include "Windows.h"


static TAutoConsoleVariable<int32> CVarD3D12GPUTimeout(
	TEXT("r.D3D12.GPUTimeout"),
	1,
	TEXT("0: Disable GPU Timeout; use with care as it could freeze your PC!\n")
	TEXT("1: Enable GPU Timeout; operation taking long on the GPU will fail(default)\n"),
	ECVF_ReadOnly
);

static int32 GD3D12ExecuteCommandListTask = 0;
static FAutoConsoleVariableRef CVarD3D12ExecuteCommandListTask(
	TEXT("r.D3D12.ExecuteCommandListTask"),
	GD3D12ExecuteCommandListTask,
	TEXT("0: Execute command lists on RHI Thread instead of separate task!\n")
	TEXT("1: Execute command lists on task created from RHIThread to offload expensive work (default)\n")
);

extern bool D3D12RHI_ShouldCreateWithD3DDebug();

void FD3D12GPUFence::WriteInternal(ED3D12CommandQueueType QueueType)
{
	if (Fence)
	{
		Value = Fence->Signal(QueueType);
	}
}

bool FD3D12GPUFence::Poll() const
{
	// @todo-mattc Value of 0 means signaled? Revisit this...
	return !Value || (Fence && Fence->PeekLastCompletedFence() >= Value);
}

bool FD3D12GPUFence::Poll(FRHIGPUMask GPUMask) const
{
	// @todo-mattc Value of 0 means signaled? Revisit this...
	return !Value || (Fence && Fence->PeekLastCompletedFence(GPUMask) >= Value);
}

void FD3D12GPUFence::Clear()
{
	Value = MAX_uint64;
}


FGPUFenceRHIRef FD3D12DynamicRHI::RHICreateGPUFence(const FName& Name)
{
	return new FD3D12GPUFence(Name, GetAdapter().GetStagingFence());
}

FStagingBufferRHIRef FD3D12DynamicRHI::RHICreateStagingBuffer()
{
	// Don't know the device yet - will be decided at copy time (lazy creation)
	return new FD3D12StagingBuffer(nullptr);
}

void* FD3D12DynamicRHI::RHILockStagingBuffer(FRHIStagingBuffer* StagingBufferRHI, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	FD3D12StagingBuffer* StagingBuffer = FD3D12DynamicRHI::ResourceCast(StagingBufferRHI);
	check(StagingBuffer);

	return StagingBuffer->Lock(Offset, SizeRHI);
}

void FD3D12DynamicRHI::RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBufferRHI)
{
	FD3D12StagingBuffer* StagingBuffer = FD3D12DynamicRHI::ResourceCast(StagingBufferRHI);
	check(StagingBuffer);
	StagingBuffer->Unlock();
}

// =============================================================================

FD3D12FenceCore::FD3D12FenceCore(FD3D12Adapter* Parent, uint64 InitialValue, uint32 InGPUIndex)
	: FD3D12AdapterChild(Parent)
	, FenceValueAvailableAt(0)
	, GPUIndex(InGPUIndex)
	, hFenceCompleteEvent(INVALID_HANDLE_VALUE)
{
	check(Parent);
	hFenceCompleteEvent = CreateEvent(nullptr, false, false, nullptr);
	check(INVALID_HANDLE_VALUE != hFenceCompleteEvent);

	VERIFYD3D12RESULT(Parent->GetD3DDevice()->CreateFence(InitialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(Fence.GetInitReference())));
}

FD3D12FenceCore::~FD3D12FenceCore()
{
	if (hFenceCompleteEvent != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hFenceCompleteEvent);
		hFenceCompleteEvent = INVALID_HANDLE_VALUE;
	}
}

FD3D12Fence::FD3D12Fence(FD3D12Adapter* InParent, FRHIGPUMask InGPUMask, const FName& InName)
	: FD3D12AdapterChild(InParent)
	, FD3D12MultiNodeGPUObject(InGPUMask, InGPUMask)
	, CurrentFence(0)
	, LastSignaledFence(0)
	, LastCompletedFence(0)
	, Name(InName)
{
	FMemory::Memzero(FenceCores);
	FMemory::Memzero(LastCompletedFences);
}

FD3D12Fence::~FD3D12Fence()
{
	Destroy();
}

void FD3D12Fence::Destroy()
{
	for (uint32 GPUIndex : GetGPUMask())
	{
		if (FenceCores[GPUIndex])
		{
			// Return the underlying fence to the pool, store the last value signaled on this fence. 
			// If not fence was signaled since CreateFence() was called, then the last completed value is the last signaled value for this GPU.
			GetParentAdapter()->GetFenceCorePool().ReleaseFenceCore(FenceCores[GPUIndex], LastSignaledFence > 0 ? LastSignaledFence : LastCompletedFences[GPUIndex]);
#if DEBUG_FENCES
			UE_LOG(LogD3D12RHI, Log, TEXT("*** GPU FENCE DESTROY Fence: %016llX (%s) Gpu (%d), Last Completed: %u ***"), FenceCores[GPUIndex]->GetFence(), *Name.ToString(), GPUIndex, LastSignaledFence > 0 ? LastSignaledFence : LastCompletedFences[GPUIndex]);
#endif
			FenceCores[GPUIndex] = nullptr;
		}
	}
}

void FD3D12Fence::CreateFence()
{
	// Can't set the last signaled fence per GPU before a common signal is sent.
	LastSignaledFence = 0;

	if (GetGPUMask().HasSingleIndex())
	{
		const uint32 GPUIndex = GetGPUMask().ToIndex();
		check(!FenceCores[GPUIndex]);

		// Get a fence from the pool
		FD3D12FenceCore* FenceCore = GetParentAdapter()->GetFenceCorePool().ObtainFenceCore(GPUIndex);
		check(FenceCore);
		FenceCores[GPUIndex] = FenceCore;

		LastCompletedFences[GPUIndex] = FenceCore->FenceValueAvailableAt;

		SetName(FenceCore->GetFence(), *Name.ToString());

		LastCompletedFence = LastCompletedFences[GPUIndex];
		CurrentFence = LastCompletedFences[GPUIndex] + 1;
	}
	else
	{
		CurrentFence = 0;
		LastCompletedFence = MAXUINT64;

		for (uint32 GPUIndex : GetGPUMask())
		{
			check(!FenceCores[GPUIndex]);
			
			// Get a fence from the pool
			FD3D12FenceCore* FenceCore = GetParentAdapter()->GetFenceCorePool().ObtainFenceCore(GPUIndex);
			check(FenceCore);
			FenceCores[GPUIndex] = FenceCore;

			LastCompletedFences[GPUIndex] = FenceCore->FenceValueAvailableAt;
#if DEBUG_FENCES
			UE_LOG(LogD3D12RHI, Log, TEXT("*** GPU FENCE CREATE Fence: %016llX (%s) Gpu (%d), Last Completed: %u ***"), FenceCores[GPUIndex]->GetFence(), *Name.ToString(), GPUIndex, LastCompletedFences[GPUIndex]);
#endif
			// Append the GPU index to the fence.
			SetName(FenceCore->GetFence(), *FString::Printf(TEXT("%s%u"), *Name.ToString(), GPUIndex));

			LastCompletedFence = FMath::Min(LastCompletedFence, LastCompletedFences[GPUIndex]);
			CurrentFence = FMath::Max(CurrentFence, LastCompletedFences[GPUIndex]);
		}

		++CurrentFence;
	}
}

uint64 FD3D12Fence::Signal(ED3D12CommandQueueType InQueueType)
{
	check(LastSignaledFence != CurrentFence);
	InternalSignal(InQueueType, CurrentFence);

	// Update the cached version of the fence value
	UpdateLastCompletedFence();

	// Increment the current Fence
	CurrentFence++;

	return LastSignaledFence;
}

void FD3D12Fence::GpuWait(uint32 DeviceGPUIndex, ED3D12CommandQueueType InQueueType, uint64 FenceValue, uint32 FenceGPUIndex)
{
	ID3D12CommandQueue* CommandQueue = GetParentAdapter()->GetDevice(DeviceGPUIndex)->GetD3DCommandQueue(InQueueType);
	check(CommandQueue);
	FD3D12FenceCore* FenceCore = FenceCores[FenceGPUIndex];
	check(FenceCore);

#if DEBUG_FENCES
	UE_LOG(LogD3D12RHI, Log, TEXT("*** GPU WAIT (CmdQueueType: %d) Fence: %016llX (%s), Gpu (%d <- %d) Value: %llu ***"), (uint32)InQueueType, FenceCore->GetFence(), *Name.ToString(), DeviceGPUIndex, FenceGPUIndex, FenceValue);
#endif
	VERIFYD3D12RESULT(CommandQueue->Wait(FenceCore->GetFence(), FenceValue));}

void FD3D12Fence::GpuWait(ED3D12CommandQueueType InQueueType, uint64 FenceValue)
{
	for (uint32 GPUIndex : GetGPUMask())
	{
		GpuWait(GPUIndex, InQueueType, FenceValue, GPUIndex);
	}
}

bool FD3D12Fence::IsFenceComplete(uint64 FenceValue)
{
	check(FenceValue <= CurrentFence);

	// Avoid repeatedly calling GetCompletedValue()
	if (FenceValue <= LastCompletedFence)
	{
#if DEBUG_FENCES
		checkf(FenceValue <= PeekLastCompletedFence(), TEXT("Fence value (%llu) sanity check failed! Last completed value is really %llu."), FenceValue, LastCompletedFence);
#endif
		return true;
	}

	// Refresh the completed fence value
	return FenceValue <= UpdateLastCompletedFence();

}

uint64 FD3D12Fence::PeekLastCompletedFence() const
{
	return PeekLastCompletedFence(GetGPUMask());
}

uint64 FD3D12Fence::PeekLastCompletedFence(FRHIGPUMask InGPUMask) const
{
	uint64 CompletedFence = MAXUINT64;
	check(GetGPUMask().ContainsAll(InGPUMask));
	for (uint32 GPUIndex : InGPUMask)
	{
		CompletedFence = FMath::Min<uint64>(FenceCores[GPUIndex]->GetFence()->GetCompletedValue(), CompletedFence);
	}
	return CompletedFence;
}

uint64 FD3D12Fence::UpdateLastCompletedFence()
{
	uint64 CompletedFence = MAXUINT64;
	for (uint32 GPUIndex : GetGPUMask())
	{
		FD3D12FenceCore* FenceCore = FenceCores[GPUIndex];
		check(FenceCore);
		LastCompletedFences[GPUIndex] = FenceCore->GetFence()->GetCompletedValue();
		CompletedFence = FMath::Min<uint64>(LastCompletedFences[GPUIndex], CompletedFence);
	}

	// Must be computed on the stack because the function can be called concurrently.
	LastCompletedFence = CompletedFence;
	return CompletedFence;
}

uint64 FD3D12ManualFence::ManualSignal(ED3D12CommandQueueType InQueueType, uint64 FenceToSignal)
{
	check(LastSignaledFence != FenceToSignal);
	InternalSignal(InQueueType, FenceToSignal);

	// Update the cached version of the fence value
	UpdateLastCompletedFence();
	check(LastSignaledFence == FenceToSignal);

	return LastSignaledFence;
}

FD3D12CommandAllocatorManager::FD3D12CommandAllocatorManager(FD3D12Device* InParent, const D3D12_COMMAND_LIST_TYPE& InType)
	: FD3D12DeviceChild(InParent)
	, Type(InType)
{}


FD3D12CommandAllocator* FD3D12CommandAllocatorManager::ObtainCommandAllocator()
{
	FScopeLock Lock(&CS);

	// See if the first command allocator in the queue is ready to be reset (will check associated fence)
	FD3D12CommandAllocator* pCommandAllocator = nullptr;
	if (CommandAllocatorQueue.Peek(pCommandAllocator) && pCommandAllocator->IsReady())
	{
		// Reset the allocator and remove it from the queue.
		pCommandAllocator->Reset();
		CommandAllocatorQueue.Dequeue(pCommandAllocator);
	}
	else
	{
		// The queue was empty, or no command allocators were ready, so create a new command allocator.
		pCommandAllocator = new FD3D12CommandAllocator(GetParentDevice()->GetDevice(), Type);
		check(pCommandAllocator);
		CommandAllocators.Add(pCommandAllocator);	// The command allocator's lifetime is managed by this manager

		// Set a valid sync point
		FD3D12Fence& FrameFence = GetParentDevice()->GetParentAdapter()->GetFrameFence();
		const FD3D12SyncPoint SyncPoint(&FrameFence, FrameFence.UpdateLastCompletedFence());
		pCommandAllocator->SetSyncPoint(SyncPoint);
	}

	check(pCommandAllocator->IsReady());
	return pCommandAllocator;
}

void FD3D12CommandAllocatorManager::ReleaseCommandAllocator(FD3D12CommandAllocator* pCommandAllocator)
{
	FScopeLock Lock(&CS);
	check(pCommandAllocator->HasValidSyncPoint());
	CommandAllocatorQueue.Enqueue(pCommandAllocator);
}

FD3D12CommandListManager::FD3D12CommandListManager(FD3D12Device* InParent, D3D12_COMMAND_LIST_TYPE InCommandListType, ED3D12CommandQueueType InQueueType)
	: FD3D12DeviceChild(InParent)
	, FD3D12SingleNodeGPUObject(InParent->GetGPUMask())
	, ResourceBarrierCommandAllocatorManager(InParent, InCommandListType)
	, ResourceBarrierCommandAllocator(nullptr)
	, CommandListFence(nullptr)
	, CommandListType(InCommandListType)
	, QueueType(InQueueType)
	, DiagnosticBuffer({ nullptr, nullptr, nullptr, 0 })
	, bExcludeBackbufferWriteTransitionTime(false)
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	, bShouldTrackCmdListTime(false)
#endif
{
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	for (int32 Idx = 0; Idx < UE_ARRAY_COUNT(CmdListTimingQueryBatchTokens); ++Idx)
	{
		CmdListTimingQueryBatchTokens[Idx] = INDEX_NONE;
	}
#endif
}

FD3D12CommandListManager::~FD3D12CommandListManager()
{
	Destroy();
}

void FD3D12CommandListManager::Destroy()
{
	// Wait for the queue to empty
	WaitForCommandQueueFlush();

	{
		FD3D12CommandListHandle hList;
		while (!ReadyLists.IsEmpty())
		{
			ReadyLists.Dequeue(hList);
		}
	}

	D3DCommandQueue.SafeRelease();

	if (CommandListFence)
	{
		CommandListFence->Destroy();
		CommandListFence.SafeRelease();
	}

	DestroyDiagnosticBuffer(DiagnosticBuffer);
}

void FD3D12CommandListManager::Create(const TCHAR* Name, uint32 NumCommandLists, uint32 Priority)
{
	FD3D12Device* Device = GetParentDevice();
	FD3D12Adapter* Adapter = Device->GetParentAdapter();

	CommandListFence = new FD3D12CommandListFence(Adapter, GetGPUMask(), L"Command List Fence");
	CommandListFence->CreateFence();

	check(D3DCommandQueue.GetReference() == nullptr);
	check(ReadyLists.IsEmpty());
	checkf(NumCommandLists <= 0xffff, TEXT("Exceeded maximum supported command lists"));

	bool bFullGPUCrashDebugging = (Adapter->GetGPUCrashDebuggingModes() == ED3D12GPUCrashDebuggingModes::All);

	D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = {};
	CommandQueueDesc.Flags = (bFullGPUCrashDebugging || CVarD3D12GPUTimeout.GetValueOnAnyThread() == 0) 
		? D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT : D3D12_COMMAND_QUEUE_FLAG_NONE;
	CommandQueueDesc.NodeMask = GetGPUMask().GetNative();
	CommandQueueDesc.Priority = Priority;
	CommandQueueDesc.Type = CommandListType;
	Adapter->GetOwningRHI()->CreateCommandQueue(Device, CommandQueueDesc, D3DCommandQueue);
	
	SetName(D3DCommandQueue.GetReference(), Name);

	if (NumCommandLists > 0)
	{
		// Create a temp command allocator for command list creation.
		FD3D12CommandAllocator TempCommandAllocator(Device->GetDevice(), CommandListType);
		for (uint32 i = 0; i < NumCommandLists; ++i)
		{
			FD3D12CommandListHandle hList = CreateCommandListHandle(TempCommandAllocator);
			ReadyLists.Enqueue(hList);
		}
	}

	// setup the bread crumb data to track GPU progress on this command queue when GPU crash debugging is enabled
	if (EnumHasAnyFlags(Adapter->GetGPUCrashDebuggingModes(), ED3D12GPUCrashDebuggingModes::BreadCrumbs))
	{		
		// QI for the ID3DDevice3 - manual buffer write from command line only supported on 1709+
		TRefCountPtr<ID3D12Device3> D3D12Device3;
		HRESULT hr = Device->GetDevice()->QueryInterface(IID_PPV_ARGS(D3D12Device3.GetInitReference()));
		if (SUCCEEDED(hr))
		{
			// find out how many entries we can much push in a single event (limit to MAX_GPU_BREADCRUMB_DEPTH)
			int32 GPUCrashDataDepth = GetParentDevice()->GetGPUProfiler().GPUCrashDataDepth;
			int32 MaxEventCount = GPUCrashDataDepth > 0 ? FMath::Min(GPUCrashDataDepth, MAX_GPU_BREADCRUMB_DEPTH) : MAX_GPU_BREADCRUMB_DEPTH;			

			const uint32 ShaderDiagnosticBufferSize = sizeof(FD3D12DiagnosticBufferData);

			// Allocate persistent CPU readable memory which will still be valid after a device lost and wrap this data in a placed resource
			// so the GPU command list can write to it
			const uint32 EventBufferSize = MaxEventCount * sizeof(uint32);
			const uint32 TotalBufferSize = EventBufferSize + ShaderDiagnosticBufferSize;

			// Create the platform-specific diagnostic buffer
			TCHAR TempStr[MAX_SPRINTF] = TEXT("");
			FCString::Sprintf(TempStr, TEXT("DiagnosticBuffer (%s)"), Name);

			const D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(TotalBufferSize, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER);
			DiagnosticBuffer = CreateDiagnosticBuffer(Adapter, Device, BufferDesc, TempStr);

			// Diagnostic buffer is split between breadcrumb events and diagnostic messages.
			DiagnosticBuffer.BreadCrumbsOffset = 0;
			DiagnosticBuffer.BreadCrumbsSize = EventBufferSize;

			DiagnosticBuffer.DiagnosticsOffset = DiagnosticBuffer.BreadCrumbsOffset + DiagnosticBuffer.BreadCrumbsSize;
			DiagnosticBuffer.DiagnosticsSize = ShaderDiagnosticBufferSize;
		}
	}
}

FGPUTimingCalibrationTimestamp FD3D12CommandListManager::GetCalibrationTimestamp()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(D3D12GetCalibrationTimestamp);

	check(CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT || CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE);

	uint64 GPUTimestampFrequency;
	GetTimestampFrequency(&GPUTimestampFrequency);

	LARGE_INTEGER CPUTimestempFrequency;
	QueryPerformanceFrequency(&CPUTimestempFrequency);

	uint64 GPUTimestamp, CPUTimestamp;
	VERIFYD3D12RESULT(D3DCommandQueue->GetClockCalibration(&GPUTimestamp, &CPUTimestamp));

	FGPUTimingCalibrationTimestamp Result = {};

	Result.GPUMicroseconds = uint64(GPUTimestamp * (1e6 / GPUTimestampFrequency));
	Result.CPUMicroseconds = uint64(CPUTimestamp * (1e6 / CPUTimestempFrequency.QuadPart));

	return Result;
}

FD3D12CommandListHandle FD3D12CommandListManager::ObtainCommandList(FD3D12CommandAllocator& CommandAllocator, bool bHasBackbufferWriteTransition)
{
	FD3D12CommandListHandle List;
	if (!ReadyLists.Dequeue(List))
	{
		// Create a command list if there are none available.
		List = CreateCommandListHandle(CommandAllocator);
	}

	check(List.GetCommandListType() == CommandListType);
	List.Reset(CommandAllocator, ShouldTrackCommandListTime() && !(bHasBackbufferWriteTransition && bExcludeBackbufferWriteTransitionTime));
	return List;
}

void FD3D12CommandListManager::ReleaseCommandList(FD3D12CommandListHandle& hList)
{
	check(hList.IsClosed());
	check(hList.GetCommandListType() == CommandListType);

	// Indicate that a command list using this allocator has either been executed or discarded.
	hList.CurrentCommandAllocator()->DecrementPendingCommandLists();

	ReadyLists.Enqueue(hList);
}

FD3D12SyncPoint FD3D12CommandListManager::ExecuteCommandListNoCopyQueueSync(FD3D12CommandListHandle& hList, bool WaitForCompletion)
{
	FD3D12SyncPoint EmptySyncPoint;
	return ExecuteCommandList(hList, EmptySyncPoint, WaitForCompletion);
}

FD3D12SyncPoint FD3D12CommandListManager::ExecuteCommandList(FD3D12CommandListHandle& hList, FD3D12SyncPoint& CopyQueueSyncPoint, bool WaitForCompletion)
{
	TArray<FD3D12CommandListHandle> Lists;
	Lists.Add(hList);

	return ExecuteCommandLists(Lists, CopyQueueSyncPoint, WaitForCompletion);
}

uint64 FD3D12CommandListManager::ExecuteAndIncrementFence(FD3D12CommandListPayload& Payload, FD3D12Fence &Fence)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ExecuteCommandListAndIncrementFence);

	FScopeLock Lock(&FenceCS);

	// Execute, signal, and wait (if requested)
#if UE_BUILD_DEBUG
	if (D3D12RHI_ShouldCreateWithD3DDebug())
	{
		// Debug layer will break when a command list does bad stuff. Helps identify the command list in question.
		for (uint32 i = 0; i < Payload.NumCommandLists; i++)
		{
#if ENABLE_RESIDENCY_MANAGEMENT
			if (GEnableResidencyManagement)
			{
				VERIFYD3D12RESULT(GetParentDevice()->GetResidencyManager().ExecuteCommandLists(D3DCommandQueue, &Payload.CommandLists[i], &Payload.ResidencySets[i], 1));
			}
			else
			{
				D3DCommandQueue->ExecuteCommandLists(1, &Payload.CommandLists[i]);
			}
#else
			D3DCommandQueue->ExecuteCommandLists(1, &Payload.CommandLists[i]);
#endif

#if LOG_EXECUTE_COMMAND_LISTS
			LogExecuteCommandLists(1, &(Payload.CommandLists[i]));
#endif
		}
	}
	else
#endif
	{
#if ENABLE_RESIDENCY_MANAGEMENT
		if (GEnableResidencyManagement)
		{
			VERIFYD3D12RESULT(GetParentDevice()->GetResidencyManager().ExecuteCommandLists(D3DCommandQueue, Payload.CommandLists, Payload.ResidencySets, Payload.NumCommandLists));
		}
		else
		{
			D3DCommandQueue->ExecuteCommandLists(Payload.NumCommandLists, Payload.CommandLists);
		}
#else
		D3DCommandQueue->ExecuteCommandLists(Payload.NumCommandLists, Payload.CommandLists);
#endif

#if LOG_EXECUTE_COMMAND_LISTS
		LogExecuteCommandLists(Payload.NumCommandLists, Payload.CommandLists);
#endif
	}

	checkf(Fence.GetGPUMask() == GetGPUMask(), TEXT("Fence GPU masks does not fit with the command list mask!"));

#if DEBUG_FENCES
	LogExecuteCommandLists(Payload.NumCommandLists, Payload.CommandLists);
#endif

	return Fence.Signal(QueueType);
}


void FD3D12CommandListManager::WaitOnExecuteTask()
{
	if (ExecuteTask != nullptr)
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(ExecuteTask, ENamedThreads::AnyThread);
		ExecuteTask = nullptr;
	}
}

FD3D12SyncPoint FD3D12CommandListManager::ExecuteCommandLists(TArray<FD3D12CommandListHandle>& Lists, FD3D12SyncPoint& CopyQueueSyncPoint, bool WaitForCompletion)
{
	// Still has a pending execute task, then make sure the current one is finished before executing a new command list set
	WaitOnExecuteTask();

	// Need to sync with copy queue before submit?
	if (CopyQueueSyncPoint.IsValid())
	{
		// Command queue should wait for copy queue to finish
		CopyQueueSyncPoint.GPUWait(GetQueueType());
	}

	check(ExecuteCommandListHandles.Num() == 0);

	// Do we want to kick via a task - only for direct/graphics queue for now
	bool bUseExecuteTask = (CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT) && !WaitForCompletion && GD3D12ExecuteCommandListTask;
	if (bUseExecuteTask)
	{		
		ExecuteCommandListHandles = Lists;

		// Increment the pending fence value so all object can be corrected fenced again future pending signal
		CommandListFence->AdvancePendingFenceValue();

		// Setup the future sync point already on the pending fence value
		FD3D12SyncPoint PendingSyncPoint(CommandListFence, CommandListFence->GetCurrentFence());

		ExecuteTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[this]()
			{	
				// Empty sync point because the copy queue sync point is only needed for async compute command list which can't execute
				// it's barriers on that queue (we know this is on direct queue already so synced already above
				FD3D12SyncPoint EmptySyncPoint;
				ExecuteCommandListInternal(ExecuteCommandListHandles, EmptySyncPoint, false);
				ExecuteCommandListHandles.Reset();
			}, TStatId(), nullptr, ENamedThreads::AnyThread);

		// Return pending sync point which can be used to sync with already if needed
		return PendingSyncPoint;
	}
	else
	{
		return ExecuteCommandListInternal(Lists, CopyQueueSyncPoint, WaitForCompletion);
	}
}


FD3D12SyncPoint FD3D12CommandListManager::ExecuteCommandListInternal(TArray<FD3D12CommandListHandle>& Lists, FD3D12SyncPoint& CopyQueueSyncPoint, bool WaitForCompletion)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D12ExecuteCommandListTime);
	check(CommandListFence);

	// Collect all the barrier desc data and find out if there are any graphics state transitions
	bool bNeedsResourceBarriers = false;
	bool bNeedsGraphicStateResourceBarriers = false;
	TArray<FBarrierDescInfo> BarrierDescInfos;
	BarrierDescInfos.SetNum(Lists.Num());
	{
		FScopeLock Lock(&ResourceStateCS);

		for (int32 CommandListIndex = 0; CommandListIndex < Lists.Num(); CommandListIndex++)
		{
			FBarrierDescInfo& BarrierDescInfo = BarrierDescInfos[CommandListIndex];
			uint32 BarrierCount = CollectInitialResourceBarriersAndUpdateFinalState(Lists[CommandListIndex], BarrierDescInfo);
			if (BarrierCount > 0)
			{
				bNeedsResourceBarriers = true;
				bNeedsGraphicStateResourceBarriers = bNeedsGraphicStateResourceBarriers | BarrierDescInfo.bHasGraphicStates;
			}
		}
	}

	uint64 SignaledFenceValue = -1;
	uint64 BarrierFenceValue = -1;
	FD3D12SyncPoint SyncPoint;
	FD3D12SyncPoint BarrierSyncPoint;

	FD3D12CommandListManager* BarrierCommandListManager = bNeedsGraphicStateResourceBarriers ? &GetParentDevice()->GetCommandListManager() : this;
	bool bBarriersUseDifferentQueue = (BarrierCommandListManager != this);

	int32 commandListIndex = 0;
	int32 barrierCommandListIndex = 0;

	// Close the resource barrier lists, get the raw command list pointers, and enqueue the command list handles
	// Note: All command lists will share the same fence
	FD3D12CommandListPayload CurrentCommandListPayload;

	check(Lists.Num() <= FD3D12CommandListPayload::MaxCommandListsPerPayload);
	FD3D12CommandListHandle BarrierCommandList[FD3D12CommandListPayload::MaxCommandListsPerPayload];
	if (bNeedsResourceBarriers)
	{
		// should not be copy queue
		check(GetQueueType() != ED3D12CommandQueueType::Copy);

#if UE_BUILD_DEBUG	
		if (ResourceStateCS.TryLock())
		{
			ResourceStateCS.Unlock();
		}
		else
		{
			FD3D12DynamicRHI::GetD3DRHI()->SubmissionLockStalls++;
			// We don't think this will get hit but it's possible. If we do see this happen,
			// we should evaluate how often and why this is happening
			check(0);
		}
#endif
		
		// Make sure the queue is done executing commands before trying to use the barrier command list from it
		if (bBarriersUseDifferentQueue)
		{
			BarrierCommandListManager->WaitOnExecuteTask();
		}

		// If the barrier command list manager is different then this command list then it might need another sync point
		// with the copy queue (didn't happen yet)
		if (bBarriersUseDifferentQueue && CopyQueueSyncPoint.IsValid())
		{
			CopyQueueSyncPoint.GPUWait(BarrierCommandListManager->GetQueueType());
		}

		// Kick all the command lists
		for (int32 CommandListIndex = 0; CommandListIndex < Lists.Num(); CommandListIndex++)
		{
			FD3D12CommandListHandle& commandList = Lists[CommandListIndex];
			FBarrierDescInfo& BarrierDescInfo = BarrierDescInfos[CommandListIndex];

			// Got any barriers to execute?
			if (BarrierDescInfo.BarrierDescs.Num() > 0 || BarrierDescInfo.BackBufferBarrierDescs.Num() > 0)
			{
				FD3D12CommandListHandle barrierCommandList = {};

				// Async compute cannot perform all resource transitions, and so it uses the direct context
				const uint32 numBarriers = BarrierCommandListManager->GetResourceBarrierCommandList(BarrierDescInfo, barrierCommandList);
				check(numBarriers > 0);

				// TODO: Unnecessary assignment here, but fixing this will require refactoring GetResourceBarrierCommandList
				BarrierCommandList[barrierCommandListIndex] = barrierCommandList;
				barrierCommandListIndex++;

				barrierCommandList.Close();

				// Kick on different queue and add fence (ideally this could be done per command list and chec if it has grahics transitions
				// but then command list also needs to be build on that manager and managing all those command list handles become more complicated)
				// Not sure if it's worth it yet
				if (bBarriersUseDifferentQueue)
				{
					FD3D12Fence& BarrierFence = BarrierCommandListManager->GetFence();
					checkf(BarrierFence.GetGPUMask() == GetGPUMask(), TEXT("Fence GPU masks does not fit with the command list mask!"));

					FD3D12CommandListPayload ComputeBarrierPayload;
					ComputeBarrierPayload.Reset();
					ComputeBarrierPayload.Append(barrierCommandList.CommandList(), &barrierCommandList.GetResidencySet());
					BarrierFenceValue = BarrierCommandListManager->ExecuteAndIncrementFence(ComputeBarrierPayload, BarrierFence);
					BarrierFence.GpuWait(QueueType, BarrierFenceValue);
				}
				else
				{
					check(BarrierCommandListManager == this);
					CurrentCommandListPayload.Append(barrierCommandList.CommandList(), &barrierCommandList.GetResidencySet());
				}
			}

			CurrentCommandListPayload.Append(commandList.CommandList(), &commandList.GetResidencySet());
			commandList.LogResourceBarriers();
		}

		SignaledFenceValue = ExecuteAndIncrementFence(CurrentCommandListPayload, *CommandListFence);
		SyncPoint = FD3D12SyncPoint(CommandListFence, SignaledFenceValue);

		if (bBarriersUseDifferentQueue)
		{
			BarrierSyncPoint = FD3D12SyncPoint(&BarrierCommandListManager->GetFence(), BarrierFenceValue);
		}
		else
		{
			BarrierSyncPoint = SyncPoint;
		}
	}
	else
	{
		for (int32 i = 0; i < Lists.Num(); i++)
		{
			CurrentCommandListPayload.Append(Lists[i].CommandList(), &Lists[i].GetResidencySet());
			Lists[i].LogResourceBarriers();
		}
		SignaledFenceValue = ExecuteAndIncrementFence(CurrentCommandListPayload, *CommandListFence);
		SyncPoint = FD3D12SyncPoint(CommandListFence, SignaledFenceValue);
		BarrierSyncPoint = SyncPoint;
	}

	for (int32 i = 0; i < Lists.Num(); i++)
	{
		FD3D12CommandListHandle& commandList = Lists[i];

		// Set a sync point on the command list so we know when it's current generation is complete on the GPU, then release it so it can be reused later.
		// Note this also updates the command list's command allocator
		commandList.SetSyncPoint(SyncPoint);
		ReleaseCommandList(commandList);
	}

	for (int32 i = 0; i < barrierCommandListIndex; i++)
	{
		FD3D12CommandListHandle& commandList = BarrierCommandList[i];

		// Set a sync point on the command list so we know when it's current generation is complete on the GPU, then release it so it can be reused later.
		// Note this also updates the command list's command allocator
		commandList.SetSyncPoint(BarrierSyncPoint);
		BarrierCommandListManager->ReleaseCommandList(commandList);
	}

	if (WaitForCompletion)
	{
		CommandListFence->WaitForFence(SignaledFenceValue);
		check(SyncPoint.IsComplete());
	}

	return SyncPoint;
}

void FD3D12CommandListManager::ReleaseResourceBarrierCommandListAllocator()
{
	// Release the resource barrier command allocator.
	if (ResourceBarrierCommandAllocator != nullptr)
	{
		WaitOnExecuteTask();

		ResourceBarrierCommandAllocatorManager.ReleaseCommandAllocator(ResourceBarrierCommandAllocator);
		ResourceBarrierCommandAllocator = nullptr;
	}
}

void FD3D12CommandListManager::StartTrackingCommandListTime()
{
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	check(QueueType == ED3D12CommandQueueType::Direct);
	if (!GetShouldTrackCmdListTime())
	{
		ResolvedTimingPairs.Reset();
		SetShouldTrackCmdListTime(true);
	}
#endif
}

void FD3D12CommandListManager::EndTrackingCommandListTime()
{
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	check(QueueType == ED3D12CommandQueueType::Direct);
	if (GetShouldTrackCmdListTime())
	{
		SetShouldTrackCmdListTime(false);
	}
#endif
}

void FD3D12CommandListManager::GetCommandListTimingResults(TArray<FResolvedCmdListExecTime>& OutTimingPairs, bool bUseBlockingCall)
{
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	check(!GetShouldTrackCmdListTime() && QueueType == ED3D12CommandQueueType::Direct);
	FlushPendingTimingPairs(bUseBlockingCall);
	if (bUseBlockingCall)
	{
		SortTimingResults();
	}
	OutTimingPairs = MoveTemp(ResolvedTimingPairs);
#endif
}

void FD3D12CommandListManager::SortTimingResults()
{
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	const int32 NumTimingPairs = ResolvedTimingPairs.Num();
	GetStartTimestamps().Empty(NumTimingPairs);
	GetEndTimestamps().Empty(NumTimingPairs);
	GetIdleTime().Empty(NumTimingPairs);

	if (NumTimingPairs > 0)
	{
		GetStartTimestamps().Add(ResolvedTimingPairs[0].StartTimestamp);
		GetEndTimestamps().Add(ResolvedTimingPairs[0].EndTimestamp);
		GetIdleTime().Add(0);
		for (int32 Idx = 1; Idx < NumTimingPairs; ++Idx)
		{
			const FResolvedCmdListExecTime& Prev = ResolvedTimingPairs[Idx - 1];
			const FResolvedCmdListExecTime& Cur = ResolvedTimingPairs[Idx];
			GetStartTimestamps().Add(Cur.StartTimestamp);
			GetEndTimestamps().Add(Cur.EndTimestamp);
			const uint64 Bubble = Cur.StartTimestamp >= Prev.EndTimestamp ? Cur.StartTimestamp - Prev.EndTimestamp : 0;
			uint64 &LastIdx = GetIdleTime().Last();
			GetIdleTime().Add(LastIdx + Bubble);
		}
	}
#endif
}

void FD3D12CommandListManager::FlushPendingTimingPairs(bool bBlock)
{
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	check(!GetShouldTrackCmdListTime());

	TArray<uint64> AllTimestamps;
	const uint64 NewToken = GetParentDevice()->GetCmdListExecTimeQueryHeap()->ResolveAndGetResults(AllTimestamps, CmdListTimingQueryBatchTokens[0], bBlock);

	if (bBlock)
	{
		for (int32 Idx = 0; Idx < UE_ARRAY_COUNT(CmdListTimingQueryBatchTokens); ++Idx)
		{
			CmdListTimingQueryBatchTokens[Idx] = INDEX_NONE;
		}
	}
	else
	{
		const int32 NumTokens = UE_ARRAY_COUNT(CmdListTimingQueryBatchTokens);
		for (int32 Idx = 1; Idx < NumTokens; ++Idx)
		{
			CmdListTimingQueryBatchTokens[Idx - 1] = CmdListTimingQueryBatchTokens[Idx];
		}
		CmdListTimingQueryBatchTokens[NumTokens - 1] = NewToken;
	}

	if (AllTimestamps.Num())
	{
		Algo::Sort(AllTimestamps, TLess<>());
		const int32 NumTimestamps = AllTimestamps.Num();
		check(!(NumTimestamps & 1));
		const int32 NumPairs = NumTimestamps >> 1;
		ResolvedTimingPairs.Empty(NumPairs);
		ResolvedTimingPairs.AddUninitialized(NumPairs);
		FMemory::Memcpy(ResolvedTimingPairs.GetData(), AllTimestamps.GetData(), NumTimestamps * sizeof(uint64));
	}
#endif
}

uint32 FD3D12CommandListManager::CollectInitialResourceBarriersAndUpdateFinalState(FD3D12CommandListHandle& InCommandListHandle, FBarrierDescInfo& BarrierDescInfo)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CollectInitialResourceBarriersAndUpdateFinalState);

	BarrierDescInfo.bHasGraphicStates = false;

	TArray<FD3D12PendingResourceBarrier>& PendingResourceBarriers = InCommandListHandle.PendingResourceBarriers();
	const uint32 NumPendingResourceBarriers = PendingResourceBarriers.Num();
	if (NumPendingResourceBarriers > 0)
	{
		// Reserve space for the descs
		BarrierDescInfo.BarrierDescs.Reserve(NumPendingResourceBarriers);

		for (uint32 i = 0; i < NumPendingResourceBarriers; ++i)
		{
			const FD3D12PendingResourceBarrier& PRB = PendingResourceBarriers[i];

			// Should only be doing this for the few resources that need state tracking
			check(PRB.Resource->RequiresResourceStateTracking());

			CResourceState& ResourceState = PRB.Resource->GetResourceState();

			const D3D12_RESOURCE_STATES Before = ResourceState.GetSubresourceState(PRB.SubResource);
			check(Before != D3D12_RESOURCE_STATE_TBD && Before != D3D12_RESOURCE_STATE_CORRUPT);

			// If state unknown then we don't enqueue a transition - only want to update the end state on the resource
			// and not really enqueue a transition
			const D3D12_RESOURCE_STATES After = (PRB.State != D3D12_RESOURCE_STATE_TBD) ? PRB.State : Before;
		
			if (Before != After)
			{
				if (IsDirectQueueExclusiveD3D12State(Before) || IsDirectQueueExclusiveD3D12State(After))
				{
					BarrierDescInfo.bHasGraphicStates = true;
				}

				if (PRB.Resource->IsBackBuffer() && EnumHasAnyFlags(After, BackBufferBarrierWriteTransitionTargets))
				{
					AddTransitionBarrier(BarrierDescInfo.BackBufferBarrierDescs, PRB.Resource, Before, After, PRB.SubResource);
				}
				// Special case for UAV access resources transitioning from UAV (then they need to transition from the cache hidden state instead)
				else if (PRB.Resource->GetUAVAccessResource() && EnumHasAnyFlags(Before | After, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
				{
					// After state should never be UAV here
					check(!EnumHasAnyFlags(After, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
					
					// Add the aliasing barrier
					BarrierDescInfo.BarrierDescs.AddUninitialized();
					D3D12_RESOURCE_BARRIER& Barrier = BarrierDescInfo.BarrierDescs.Last();
					Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
					Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
					Barrier.Aliasing.pResourceBefore = PRB.Resource->GetUAVAccessResource();
					Barrier.Aliasing.pResourceAfter = PRB.Resource->GetResource();

					AddTransitionBarrier(BarrierDescInfo.BarrierDescs, PRB.Resource, ResourceState.GetUAVHiddenResourceState(), After, PRB.SubResource);
				}
				else
				{
					AddTransitionBarrier(BarrierDescInfo.BarrierDescs, PRB.Resource, Before, After, PRB.SubResource);
				}
			}

			// Update the state to the what it will be after hList executes
			const CResourceState& CommandListState = InCommandListHandle.GetResourceState(PRB.Resource);
			const D3D12_RESOURCE_STATES CommandListLastState = CommandListState.GetSubresourceState(PRB.SubResource);
			const D3D12_RESOURCE_STATES LastState = (CommandListLastState != D3D12_RESOURCE_STATE_TBD) ? CommandListLastState : After;

			// Copy over the hidden UAV state if the last state in the command list was UAV (needed for patch up transition on next command list)
			if (PRB.Resource->GetUAVAccessResource() && EnumHasAnyFlags(LastState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
			{
				ResourceState.SetUAVHiddenResourceState(CommandListState.GetUAVHiddenResourceState());
			}

			if (Before != LastState)
			{
				ResourceState.SetSubresourceState(PRB.SubResource, LastState);
			}

#if ENABLE_RESIDENCY_MANAGEMENT
			BarrierDescInfo.ResidencyHandles.Add(PRB.Resource->GetResidencyHandle());
#endif // ENABLE_RESIDENCY_MANAGEMENT
		}
	}

	return BarrierDescInfo.BarrierDescs.Num() + BarrierDescInfo.BackBufferBarrierDescs.Num();
}

uint32 FD3D12CommandListManager::GetResourceBarrierCommandList(FBarrierDescInfo& InBarrierDescInfo, FD3D12CommandListHandle& hResourceBarrierList)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GetResourceBarrierCommandList);

	const uint32 BarrierCount = InBarrierDescInfo.BarrierDescs.Num() + InBarrierDescInfo.BackBufferBarrierDescs.Num();
	if (BarrierCount > 0)
	{
		// Get a new resource barrier command allocator if we don't already have one.
		if (ResourceBarrierCommandAllocator == nullptr)
		{
			ResourceBarrierCommandAllocator = ResourceBarrierCommandAllocatorManager.ObtainCommandAllocator();
		}

		hResourceBarrierList = ObtainCommandList(*ResourceBarrierCommandAllocator, InBarrierDescInfo.BackBufferBarrierDescs.Num() > 0);

#if ENABLE_RESIDENCY_MANAGEMENT
		hResourceBarrierList.UpdateResidency(InBarrierDescInfo.ResidencyHandles.GetData(), InBarrierDescInfo.ResidencyHandles.Num());
#endif // #if ENABLE_RESIDENCY_MANAGEMENT

#if DEBUG_RESOURCE_STATES
		LogResourceBarriers(InBarrierDescInfo.BarrierDescs.Num(), InBarrierDescInfo.BarrierDescs.GetData(), hResourceBarrierList.CommandList());
		LogResourceBarriers(InBarrierDescInfo.BackBufferBarrierDescs.Num(), InBarrierDescInfo.BackBufferBarrierDescs.GetData(), hResourceBarrierList.CommandList());
#endif // #if DEBUG_RESOURCE_STATES

		InBarrierDescInfo.BarrierDescs.Append(InBarrierDescInfo.BackBufferBarrierDescs);
		InBarrierDescInfo.BackBufferBarrierDescs.Empty();

		const int32 BarrierBatchMax = FD3D12DynamicRHI::GetResourceBarrierBatchSizeLimit();
		extern void ResourceBarriersSeparateRTV2SRV(ID3D12GraphicsCommandList*, const TArray<D3D12_RESOURCE_BARRIER>&, int32);
		ResourceBarriersSeparateRTV2SRV(hResourceBarrierList.GraphicsCommandList(), InBarrierDescInfo.BarrierDescs, BarrierBatchMax);
	}

	return BarrierCount;
}

bool FD3D12CommandListManager::IsComplete(const FD3D12CLSyncPoint& hSyncPoint, uint64 FenceOffset)
{
	if (!hSyncPoint)
	{
		return false;
	}

	checkf(FenceOffset == 0, TEXT("This currently doesn't support offsetting fence values."));
	return hSyncPoint.IsComplete();
}

CommandListState FD3D12CommandListManager::GetCommandListState(const FD3D12CLSyncPoint& hSyncPoint)
{
	// hSyncPoint in rare conditions goes invalid in multi-gpu environment so "check(hSyncPoint)" causes engine to crash. 
	// Instead this plug would let the command list continue if synchpoint is invalid.
	if (!hSyncPoint || hSyncPoint.Generation == hSyncPoint.CommandList.CurrentGeneration())
	{
		return CommandListState::kOpen;
	}
	else if (hSyncPoint.IsComplete())
	{
		return CommandListState::kFinished;
	}
	else
	{
		return CommandListState::kQueued;
	}
}

void FD3D12CommandListManager::WaitForCommandQueueFlush()
{
	// Make sure pending execute tasks are done
	WaitOnExecuteTask();

	if (D3DCommandQueue)
	{
		check(CommandListFence);
		const uint64 SignaledFence = CommandListFence->Signal(QueueType);
		CommandListFence->WaitForFence(SignaledFence);
	}
}

FD3D12CommandListHandle FD3D12CommandListManager::CreateCommandListHandle(FD3D12CommandAllocator& CommandAllocator)
{
	FD3D12CommandListHandle List;
	List.Create(GetParentDevice(), CommandListType, CommandAllocator, this);
	return List;
}

bool FD3D12CommandListManager::ShouldTrackCommandListTime() const
{
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	return GetShouldTrackCmdListTime();
#else
	return false;
#endif
}

FD3D12CommandListManager::FDiagnosticBuffer FD3D12CommandListManager::CreateDiagnosticBuffer(FD3D12Adapter *Adapter, FD3D12Device *Device, const D3D12_RESOURCE_DESC& Desc, const TCHAR* Name)
{
	TRefCountPtr<ID3D12Device3> D3D12Device3;
	HRESULT hr = Device->GetDevice()->QueryInterface(IID_PPV_ARGS(D3D12Device3.GetInitReference()));
	if (SUCCEEDED(hr))
	{
		void* BreadCrumbResourceAddress = VirtualAlloc(nullptr, Desc.Width, MEM_COMMIT, PAGE_READWRITE);
		if (BreadCrumbResourceAddress)
		{
			ID3D12Heap* D3D12Heap = nullptr;
			hr = D3D12Device3->OpenExistingHeapFromAddress(BreadCrumbResourceAddress, IID_PPV_ARGS(&D3D12Heap));
			if (SUCCEEDED(hr))
			{
				TRefCountPtr<FD3D12Heap> BreadCrumbHeap = new FD3D12Heap(Device, GetVisibilityMask());
				BreadCrumbHeap->SetHeap(D3D12Heap, TEXT("DiagnosticBuffer"));

				TRefCountPtr<FD3D12Resource> BreadCrumbResource;
				hr = Adapter->CreatePlacedResource(Desc, BreadCrumbHeap.GetReference(), 0, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, BreadCrumbResource.GetInitReference(), Name, false);
				if (SUCCEEDED(hr))
				{
					UE_LOG(LogD3D12RHI, Log, TEXT("[GPUBreadCrumb] Successfully setup breadcrumb resource for %s"), Name);

					return { BreadCrumbHeap, BreadCrumbResource, BreadCrumbResourceAddress, BreadCrumbResource->GetGPUVirtualAddress() };
				}
				else
				{
					BreadCrumbHeap.SafeRelease();
					VirtualFree(BreadCrumbResourceAddress, 0, MEM_RELEASE);
					UE_LOG(LogD3D12RHI, Warning, TEXT("[GPUBreadCrumb] Failed to CreatePlacedResource, error: %x"), hr);
				}
			}
			else
			{
				VirtualFree(BreadCrumbResourceAddress, 0, MEM_RELEASE);
				UE_LOG(LogD3D12RHI, Warning, TEXT("[GPUBreadCrumb] Failed to OpenExistingHeapFromAddress, error: %x"), hr);
			}
		}
		else
		{
			UE_LOG(LogD3D12RHI, Warning, TEXT("[GPUBreadCrumb] Failed to VirtualAlloc resource memory"));
		}
	}
	else
	{
		UE_LOG(LogD3D12RHI, Warning, TEXT("[GPUBreadCrumb] ID3D12Device3 not available (only available on Windows 10 1709+), error: %x"), hr);
	}

	return { nullptr, nullptr, nullptr, 0 };
}

void FD3D12CommandListManager::DestroyDiagnosticBuffer(FDiagnosticBuffer& Buffer)
{
	Buffer.Resource.SafeRelease();
	Buffer.Heap.SafeRelease();

	VirtualFree(Buffer.CpuAddress, 0, MEM_RELEASE);
	Buffer.CpuAddress = nullptr;
	Buffer.GpuAddress = 0;
}

FD3D12FenceCore* FD3D12FenceCorePool::ObtainFenceCore(uint32 GPUIndex)
{
	{
		FScopeLock Lock(&CS);
		FD3D12FenceCore* Fence = nullptr;
		if (AvailableFences[GPUIndex].Peek(Fence) && Fence->IsAvailable())
		{
			AvailableFences[GPUIndex].Dequeue(Fence);
			return Fence;
		}
	}

	return new FD3D12FenceCore(GetParentAdapter(), 0, GPUIndex);
}

void FD3D12FenceCorePool::ReleaseFenceCore(FD3D12FenceCore* Fence, uint64 CurrentFenceValue)
{
	FScopeLock Lock(&CS);
	Fence->FenceValueAvailableAt = CurrentFenceValue;
	AvailableFences[Fence->GetGPUIndex()].Enqueue(Fence);
}

void FD3D12FenceCorePool::Destroy()
{
	for (uint32 GPUIndex = 0; GPUIndex < MAX_NUM_GPUS; ++GPUIndex)
	{
		FD3D12FenceCore* Fence = nullptr;
		while (AvailableFences[GPUIndex].Dequeue(Fence))
		{
			delete(Fence);
		}
	}
}

void FD3D12CommandListPayload::Reset()
{
	NumCommandLists = 0;
	FMemory::Memzero(CommandLists);
	FMemory::Memzero(ResidencySets);
}

void FD3D12CommandListPayload::Append(ID3D12CommandList* CommandList, FD3D12ResidencySet* Set)
{
	check(NumCommandLists < FD3D12CommandListPayload::MaxCommandListsPerPayload);

	CommandLists[NumCommandLists] = CommandList;
	ResidencySets[NumCommandLists] = Set;
	NumCommandLists++;
}