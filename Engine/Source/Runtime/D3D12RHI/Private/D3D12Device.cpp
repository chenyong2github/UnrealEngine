// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Device.cpp: D3D device RHI implementation.
=============================================================================*/
#include "D3D12RHIPrivate.h"

namespace D3D12RHI
{
	extern void EmptyD3DSamplerStateCache();
}
using namespace D3D12RHI;

FD3D12Device::FD3D12Device() :
	FD3D12Device(FRHIGPUMask::GPU0(), nullptr)
	{
	}

FD3D12Device::FD3D12Device(FRHIGPUMask InGPUMask, FD3D12Adapter* InAdapter) :
	FD3D12SingleNodeGPUObject(InGPUMask),
	FD3D12AdapterChild(InAdapter),
	CommandListManager(nullptr),
	CopyCommandListManager(nullptr),
	AsyncCommandListManager(nullptr),
	TextureStreamingCommandAllocatorManager(this, D3D12_COMMAND_LIST_TYPE_COPY),
	RTVAllocator(InGPUMask, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256),
	DSVAllocator(InGPUMask, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 256),
	SRVAllocator(InGPUMask, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024),
	UAVAllocator(InGPUMask, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024),
#if USE_STATIC_ROOT_SIGNATURE
	CBVAllocator(InGPUMask, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048),
#endif
	SamplerAllocator(InGPUMask, FD3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 128),
	GlobalSamplerHeap(this, InGPUMask),
	GlobalViewHeap(this, InGPUMask),
	OcclusionQueryHeap(this, D3D12_QUERY_TYPE_OCCLUSION, 65536, 4 /*frames to keep results */ * 1 /*batches per frame*/),
	TimestampQueryHeap(this, D3D12_QUERY_TYPE_TIMESTAMP, 8192, 4 /*frames to keep results */ * 5 /*batches per frame*/ ),
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	CmdListExecTimeQueryHeap(new FD3D12LinearQueryHeap(this, D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 8192)),
#endif
	DefaultBufferAllocator(this, FRHIGPUMask::All()), //Note: Cross node buffers are possible 
	SamplerID(0),
	DefaultFastAllocator(this, FRHIGPUMask::All(), D3D12_HEAP_TYPE_UPLOAD, 1024 * 1024 * 4),
	TextureAllocator(this, FRHIGPUMask::All()),
	GPUProfilingData(this)
{
	InitPlatformSpecific();
}

FD3D12Device::~FD3D12Device()
{
#if D3D12_RHI_RAYTRACING
	DestroyRayTracingDescriptorCache(); // #dxr_todo UE-72158: unify RT descriptor cache with main FD3D12DescriptorCache
#endif

	// Cleanup the allocator near the end, as some resources may be returned to the allocator or references are shared by multiple GPUs
	DefaultBufferAllocator.FreeDefaultBufferPools();

	DefaultFastAllocator.Destroy();

	TextureAllocator.CleanUpAllocations();
	TextureAllocator.Destroy();

	delete CommandListManager;
	delete CopyCommandListManager;
	delete AsyncCommandListManager;
#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
	delete BackBufferWriteBarrierTracker;
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
}

ID3D12Device* FD3D12Device::GetDevice()
{
	return GetParentAdapter()->GetD3DDevice();
}

#if D3D12_RHI_RAYTRACING
ID3D12Device5* FD3D12Device::GetDevice5()
{
	return GetParentAdapter()->GetD3DDevice5();
}

ID3D12Device7* FD3D12Device::GetDevice7()
{
	return GetParentAdapter()->GetD3DDevice7();
}
#endif // D3D12_RHI_RAYTRACING

FD3D12LinearQueryHeap* FD3D12Device::GetCmdListExecTimeQueryHeap()
{
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	return CmdListExecTimeQueryHeap.Get();
#else
	return nullptr;
#endif
}

FD3D12DynamicRHI* FD3D12Device::GetOwningRHI()
{ 
	return GetParentAdapter()->GetOwningRHI();
}

