// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RHIPrivate.h"
#include "D3D12CommandList.h"
#include "D3D12RHIBridge.h"
#include "RHIValidation.h"

static int64 GCommandListIDCounter = 0;
static uint64 GenerateCommandListID()
{
	return FPlatformAtomics::InterlockedIncrement(&GCommandListIDCounter);
}

void FD3D12CommandListHandle::AddPendingResourceBarrier(FD3D12Resource* Resource, D3D12_RESOURCE_STATES State, uint32 SubResource)
{
	check(CommandListData);

	FD3D12PendingResourceBarrier PRB = { Resource, State, SubResource };

	CommandListData->PendingResourceBarriers.Add(PRB);
	CommandListData->CurrentOwningContext->numPendingBarriers++;
}

void FD3D12CommandListHandle::AddTransitionBarrier(FD3D12Resource* pResource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After, uint32 Subresource)
{
	check(CommandListData);
	if (Before != After)
	{
		int32 NumAdded = CommandListData->ResourceBarrierBatcher.AddTransition(pResource, Before, After, Subresource);
		CommandListData->CurrentOwningContext->numBarriers += NumAdded;

		pResource->UpdateResidency(*this);
	}
	else
	{
		ensureMsgf(0, TEXT("AddTransitionBarrier: Before == After (%d)"), (uint32)Before);
	}
}

void FD3D12CommandListHandle::AddUAVBarrier()
{
	check(CommandListData);
	CommandListData->ResourceBarrierBatcher.AddUAV();
	CommandListData->CurrentOwningContext->numBarriers++;
}

void FD3D12CommandListHandle::AddAliasingBarrier(FD3D12Resource* pResource)
{
	check(CommandListData);
	CommandListData->ResourceBarrierBatcher.AddAliasingBarrier(pResource->GetResource());
	CommandListData->CurrentOwningContext->numBarriers++;
}

void FD3D12CommandListHandle::Create(FD3D12Device* ParentDevice, D3D12_COMMAND_LIST_TYPE CommandListType, FD3D12CommandAllocator& CommandAllocator, FD3D12CommandListManager* InCommandListManager)
{
	check(!CommandListData);

	CommandListData = new FD3D12CommandListData(ParentDevice, CommandListType, CommandAllocator, InCommandListManager);

	CommandListData->AddRef();
}

FD3D12CommandListHandle::FD3D12CommandListData::FD3D12CommandListData(FD3D12Device* ParentDevice, D3D12_COMMAND_LIST_TYPE InCommandListType, FD3D12CommandAllocator& CommandAllocator, FD3D12CommandListManager* InCommandListManager)
	: FD3D12DeviceChild(ParentDevice)
	, FD3D12SingleNodeGPUObject(ParentDevice->GetGPUMask())
	, CommandListManager(InCommandListManager)
	, CurrentOwningContext(nullptr)
	, CommandListType(InCommandListType)
	, CurrentCommandAllocator(&CommandAllocator)
	, CurrentGeneration(1)
	, LastCompleteGeneration(0)
	, IsClosed(false)
	, bShouldTrackStartEndTime(false)
	, PendingResourceBarriers()
	, ResidencySet(nullptr)
	, CommandListID(GenerateCommandListID())
{
	VERIFYD3D12RESULT(ParentDevice->GetDevice()->CreateCommandList(GetGPUMask().GetNative(), CommandListType, CommandAllocator, nullptr, IID_PPV_ARGS(CommandList.GetInitReference())));
	INC_DWORD_STAT(STAT_D3D12NumCommandLists);

#if PLATFORM_WINDOWS
	// Optionally obtain the ID3D12GraphicsCommandList1 & ID3D12GraphicsCommandList2 interface, we don't check the HRESULT.
	CommandList->QueryInterface(IID_PPV_ARGS(CommandList1.GetInitReference()));
	CommandList->QueryInterface(IID_PPV_ARGS(CommandList2.GetInitReference()));
#endif

#if PLATFORM_SUPPORTS_VARIABLE_RATE_SHADING
	CommandList->QueryInterface(IID_PPV_ARGS(CommandList5.GetInitReference()));
#endif

#if D3D12_RHI_RAYTRACING
	// Obtain ID3D12CommandListRaytracingPrototype if parent device supports ray tracing and this is a compatible command list type (compute or graphics).
	if (ParentDevice->GetDevice5() && (InCommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT || InCommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE))
	{
		VERIFYD3D12RESULT(CommandList->QueryInterface(IID_PPV_ARGS(RayTracingCommandList.GetInitReference())));
	}
#endif // D3D12_RHI_RAYTRACING

#if NAME_OBJECTS
	TArray<FStringFormatArg> Args;
	Args.Add(LexToString(ParentDevice->GetGPUIndex()));
	FString Name = FString::Format(TEXT("FD3D12CommandListData (GPU {0})"), Args);
	SetName(CommandList, Name.GetCharArray().GetData());
#endif

#if NV_AFTERMATH
	AftermathHandle = nullptr;

	if (GDX12NVAfterMathEnabled)
	{
		GFSDK_Aftermath_Result Result = GFSDK_Aftermath_DX12_CreateContextHandle(CommandList, &AftermathHandle);

		check(Result == GFSDK_Aftermath_Result_Success);
		ParentDevice->GetGPUProfiler().RegisterCommandList(CommandList, AftermathHandle);
	}
#endif

	// Initially start with all lists closed.  We'll open them as we allocate them.
	Close();

	PendingResourceBarriers.Reserve(256);

	ResidencySet = D3DX12Residency::CreateResidencySet(ParentDevice->GetResidencyManager());
}

