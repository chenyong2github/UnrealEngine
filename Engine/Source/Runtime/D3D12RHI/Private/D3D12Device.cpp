// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RHIPrivate.h"
#include "D3D12RayTracing.h"

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
	DescriptorHeapManager(this),
	BindlessDescriptorManager(this),
	OfflineDescriptorManagers{
		FD3D12OfflineDescriptorManager(this),
		FD3D12OfflineDescriptorManager(this),
		FD3D12OfflineDescriptorManager(this),
		FD3D12OfflineDescriptorManager(this),
	},
	GlobalSamplerHeap(this),
	OnlineDescriptorManager(this),
	OcclusionQueryHeap(this, D3D12_QUERY_TYPE_OCCLUSION, 65536, 4 /*frames to keep results */, 1 /*batches per frame*/),
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	CmdListExecTimeQueryHeap(new FD3D12LinearQueryHeap(this, D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 8192)),
#endif
	DefaultBufferAllocator(this, FRHIGPUMask::All()), //Note: Cross node buffers are possible 
	SamplerID(0),
	DefaultFastAllocator(this, FRHIGPUMask::All(), D3D12_HEAP_TYPE_UPLOAD, 1024 * 1024 * 4),
	TextureAllocator(this, FRHIGPUMask::All()),
	GPUProfilingData(this)
{
	for (uint32 QueueType = 0; QueueType < (uint32)ED3D12CommandQueueType::Count; ++QueueType)
	{
		TimestampQueryHeaps[QueueType] = new FD3D12QueryHeap(this, D3D12_QUERY_TYPE_TIMESTAMP, 8192, 4 /*frames to keep results */, 5 /*batches per frame*/);
	}

	InitPlatformSpecific();
}

