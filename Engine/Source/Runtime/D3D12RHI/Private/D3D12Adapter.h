// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Adapter.h: D3D12 Adapter Interfaces

The D3D12 RHI is layed out in the following stucture. 

	[Engine]--
			|
			|-[RHI]--
					|
					|-[Adapter]-- (LDA)
					|			|
					|			|- [Device]
					|			|
					|			|- [Device]
					|
					|-[Adapter]--
								|
								|- [Device]--
											|
											|-[CommandContext]
											|
											|-[CommandContext]---
																|
																|-[StateCache]

Under this scheme an FD3D12Device represents 1 node belonging to 1 physical adapter.

This structure allows a single RHI to control several different hardware setups. Some example arrangements:
	- Single-GPU systems (the common case)
	- Multi-GPU systems i.e. LDA (Crossfire/SLI)
	- Asymmetric Multi-GPU systems i.e. Discrete/Integrated GPU cooperation
=============================================================================*/

#pragma once

THIRD_PARTY_INCLUDES_START
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "dxgidebug.h"
#endif // PLATFORM_WINDOWS || PLATFORM_HOLOLENS
THIRD_PARTY_INCLUDES_END

class FD3D12DynamicRHI;

struct FD3D12AdapterDesc
{
	FD3D12AdapterDesc()
		: AdapterIndex(-1)
		, MaxSupportedFeatureLevel((D3D_FEATURE_LEVEL)0)
		, NumDeviceNodes(0)
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
		, GpuPreference(DXGI_GPU_PREFERENCE_UNSPECIFIED)
#endif
	{
	}

	FD3D12AdapterDesc(DXGI_ADAPTER_DESC& DescIn, int32 InAdapterIndex, D3D_FEATURE_LEVEL InMaxSupportedFeatureLevel, uint32 NumNodes
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
		, DXGI_GPU_PREFERENCE InGpuPreference
#endif
	)
		: AdapterIndex(InAdapterIndex)
		, MaxSupportedFeatureLevel(InMaxSupportedFeatureLevel)
		, Desc(DescIn)
		, NumDeviceNodes(NumNodes)
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
		, GpuPreference(InGpuPreference)
#endif
	{
	}

	bool IsValid() const { return MaxSupportedFeatureLevel != (D3D_FEATURE_LEVEL)0 && AdapterIndex >= 0; }

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
	static HRESULT EnumAdapters(int32 AdapterIndex, DXGI_GPU_PREFERENCE GpuPreference, IDXGIFactory* DxgiFactory, IDXGIFactory6* DxgiFactory6, IDXGIAdapter** TempAdapter)
	{
		if (!DxgiFactory6 || GpuPreference == DXGI_GPU_PREFERENCE_UNSPECIFIED)
		{
			return DxgiFactory->EnumAdapters(AdapterIndex, TempAdapter);
		}
		else
		{
			return DxgiFactory6->EnumAdapterByGpuPreference(AdapterIndex, GpuPreference, IID_PPV_ARGS(TempAdapter));
		}
	}

	HRESULT EnumAdapters(IDXGIFactory* DxgiFactory, IDXGIFactory6* DxgiFactory6, IDXGIAdapter** TempAdapter) const
	{
		return EnumAdapters(AdapterIndex, GpuPreference, DxgiFactory, DxgiFactory6, TempAdapter);
	}
#endif

	/** -1 if not supported or FindAdpater() wasn't called. Ideally we would store a pointer to IDXGIAdapter but it's unlikely the adpaters change during engine init. */
	int32 AdapterIndex;
	/** The maximum D3D12 feature level supported. 0 if not supported or FindAdpater() wasn't called */
	D3D_FEATURE_LEVEL MaxSupportedFeatureLevel;

	DXGI_ADAPTER_DESC Desc;

	uint32 NumDeviceNodes;

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
	DXGI_GPU_PREFERENCE GpuPreference;
#endif
};

enum class ED3D12GPUCrashDebugginMode
{
	Disabled,
	Minimal,
	Full
};

// Represents a set of linked D3D12 device nodes (LDA i.e 1 or more identical GPUs). In most cases there will be only 1 node, however if the system supports
// SLI/Crossfire and the app enables it an Adapter will have 2 or more nodes. This class will own anything that can be shared
// across LDA including: System Pool Memory,.Pipeline State Objects, Root Signatures etc.
class FD3D12Adapter : public FNoncopyable
{
public:

	FD3D12Adapter(FD3D12AdapterDesc& DescIn);
	virtual ~FD3D12Adapter() { }

	void Initialize(FD3D12DynamicRHI* RHI);
	void InitializeDevices();
	void InitializeRayTracing();

	// Getters
	FORCEINLINE const uint32 GetAdapterIndex() const { return Desc.AdapterIndex; }
	FORCEINLINE const D3D_FEATURE_LEVEL GetFeatureLevel() const { return Desc.MaxSupportedFeatureLevel; }
	FORCEINLINE ID3D12Device* GetD3DDevice() const { return RootDevice.GetReference(); }
	FORCEINLINE ID3D12Device1* GetD3DDevice1() const { return RootDevice1.GetReference(); }
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
	FORCEINLINE ID3D12Device2* GetD3DDevice2() const { return RootDevice2.GetReference(); }
#endif
#if D3D12_RHI_RAYTRACING
	FORCEINLINE ID3D12Device5* GetD3DDevice5() { return RootDevice5.GetReference(); }
	FORCEINLINE ID3D12Device7* GetD3DDevice7() { return RootDevice7.GetReference(); }
#endif // D3D12_RHI_RAYTRACING
	FORCEINLINE void SetDeviceRemoved(bool value) { bDeviceRemoved = value; }
	FORCEINLINE const bool IsDeviceRemoved() const { return bDeviceRemoved; }
	FORCEINLINE const bool IsDebugDevice() const { return bDebugDevice; }
	FORCEINLINE const ED3D12GPUCrashDebugginMode GetGPUCrashDebuggingMode() const { return GPUCrashDebuggingMode; }
	FORCEINLINE FD3D12DynamicRHI* GetOwningRHI() { return OwningRHI; }
	FORCEINLINE const D3D12_RESOURCE_HEAP_TIER GetResourceHeapTier() const { return ResourceHeapTier; }
	FORCEINLINE const D3D12_RESOURCE_BINDING_TIER GetResourceBindingTier() const { return ResourceBindingTier; }
	FORCEINLINE const D3D_ROOT_SIGNATURE_VERSION GetRootSignatureVersion() const { return RootSignatureVersion; }
	FORCEINLINE const bool IsDepthBoundsTestSupported() const { return bDepthBoundsTestSupported; }
	FORCEINLINE const bool IsHeapNotZeroedSupported() const { return bHeapNotZeroedSupported; }
	FORCEINLINE const DXGI_ADAPTER_DESC& GetD3DAdapterDesc() const { return Desc.Desc; }
	FORCEINLINE IDXGIAdapter* GetAdapter() { return DxgiAdapter; }
	FORCEINLINE const FD3D12AdapterDesc& GetDesc() const { return Desc; }
	FORCEINLINE TArray<FD3D12Viewport*>& GetViewports() { return Viewports; }
	FORCEINLINE FD3D12Viewport* GetDrawingViewport() { return DrawingViewport; }
	FORCEINLINE void SetDrawingViewport(FD3D12Viewport* InViewport) { DrawingViewport = InViewport; }

	FORCEINLINE ID3D12CommandSignature* GetDrawIndirectCommandSignature() { return DrawIndirectCommandSignature; }
	FORCEINLINE ID3D12CommandSignature* GetDrawIndexedIndirectCommandSignature() { return DrawIndexedIndirectCommandSignature; }
	FORCEINLINE ID3D12CommandSignature* GetDispatchIndirectCommandSignature() { return DispatchIndirectCommandSignature; }

	FORCEINLINE FD3D12PipelineStateCache& GetPSOCache() { return PipelineStateCache; }