FD3D12CommandListHandle::FD3D12CommandListData::~FD3D12CommandListData()
{
#if NV_AFTERMATH
	if (AftermathHandle)
	{
		GetParentDevice()->GetGPUProfiler().UnregisterCommandList(AftermathHandle);

		GFSDK_Aftermath_Result Result = GFSDK_Aftermath_ReleaseContextHandle(AftermathHandle);

		check(Result == GFSDK_Aftermath_Result_Success);
	}
#endif

	CommandList.SafeRelease();
	DEC_DWORD_STAT(STAT_D3D12NumCommandLists);

	D3DX12Residency::DestroyResidencySet(GetParentDevice()->GetResidencyManager(), ResidencySet);
}

void FD3D12CommandListHandle::FD3D12CommandListData::Close()
{
	if (!IsClosed)
	{
		FlushResourceBarriers();
		if (bShouldTrackStartEndTime)
		{
			FinishTrackingCommandListTime();
		}
		VERIFYD3D12RESULT(CommandList->Close());

		D3DX12Residency::Close(ResidencySet);
		IsClosed = true;
	}
}

void FD3D12CommandListHandle::FD3D12CommandListData::FlushResourceBarriers()
{
#if DEBUG_RESOURCE_STATES
	// Keep track of all the resource barriers that have been submitted to the current command list.
	const TArray<D3D12_RESOURCE_BARRIER>& Barriers = ResourceBarrierBatcher.GetBarriers();
	if (Barriers.Num())
	{
		ResourceBarriers.Append(Barriers.GetData(), Barriers.Num());
	}
#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
	const TArray<D3D12_RESOURCE_BARRIER>& BackBufferBarriers = ResourceBarrierBatcher.GetBackBufferBarriers();
	if (BackBufferBarriers.Num())
	{
		ResourceBarriers.Append(BackBufferBarriers.GetData(), BackBufferBarriers.Num());
	}
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
#endif // #if DEBUG_RESOURCE_STATES

	ResourceBarrierBatcher.Flush(GetParentDevice(), CommandList, FD3D12DynamicRHI::GetResourceBarrierBatchSizeLimit());
}

void FD3D12CommandListHandle::FD3D12CommandListData::Reset(FD3D12CommandAllocator& CommandAllocator, bool bTrackExecTime)
{
	VERIFYD3D12RESULT(CommandList->Reset(CommandAllocator, nullptr));

	CurrentCommandAllocator = &CommandAllocator;
	IsClosed = false;

	// Indicate this command allocator is being used.
	CurrentCommandAllocator->IncrementPendingCommandLists();

	CleanupActiveGenerations();

	// Remove all pendering barriers from the command list
	PendingResourceBarriers.Reset();

	// Empty tracked resource state for this command list
	TrackedResourceState.Empty();

	// If this fails there are too many concurrently open residency sets. Increase the value of MAX_NUM_CONCURRENT_CMD_LISTS
	// in the residency manager. Beware, this will increase the CPU memory usage of every tracked resource.
	D3DX12Residency::Open(ResidencySet);

	// If this fails then some previous resource barriers were never submitted.
	check(ResourceBarrierBatcher.GetBarriers().Num() == 0);
#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
	check(ResourceBarrierBatcher.GetBackBufferBarriers().Num() == 0);
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING

#if DEBUG_RESOURCE_STATES
	ResourceBarriers.Reset();
#endif

	if (bTrackExecTime)
	{
		StartTrackingCommandListTime();
	}

	CommandListID = GenerateCommandListID();
}

int32 FD3D12CommandListHandle::FD3D12CommandListData::CreateAndInsertTimestampQuery()
{	
	FD3D12LinearQueryHeap* QueryHeap = GetParentDevice()->GetCmdListExecTimeQueryHeap();
	check(QueryHeap);
	return QueryHeap->EndQuery(this);
}

void FD3D12CommandListHandle::FD3D12CommandListData::StartTrackingCommandListTime()
{
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	check(!IsClosed && !bShouldTrackStartEndTime);
	bShouldTrackStartEndTime = true;
	CreateAndInsertTimestampQuery();
#endif
}