void FD3D12Device::CreateCommandContexts()
{
	check(CommandContextArray.Num() == 0);
	check(AsyncComputeContextArray.Num() == 0);

	const uint32 NumContexts = FTaskGraphInterface::Get().GetNumWorkerThreads() + 1;
	const uint32 NumAsyncComputeContexts = GEnableAsyncCompute ? 1 : 0;
	
	// We never make the default context free for allocation by the context containers
	CommandContextArray.Reserve(NumContexts);
	FreeCommandContexts.Reserve(NumContexts - 1);
	AsyncComputeContextArray.Reserve(NumAsyncComputeContexts);

	for (uint32 i = 0; i < NumContexts; ++i)
	{	
		const bool bIsDefaultContext = (i == 0);
		const bool bIsAsyncComputeContext = false;
		FD3D12CommandContext* NewCmdContext = GetOwningRHI()->CreateCommandContext(this, bIsDefaultContext, bIsAsyncComputeContext);

		// without that the first RHIClear would get a scissor rect of (0,0)-(0,0) which means we get a draw call clear 
		NewCmdContext->RHISetScissorRect(false, 0, 0, 0, 0);

		CommandContextArray.Add(NewCmdContext);

		// Make available all but the first command context for parallel threads
		if (!bIsDefaultContext)
		{
			FreeCommandContexts.Add(CommandContextArray[i]);
		}
	}

	for (uint32 i = 0; i < NumAsyncComputeContexts; ++i)
	{		
		const bool bIsDefaultContext = (i == 0); //-V547
		const bool bIsAsyncComputeContext = true;
		FD3D12CommandContext* NewCmdContext = GetOwningRHI()->CreateCommandContext(this, bIsDefaultContext, bIsAsyncComputeContext);
	
		AsyncComputeContextArray.Add(NewCmdContext);
	}

	CommandContextArray[0]->OpenCommandList();
	if (GEnableAsyncCompute)
	{
		AsyncComputeContextArray[0]->OpenCommandList();
	}
}

bool FD3D12Device::IsGPUIdle()
{
	FD3D12Fence& Fence = CommandListManager->GetFence();
	return Fence.IsFenceComplete(Fence.GetLastSignaledFence());
}

#if (PLATFORM_WINDOWS || PLATFORM_HOLOLENS)
typedef HRESULT(WINAPI *FDXGIGetDebugInterface1)(UINT, REFIID, void **);
#endif

ID3D12CommandQueue* gD3D12CommandQueue;