	FORCEINLINE FD3D12FenceCorePool& GetFenceCorePool() { return FenceCorePool; }

#if USE_STATIC_ROOT_SIGNATURE
	FORCEINLINE const FD3D12RootSignature* GetStaticGraphicsRootSignature()
	{
		static const FD3D12RootSignature StaticGraphicsRootSignature(this, FD3D12RootSignatureDesc::GetStaticGraphicsRootSignatureDesc());
		return &StaticGraphicsRootSignature;
	}
	FORCEINLINE const FD3D12RootSignature* GetStaticComputeRootSignature()
	{
		static const FD3D12RootSignature StaticComputeRootSignature(this, FD3D12RootSignatureDesc::GetStaticComputeRootSignatureDesc());
		return &StaticComputeRootSignature;
	}
	FORCEINLINE const FD3D12RootSignature* GetStaticRayTracingGlobalRootSignature()
	{
		static const FD3D12RootSignature StaticRootSignature(this, FD3D12RootSignatureDesc::GetStaticRayTracingGlobalRootSignatureDesc(), 1 /*RAY_TRACING_REGISTER_SPACE_GLOBAL*/);
		return &StaticRootSignature;
	}
	FORCEINLINE const FD3D12RootSignature* GetStaticRayTracingLocalRootSignature()
	{
		static const FD3D12RootSignature StaticRootSignature(this, FD3D12RootSignatureDesc::GetStaticRayTracingLocalRootSignatureDesc(), 0 /*RAY_TRACING_REGISTER_SPACE_LOCAL*/);
		return &StaticRootSignature;
	}
#else // USE_STATIC_ROOT_SIGNATURE
	FORCEINLINE const FD3D12RootSignature* GetStaticGraphicsRootSignature(){ return nullptr; }
	FORCEINLINE const FD3D12RootSignature* GetStaticComputeRootSignature() { return nullptr; }
	FORCEINLINE const FD3D12RootSignature* GetStaticRayTracingGlobalRootSignature() { return nullptr; }
	FORCEINLINE const FD3D12RootSignature* GetStaticRayTracingLocalRootSignature() { return nullptr; }
#endif // USE_STATIC_ROOT_SIGNATURE

	FORCEINLINE FD3D12RootSignature* GetRootSignature(const FD3D12QuantizedBoundShaderState& QBSS) 
	{
		return RootSignatureManager.GetRootSignature(QBSS);
	}

	FORCEINLINE FD3D12RootSignatureManager* GetRootSignatureManager()
	{
		return &RootSignatureManager;
	}

	FORCEINLINE FD3D12DeferredDeletionQueue& GetDeferredDeletionQueue() { return DeferredDeletionQueue; }

	FORCEINLINE FD3D12ManualFence& GetFrameFence()  { check(FrameFence); return *FrameFence; }

	FORCEINLINE FD3D12Fence* GetStagingFence()  { return StagingFence.GetReference(); }

	FORCEINLINE FD3D12Device* GetDevice(uint32 GPUIndex)
	{
		check(GPUIndex < GNumExplicitGPUsForRendering);
		return Devices[GPUIndex];
	}

	FORCEINLINE uint32 GetVRSTileSize() const { return VRSTileSize; }

	void CreateDXGIFactory(bool bWithDebug);
	FORCEINLINE IDXGIFactory* GetDXGIFactory() const { return DxgiFactory; }
	FORCEINLINE IDXGIFactory2* GetDXGIFactory2() const { return DxgiFactory2; }
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
	FORCEINLINE IDXGIFactory6* GetDXGIFactory6() const { return DxgiFactory6; }
#endif

	FORCEINLINE FD3D12DynamicHeapAllocator& GetUploadHeapAllocator(uint32 GPUIndex) 
	{ 
		return *(UploadHeapAllocator[GPUIndex]); 
	}

	FORCEINLINE uint32 GetDebugFlags() const { return DebugFlags; }

	void Cleanup();

	void EndFrame();

	// Resource Creation
	HRESULT CreateCommittedResource(const D3D12_RESOURCE_DESC& InDesc,
		FRHIGPUMask CreationNode,
		const D3D12_HEAP_PROPERTIES& HeapProps,
		D3D12_RESOURCE_STATES InInitialState,
		const D3D12_CLEAR_VALUE* ClearValue,
		FD3D12Resource** ppOutResource,
		const TCHAR* Name,
		bool bVerifyHResult = true)
	{
		return CreateCommittedResource(InDesc, CreationNode, HeapProps, InInitialState, ED3D12ResourceStateMode::Default, D3D12_RESOURCE_STATE_TBD, ClearValue, ppOutResource, Name, bVerifyHResult);
	}

	HRESULT CreateCommittedResource(const D3D12_RESOURCE_DESC& Desc,
		FRHIGPUMask CreationNode,
		const D3D12_HEAP_PROPERTIES& HeapProps,
		D3D12_RESOURCE_STATES InInitialState,
		ED3D12ResourceStateMode InResourceStateMode,
		D3D12_RESOURCE_STATES InDefaultState,
		const D3D12_CLEAR_VALUE* ClearValue,
		FD3D12Resource** ppOutResource,
		const TCHAR* Name,
		bool bVerifyHResult = true);