FD3D12Device::~FD3D12Device()
{
#if D3D12_RHI_RAYTRACING
	delete RayTracingCompactionRequestHandler;
	RayTracingCompactionRequestHandler = nullptr;

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

	uint32 WorkerThreadCount = FTaskGraphInterface::Get().GetNumWorkerThreads();

#if PLATFORM_WINDOWS
	bool bEnableReserveWorkers = true; // by default
	GConfig->GetBool(TEXT("TaskGraph"), TEXT("EnableReserveWorkers"), bEnableReserveWorkers, GEngineIni);
	if (bEnableReserveWorkers)
	{
		WorkerThreadCount *= 2;
	}
#endif

	const uint32 NumContexts = WorkerThreadCount + 1;
	const uint32 NumAsyncComputeContexts = GEnableAsyncCompute ? 1 : 0;
	
	// We never make the default context free for allocation by the context containers
	CommandContextArray.Reserve(NumContexts);
	FreeCommandContexts.Reserve(NumContexts - 1);
	AsyncComputeContextArray.Reserve(NumAsyncComputeContexts);

	for (uint32 i = 0; i < NumContexts; ++i)
	{	
		const bool bIsDefaultContext = (i == 0);
		const ED3D12CommandQueueType CommandQueueType = ED3D12CommandQueueType::Direct;
		FD3D12CommandContext* NewCmdContext = GetOwningRHI()->CreateCommandContext(this, CommandQueueType, bIsDefaultContext);

		// without that the first RHIClear would get a scissor rect of (0,0)-(0,0) which means we get a draw call clear 
		NewCmdContext->RHISetScissorRect(false, 0, 0, 0, 0);

		CommandContextArray.Add(NewCmdContext);

		// Make available all but the first command context for parallel threads
		if (!bIsDefaultContext)
		{
			FreeCommandContexts.Add(CommandContextArray[i]);
		}
	}

	for (uint32 i = 0; i < NumAsyncComputeContexts; ++i) //-V1008
	{		
		const bool bIsDefaultContext = (i == 0); //-V547
		const ED3D12CommandQueueType CommandQueueType = ED3D12CommandQueueType::Async;
		FD3D12CommandContext* NewCmdContext = GetOwningRHI()->CreateCommandContext(this, CommandQueueType, bIsDefaultContext);
	
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

static D3D12_FEATURE_DATA_FORMAT_SUPPORT GetFormatSupport(ID3D12Device* InDevice, DXGI_FORMAT InFormat)
{
	D3D12_FEATURE_DATA_FORMAT_SUPPORT FormatSupport{};
	FormatSupport.Format = InFormat;

	InDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &FormatSupport, sizeof(FormatSupport));

	return FormatSupport;
}

void FD3D12Device::SetupAfterDeviceCreation()
{
	ID3D12Device* Direct3DDevice = GetParentAdapter()->GetD3DDevice();

	for (uint32 FormatIndex = PF_Unknown; FormatIndex < PF_MAX; FormatIndex++)
	{
		FPixelFormatInfo& PixelFormatInfo = GPixelFormats[FormatIndex];
		const DXGI_FORMAT PlatformFormat = static_cast<DXGI_FORMAT>(PixelFormatInfo.PlatformFormat);

		EPixelFormatCapabilities Capabilities = EPixelFormatCapabilities::None;

		if (PlatformFormat != DXGI_FORMAT_UNKNOWN)
		{
			const D3D12_FEATURE_DATA_FORMAT_SUPPORT FormatSupport    = GetFormatSupport(Direct3DDevice, PlatformFormat);
			const D3D12_FEATURE_DATA_FORMAT_SUPPORT SRVFormatSupport = GetFormatSupport(Direct3DDevice, FindShaderResourceDXGIFormat(PlatformFormat, false));
			const D3D12_FEATURE_DATA_FORMAT_SUPPORT UAVFormatSupport = GetFormatSupport(Direct3DDevice, FindUnorderedAccessDXGIFormat(PlatformFormat));
			const D3D12_FEATURE_DATA_FORMAT_SUPPORT RTVFormatSupport = GetFormatSupport(Direct3DDevice, FindShaderResourceDXGIFormat(PlatformFormat, false));
			const D3D12_FEATURE_DATA_FORMAT_SUPPORT DSVFormatSupport = GetFormatSupport(Direct3DDevice, FindDepthStencilDXGIFormat(PlatformFormat));

			auto ConvertCap1 = [&Capabilities](const D3D12_FEATURE_DATA_FORMAT_SUPPORT& InSupport, EPixelFormatCapabilities UnrealCap, D3D12_FORMAT_SUPPORT1 InFlags)
			{
				if (EnumHasAnyFlags(InSupport.Support1, InFlags))
				{
					EnumAddFlags(Capabilities, UnrealCap);
				}
			};
			auto ConvertCap2 = [&Capabilities](const D3D12_FEATURE_DATA_FORMAT_SUPPORT& InSupport, EPixelFormatCapabilities UnrealCap, D3D12_FORMAT_SUPPORT2 InFlags)
			{
				if (EnumHasAnyFlags(InSupport.Support2, InFlags))
				{
					EnumAddFlags(Capabilities, UnrealCap);
				}
			};

			ConvertCap1(FormatSupport, EPixelFormatCapabilities::Texture1D,               D3D12_FORMAT_SUPPORT1_TEXTURE1D);
			ConvertCap1(FormatSupport, EPixelFormatCapabilities::Texture2D,               D3D12_FORMAT_SUPPORT1_TEXTURE2D);
			ConvertCap1(FormatSupport, EPixelFormatCapabilities::Texture3D,               D3D12_FORMAT_SUPPORT1_TEXTURE3D);
			ConvertCap1(FormatSupport, EPixelFormatCapabilities::TextureCube,             D3D12_FORMAT_SUPPORT1_TEXTURECUBE);
			ConvertCap1(FormatSupport, EPixelFormatCapabilities::Buffer,                  D3D12_FORMAT_SUPPORT1_BUFFER);
			ConvertCap1(FormatSupport, EPixelFormatCapabilities::VertexBuffer,            D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER);
			ConvertCap1(FormatSupport, EPixelFormatCapabilities::IndexBuffer,             D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER);

			if (EnumHasAnyFlags(Capabilities, EPixelFormatCapabilities::AnyTexture))
			{
				ConvertCap1(FormatSupport, EPixelFormatCapabilities::RenderTarget,        D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
				ConvertCap1(FormatSupport, EPixelFormatCapabilities::DepthStencil,        D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
				ConvertCap1(FormatSupport, EPixelFormatCapabilities::TextureMipmaps,      D3D12_FORMAT_SUPPORT1_MIP);
				ConvertCap1(SRVFormatSupport, EPixelFormatCapabilities::TextureLoad,      D3D12_FORMAT_SUPPORT1_SHADER_LOAD);
				ConvertCap1(SRVFormatSupport, EPixelFormatCapabilities::TextureSample,    D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE);
				ConvertCap1(SRVFormatSupport, EPixelFormatCapabilities::TextureGather,    D3D12_FORMAT_SUPPORT1_SHADER_GATHER);
				ConvertCap2(UAVFormatSupport, EPixelFormatCapabilities::TextureAtomics,   D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE);
				ConvertCap1(SRVFormatSupport, EPixelFormatCapabilities::TextureBlendable, D3D12_FORMAT_SUPPORT1_BLENDABLE);
				ConvertCap2(UAVFormatSupport, EPixelFormatCapabilities::TextureStore,     D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
			}

			if (EnumHasAnyFlags(Capabilities, EPixelFormatCapabilities::Buffer))
			{
				ConvertCap1(SRVFormatSupport, EPixelFormatCapabilities::BufferLoad,       D3D12_FORMAT_SUPPORT1_BUFFER);
				ConvertCap2(UAVFormatSupport, EPixelFormatCapabilities::BufferStore,      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
				ConvertCap2(UAVFormatSupport, EPixelFormatCapabilities::BufferAtomics,    D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE);
			}

			ConvertCap1(UAVFormatSupport, EPixelFormatCapabilities::UAV,                  D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW);
			ConvertCap2(UAVFormatSupport, EPixelFormatCapabilities::TypedUAVLoad,         D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
			ConvertCap2(UAVFormatSupport, EPixelFormatCapabilities::TypedUAVStore,        D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
		}

		PixelFormatInfo.Capabilities = Capabilities;
	}

	GRHISupportsArrayIndexFromAnyShader = true;
	GRHISupportsStencilRefFromPixelShader = false; // TODO: Sort out DXC shader database SM6.0 usage. DX12 supports this feature, but need to improve DXC support.

#if (PLATFORM_WINDOWS || PLATFORM_HOLOLENS)
	// Check if we're running under GPU capture
	bool bUnderGPUCapture = false;

	// RenderDoc
	if (D3D12RHI_IsRenderDocPresent(Direct3DDevice))
	{
		// Running under RenderDoc, so enable capturing mode
		bUnderGPUCapture = true;
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


	const int32 MaximumResourceHeapSize = GetParentAdapter()->GetMaxDescriptorsForHeapType(ERHIDescriptorHeapType::Standard);
	const int32 MaximumSamplerHeapSize = GetParentAdapter()->GetMaxDescriptorsForHeapType(ERHIDescriptorHeapType::Sampler);

	// This value can be tuned on a per app basis. I.e. most apps will never run into descriptor heap pressure so
	// can make this global heap smaller
	check(GGlobalResourceDescriptorHeapSize <= MaximumResourceHeapSize || MaximumResourceHeapSize < 0);
	check(GGlobalSamplerDescriptorHeapSize <= MaximumSamplerHeapSize);

	check(GGlobalSamplerHeapSize <= MaximumSamplerHeapSize);

	check(GOnlineDescriptorHeapSize <= GGlobalResourceDescriptorHeapSize);
	check(GBindlessResourceDescriptorHeapSize <= GGlobalResourceDescriptorHeapSize);
	check(GBindlessSamplerDescriptorHeapSize <= GGlobalSamplerDescriptorHeapSize);

	DescriptorHeapManager.Init(GGlobalResourceDescriptorHeapSize, GGlobalSamplerDescriptorHeapSize);

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
	const bool bBindlessResources = RHIGetBindlessResourcesConfiguration(GMaxRHIShaderPlatform) != ERHIBindlessConfiguration::Disabled;
	const bool bBindlessSamplers = RHIGetBindlessSamplersConfiguration(GMaxRHIShaderPlatform) != ERHIBindlessConfiguration::Disabled;
	if (bBindlessResources || bBindlessSamplers)
	{
		BindlessDescriptorManager.Init(bBindlessResources ? GBindlessResourceDescriptorHeapSize : 0, bBindlessSamplers ? GBindlessSamplerDescriptorHeapSize : 0);
	}
#endif

	// Init offline descriptor managers
	for (uint32 Index = 0; Index < static_cast<uint32>(ERHIDescriptorHeapType::count); Index++)
	{
		OfflineDescriptorManagers[Index].Init(static_cast<ERHIDescriptorHeapType>(Index));
	}

	GlobalSamplerHeap.Init(GGlobalSamplerHeapSize);

	OnlineDescriptorManager.Init(GOnlineDescriptorHeapSize, GOnlineDescriptorHeapBlockSize);

	// Init the occlusion and timestamp query heaps
	OcclusionQueryHeap.Init();
	for (uint32 QueueType = 0; QueueType < (uint32)ED3D12CommandQueueType::Count; ++QueueType)
	{
		TimestampQueryHeaps[QueueType]->Init();
	}

	CommandListManager->Create(*FString::Printf(TEXT("3D Queue %d"), GetGPUIndex()));
	gD3D12CommandQueue = CommandListManager->GetD3DCommandQueue();
	CopyCommandListManager->Create(*FString::Printf(TEXT("Copy Queue %d"), GetGPUIndex()));
	AsyncCommandListManager->Create(*FString::Printf(TEXT("Compute Queue %d"), GetGPUIndex()), 0, AsyncComputePriority_Default);

	// Needs to be called before creating command contexts
	UpdateConstantBufferPageProperties();

	CreateCommandContexts();

	UpdateMSAASettings();

#if D3D12_RHI_RAYTRACING
	check(RayTracingCompactionRequestHandler == nullptr);
	RayTracingCompactionRequestHandler = new FD3D12RayTracingCompactionRequestHandler(this);

	D3D12_RESOURCE_DESC DispatchRaysDescBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_DISPATCH_RAYS_DESC), D3D12RHI_RESOURCE_FLAG_ALLOW_INDIRECT_BUFFER);
	RayTracingDispatchRaysDescBuffer = GetParentAdapter()->CreateRHIBuffer(
		DispatchRaysDescBufferDesc, 256,
		0, DispatchRaysDescBufferDesc.Width, BUF_DrawIndirect,
		ED3D12ResourceStateMode::MultiState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, false /*bInitialData*/,
		GetGPUMask(), nullptr /*ResourceAllocator*/, TEXT("DispatchRaysDescBuffer"));
#endif // D3D12_RHI_RAYTRACING

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
	ValidateCommandQueue(ED3D12CommandQueueType::Direct, TEXT("Direct"));
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
	int32 DeletedCount;
	do
	{
		DeletedCount = FRHIResource::FlushPendingDeletes(RHICmdList);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	} while (DeletedCount);

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
		FD3D12UploadHeapAllocator* pHeapAllocator = ThreadDynamicHeapAllocatorArray[index];
		pHeapAllocator->ReleaseAllResources();
		delete(pHeapAllocator);
	}
	*/

	CommandListManager->Destroy();
	CopyCommandListManager->Destroy();
	AsyncCommandListManager->Destroy();

	OcclusionQueryHeap.Destroy();
	for (uint32 QueueType = 0; QueueType < (uint32)ED3D12CommandQueueType::Count; ++QueueType)
	{
		TimestampQueryHeaps[QueueType]->Destroy();
		delete TimestampQueryHeaps[QueueType];
		TimestampQueryHeaps[QueueType] = nullptr;
	}

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
	case ED3D12CommandQueueType::Direct:
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

D3D12_RESOURCE_ALLOCATION_INFO FD3D12Device::GetResourceAllocationInfo(const D3D12_RESOURCE_DESC& InDesc)
{
	uint64 Hash = CityHash64((const char*)&InDesc, sizeof(D3D12_RESOURCE_DESC));

	// By default there'll be more threads trying to read this than to write it.
	ResourceAllocationInfoMapMutex.ReadLock();
	D3D12_RESOURCE_ALLOCATION_INFO* CachedInfo = ResourceAllocationInfoMap.Find(Hash);
	ResourceAllocationInfoMapMutex.ReadUnlock();

	if (CachedInfo)
	{
		return *CachedInfo;
	}
	else
	{
		D3D12_RESOURCE_ALLOCATION_INFO Result = GetDevice()->GetResourceAllocationInfo(0, 1, &InDesc);

		ResourceAllocationInfoMapMutex.WriteLock();
		// Try search again with write lock because could have been added already
		CachedInfo = ResourceAllocationInfoMap.Find(Hash);
		if (CachedInfo == nullptr)
		{
			ResourceAllocationInfoMap.Add(Hash, Result);
		}
		ResourceAllocationInfoMapMutex.WriteUnlock();
		return Result;
	}
}