void FD3D12Device::SetupAfterDeviceCreation()
{
	ID3D12Device* Direct3DDevice = GetParentAdapter()->GetD3DDevice();

	GRHISupportsArrayIndexFromAnyShader = true;

#if (PLATFORM_WINDOWS || PLATFORM_HOLOLENS)
	// Check if we're running under GPU capture
	bool bUnderGPUCapture = false;

	// RenderDoc
	{
		IID RenderDocID;
		if (SUCCEEDED(IIDFromString(L"{A7AA6116-9C8D-4BBA-9083-B4D816B71B78}", &RenderDocID)))
		{
			TRefCountPtr<IUnknown> RenderDoc;
			if (SUCCEEDED(Direct3DDevice->QueryInterface(RenderDocID, (void**)RenderDoc.GetInitReference())))
			{
				// Running under RenderDoc, so enable capturing mode
				bUnderGPUCapture = true;
			}
		}
	}

	// Intel GPA
	{
		TRefCountPtr<IUnknown> IntelGPA;
		static const IID IntelGPAID = { 0xCCFFEF16, 0x7B69, 0x468F, {0xBC, 0xE3, 0xCD, 0x95, 0x33, 0x69, 0xA3, 0x9A} };

		if (SUCCEEDED(Direct3DDevice->QueryInterface(IntelGPAID, (void**)(IntelGPA.GetInitReference()))))
		{
			// Running under Intel GPA, so enable capturing mode
			bUnderGPUCapture = true;
		}
	}

	// AMD RGP profiler
	if (GEmitRgpFrameMarkers && GetOwningRHI()->GetAmdAgsContext())
	{
		// Running on AMD with RGP profiling enabled, so enable capturing mode
		bUnderGPUCapture = true;
	}
#if USE_PIX

	// Only check windows version on PLATFORM_WINDOWS - Hololens can assume windows > 10.0 so the condition would always be true.
#if PLATFORM_WINDOWS
	// PIX (note that DXGIGetDebugInterface1 requires Windows 8.1 and up)
	if (FPlatformMisc::VerifyWindowsVersion(6, 3))
#endif
	{
		FDXGIGetDebugInterface1 DXGIGetDebugInterface1FnPtr = nullptr;

#if PLATFORM_HOLOLENS
		DXGIGetDebugInterface1FnPtr = DXGIGetDebugInterface1;
#else
		// CreateDXGIFactory2 is only available on Win8.1+, find it if it exists
		HMODULE DxgiDLL = LoadLibraryA("dxgi.dll");
		if (DxgiDLL)
		{
#pragma warning(push)
#pragma warning(disable: 4191) // disable the "unsafe conversion from 'FARPROC' to 'blah'" warning
			DXGIGetDebugInterface1FnPtr = (FDXGIGetDebugInterface1)(GetProcAddress(DxgiDLL, "DXGIGetDebugInterface1"));
#pragma warning(pop)
			FreeLibrary(DxgiDLL);
		}
#endif
		
		if (DXGIGetDebugInterface1FnPtr)
		{
			IID GraphicsAnalysisID;
			if (SUCCEEDED(IIDFromString(L"{9F251514-9D4D-4902-9D60-18988AB7D4B5}", &GraphicsAnalysisID)))
			{
				TRefCountPtr<IUnknown> GraphicsAnalysis;
				if (SUCCEEDED(DXGIGetDebugInterface1FnPtr(0, GraphicsAnalysisID, (void**)GraphicsAnalysis.GetInitReference())))
				{
					// Running under PIX, so enable capturing mode
					bUnderGPUCapture = true;
				}
			}
		}
	}
#endif // USE_PIX

	if(bUnderGPUCapture)
	{
		GDynamicRHI->EnableIdealGPUCaptureOptions(true);
	}
#endif // (PLATFORM_WINDOWS || PLATFORM_HOLOLENS)

	// Init offline descriptor allocators
	RTVAllocator.Init(Direct3DDevice);
	DSVAllocator.Init(Direct3DDevice);
	SRVAllocator.Init(Direct3DDevice);
	UAVAllocator.Init(Direct3DDevice);
#if USE_STATIC_ROOT_SIGNATURE
	CBVAllocator.Init(Direct3DDevice);
#endif
	SamplerAllocator.Init(Direct3DDevice);

	GlobalSamplerHeap.Init(NUM_SAMPLER_DESCRIPTORS);

	// This value can be tuned on a per app basis. I.e. most apps will never run into descriptor heap pressure so
	// can make this global heap smaller
	uint32 NumGlobalViewDesc = GGlobalViewHeapSize;

	const D3D12_RESOURCE_BINDING_TIER Tier = GetParentAdapter()->GetResourceBindingTier();
	uint32 MaximumSupportedHeapSize = NUM_VIEW_DESCRIPTORS_TIER_1;

	switch (Tier)
	{
	case D3D12_RESOURCE_BINDING_TIER_1:
		MaximumSupportedHeapSize = NUM_VIEW_DESCRIPTORS_TIER_1;
		break;
	case D3D12_RESOURCE_BINDING_TIER_2:
		MaximumSupportedHeapSize = NUM_VIEW_DESCRIPTORS_TIER_2;
		break;
	case D3D12_RESOURCE_BINDING_TIER_3:
	default:
		MaximumSupportedHeapSize = NUM_VIEW_DESCRIPTORS_TIER_3;
		break;
	}
	check(NumGlobalViewDesc <= MaximumSupportedHeapSize);
		
	GlobalViewHeap.Init(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, NumGlobalViewDesc);

	// Init the occlusion and timestamp query heaps
	OcclusionQueryHeap.Init();
	TimestampQueryHeap.Init();

	CommandListManager->Create(*FString::Printf(TEXT("3D Queue %d"), GetGPUIndex()));
	gD3D12CommandQueue = CommandListManager->GetD3DCommandQueue();
	CopyCommandListManager->Create(*FString::Printf(TEXT("Copy Queue %d"), GetGPUIndex()));
	AsyncCommandListManager->Create(*FString::Printf(TEXT("Compute Queue %d"), GetGPUIndex()), 0, AsyncComputePriority_Default);

	// Needs to be called before creating command contexts
	UpdateConstantBufferPageProperties();

	CreateCommandContexts();

	UpdateMSAASettings();

	GPUProfilingData.Init();
}

void FD3D12Device::UpdateConstantBufferPageProperties()
{
	//In genera, constant buffers should use write-combine memory (i.e. upload heaps) for optimal performance
	bool bForceWriteBackConstantBuffers = false;

	if (bForceWriteBackConstantBuffers)
	{
		ConstantBufferPageProperties = GetDevice()->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_UPLOAD);
		ConstantBufferPageProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	}
	else
	{
		ConstantBufferPageProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	}
}

void FD3D12Device::UpdateMSAASettings()
{
	check(DX_MAX_MSAA_COUNT == 8);

	// quality levels are only needed for CSAA which we cannot use with custom resolves

	// 0xffffffff means not available
	AvailableMSAAQualities[0] = 0xffffffff;
	AvailableMSAAQualities[1] = 0xffffffff;
	AvailableMSAAQualities[2] = 0;
	AvailableMSAAQualities[3] = 0xffffffff;
	AvailableMSAAQualities[4] = 0;
	AvailableMSAAQualities[5] = 0xffffffff;
	AvailableMSAAQualities[6] = 0xffffffff;
	AvailableMSAAQualities[7] = 0xffffffff;
	AvailableMSAAQualities[8] = 0;
}