	HRESULT CreatePlacedResource(const D3D12_RESOURCE_DESC& InDesc,
		FD3D12Heap* BackingHeap,
		uint64 HeapOffset,
		D3D12_RESOURCE_STATES InInitialState,
		const D3D12_CLEAR_VALUE* ClearValue,
		FD3D12Resource** ppOutResource,
		const TCHAR* Name,
		bool bVerifyHResult = true)
	{
		return CreatePlacedResource(InDesc, BackingHeap, HeapOffset, InInitialState, ED3D12ResourceStateMode::Default, D3D12_RESOURCE_STATE_TBD, ClearValue, ppOutResource, Name, bVerifyHResult);
	}

	HRESULT CreatePlacedResource(const D3D12_RESOURCE_DESC& Desc,
		FD3D12Heap* BackingHeap,
		uint64 HeapOffset,
		D3D12_RESOURCE_STATES InInitialState,
		ED3D12ResourceStateMode InResourceStateMode,
		D3D12_RESOURCE_STATES InDefaultState,
		const D3D12_CLEAR_VALUE* ClearValue,
		FD3D12Resource** ppOutResource,
		const TCHAR* Name,
		bool bVerifyHResult = true);

	HRESULT CreateBuffer(D3D12_HEAP_TYPE HeapType,
		FRHIGPUMask CreationNode,
		FRHIGPUMask VisibleNodes,
		uint64 HeapSize,
		FD3D12Resource** ppOutResource,
		const TCHAR* Name,
		D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE);

	HRESULT CreateBuffer(D3D12_HEAP_TYPE HeapType,
		FRHIGPUMask CreationNode,
		FRHIGPUMask VisibleNodes,
		D3D12_RESOURCE_STATES InitialState,
		ED3D12ResourceStateMode InResourceStateMode,
		uint64 HeapSize,
		FD3D12Resource** ppOutResource,
		const TCHAR* Name,
		D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE);

	HRESULT CreateBuffer(const D3D12_HEAP_PROPERTIES& HeapProps,
		FRHIGPUMask CreationNode,
		D3D12_RESOURCE_STATES InInitialState,
		ED3D12ResourceStateMode InResourceStateMode,
		D3D12_RESOURCE_STATES InDefaultState,
		uint64 HeapSize,
		FD3D12Resource** ppOutResource,
		const TCHAR* Name,
		D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE);

	template <typename BufferType> 
	BufferType* CreateRHIBuffer(FRHICommandListImmediate* RHICmdList,
		const D3D12_RESOURCE_DESC& Desc,
		uint32 Alignment, uint32 Stride, uint32 Size, uint32 InUsage,
		ED3D12ResourceStateMode InResourceStateMode,
		FRHIResourceCreateInfo& CreateInfo);

	template <typename ObjectType, typename CreationCoreFunction>
	inline ObjectType* CreateLinkedObject(FRHIGPUMask GPUMask, const CreationCoreFunction& pfnCreationCore)
	{
		return FD3D12LinkedAdapterObject<typename ObjectType::LinkedObjectType>::template CreateLinkedObjects<ObjectType>(
			GPUMask,
			[this](uint32 GPUIndex) { return GetDevice(GPUIndex); },
			pfnCreationCore
		);
	}

	template <typename ResourceType, typename ViewType, typename CreationCoreFunction>
	inline ViewType* CreateLinkedViews(ResourceType* Resource, const CreationCoreFunction& pfnCreationCore)
	{
		return FD3D12LinkedAdapterObject<typename ViewType::LinkedObjectType>::template CreateLinkedObjects<ViewType>(
			Resource->GetLinkedObjectsGPUMask(),
			[Resource](uint32 GPUIndex) { return static_cast<ResourceType*>(Resource->GetLinkedObject(GPUIndex)); },
			pfnCreationCore
		);
	}

	inline FD3D12CommandContextRedirector& GetDefaultContextRedirector() { return DefaultContextRedirector; }
	inline FD3D12CommandContextRedirector& GetDefaultAsyncComputeContextRedirector() { return DefaultAsyncComputeContextRedirector; }

	FD3D12TemporalEffect* GetTemporalEffect(const FName& EffectName);

	FD3D12FastConstantAllocator& GetTransientUniformBufferAllocator();

	void BlockUntilIdle();

	void GetLocalVideoMemoryInfo(DXGI_QUERY_VIDEO_MEMORY_INFO* LocalVideoMemoryInfo);