void FD3D12CommandListHandle::FD3D12CommandListData::FinishTrackingCommandListTime()
{
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	check(!IsClosed && bShouldTrackStartEndTime);
	bShouldTrackStartEndTime = false;
	CreateAndInsertTimestampQuery();
#endif
}

void inline FD3D12CommandListHandle::FD3D12CommandListData::FCommandListResourceState::ConditionalInitalize(FD3D12Resource* pResource, CResourceState& ResourceState)
{
	// If there is no entry, all subresources should be in the resource's TBD state.
	// This means we need to have pending resource barrier(s).
	if (!ResourceState.CheckResourceStateInitalized())
	{
		ResourceState.Initialize(pResource->GetSubresourceCount());
		check(ResourceState.CheckResourceState(D3D12_RESOURCE_STATE_TBD));
	}

	check(ResourceState.CheckResourceStateInitalized());
}

CResourceState& FD3D12CommandListHandle::FD3D12CommandListData::FCommandListResourceState::GetResourceState(FD3D12Resource* pResource)
{
	// Only certain resources should use this
	check(pResource->RequiresResourceStateTracking());

	CResourceState& ResourceState = ResourceStates.FindOrAdd(pResource);
	ConditionalInitalize(pResource, ResourceState);
	return ResourceState;
}

void FD3D12CommandListHandle::FD3D12CommandListData::FCommandListResourceState::Empty()
{
	ResourceStates.Empty();
}

void FD3D12CommandListHandle::Execute(bool WaitForCompletion)
{
	check(CommandListData);
	CommandListData->CommandListManager->ExecuteCommandList(*this, WaitForCompletion);
}

FD3D12CommandAllocator::FD3D12CommandAllocator(ID3D12Device* InDevice, const D3D12_COMMAND_LIST_TYPE& InType)
	: PendingCommandListCount(0)
{
	Init(InDevice, InType);
}

FD3D12CommandAllocator::~FD3D12CommandAllocator()
{
	CommandAllocator.SafeRelease();
	DEC_DWORD_STAT(STAT_D3D12NumCommandAllocators);
}

void FD3D12CommandAllocator::Init(ID3D12Device* InDevice, const D3D12_COMMAND_LIST_TYPE& InType)
{
	check(CommandAllocator.GetReference() == nullptr);
	VERIFYD3D12RESULT(InDevice->CreateCommandAllocator(InType, IID_PPV_ARGS(CommandAllocator.GetInitReference())));
	INC_DWORD_STAT(STAT_D3D12NumCommandAllocators);
}


namespace D3D12RHI
{
	static FCriticalSection CopyQueueCS;

	void GetGfxCommandListAndQueue(FRHICommandList& RHICmdList, void*& OutGfxCmdList, void*& OutCommandQueue)
	{
		IRHICommandContext& RHICmdContext = RHICmdList.GetContext();
		FD3D12CommandContextBase& BaseCmdContext = (FD3D12CommandContextBase&)RHICmdContext;
		check(BaseCmdContext.IsDefaultContext());
		FD3D12CommandContext& CmdContext = (FD3D12CommandContext&)BaseCmdContext;
		FD3D12CommandListHandle& NativeCmdList = CmdContext.CommandListHandle;
		/*
				FD3D12DynamicRHI* RHI = GetDynamicRHI<FD3D12DynamicRHI>();
				FD3D12Device* Device = RHI->GetAdapter(0).GetDevice(0);
				FD3D12CommandListManager& CommandListManager = Device->GetCommandListManager();
				FD3D12CommandAllocatorManager& CommandAllocatorManager = Device->GetTextureStreamingCommandAllocatorManager();
				FD3D12CommandAllocator* CurrentCommandAllocator = CommandAllocatorManager.ObtainCommandAllocator();
				FD3D12CommandListHandle hCommandList = Device->GetCopyCommandListManager().ObtainCommandList(*CurrentCommandAllocator);
		*/
		OutGfxCmdList = NativeCmdList.GraphicsCommandList();

		ID3D12CommandQueue* CommandQueue = BaseCmdContext.GetParentAdapter()->GetDevice(0)->GetD3DCommandQueue();
		OutCommandQueue = CommandQueue;
	}

	D3D12RHI_API void ExecuteCodeWithCopyCommandQueueUsage(TFunction<void(ID3D12CommandQueue*)>&& CodeToRun)
	{
		IRHICommandContext* Context = GDynamicRHI->RHIGetDefaultContext();
		checkSlow(Context);
		FD3D12CommandContextBase& BaseCmdContext = (FD3D12CommandContextBase&)*Context;

		ID3D12CommandQueue* CommandQueue = BaseCmdContext.GetParentAdapter()->GetDevice(0)->GetD3DCommandQueue(ED3D12CommandQueueType::Copy);
		checkSlow(CommandQueue);

		{
			FScopeLock Lock(&CopyQueueCS);
			CodeToRun(CommandQueue);
		}
	}

}