void FD3D12Device::Cleanup()
{
	const auto ValidateCommandQueue = [this](ED3D12CommandQueueType QueueType, const TCHAR* Name)
	{
		ID3D12CommandQueue* CommandQueue = GetD3DCommandQueue(QueueType);
		if (CommandQueue)
		{
			CommandQueue->AddRef();
			int RefCount = CommandQueue->Release();
			if (RefCount < 0)
			{
				UE_LOG(LogD3D12RHI, Error, TEXT("%s CommandQueue is already destroyed  (Refcount %d)!"), Name, RefCount);
			}
			else if (RefCount > 2)
			{
				UE_LOG(LogD3D12RHI, Warning, TEXT("%s CommandQueue is leaking (Refcount %d)"), Name, RefCount);
			}
			ensure(RefCount >= 1);
		}
	};

	// Validate that all the D3D command queues are still valid (temp code to check for a shutdown crash)
	ValidateCommandQueue(ED3D12CommandQueueType::Default, TEXT("Direct"));
	ValidateCommandQueue(ED3D12CommandQueueType::Copy, TEXT("Copy"));
	ValidateCommandQueue(ED3D12CommandQueueType::Async, TEXT("Async"));

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	// We want to make sure that all operations like FlushPendingDeletes happen only for the current device.
	SCOPED_GPU_MASK(RHICmdList, GPUMask);

	// Wait for the command queues to flush
	CommandListManager->WaitForCommandQueueFlush();
	CopyCommandListManager->WaitForCommandQueueFlush();
	AsyncCommandListManager->WaitForCommandQueueFlush();

	check(!GIsCriticalError);

	SamplerMap.Empty();

	ReleasePooledUniformBuffers();

	// Flush all pending deletes before destroying the device or any command contexts.
	FRHIResource::FlushPendingDeletes();

	// Delete array index 0 (the default context) last
	for (int32 i = CommandContextArray.Num() - 1; i >= 0; i--)
	{
		delete CommandContextArray[i];
		CommandContextArray[i] = nullptr;
	}

	// Delete array index 0 last
	for (int32 i = AsyncComputeContextArray.Num() - 1; i >= 0; i--)
	{
		delete AsyncComputeContextArray[i];
		AsyncComputeContextArray[i] = nullptr;
	}


	/*
	// Cleanup thread resources
	for (int32 index; (index = FPlatformAtomics::InterlockedDecrement(&NumThreadDynamicHeapAllocators)) != -1;)
	{
		FD3D12DynamicHeapAllocator* pHeapAllocator = ThreadDynamicHeapAllocatorArray[index];
		pHeapAllocator->ReleaseAllResources();
		delete(pHeapAllocator);
	}
	*/

	CommandListManager->Destroy();
	CopyCommandListManager->Destroy();
	AsyncCommandListManager->Destroy();

	OcclusionQueryHeap.Destroy();
	TimestampQueryHeap.Destroy();

#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	CmdListExecTimeQueryHeap = nullptr;
#endif

	D3DX12Residency::DestroyResidencyManager(ResidencyManager);

	// Release buffered timestamp queries
	GPUProfilingData.FrameTiming.ReleaseResource();
}

FD3D12CommandListManager* FD3D12Device::GetCommandListManager(ED3D12CommandQueueType InQueueType) const
{
	switch (InQueueType)
	{
	case ED3D12CommandQueueType::Default:
		check(CommandListManager->GetQueueType() == InQueueType);
		return CommandListManager;
	case ED3D12CommandQueueType::Async:
		check(AsyncCommandListManager->GetQueueType() == InQueueType);
		return AsyncCommandListManager;
	case ED3D12CommandQueueType::Copy:
		check(CopyCommandListManager->GetQueueType() == InQueueType);
		return CopyCommandListManager;
	default:
		check(false);
		return nullptr;
	}
}

void FD3D12Device::RegisterGPUWork(uint32 NumPrimitives, uint32 NumVertices)
{
	GetGPUProfiler().RegisterGPUWork(NumPrimitives, NumVertices);
}

void FD3D12Device::RegisterGPUDispatch(FIntVector GroupCount)
{
	GetGPUProfiler().RegisterGPUDispatch(GroupCount);
}

void FD3D12Device::BlockUntilIdle()
{
	GetDefaultCommandContext().FlushCommands();

	if (GEnableAsyncCompute)
	{
		GetDefaultAsyncComputeContext().FlushCommands();
	}

	GetCommandListManager().WaitForCommandQueueFlush();
	GetCopyCommandListManager().WaitForCommandQueueFlush();
	GetAsyncCommandListManager().WaitForCommandQueueFlush();
}