	FORCEINLINE uint32 GetFrameCount() const { return FrameCounter; }

#if D3D12_SUBMISSION_GAP_RECORDER
	FD3D12SubmissionGapRecorder SubmissionGapRecorder;

	void SubmitGapRecorderTimestamps();
#endif

protected:

	virtual void CreateRootDevice(bool bWithDebug);

	virtual void AllocateBuffer(FD3D12Device* Device,
		const D3D12_RESOURCE_DESC& Desc,
		uint32 Size,
		uint32 InUsage,
		ED3D12ResourceStateMode InResourceStateMode,
		FRHIResourceCreateInfo& CreateInfo,
		uint32 Alignment,
		FD3D12TransientResource& TransientResource,
		FD3D12ResourceLocation& ResourceLocation);

	// Creates default root and execute indirect signatures
	void CreateSignatures();

	FD3D12DynamicRHI* OwningRHI;

	// LDA setups have one ID3D12Device
	TRefCountPtr<ID3D12Device> RootDevice;
	TRefCountPtr<ID3D12Device1> RootDevice1;
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
	TRefCountPtr<ID3D12Device2> RootDevice2;
	TRefCountPtr<IDXGIDebug> DXGIDebug;

	HANDLE ExceptionHandlerHandle = INVALID_HANDLE_VALUE;
#endif
#if D3D12_RHI_RAYTRACING
	TRefCountPtr<ID3D12Device5> RootDevice5;
	TRefCountPtr<ID3D12Device7> RootDevice7;
#endif // D3D12_RHI_RAYTRACING
	D3D12_RESOURCE_HEAP_TIER ResourceHeapTier;
	D3D12_RESOURCE_BINDING_TIER ResourceBindingTier;
	D3D_ROOT_SIGNATURE_VERSION RootSignatureVersion;
	bool bDepthBoundsTestSupported;
	bool bHeapNotZeroedSupported;

	uint32 VRSTileSize;

	/** Running with debug device */
	bool bDebugDevice;

	/** GPU Crash debugging mode */
	ED3D12GPUCrashDebugginMode GPUCrashDebuggingMode;

	/** True if the device being used has been removed. */
	bool bDeviceRemoved;

	FD3D12AdapterDesc Desc;
	TRefCountPtr<IDXGIAdapter> DxgiAdapter;

	FD3D12RootSignatureManager RootSignatureManager;

	FD3D12PipelineStateCache PipelineStateCache;

	TRefCountPtr<ID3D12CommandSignature> DrawIndirectCommandSignature;
	TRefCountPtr<ID3D12CommandSignature> DrawIndexedIndirectCommandSignature;
	TRefCountPtr<ID3D12CommandSignature> DispatchIndirectCommandSignature;

	FD3D12FenceCorePool FenceCorePool;

	FD3D12DynamicHeapAllocator*	UploadHeapAllocator[MAX_NUM_GPUS];

	/** A list of all viewport RHIs that have been created. */
	TArray<FD3D12Viewport*> Viewports;

	/** The viewport which is currently being drawn. */
	TRefCountPtr<FD3D12Viewport> DrawingViewport;

	TRefCountPtr<IDXGIFactory> DxgiFactory;
	TRefCountPtr<IDXGIFactory2> DxgiFactory2;
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
	TRefCountPtr<IDXGIFactory6> DxgiFactory6;
#endif

	/** A Fence whos value increases every frame*/
	TRefCountPtr<FD3D12ManualFence> FrameFence;

	/** A Fence used to syncrhonize FD3D12GPUFence and FD3D12StagingBuffer */
	TRefCountPtr<FD3D12Fence> StagingFence;

	FD3D12DeferredDeletionQueue DeferredDeletionQueue;

	FD3D12CommandContextRedirector DefaultContextRedirector;
	FD3D12CommandContextRedirector DefaultAsyncComputeContextRedirector;

	uint32 FrameCounter;

#if D3D12_SUBMISSION_GAP_RECORDER
	TArray<uint64> StartOfSubmissionTimestamps;
	TArray<uint64> EndOfSubmissionTimestamps;
#endif

#if WITH_MGPU
	TMap<FName, FD3D12TemporalEffect> TemporalEffectMap;
#endif

	// Each of these devices represents a physical GPU 'Node'
	FD3D12Device* Devices[MAX_NUM_GPUS];

	uint32 DebugFlags;
};