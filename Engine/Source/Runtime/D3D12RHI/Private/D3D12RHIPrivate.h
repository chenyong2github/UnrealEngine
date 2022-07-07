// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12RHIPrivate.h: Private D3D RHI definitions.
	=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadManager.h"
#include "Containers/ResourceArray.h"
#include "Serialization/MemoryReader.h"
#include "EngineGlobals.h"
#include "StaticBoundShaderState.h"

#define D3D12_SUPPORTS_PARALLEL_RHI_EXECUTE 1

#define BATCH_COPYPAGEMAPPINGS 1

#define D3D12_RHI_RAYTRACING (RHI_RAYTRACING)

// Dependencies.
#include "CoreMinimal.h"
#include "ID3D12DynamicRHI.h"
#include "GPUProfiler.h"
#include "ShaderCore.h"
#include "HDRHelper.h"

DECLARE_LOG_CATEGORY_EXTERN(LogD3D12RHI, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogD3D12GapRecorder, Log, All);

#include "D3D12RHI.h"
#include "D3D12RHICommon.h"

#if PLATFORM_WINDOWS
#include "Windows/D3D12RHIBasePrivate.h"
#else
#include "D3D12RHIBasePrivate.h"
#endif

#if !defined(NV_AFTERMATH)
	#define NV_AFTERMATH 0
#endif

#if NV_AFTERMATH
#define GFSDK_Aftermath_WITH_DX12 1
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_GpuCrashdump.h"
#undef GFSDK_Aftermath_WITH_DX12
extern bool GDX12NVAfterMathModuleLoaded;
extern int32 GDX12NVAfterMathEnabled;
extern int32 GDX12NVAfterMathTrackResources;
extern int32 GDX12NVAfterMathMarkers;
#endif

#include "D3D12Residency.h"

// D3D RHI public headers.
#include "../Public/D3D12Util.h"
#include "../Public/D3D12State.h"
#include "../Public/D3D12Resources.h"
#include "D3D12RootSignature.h"
#include "D3D12Shader.h"
#include "D3D12View.h"
#include "D3D12CommandList.h"
#include "D3D12Texture.h"
#include "D3D12DirectCommandListManager.h"
#include "../Public/D3D12Viewport.h"
#include "../Public/D3D12ConstantBuffer.h"
#include "D3D12Query.h"
#include "D3D12DescriptorCache.h"
#include "D3D12StateCachePrivate.h"
typedef FD3D12StateCacheBase FD3D12StateCache;
#include "D3D12Allocation.h"
#include "D3D12TransientResourceAllocator.h"
#include "D3D12CommandContext.h"
#include "D3D12Stats.h"
#include "D3D12Device.h"
#include "D3D12Adapter.h"


#define EXECUTE_DEBUG_COMMAND_LISTS 0
#define ENABLE_PLACED_RESOURCES 0 // Disabled due to a couple of NVidia bugs related to placed resources. Works fine on Intel
#define NAME_OBJECTS !(UE_BUILD_SHIPPING || UE_BUILD_TEST)	// Name objects in all builds except shipping
#define LOG_PSO_CREATES (0 && STATS)	// Logs Create Pipeline State timings (also requires STATS)
#define TRACK_RESOURCE_ALLOCATIONS (PLATFORM_WINDOWS && !UE_BUILD_SHIPPING && !UE_BUILD_TEST)

//@TODO: Improve allocator efficiency so we can increase these thresholds and improve performance
// We measured 149MB of wastage in 340MB of allocations with DEFAULT_BUFFER_POOL_MAX_ALLOC_SIZE set to 512KB
#if !defined(DEFAULT_BUFFER_POOL_MAX_ALLOC_SIZE)
#if D3D12_RHI_RAYTRACING
  // #dxr_todo: Reevaluate these values. Currently optimized to reduce number of CreateCommitedResource() calls, at the expense of memory use.
  #define DEFAULT_BUFFER_POOL_MAX_ALLOC_SIZE (64 * 1024 * 1024)
  #define DEFAULT_BUFFER_POOL_DEFAULT_POOL_SIZE (16 * 1024 * 1024)
#else
  // On PC, buffers are 64KB aligned, so anything smaller should be sub-allocated
  #define DEFAULT_BUFFER_POOL_MAX_ALLOC_SIZE (64 * 1024)
  #define DEFAULT_BUFFER_POOL_DEFAULT_POOL_SIZE (8 * 1024 * 1024)
#endif //D3D12_RHI_RAYTRACING
#endif //DEFAULT_BUFFER_POOL_MAX_ALLOC_SIZE

#define READBACK_BUFFER_POOL_MAX_ALLOC_SIZE (64 * 1024)
#define READBACK_BUFFER_POOL_DEFAULT_POOL_SIZE (4 * 1024 * 1024)

#define TEXTURE_POOL_SIZE (8 * 1024 * 1024)

#define MAX_GPU_BREADCRUMB_DEPTH 1024

#ifndef FD3D12_HEAP_FLAG_CREATE_NOT_ZEROED
#define FD3D12_HEAP_FLAG_CREATE_NOT_ZEROED D3D12_HEAP_FLAG_CREATE_NOT_ZEROED
#endif

#if DEBUG_RESOURCE_STATES
#define LOG_EXECUTE_COMMAND_LISTS 1
#define ASSERT_RESOURCE_STATES 0	// Disabled for now.
#define LOG_PRESENT 1
#else
#define LOG_EXECUTE_COMMAND_LISTS 0
#define ASSERT_RESOURCE_STATES 0
#define LOG_PRESENT 0
#endif

#define DEBUG_FRAME_TIMING 0
#if DEBUG_FRAME_TIMING
#define LOG_VIEWPORT_EVENTS 1
#define LOG_PRESENT 1
#define LOG_EXECUTE_COMMAND_LISTS 1
#else
#define LOG_VIEWPORT_EVENTS 0
#endif

#if EXECUTE_DEBUG_COMMAND_LISTS
#define DEBUG_EXECUTE_COMMAND_LIST(scope) if (!scope##->bIsDoingQuery) { scope##->FlushCommands(true); }
#define DEBUG_EXECUTE_COMMAND_CONTEXT(context) if (!context.bIsDoingQuery) { context##.FlushCommands(true); }
#define DEBUG_RHI_EXECUTE_COMMAND_LIST(scope) if (!scope##->GetRHIDevice(0)->GetDefaultCommandContext().bIsDoingQuery) { scope##->GetRHIDevice(0)->GetDefaultCommandContext().FlushCommands(true); }
#else
#define DEBUG_EXECUTE_COMMAND_LIST(scope) 
#define DEBUG_EXECUTE_COMMAND_CONTEXT(context) 
#define DEBUG_RHI_EXECUTE_COMMAND_LIST(scope) 
#endif

// Use the D3D12 RHI internal transitions to drive all resource transitions
extern bool GUseInternalTransitions;
// Use the D3D12 RHI internal transitions to validate the engine pushed RHI transitions
extern bool GValidateInternalTransitions;

template< typename t_A, typename t_B >
inline t_A RoundUpToNextMultiple(const t_A& a, const t_B& b)
{
	return ((a - 1) / b + 1) * b;
}

using namespace D3D12RHI;

static bool D3D12RHI_ShouldCreateWithD3DDebug()
{
	// Use a debug device if specified on the command line.
	static bool bCreateWithD3DDebug =
		FParse::Param(FCommandLine::Get(), TEXT("d3ddebug")) ||
		FParse::Param(FCommandLine::Get(), TEXT("d3debug")) ||
		FParse::Param(FCommandLine::Get(), TEXT("dxdebug"));
	return bCreateWithD3DDebug;
}

static bool D3D12RHI_ShouldCreateWithWarp()
{
	// Use the warp adapter if specified on the command line.
	static bool bCreateWithWarp = FParse::Param(FCommandLine::Get(), TEXT("warp"));
	return bCreateWithWarp;
}

static bool D3D12RHI_AllowSoftwareFallback()
{
	static bool bAllowSoftwareRendering = FParse::Param(FCommandLine::Get(), TEXT("AllowSoftwareRendering"));
	return bAllowSoftwareRendering;
}

static bool D3D12RHI_ShouldAllowAsyncResourceCreation()
{
	static bool bAllowAsyncResourceCreation = !FParse::Param(FCommandLine::Get(), TEXT("nod3dasync"));
	return bAllowAsyncResourceCreation;
}

static bool D3D12RHI_ShouldForceCompatibility()
{
	// Suppress the use of newer D3D12 features.
	static bool bForceCompatibility =
		FParse::Param(FCommandLine::Get(), TEXT("d3dcompat")) ||
		FParse::Param(FCommandLine::Get(), TEXT("d3d12compat"));
	return bForceCompatibility;
}

static bool D3D12RHI_IsRenderDocPresent(ID3D12Device* Device)
{
	IID RenderDocID;
	if (SUCCEEDED(IIDFromString(L"{A7AA6116-9C8D-4BBA-9083-B4D816B71B78}", &RenderDocID)))
	{
		TRefCountPtr<IUnknown> RenderDoc;
		if (SUCCEEDED(Device->QueryInterface(RenderDocID, (void**)RenderDoc.GetInitReference())))
		{
			return true;
		}
	}

	return false;
}

struct FD3D12UpdateTexture3DData
{
	FD3D12ResourceLocation* UploadHeapResourceLocation;
	bool bComputeShaderCopy;
};

/**
* Structure that represents various RTPSO properties (0 if unknown).
* These can be used to report performance characteristics, sort shaders by occupancy, etc.
*/
struct FD3D12RayTracingPipelineInfo
{
	static constexpr uint32 MaxPerformanceGroups = 10;

	// Estimated RTPSO group based on occupancy or other platform-specific heuristics.
	// Group 0 is expected to be performing worst, 9 (MaxPerformanceGroups-1) is expected to be the best.
	uint32 PerformanceGroup = 0;

	uint32 NumVGPR = 0;
	uint32 NumSGPR = 0;
	uint32 StackSize = 0;
	uint32 ScratchSize = 0;
};

struct FD3D12WorkaroundFlags
{
	/** 
	* Certain drivers crash when GetShaderIdentifier() is called on a ray tracing pipeline collection.
	* If we detect such driver, we have to fall back to the path that queries identifiers on full linked RTPSO.
	* This is less efficient and can also trigger another known issue with D3D12 Agility version <= 4.
	*/
	bool bAllowGetShaderIdentifierOnCollectionSubObject = true;

	/**
	* Some machine configurations have known issues when transient resource aliasing is used.
	* If we detect such configuration, we can fall back to non-aliasing code path which is much less efficient.
	*/
	bool bAllowTransientResourceAllocator = true;
};

extern FD3D12WorkaroundFlags GD3D12WorkaroundFlags;

/** Forward declare the context for the AMD AGS utility library. */
struct AGSContext;

struct INTCExtensionContext;

PRAGMA_DISABLE_DEPRECATION_WARNINGS

/** The interface which is implemented by the dynamically bound RHI. */
class FD3D12DynamicRHI : public ID3D12PlatformDynamicRHI
{
	friend class FD3D12CommandContext;

	static FD3D12DynamicRHI* SingleD3DRHI;

public:

	static FD3D12DynamicRHI* GetD3DRHI() { return SingleD3DRHI; }

private:

	/** Texture pool size */
	int64 RequestedTexturePoolSize;

public:

	/** Initialization constructor. */
	FD3D12DynamicRHI(const TArray<TSharedPtr<FD3D12Adapter>>& ChosenAdaptersIn, bool bInPixEventEnabled);

	/** Destructor */
	virtual ~FD3D12DynamicRHI();

	// FDynamicRHI interface.
	virtual void Init() override;
	virtual void PostInit() override;
	virtual void Shutdown() override;
	virtual const TCHAR* GetName() override { return TEXT("D3D12"); }

	template<typename TRHIType>
	static FORCEINLINE typename TD3D12ResourceTraits<TRHIType>::TConcreteType* ResourceCast(TRHIType* Resource)
	{
		return static_cast<typename TD3D12ResourceTraits<TRHIType>::TConcreteType*>(Resource);
	}

	template<typename TRHIType>
	static FORCEINLINE_DEBUGGABLE typename TD3D12ResourceTraits<TRHIType>::TConcreteType* ResourceCast(TRHIType* Resource, uint32 GPUIndex)
	{
		using ReturnType = typename TD3D12ResourceTraits<TRHIType>::TConcreteType;
		ReturnType* Object = ResourceCast(Resource);
		return Object ? static_cast<ReturnType*>(Object->GetLinkedObject(GPUIndex)) : nullptr;
	}

	virtual FD3D12CommandContext* CreateCommandContext(FD3D12Device* InParent, ED3D12CommandQueueType InQueueType, bool InIsDefaultContext);
	virtual void CreateCommandQueue(FD3D12Device* Device, const D3D12_COMMAND_QUEUE_DESC& Desc, TRefCountPtr<ID3D12CommandQueue>& OutCommandQueue);

	virtual bool GetHardwareGPUFrameTime(double& OutGPUFrameTime) const
	{ 
		OutGPUFrameTime = 0.0;
		return false;
	}

	virtual FSamplerStateRHIRef RHICreateSamplerState(const FSamplerStateInitializerRHI& Initializer) final override;
	virtual FRasterizerStateRHIRef RHICreateRasterizerState(const FRasterizerStateInitializerRHI& Initializer) final override;
	virtual FDepthStencilStateRHIRef RHICreateDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer) final override;
	virtual FBlendStateRHIRef RHICreateBlendState(const FBlendStateInitializerRHI& Initializer) final override;
	virtual FVertexDeclarationRHIRef RHICreateVertexDeclaration(const FVertexDeclarationElementList& Elements) final override;
	virtual FPixelShaderRHIRef RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override;
	virtual FVertexShaderRHIRef RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override;
	virtual FMeshShaderRHIRef RHICreateMeshShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override;
	virtual FAmplificationShaderRHIRef RHICreateAmplificationShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override;
	virtual FGeometryShaderRHIRef RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override;
	virtual FComputeShaderRHIRef RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash) override;
	virtual FGPUFenceRHIRef RHICreateGPUFence(const FName& Name) final override;
	virtual FStagingBufferRHIRef RHICreateStagingBuffer() final override;
	virtual void* RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI) final override;
    virtual void RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer) final override;
	virtual FBoundShaderStateRHIRef RHICreateBoundShaderState(FRHIVertexDeclaration* VertexDeclaration, FRHIVertexShader* VertexShader, FRHIPixelShader* PixelShader, FRHIGeometryShader* GeometryShader) final override;
	FBoundShaderStateRHIRef DX12CreateBoundShaderState(const FBoundShaderStateInput& BoundShaderStateInput);
	virtual FGraphicsPipelineStateRHIRef RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer) final override;
	virtual TRefCountPtr<FRHIComputePipelineState> RHICreateComputePipelineState(FRHIComputeShader* ComputeShader) final override;
	virtual void RHICreateTransition(FRHITransition* Transition, const FRHITransitionCreateInfo& CreateInfo) final override;
	virtual void RHIReleaseTransition(FRHITransition* Transition) final override;
	virtual FUniformBufferRHIRef RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout* Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation) final override;
	virtual void RHIUpdateUniformBuffer(FRHICommandListBase& RHICmdList, FRHIUniformBuffer* UniformBufferRHI, const void* Contents) final override;
	virtual FBufferRHIRef RHICreateBuffer(FRHICommandListBase& RHICmdList, uint32 Size, EBufferUsageFlags Usage, uint32 Stride, ERHIAccess ResourceState, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual void RHITransferBufferUnderlyingResource(FRHIBuffer* DestBuffer, FRHIBuffer* SrcBuffer) final override;
	virtual void RHICopyBuffer(FRHIBuffer* SourceBuffer, FRHIBuffer* DestBuffer) final override;
	virtual void* RHILockBuffer(FRHICommandListBase& RHICmdList, FRHIBuffer* Buffer, uint32 Offset, uint32 Size, EResourceLockMode LockMode) final override;
	virtual void* RHILockBufferMGPU(FRHICommandListBase& RHICmdList, FRHIBuffer* Buffer, uint32 GPUIndex, uint32 Offset, uint32 Size, EResourceLockMode LockMode) final override;
	virtual void RHIUnlockBuffer(FRHICommandListBase& RHICmdList, FRHIBuffer* Buffer) final override;
	virtual void RHIUnlockBufferMGPU(FRHICommandListBase& RHICmdList, FRHIBuffer* Buffer, uint32 GPUIndex) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIBuffer* Buffer, bool bUseUAVCounter, bool bAppendBuffer) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel, uint16 FirstArraySlice, uint16 NumArraySlices) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel, uint8 Format, uint16 FirstArraySlice, uint16 NumArraySlices) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIBuffer* Buffer, uint8 Format) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIBuffer* Buffer) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIBuffer* Buffer, uint32 Stride, uint8 Format) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(const FShaderResourceViewInitializer& Initializer) final override;
	virtual void RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIBuffer* Buffer, uint32 Stride, uint8 Format) final override;
	virtual void RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIBuffer* Buffer) final override;
	virtual FRHICalcTextureSizeResult RHICalcTexturePlatformSize(const FRHITextureDesc& Desc, uint32 FirstMipIndex) override;
	virtual uint64 RHIGetMinimumAlignmentForBufferBackedSRV(EPixelFormat Format) final override;
	virtual void RHIGetTextureMemoryStats(FTextureMemoryStats& OutStats) final override;
	virtual bool RHIGetTextureMemoryVisualizeData(FColor* TextureData, int32 SizeX, int32 SizeY, int32 Pitch, int32 PixelSize) final override;
	virtual FTextureRHIRef RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, void** InitialMipData, uint32 NumInitialMips) final override;
	virtual FTextureRHIRef RHICreateTexture(const FRHITextureCreateDesc& CreateDesc) override;
	virtual void RHICopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo) final override;
	virtual uint32 RHIComputeMemorySize(FRHITexture* TextureRHI) final override;
	virtual FTexture2DRHIRef RHIAsyncReallocateTexture2D(FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus) override;
	virtual ETextureReallocationStatus RHIFinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted) final override;
	virtual ETextureReallocationStatus RHICancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted) final override;
	virtual void* RHILockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override;
	virtual void RHIUnlockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail) final override;
	virtual void* RHILockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override;
	virtual void RHIUnlockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, bool bLockWithinMiptail) final override;
	virtual void RHIUpdateTexture2D(FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData) final override;
	virtual void RHIUpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData) final override;
	virtual FUpdateTexture3DData BeginUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion) final override;
	virtual void EndUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FUpdateTexture3DData& UpdateData) final override;
	virtual void EndMultiUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, TArray<FUpdateTexture3DData>& UpdateDataArray) final override;
	virtual void* RHILockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override;
	virtual void RHIUnlockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail) final override;
	virtual void RHIBindDebugLabelName(FRHITexture* Texture, const TCHAR* Name) final override;
	virtual void RHIBindDebugLabelName(FRHIBuffer* Buffer, const TCHAR* Name) final override;
	virtual void RHIReadSurfaceData(FRHITexture* Texture, FIntRect Rect, TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags) final override;
	virtual void RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect InRect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags) final override;
	virtual void RHIMapStagingSurface(FRHITexture* Texture, FRHIGPUFence* Fence, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex = 0) final override;
	virtual void RHIUnmapStagingSurface(FRHITexture* Texture, uint32 GPUIndex = 0) final override;
	virtual void RHIReadSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, FReadSurfaceDataFlags InFlags) final override;
	virtual void RHIReadSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace, int32 ArrayIndex, int32 MipIndex) final override;
	virtual void RHIRead3DSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, FIntPoint ZMinMax, TArray<FFloat16Color>& OutData) final override;
	virtual void RHIRead3DSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, FIntPoint ZMinMax, TArray<FFloat16Color>& OutData, FReadSurfaceDataFlags InFlags) final override;
	virtual FRenderQueryRHIRef RHICreateRenderQuery(ERenderQueryType QueryType) final override;
	virtual bool RHIGetRenderQueryResult(FRHIRenderQuery* RenderQuery, uint64& OutResult, bool bWait, uint32 GPUIndex = INDEX_NONE) final override;
	virtual uint32 RHIGetViewportNextPresentGPUIndex(FRHIViewport* Viewport) final override;
	virtual FTexture2DRHIRef RHIGetViewportBackBuffer(FRHIViewport* Viewport) final override;
	virtual void RHIAliasTextureResources(FTextureRHIRef& DestTexture, FTextureRHIRef& SrcTexture) final override;
	virtual FTextureRHIRef RHICreateAliasedTexture(FTextureRHIRef& SourceTexture) final override;
	virtual void RHIGetDisplaysInformation(FDisplayInformationArray& OutDisplayInformation) final override;
	virtual void RHIAdvanceFrameFence() final override;
	virtual void RHIAdvanceFrameForGetViewportBackBuffer(FRHIViewport* Viewport) final override;
	virtual void RHIAcquireThreadOwnership() final override;
	virtual void RHIReleaseThreadOwnership() final override;
	virtual void RHIFlushResources() final override;
	virtual uint32 RHIGetGPUFrameCycles(uint32 GPUIndex = 0) final override;
	virtual FViewportRHIRef RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat) final override;
	virtual void RHIResizeViewport(FRHIViewport* Viewport, uint32 SizeX, uint32 SizeY, bool bIsFullscreen) final override;
	virtual void RHIResizeViewport(FRHIViewport* ViewportRHI, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat) final override;
	virtual void RHITick(float DeltaTime) final override;
	virtual void RHIBlockUntilGPUIdle() final override;
	virtual void RHISubmitCommandsAndFlushGPU() final override;
	virtual bool RHIGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate) final override;
	virtual void RHIGetSupportedResolution(uint32& Width, uint32& Height) final override;
	virtual void RHIVirtualTextureSetFirstMipInMemory(FRHITexture2D* Texture, uint32 FirstMip) override;
	virtual void RHIVirtualTextureSetFirstMipVisible(FRHITexture2D* Texture, uint32 FirstMip) override;
	virtual void RHIExecuteCommandList(FRHICommandList* CmdList) final override;
	virtual void* RHIGetNativeDevice() final override;
	virtual void* RHIGetNativeGraphicsQueue() final override;
	virtual void* RHIGetNativeComputeQueue() final override;
	virtual void* RHIGetNativeInstance() final override;
	virtual class IRHICommandContext* RHIGetDefaultContext() final override;
	virtual class IRHIComputeContext* RHIGetDefaultAsyncComputeContext() final override;
	virtual class IRHICommandContextContainer* RHIGetCommandContextContainer(int32 Index, int32 Num) final override;

	virtual IRHITransientResourceAllocator* RHICreateTransientResourceAllocator() override;

#if WITH_MGPU
	virtual IRHICommandContextContainer* RHIGetCommandContextContainer(int32 Index, int32 Num, FRHIGPUMask GPUMask)final override;
#endif

	// ID3D12DynamicRHI interface.
	virtual TArray<FD3D12MinimalAdapterDesc> RHIGetAdapterDescs() const final override;
	virtual bool RHIIsPixEnabled() const final override;
	virtual ID3D12CommandQueue* RHIGetCommandQueue() const final override;
	virtual ID3D12Device* RHIGetDevice(uint32 InIndex) const final override;
	virtual uint32 RHIGetDeviceNodeMask(uint32 InIndex) const final override;
	virtual ID3D12GraphicsCommandList* RHIGetGraphicsCommandList(uint32 InDeviceIndex) const final override;
	virtual DXGI_FORMAT RHIGetSwapChainFormat(EPixelFormat InFormat) const final override;
	virtual FTexture2DRHIRef RHICreateTexture2DFromResource(EPixelFormat Format, ETextureCreateFlags TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D12Resource* Resource) final override;
	virtual FTexture2DArrayRHIRef RHICreateTexture2DArrayFromResource(EPixelFormat Format, ETextureCreateFlags TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D12Resource* Resource) final override;
	virtual FTextureCubeRHIRef RHICreateTextureCubeFromResource(EPixelFormat Format, ETextureCreateFlags TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D12Resource* Resource) final override;
	virtual ID3D12Resource* RHIGetResource(FRHIBuffer* InBuffer) const final override;
	virtual uint32 RHIGetResourceDeviceIndex(FRHIBuffer* InBuffer) const final override;
	virtual ID3D12Resource* RHIGetResource(FRHITexture* InTexture) const final override;
	virtual uint32 RHIGetResourceDeviceIndex(FRHITexture* InTexture) const final override;
	virtual int64 RHIGetResourceMemorySize(FRHITexture* InTexture) const final override;
	virtual bool RHIIsResourcePlaced(FRHITexture* InTexture) const final override;
	virtual D3D12_CPU_DESCRIPTOR_HANDLE RHIGetRenderTargetView(FRHITexture* InTexture, int32 InMipIndex = 0, int32 InArraySliceIndex = 0) const final override;
	virtual void RHIFinishExternalComputeWork(uint32 InDeviceIndex, ID3D12GraphicsCommandList* InCommandList) final override;
	virtual void RHIRegisterWork(uint32 InDeviceIndex, uint32 NumPrimitives) final override;
	virtual void RHIAddPendingBarrier(FRHITexture* InTexture, D3D12_RESOURCE_STATES InState, uint32 InSubResource) final override;
	virtual void RHIExecuteOnCopyCommandQueue(TFunction<void(ID3D12CommandQueue*)>&& CodeToRun) final override;

	//
	// The Following functions are the _RenderThread version of the above functions. They allow the RHI to control the thread synchronization for greater efficiency.
	// These will be un-commented as they are implemented.
	//

	virtual FVertexShaderRHIRef CreateVertexShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreateVertexShader(Code, Hash);
	}

	virtual FMeshShaderRHIRef CreateMeshShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreateMeshShader(Code, Hash);
	}

	virtual FAmplificationShaderRHIRef CreateAmplificationShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreateAmplificationShader(Code, Hash);
	}

	virtual FGeometryShaderRHIRef CreateGeometryShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreateGeometryShader(Code, Hash);
	}

	virtual FPixelShaderRHIRef CreatePixelShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreatePixelShader(Code, Hash);
	}

	virtual FComputeShaderRHIRef CreateComputeShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreateComputeShader(Code, Hash);
	}

	virtual void UpdateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData) override final;
	virtual void* LockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush = true) override final;
	virtual void UnlockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush = true) override final;

	virtual void* LockTexture2DArray_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2DArray* Texture, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) override final;
	virtual void UnlockTexture2DArray_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2DArray* Texture, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail) override final;

	virtual FTexture2DRHIRef AsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus);
	virtual ETextureReallocationStatus FinalizeAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
	{
		return RHIFinalizeAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
	}
	virtual ETextureReallocationStatus CancelAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
	{
		return RHICancelAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
	}

	virtual FTextureRHIRef RHICreateTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, const FRHITextureCreateDesc& CreateDesc) override;

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, bool bUseUAVCounter, bool bAppendBuffer) override final;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel, uint16 FirstArraySlice, uint16 NumArraySlices) override final;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel, uint8 Format, uint16 FirstArraySlice, uint16 NumArraySlices) override final;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint8 Format) override final;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo) override final;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint32 Stride, uint8 Format) override final;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer) override final;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceViewWriteMask_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D) override final;

	virtual FShaderResourceViewRHIRef CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint32 Stride, uint8 Format) override final;
	virtual FShaderResourceViewRHIRef CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer) override final;
	virtual FShaderResourceViewRHIRef CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer) override final;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer) override final;

	virtual FRenderQueryRHIRef RHICreateRenderQuery_RenderThread(class FRHICommandListImmediate& RHICmdList, ERenderQueryType QueryType) override final
	{
		return RHICreateRenderQuery(QueryType);
	}

	void RHICalibrateTimers() override;

#if D3D12_RHI_RAYTRACING

	virtual FRayTracingAccelerationStructureSize RHICalcRayTracingSceneSize(uint32 MaxInstances, ERayTracingAccelerationStructureFlags Flags) final override;
	virtual FRayTracingAccelerationStructureSize RHICalcRayTracingGeometrySize(const FRayTracingGeometryInitializer& Initializer) final override;

	virtual FRayTracingGeometryRHIRef RHICreateRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer) final override;
	virtual FRayTracingSceneRHIRef RHICreateRayTracingScene(FRayTracingSceneInitializer2 Initializer) final override;
	virtual FRayTracingShaderRHIRef RHICreateRayTracingShader(TArrayView<const uint8> Code, const FSHAHash& Hash, EShaderFrequency ShaderFrequency) final override;
	virtual FRayTracingPipelineStateRHIRef RHICreateRayTracingPipelineState(const FRayTracingPipelineStateInitializer& Initializer) final override;
	virtual void RHITransferRayTracingGeometryUnderlyingResource(FRHIRayTracingGeometry* DestGeometry, FRHIRayTracingGeometry* SrcGeometry) final override;
#endif //D3D12_RHI_RAYTRACING

	bool CheckGpuHeartbeat() const override;

	bool RHIRequiresComputeGenerateMips() const override { return true; };

	bool IsQuadBufferStereoEnabled() const;
	void DisableQuadBufferStereo();

	static int32 GetResourceBarrierBatchSizeLimit();

	FBufferRHIRef CreateBuffer(FRHICommandListBase& RHICmdList, uint32 Size, EBufferUsageFlags Usage, uint32 Stride, ERHIAccess ResourceState, FRHIResourceCreateInfo& CreateInfo);
	void* LockBuffer(FRHICommandListBase& RHICmdList, FD3D12Buffer* Buffer, uint32 BufferSize, EBufferUsageFlags BufferUsage, uint32 Offset, uint32 Size, EResourceLockMode LockMode);
	void UnlockBuffer(FRHICommandListBase& RHICmdList, FD3D12Buffer* Buffer, EBufferUsageFlags BufferUsage);

	static inline bool ShouldDeferBufferLockOperation(FRHICommandListBase* RHICmdList)
	{
		if (RHICmdList == nullptr)
		{
			return false;
		}

		if (RHICmdList->IsBottomOfPipe())
		{
			return false;
		}

		return true;
	}

	virtual bool BeginUpdateTexture3D_ComputeShader(FUpdateTexture3DData& UpdateData, FD3D12UpdateTexture3DData* UpdateDataD3D12)
	{
		// Not supported on PC
		return false;
	}
	virtual void EndUpdateTexture3D_ComputeShader(FUpdateTexture3DData& UpdateData, FD3D12UpdateTexture3DData* UpdateDataD3D12)
	{
		// Not supported on PC
	}

	FUpdateTexture3DData BeginUpdateTexture3D_Internal(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion);
	void EndUpdateTexture3D_Internal(FUpdateTexture3DData& UpdateData);

	void UpdateBuffer(FD3D12ResourceLocation* Dest, uint32 DestOffset, FD3D12ResourceLocation* Source, uint32 SourceOffset, uint32 NumBytes);

#if UE_BUILD_DEBUG	
	uint32 SubmissionLockStalls;
	uint32 DrawCount;
	uint64 PresentCount;
#endif

	/** Determine if an two views intersect */
	template <class LeftT, class RightT>
	static inline bool ResourceViewsIntersect(FD3D12View<LeftT>* pLeftView, FD3D12View<RightT>* pRightView)
	{
		if (pLeftView == nullptr || pRightView == nullptr)
		{
			// Cannot intersect if at least one is null
			return false;
		}

		if ((void*)pLeftView == (void*)pRightView)
		{
			// Cannot intersect with itself
			return false;
		}

		FD3D12Resource* pRTVResource = pLeftView->GetResource();
		FD3D12Resource* pSRVResource = pRightView->GetResource();
		if (pRTVResource != pSRVResource)
		{
			// Not the same resource
			return false;
		}

		// Same resource, so see if their subresources overlap
		return !pLeftView->DoesNotOverlap(*pRightView);
	}

	static inline bool IsTransitionNeeded(bool bInAllowStateMerging, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES& After)
	{
		check(Before != D3D12_RESOURCE_STATE_CORRUPT && After != D3D12_RESOURCE_STATE_CORRUPT);
		check(Before != D3D12_RESOURCE_STATE_TBD && After != D3D12_RESOURCE_STATE_TBD);

		// Depth write is actually a suitable for read operations as a "normal" depth buffer.
		if (bInAllowStateMerging && (Before == D3D12_RESOURCE_STATE_DEPTH_WRITE) && (After == D3D12_RESOURCE_STATE_DEPTH_READ))
		{
			return false;
		}

		// COMMON is an oddball state that doesn't follow the RESOURE_STATE pattern of 
		// having exactly one bit set so we need to special case these
		if (After == D3D12_RESOURCE_STATE_COMMON)
		{
			// Before state should not have the common state otherwise it's invalid transition
			check(Before != D3D12_RESOURCE_STATE_COMMON);
			return true;
		}

		if (bInAllowStateMerging)
		{
			// We should avoid doing read-to-read state transitions. But when we do, we should avoid turning off already transitioned bits,
			// e.g. VERTEX_BUFFER -> SHADER_RESOURCE is turned into VERTEX_BUFFER -> VERTEX_BUFFER | SHADER_RESOURCE.
			// This reduces the number of resource transitions and ensures automatic states from resource bindings get properly combined.
			D3D12_RESOURCE_STATES Combined = Before | After;
			if ((Combined & (D3D12_RESOURCE_STATE_GENERIC_READ | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)) == Combined)
			{
				After = Combined;
			}
		}

		return Before != After;
	}

	enum class ETransitionMode
	{
		Apply,
		Validate
	};

	/** Transition a resource's state based on a Render target view */
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12RenderTargetView* pView, D3D12_RESOURCE_STATES after, ETransitionMode InMode)
	{
		// Early out if we are not using engine transitions and not validating them
		check(InMode == ETransitionMode::Validate);
		if (!GUseInternalTransitions && !GValidateInternalTransitions)
			return;

		FD3D12Resource* pResource = pView->GetResource();

		const D3D12_RENDER_TARGET_VIEW_DESC &desc = pView->GetDesc();
		switch (desc.ViewDimension)
		{
		case D3D12_RTV_DIMENSION_TEXTURE3D:
			// Note: For volume (3D) textures, all slices for a given mipmap level are a single subresource index.
			// Fall-through
		case D3D12_RTV_DIMENSION_TEXTURE2D:
		case D3D12_RTV_DIMENSION_TEXTURE2DMS:
			// Only one subresource to transition
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, desc.Texture2D.MipSlice, InMode);
			break;

		case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
		{
			// Multiple subresources to transition
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, pView->GetViewSubresourceSubset(), InMode);
			break;
		}

		default:
			check(false);	// Need to update this code to include the view type
			break;
		}
	}

	/** Transition a resource's state based on a Depth stencil view's desc flags */
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12DepthStencilView* pView, ETransitionMode InMode)
	{
		// Early out if we are not using engine transitions and not validating them
		check(InMode == ETransitionMode::Validate);
		if (!GUseInternalTransitions && !GValidateInternalTransitions)
			return;

		// Determine the required subresource states from the view desc
		const D3D12_DEPTH_STENCIL_VIEW_DESC& DSVDesc = pView->GetDesc();
		const bool bDSVDepthIsWritable = (DSVDesc.Flags & D3D12_DSV_FLAG_READ_ONLY_DEPTH) == 0;
		const bool bDSVStencilIsWritable = (DSVDesc.Flags & D3D12_DSV_FLAG_READ_ONLY_STENCIL) == 0;
		// TODO: Check if the PSO depth stencil is writable. When this is done, we need to transition in SetDepthStencilState too.

		// This code assumes that the DSV always contains the depth plane
		check(pView->HasDepth());
		const bool bHasDepth = true;
		const bool bHasStencil = pView->HasStencil();
		const bool bDepthIsWritable = bHasDepth && bDSVDepthIsWritable;
		const bool bStencilIsWritable = bHasStencil && bDSVStencilIsWritable;

		// DEPTH_WRITE is suitable for read operations when used as a normal depth/stencil buffer.
		FD3D12Resource* pResource = pView->GetResource();
		if (bDepthIsWritable)
		{
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, D3D12_RESOURCE_STATE_DEPTH_WRITE, pView->GetDepthOnlyViewSubresourceSubset(), InMode);
		}

		if (bStencilIsWritable)
		{
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, D3D12_RESOURCE_STATE_DEPTH_WRITE, pView->GetStencilOnlyViewSubresourceSubset(), InMode);
		}
	}

	/** Transition a resource's state based on a Depth stencil view */
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12DepthStencilView* pView, D3D12_RESOURCE_STATES after, ETransitionMode InMode)
	{
		// Early out if we are not using engine transitions and not validating them
		check(InMode == ETransitionMode::Validate);
		if (!GUseInternalTransitions && !GValidateInternalTransitions)
			return;

		FD3D12Resource* pResource = pView->GetResource();

		const D3D12_DEPTH_STENCIL_VIEW_DESC &desc = pView->GetDesc();
		switch (desc.ViewDimension)
		{
		case D3D12_DSV_DIMENSION_TEXTURE2D:
		case D3D12_DSV_DIMENSION_TEXTURE2DMS:
			if (pResource->GetPlaneCount() > 1)
			{
				// Multiple subresources to transtion
				TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, pView->GetViewSubresourceSubset(), InMode);
				break;
			}
			else
			{
				// Only one subresource to transition
				check(pResource->GetPlaneCount() == 1);
				TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, desc.Texture2D.MipSlice, InMode);
			}
			break;

		case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
		case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
		{
			// Multiple subresources to transtion
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, pView->GetViewSubresourceSubset(), InMode);
			break;
		}

		default:
			check(false);	// Need to update this code to include the view type
			break;
		}
	}

	/** Transition a resource's state based on a Unordered access view */
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12UnorderedAccessView* pView, D3D12_RESOURCE_STATES after, ETransitionMode InMode)
	{
		// Early out if we are not using engine transitions and not validating them
		if (!GUseInternalTransitions && !GValidateInternalTransitions)
			return;

		FD3D12Resource* pResource = pView->GetResource();

		const D3D12_UNORDERED_ACCESS_VIEW_DESC &desc = pView->GetDesc();
		switch (desc.ViewDimension)
		{
		case D3D12_UAV_DIMENSION_BUFFER:
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, 0, InMode);
			break;

		case D3D12_UAV_DIMENSION_TEXTURE2D:
			// Only one subresource to transition
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, desc.Texture2D.MipSlice, InMode);
			break;

		case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
		{
			// Multiple subresources to transtion
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, pView->GetViewSubresourceSubset(), InMode);
			break;
		}
		case D3D12_UAV_DIMENSION_TEXTURE3D:
		{
			// Multiple subresources to transtion
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, pView->GetViewSubresourceSubset(), InMode);
			break;
		}

		default:
			check(false);	// Need to update this code to include the view type
			break;
		}
	}

	/** Transition a resource's state based on a Shader resource view */
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12ShaderResourceView* pView, D3D12_RESOURCE_STATES after, ETransitionMode InMode)
	{
		// Early out if we are not using engine transitions and not validating them
		if (!GUseInternalTransitions && !GValidateInternalTransitions)
			return;

		FD3D12Resource* pResource = pView->GetResource();

		if (!pResource || !pResource->RequiresResourceStateTracking())
		{
			// Early out if we never need to do state tracking, the resource should always be in an SRV state.
			return;
		}

		const D3D12_RESOURCE_DESC &ResDesc = pResource->GetDesc();
		const CViewSubresourceSubset &subresourceSubset = pView->GetViewSubresourceSubset();

		const D3D12_SHADER_RESOURCE_VIEW_DESC &desc = pView->GetDesc();
		switch (desc.ViewDimension)
		{
		default:
		{
			// Transition the resource
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, subresourceSubset, InMode);
			break;
		}

		case D3D12_SRV_DIMENSION_BUFFER:
		{
			if (pResource->GetHeapType() == D3D12_HEAP_TYPE_DEFAULT)
			{
				// Transition the resource
				TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_TBD, after, subresourceSubset, InMode);
			}
			break;
		}
		}
	}

	// Transition a specific subresource to the after state.
	// Return true if UAV barrier is required
	static inline bool TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12Resource* pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, uint32 subresource, ETransitionMode InMode)
	{
		// Early out if we are not using engine transitions and not validating them
		if (InMode == ETransitionMode::Validate && !GUseInternalTransitions && !GValidateInternalTransitions)
			return false;

		return TransitionResourceWithTracking(hCommandList, pResource, before, after, subresource, InMode);
	}

	// Transition a subset of subresources to the after state.
	// Return true if UAV barrier is required
	static inline bool TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12Resource* pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, const CViewSubresourceSubset& subresourceSubset, ETransitionMode InMode)
	{
		// Early out if we are not using engine transitions and not validating them
		if (InMode == ETransitionMode::Validate && !GUseInternalTransitions && !GValidateInternalTransitions)
			return false;

		return TransitionResourceWithTracking(hCommandList, pResource, before, after, subresourceSubset, InMode);
	}

	// Transition a subresource from current to a new state, using resource state tracking.
	static bool TransitionResourceWithTracking(FD3D12CommandListHandle& hCommandList, FD3D12Resource* pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, uint32 subresource, ETransitionMode InMode)
	{
		check(pResource);
		check(pResource->RequiresResourceStateTracking());
		check(!((after & (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) && (pResource->GetDesc().Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)));

#ifdef PLATFORM_SUPPORTS_RESOURCE_COMPRESSION
		after |= pResource->GetCompressedState();
#endif

		hCommandList.UpdateResidency(pResource);

		bool bRequireUAVBarrier = false;

		CResourceState& ResourceState = hCommandList.GetResourceState(pResource);
		if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES && !ResourceState.AreAllSubresourcesSame())
		{
			// Slow path. Want to transition the entire resource (with multiple subresources). But they aren't in the same state.

			const uint8 SubresourceCount = pResource->GetSubresourceCount();
			for (uint32 SubresourceIndex = 0; SubresourceIndex < SubresourceCount; SubresourceIndex++)
			{
				bool bForceInAfterState = true;
				bRequireUAVBarrier |= ValidateAndSetResourceState(hCommandList, pResource, ResourceState, SubresourceIndex, before, after, bForceInAfterState, InMode);
			}

			// The entire resource should now be in the after state on this command list (even if all barriers are pending)
			verify(ResourceState.CheckAllSubresourceSame());
			check(EnumHasAllFlags(ResourceState.GetSubresourceState(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES), after));
		}
		else
		{
			bool bForceInAfterState = false;
			bRequireUAVBarrier = ValidateAndSetResourceState(hCommandList, pResource, ResourceState, subresource, before, after, bForceInAfterState, InMode);
		}

		return bRequireUAVBarrier;
	}

	// Transition subresources from current to a new state, using resource state tracking.
	static bool TransitionResourceWithTracking(FD3D12CommandListHandle& hCommandList, FD3D12Resource* pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, const CViewSubresourceSubset& subresourceSubset, ETransitionMode InMode)
	{
		check(pResource);
		check(pResource->RequiresResourceStateTracking());
		check(!((after & (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) && (pResource->GetDesc().Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)));

#ifdef PLATFORM_SUPPORTS_RESOURCE_COMPRESSION
		after |= pResource->GetCompressedState();
#endif

		hCommandList.UpdateResidency(pResource);
		const bool bIsWholeResource = subresourceSubset.IsWholeResource();
		CResourceState& ResourceState = hCommandList.GetResourceState(pResource);

		bool bRequireUAVBarrier = false;

		if (bIsWholeResource && ResourceState.AreAllSubresourcesSame())
		{
			// Fast path. Transition the entire resource from one state to another.
			bool bForceInAfterState = false;
			bRequireUAVBarrier = ValidateAndSetResourceState(hCommandList, pResource, ResourceState, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after, bForceInAfterState, InMode);
		}
		else
		{
			// Slower path. Either the subresources are in more than 1 state, or the view only partially covers the resource.
			// Either way, we'll need to loop over each subresource in the view...

			bool bWholeResourceWasTransitionedToSameState = bIsWholeResource;
			for (CViewSubresourceSubset::CViewSubresourceIterator it = subresourceSubset.begin(); it != subresourceSubset.end(); ++it)
			{
				for (uint32 SubresourceIndex = it.StartSubresource(); SubresourceIndex < it.EndSubresource(); SubresourceIndex++)
				{
					bool bForceInAfterState = false;
					bRequireUAVBarrier |= ValidateAndSetResourceState(hCommandList, pResource, ResourceState, SubresourceIndex, before, after, bForceInAfterState, InMode);

					// Subresource not in the same state, then whole resource is not in the same state anymore
					if (ResourceState.GetSubresourceState(SubresourceIndex) != after)
						bWholeResourceWasTransitionedToSameState = false;
				}
			}

			// If we just transtioned every subresource to the same state, lets update it's tracking so it's on a per-resource level
			if (bWholeResourceWasTransitionedToSameState)
			{
				// Sanity check to make sure all subresources are really in the 'after' state
				verify(ResourceState.CheckAllSubresourceSame());
				check(EnumHasAllFlags(ResourceState.GetSubresourceState(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES), after));
			}
		}

		return bRequireUAVBarrier;
	}

	static bool ValidateAndSetResourceState(FD3D12CommandListHandle& InCommandList, FD3D12Resource* InResource, CResourceState& InResourceState, uint32 InSubresourceIndex, D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState, bool bInForceAfterState, ETransitionMode InMode)
	{
		// Only validate the current state?
		bool bValidateState = !GUseInternalTransitions && (InMode == ETransitionMode::Validate);

		// Try and get the correct D3D before state for the transition
		D3D12_RESOURCE_STATES TrackedState = InResourceState.GetSubresourceState(InSubresourceIndex);
		D3D12_RESOURCE_STATES BeforeState = TrackedState;

		// Special case for UAV access resources
		bool bHasUAVAccessResource = InResource->GetUAVAccessResource() != nullptr;

		// Still untracked in this command list, then try and find out a before state to use
		if (BeforeState == D3D12_RESOURCE_STATE_TBD)
		{
			if (bValidateState)
			{
				// Can't correctly validate on parallel command list because command list with final state which
				// updates the resource state might not have been executed yet (on RHI Thread)
				// Unless it's a transition on the default context and all transitions happen on the default context (validated somewhere else)
				if (GRHICommandList.Bypass() || InCommandList.GetCurrentOwningContext()->IsDefaultContext())
				{
					BeforeState = InResource->GetResourceState().GetSubresourceState(InSubresourceIndex);
				}
			}
			else if (GUseInternalTransitions)
			{
				// Already perform transition here if possible to skip patch up during command list execution
				if (InBeforeState != D3D12_RESOURCE_STATE_TBD)
				{
					check(BeforeState == D3D12_RESOURCE_STATE_TBD || BeforeState == InBeforeState);
					BeforeState = InBeforeState;

					// Add dummy pending barrier, because the end state needs to be updated during execute
					InCommandList.AddPendingResourceBarrier(InResource, D3D12_RESOURCE_STATE_TBD, InSubresourceIndex);
				}
				else
				{
					// Special handling for UAVAccessResource and transition to UAV - don't want to
					// enqueue pending resource to UAV because the actual resource won't transition
					// Adding of patch up will only be done when transitioning to non UAV state
					if (bHasUAVAccessResource && EnumHasAnyFlags(InAfterState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
					{
						InCommandList.AddAliasingBarrier(InResource->GetResource(), InResource->GetUAVAccessResource());
						InResourceState.SetSubresourceState(InSubresourceIndex, InAfterState);
					}
					else
					{
						// We need a pending resource barrier so we can setup the state before this command list executes
						InResourceState.SetSubresourceState(InSubresourceIndex, InAfterState);
						InCommandList.AddPendingResourceBarrier(InResource, InAfterState, InSubresourceIndex);
					}
				}
			}
			else
			{
				// We have enqueue the transition right now in the command list and can't add it to the pending list because
				// this resource can already have been used in the current state in the command list
				// so changing that state before this command list is invalid.				
				BeforeState = InBeforeState;
				if (BeforeState == D3D12_RESOURCE_STATE_TBD)
				{
					// If we don't have a valid before state, then we have to use the actual last stored state of the
					// resource. We can sadly enough only correctly do this when parallel command lists don't perform
					// any resource transition because then the current stored state might be invalid
					// (Currently validated during begin/end transition in D3D12Commands)
					BeforeState = InResource->GetResourceState().GetSubresourceState(InSubresourceIndex);
				}

				// Add dummy pending barrier, because the end state needs to be updated during execute
				InCommandList.AddPendingResourceBarrier(InResource, D3D12_RESOURCE_STATE_TBD, InSubresourceIndex);
			}
		}

		bool bRequireUAVBarrier = false;

		// Have a valid state now?
		check(BeforeState != D3D12_RESOURCE_STATE_TBD || GUseInternalTransitions || bValidateState);
		if (BeforeState != D3D12_RESOURCE_STATE_TBD)
		{
			// Make sure the before states match up or are unknown
			check(InBeforeState == D3D12_RESOURCE_STATE_TBD || BeforeState == InBeforeState);

			if (bValidateState)
			{
				// Check if all after states are valid and special case for DepthRead because then DepthWrite is also valid
				check(EnumHasAllFlags(BeforeState, InAfterState) || (BeforeState == D3D12_RESOURCE_STATE_DEPTH_WRITE && InAfterState == D3D12_RESOURCE_STATE_DEPTH_READ));
			}
			else
			{
				bool bApplyTransitionBarrier = true;

				// Require UAV barrier when before and after are UAV
				if (BeforeState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && InAfterState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
				{
					bRequireUAVBarrier = true;
				}
				// Special case for UAV access resources
				else if (bHasUAVAccessResource && EnumHasAnyFlags(BeforeState | InAfterState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
				{
					// inject an aliasing barrier
					const bool bFromUAV = EnumHasAnyFlags(BeforeState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					const bool bToUAV = EnumHasAnyFlags(InAfterState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					check(bFromUAV != bToUAV);

					InCommandList.AddAliasingBarrier(
						bFromUAV ? InResource->GetUAVAccessResource() : InResource->GetResource(),
						bToUAV ? InResource->GetUAVAccessResource() : InResource->GetResource());

					if (bToUAV)
					{
						InResourceState.SetUAVHiddenResourceState(BeforeState);
						bApplyTransitionBarrier = false;
					}
					else
					{
						D3D12_RESOURCE_STATES HiddenState = InResourceState.GetUAVHiddenResourceState();

						// Still unknown in this command list?
						if (HiddenState == D3D12_RESOURCE_STATE_TBD)
						{
							InCommandList.AddPendingResourceBarrier(InResource, InAfterState, InSubresourceIndex);
							InResourceState.SetSubresourceState(InSubresourceIndex, InAfterState);
							bApplyTransitionBarrier = false;
						}
						else
						{
							// Use the hidden state as the before state on the resource
							BeforeState = HiddenState;
						}
					}
				}

				if (bApplyTransitionBarrier)
				{
					// We're not using IsTransitionNeeded() when bInForceAfterState is set because we do want to transition even if 'after' is a subset of 'before'
					// This is so that we can ensure all subresources are in the same state, simplifying future barriers
					// No state merging when using engine transitions - otherwise next before state might not match up anymore)
					bool bAllowStateMerging = GUseInternalTransitions;
					if ((bInForceAfterState && BeforeState != InAfterState) || IsTransitionNeeded(bAllowStateMerging, BeforeState, InAfterState))
					{
						InCommandList.AddTransitionBarrier(InResource, BeforeState, InAfterState, InSubresourceIndex);
						InResourceState.SetSubresourceState(InSubresourceIndex, InAfterState);
					}
					// Force update the state when the tracked state is still unknown
					else if (TrackedState == D3D12_RESOURCE_STATE_TBD)
					{
						InResourceState.SetSubresourceState(InSubresourceIndex, InAfterState);
					}
				}
			}
		}

		return bRequireUAVBarrier;
	}

public:

#if	PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
	virtual void* CreateVirtualTexture(ETextureCreateFlags InFlags, D3D12_RESOURCE_DESC& ResourceDesc, const struct FD3D12TextureLayout& TextureLayout, FD3D12Resource** ppResource, FPlatformMemory::FPlatformVirtualMemoryBlock& RawTextureBlock, D3D12_RESOURCE_STATES InitialUsage = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) = 0;
	virtual void DestroyVirtualTexture(ETextureCreateFlags InFlags, void* RawTextureMemory, FPlatformMemory::FPlatformVirtualMemoryBlock& RawTextureBlock, uint64 CommittedTextureSize) = 0;
#endif
	virtual bool HandleSpecialLock(void*& MemoryOut, uint32 MipIndex, uint32 ArrayIndex, FD3D12Texture* InTexture, EResourceLockMode LockMode, uint32& DestStride) { return false; }
	virtual bool HandleSpecialUnlock(FRHICommandListBase* RHICmdList, uint32 MipIndex, FD3D12Texture* InTexture) { return false; }

	FD3D12Adapter& GetAdapter(uint32_t Index = 0) { return *ChosenAdapters[Index]; }
	const FD3D12Adapter& GetAdapter(uint32_t Index = 0) const { return *ChosenAdapters[Index]; }

	int32 GetNumAdapters() const { return ChosenAdapters.Num(); }

	bool IsPixEventEnabled() const { return bPixEventEnabled; }

	template<typename PerDeviceFunction>
	void ForEachDevice(ID3D12Device* inDevice, const PerDeviceFunction& pfPerDeviceFunction)
	{
		for (int AdapterIndex = 0; AdapterIndex < GetNumAdapters(); ++AdapterIndex)
		{
			FD3D12Adapter& D3D12Adapter = GetAdapter(AdapterIndex);
			for (uint32 GPUIndex : FRHIGPUMask::All())
			{
				FD3D12Device* D3D12Device = D3D12Adapter.GetDevice(GPUIndex);
				if (inDevice == nullptr || D3D12Device->GetDevice() == inDevice)
				{
					pfPerDeviceFunction(D3D12Device);
				}
			}
		}
	}

	AGSContext* GetAmdAgsContext() { return AmdAgsContext; }
	void SetAmdSupportedExtensionFlags(uint32 Flags) { AmdSupportedExtensionFlags = Flags; }
	uint32 GetAmdSupportedExtensionFlags() const { return AmdSupportedExtensionFlags; }

	INTCExtensionContext* GetIntelExtensionContext() { return IntelExtensionContext; }

protected:

	TArray<TSharedPtr<FD3D12Adapter>> ChosenAdapters;

#if D3D12RHI_SUPPORTS_WIN_PIX
	void* WinPixGpuCapturerHandle = nullptr;
#endif

	/** Can pix events be used */
	bool bPixEventEnabled = false;

	/** The feature level of the device. */
	D3D_FEATURE_LEVEL FeatureLevel;

	/**
	 * The context for the AMD AGS utility library.
	 * AGSContext does not implement AddRef/Release.
	 * Just use a bare pointer.
	 */
	AGSContext* AmdAgsContext;
	uint32 AmdSupportedExtensionFlags;

	INTCExtensionContext* IntelExtensionContext = nullptr;

	/** A buffer in system memory containing all zeroes of the specified size. */
	void* ZeroBuffer;
	uint32 ZeroBufferSize;

	/* Primary lock for RHIExecuteOnCopyCommandQueue */
	FCriticalSection CopyQueueCS;

public:

	virtual FD3D12ResourceDesc GetResourceDesc(const FRHITextureDesc& CreateInfo) const;

	virtual FD3D12Texture* CreateD3D12Texture(const FRHITextureCreateDesc& CreateDesc, class FRHICommandListImmediate* RHICmdList, ID3D12ResourceAllocator* ResourceAllocator = nullptr);
	FD3D12Buffer* CreateD3D12Buffer(class FRHICommandListBase* RHICmdList, uint32 Size, EBufferUsageFlags Usage, uint32 Stride, ERHIAccess ResourceState, FRHIResourceCreateInfo& CreateInfo, ID3D12ResourceAllocator* ResourceAllocator = nullptr);
	virtual FD3D12Texture* CreateNewD3D12Texture(const FRHITextureCreateDesc& CreateDesc, class FD3D12Device* Device);

	FRHIBuffer* CreateBuffer(const FRHIBufferCreateInfo& CreateInfo, const TCHAR* DebugName, ERHIAccess InitialState, ID3D12ResourceAllocator* ResourceAllocator);

	bool SetupDisplayHDRMetaData();

protected:

	FD3D12Texture* CreateTextureFromResource(bool bTextureArray, bool bCubeTexture, EPixelFormat Format, ETextureCreateFlags TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D12Resource* Resource);
	FD3D12Texture* CreateAliasedD3D12Texture2D(FD3D12Texture* SourceTexture);

	/**
	 * Gets the best supported MSAA settings from the provided MSAA count to check against.
	 *
	 * @param PlatformFormat		The format of the texture being created
	 * @param MSAACount				The MSAA count to check against.
	 * @param OutBestMSAACount		The best MSAA count that is suppored.  Could be smaller than MSAACount if it is not supported
	 * @param OutMSAAQualityLevels	The number MSAA quality levels for the best msaa count supported
	 */
	void GetBestSupportedMSAASetting(DXGI_FORMAT PlatformFormat, uint32 MSAACount, uint32& OutBestMSAACount, uint32& OutMSAAQualityLevels);

	/**
	* Returns a pointer to a texture resource that can be used for CPU reads.
	* Note: the returned resource could be the original texture or a new temporary texture.
	* @param TextureRHI - Source texture to create a staging texture from.
	* @param InRect - rectangle to 'stage'.
	* @param StagingRectOUT - parameter is filled with the rectangle to read from the returned texture.
	* @return The CPU readable Texture object.
	*/
	TRefCountPtr<FD3D12Resource> GetStagingTexture(FRHITexture* TextureRHI, FIntRect InRect, FIntRect& OutRect, FReadSurfaceDataFlags InFlags, D3D12_PLACED_SUBRESOURCE_FOOTPRINT &readBackHeapDesc, uint32 GPUIndex);

	void ReadSurfaceDataNoMSAARaw(FRHITexture* TextureRHI, FIntRect Rect, TArray<uint8>& OutData, FReadSurfaceDataFlags InFlags);

	void ReadSurfaceDataMSAARaw(FRHICommandList_RecursiveHazardous& RHICmdList, FRHITexture* TextureRHI, FIntRect Rect, TArray<uint8>& OutData, FReadSurfaceDataFlags InFlags);

	void SetupRecursiveResources();

	// This should only be called by Dynamic RHI member functions
	inline FD3D12Device* GetRHIDevice(uint32 GPUIndex) const
	{
		return GetAdapter().GetDevice(GPUIndex);
	}

	HANDLE FlipEvent;

	const bool bAllowVendorDevice;

	FDisplayInformationArray DisplayList;
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

/** Implements the D3D12RHI module as a dynamic RHI providing module. */
class FD3D12DynamicRHIModule : public IDynamicRHIModule
{
public:

	FD3D12DynamicRHIModule()
	{
	}

	~FD3D12DynamicRHIModule()
	{
	}

	// IModuleInterface
	virtual bool SupportsDynamicReloading() override { return false; }
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// IDynamicRHIModule
	virtual bool IsSupported() override { return IsSupported(ERHIFeatureLevel::SM5); }
	virtual bool IsSupported(ERHIFeatureLevel::Type RequestedFeatureLevel) override;
	virtual FDynamicRHI* CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel = ERHIFeatureLevel::Num) override;

private:

#if D3D12RHI_SUPPORTS_WIN_PIX
	void* WindowsPixDllHandle = nullptr;
	void* WinPixGpuCapturerHandle = nullptr;
#endif

	TArray<TSharedPtr<FD3D12Adapter>> ChosenAdapters;

	// set MaxSupportedFeatureLevel and ChosenAdapter
	void FindAdapter();
};

/**
*	Class of a scoped resource barrier.
*	This class avoids resource state tracking because resources will be
*	returned to their original state when the object leaves scope.
*/
class FScopeResourceBarrier
{
private:
	FD3D12CommandListHandle& hCommandList;
	FD3D12Resource* const pResource;
	const D3D12_RESOURCE_STATES Current;
	const D3D12_RESOURCE_STATES Desired;
	const uint32 Subresource;

public:
	explicit FScopeResourceBarrier(FD3D12CommandListHandle& hInCommandList, FD3D12Resource* pInResource, const D3D12_RESOURCE_STATES& InCurrent, const D3D12_RESOURCE_STATES InDesired, const uint32 InSubresource)
		: hCommandList(hInCommandList)
		, pResource(pInResource)
		, Current(InCurrent)
		, Desired(InDesired)
		, Subresource(InSubresource)
	{
		check(!pResource->RequiresResourceStateTracking());
		hCommandList.AddTransitionBarrier(pResource, Current, Desired, Subresource);
	}

	~FScopeResourceBarrier()
	{
		hCommandList.AddTransitionBarrier(pResource, Desired, Current, Subresource);
	}
};

/**
*	Class of a scoped resource barrier - handles both tracked and untracked resources
*/
class FScopedResourceBarrier
{
private:
	FD3D12CommandListHandle& hCommandList;
	FD3D12Resource* const pResource;
	D3D12_RESOURCE_STATES CurrentState;
	const D3D12_RESOURCE_STATES DesiredState;
	const uint32 Subresource;
	FD3D12DynamicRHI::ETransitionMode TransitionMode;

	bool bRestoreState;

public:
	FScopedResourceBarrier(FD3D12CommandListHandle& hInCommandList, FD3D12Resource* pInResource, const D3D12_RESOURCE_STATES InDesiredState, const uint32 InSubresource, FD3D12DynamicRHI::ETransitionMode InTransitionMode)
		: hCommandList(hInCommandList)
		, pResource(pInResource)
		, CurrentState(D3D12_RESOURCE_STATE_TBD)
		, DesiredState(InDesiredState)
		, Subresource(InSubresource)
		, TransitionMode(InTransitionMode)
		, bRestoreState(false)
	{
		// when we don't use resource state tracking, transition the resource (only if necessary)
		if (!pResource->RequiresResourceStateTracking())
		{
			CurrentState = pResource->GetDefaultResourceState();
			// Some states such as D3D12_RESOURCE_STATE_GENERIC_READ already includes D3D12_RESOURCE_STATE_COPY_SOURCE as well as other states, therefore transition isn't required.
			if (CurrentState != DesiredState && !EnumHasAllFlags(CurrentState, InDesiredState))
			{
				// we will add a transition, we need to transition back to the default state when the scoped object dies : 
				bRestoreState = true;
				hCommandList.AddTransitionBarrier(pResource, CurrentState, DesiredState, Subresource);
			}
		}
		else
		{
			// If we are not using the internal transitions and need to apply the state change, then store the current state of restore
			if (!GUseInternalTransitions && InTransitionMode == FD3D12DynamicRHI::ETransitionMode::Apply)
			{
				// try tracked state in command list
				CResourceState& ResourceState = hCommandList.GetResourceState(pInResource);
				CurrentState = ResourceState.GetSubresourceState(InSubresource);

				// if still unknown then use the stored state (not valid when transitions happen in parallel command lists)
				if (CurrentState == D3D12_RESOURCE_STATE_TBD)
				{
					CurrentState = pInResource->GetResourceState().GetSubresourceState(InSubresource);
				}

				// Restore state to current state when done
				bRestoreState = true;
			}

			FD3D12DynamicRHI::TransitionResource(hCommandList, pResource, CurrentState, DesiredState, Subresource, TransitionMode);
		}
	}

	~FScopedResourceBarrier()
	{
		// Return the resource to the original state if requested
		if (bRestoreState)
		{
			if (!pResource->RequiresResourceStateTracking())
			{
				hCommandList.AddTransitionBarrier(pResource, DesiredState, CurrentState, Subresource);
			}
			else
			{
				FD3D12DynamicRHI::TransitionResource(hCommandList, pResource, DesiredState, CurrentState, Subresource, TransitionMode);
			}
		}
	}
};


/**
*	Class of a scoped Map/Unmap().
*	This class ensures that Mapped subresources are appropriately unmapped.
*/
template<typename TType>
class FD3D12ScopeMap
{
private:
	ID3D12Resource* const pResource;
	const uint32 Subresource;
	const D3D12_RANGE* pReadRange;	// This indicates the region the CPU might read, and the coordinates are subresource-relative. A null pointer indicates the entire subresource might be read by the CPU.
	const D3D12_RANGE* pWriteRange;	// This indicates the region the CPU might have modified, and the coordinates are subresource-relative. A null pointer indicates the entire subresource might have been modified by the CPU.
	TType* pData;

public:
	explicit FD3D12ScopeMap(FD3D12Resource* pInResource, const uint32 InSubresource, const D3D12_RANGE* InReadRange, const D3D12_RANGE* InWriteRange)
		: pResource(pInResource->GetResource())
		, Subresource(InSubresource)
		, pReadRange(InReadRange)
		, pWriteRange(InWriteRange)
		, pData(nullptr)
	{
		VERIFYD3D12RESULT_EX(pResource->Map(Subresource, pReadRange, reinterpret_cast<void**>(&pData)), pInResource->GetParentDevice()->GetDevice());
	}

	explicit FD3D12ScopeMap(ID3D12Resource* pInResource, const uint32 InSubresource, const D3D12_RANGE* InReadRange, const D3D12_RANGE* InWriteRange)
		: pResource(pInResource)
		, Subresource(InSubresource)
		, pReadRange(InReadRange)
		, pWriteRange(InWriteRange)
		, pData(nullptr)
	{
		VERIFYD3D12RESULT_EX(pResource->Map(Subresource, pReadRange, reinterpret_cast<void**>(&pData)), pInResource->GetDevice());
	}

	~FD3D12ScopeMap()
	{
		pResource->Unmap(Subresource, pWriteRange);
	}

	bool IsValidForRead(const uint32 Index) const
	{
		return IsInRange(pReadRange, Index);
	}

	bool IsValidForWrite(const uint32 Index) const
	{
		return IsInRange(pWriteRange, Index);
	}

	TType& operator[] (const uint32 Index)
	{
		checkf(IsValidForRead(Index) || IsValidForWrite(Index), TEXT("Index %u is not valid for read or write based on the ranges used to Map/Unmap the resource."), Index);
		return *(pData + Index);
	}

	const TType& operator[] (const uint32 Index) const
	{
		checkf(IsValidForRead(Index), TEXT("Index %u is not valid for read based on the range used to Map the resource."), Index);
		return *(pData + Index);
	}

private:
	inline bool IsInRange(const D3D12_RANGE* pRange, const uint32 Index) const
	{
		if (pRange)
		{
			const uint64 Offset = Index * sizeof(TType);
			return (Offset >= pRange->Begin) && (Offset < pRange->End);
		}
		else
		{
			// Null means the entire resource is mapped for read or will be written to.
			return true;
		}
	}
};

// This namespace is needed to avoid a name clash with D3D11 RHI when linked together in monolithic builds. Otherwise the linker will just pick any variant instead of each RHI using their own version.
namespace D3D12RHI
{

inline DXGI_FORMAT FindSharedResourceDXGIFormat(DXGI_FORMAT InFormat, bool bSRGB)
{
	if (bSRGB)
	{
		switch (InFormat)
		{
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:    return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:    return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:    return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case DXGI_FORMAT_BC1_TYPELESS:         return DXGI_FORMAT_BC1_UNORM_SRGB;
		case DXGI_FORMAT_BC2_TYPELESS:         return DXGI_FORMAT_BC2_UNORM_SRGB;
		case DXGI_FORMAT_BC3_TYPELESS:         return DXGI_FORMAT_BC3_UNORM_SRGB;
		case DXGI_FORMAT_BC7_TYPELESS:         return DXGI_FORMAT_BC7_UNORM_SRGB;
		};
	}
	else
	{
		switch (InFormat)
		{
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:    return DXGI_FORMAT_B8G8R8X8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_BC1_TYPELESS:      return DXGI_FORMAT_BC1_UNORM;
		case DXGI_FORMAT_BC2_TYPELESS:      return DXGI_FORMAT_BC2_UNORM;
		case DXGI_FORMAT_BC3_TYPELESS:      return DXGI_FORMAT_BC3_UNORM;
		case DXGI_FORMAT_BC7_TYPELESS:      return DXGI_FORMAT_BC7_UNORM;
		};
	}
	switch (InFormat)
	{
	case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_UINT;
	case DXGI_FORMAT_R32G32B32_TYPELESS:    return DXGI_FORMAT_R32G32B32_UINT;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_UNORM;
	case DXGI_FORMAT_R32G32_TYPELESS:       return DXGI_FORMAT_R32G32_UINT;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return DXGI_FORMAT_R10G10B10A2_UNORM;
	case DXGI_FORMAT_R16G16_TYPELESS:       return DXGI_FORMAT_R16G16_UNORM;
	case DXGI_FORMAT_R8G8_TYPELESS:         return DXGI_FORMAT_R8G8_UNORM;
	case DXGI_FORMAT_R8_TYPELESS:           return DXGI_FORMAT_R8_UNORM;

	case DXGI_FORMAT_BC4_TYPELESS:         return DXGI_FORMAT_BC4_UNORM;
	case DXGI_FORMAT_BC5_TYPELESS:         return DXGI_FORMAT_BC5_UNORM;



	case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
	case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
		// Changing Depth Buffers to 32 bit on Dingo as D24S8 is actually implemented as a 32 bit buffer in the hardware
	case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
	}
	return InFormat;
}

inline DXGI_FORMAT FindDepthStencilResourceDXGIFormat(DXGI_FORMAT InFormat)
{
	switch (InFormat)
	{
	case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
	case DXGI_FORMAT_R16_FLOAT: return DXGI_FORMAT_R16_TYPELESS;
	}

	return InFormat;
}

inline DXGI_FORMAT GetPlatformTextureResourceFormat(DXGI_FORMAT InFormat, ETextureCreateFlags InFlags)
{
	// Find valid shared texture format
	if (EnumHasAnyFlags(InFlags, TexCreate_Shared))
	{
		return FindSharedResourceDXGIFormat(InFormat, EnumHasAnyFlags(InFlags, TexCreate_SRGB));
	}
	if (EnumHasAnyFlags(InFlags, TexCreate_DepthStencilTargetable))
	{
		return FindDepthStencilResourceDXGIFormat(InFormat);
	}

	return InFormat;
}

/** Find an appropriate DXGI format for the input format and SRGB setting. */
inline DXGI_FORMAT FindShaderResourceDXGIFormat(DXGI_FORMAT InFormat, bool bSRGB)
{
	if (bSRGB)
	{
		switch (InFormat)
		{
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:    return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:    return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case DXGI_FORMAT_BC1_TYPELESS:         return DXGI_FORMAT_BC1_UNORM_SRGB;
		case DXGI_FORMAT_BC2_TYPELESS:         return DXGI_FORMAT_BC2_UNORM_SRGB;
		case DXGI_FORMAT_BC3_TYPELESS:         return DXGI_FORMAT_BC3_UNORM_SRGB;
		case DXGI_FORMAT_BC7_TYPELESS:         return DXGI_FORMAT_BC7_UNORM_SRGB;
		};
	}
	else
	{
		switch (InFormat)
		{
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_BC1_TYPELESS:      return DXGI_FORMAT_BC1_UNORM;
		case DXGI_FORMAT_BC2_TYPELESS:      return DXGI_FORMAT_BC2_UNORM;
		case DXGI_FORMAT_BC3_TYPELESS:      return DXGI_FORMAT_BC3_UNORM;
		case DXGI_FORMAT_BC7_TYPELESS:      return DXGI_FORMAT_BC7_UNORM;
		};
	}
	switch (InFormat)
	{
	case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
	case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
		// Changing Depth Buffers to 32 bit on Dingo as D24S8 is actually implemented as a 32 bit buffer in the hardware
	case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
	}
	return InFormat;
}

/** Find an appropriate DXGI format unordered access of the raw format. */
inline DXGI_FORMAT FindUnorderedAccessDXGIFormat(DXGI_FORMAT InFormat)
{
	switch (InFormat)
	{
	case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
	return InFormat;
}

/** Find the appropriate depth-stencil targetable DXGI format for the given format. */
inline DXGI_FORMAT FindDepthStencilDXGIFormat(DXGI_FORMAT InFormat)
{
	switch (InFormat)
	{
	case DXGI_FORMAT_R24G8_TYPELESS:
		return DXGI_FORMAT_D24_UNORM_S8_UINT;
		// Changing Depth Buffers to 32 bit on Dingo as D24S8 is actually implemented as a 32 bit buffer in the hardware
	case DXGI_FORMAT_R32G8X24_TYPELESS:
		return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	case DXGI_FORMAT_R32_TYPELESS:
		return DXGI_FORMAT_D32_FLOAT;
	case DXGI_FORMAT_R16_TYPELESS:
		return DXGI_FORMAT_D16_UNORM;
	};
	return InFormat;
}

/**
* Returns whether the given format contains stencil information.
* Must be passed a format returned by FindDepthStencilDXGIFormat, so that typeless versions are converted to their corresponding depth stencil view format.
*/
inline bool HasStencilBits(DXGI_FORMAT InFormat)
{
	switch (InFormat)
	{
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
		// Changing Depth Buffers to 32 bit on Dingo as D24S8 is actually implemented as a 32 bit buffer in the hardware
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		return true;
	};

	return false;
}

static void TranslateRenderTargetFormats(
	const FGraphicsPipelineStateInitializer &PsoInit,
	D3D12_RT_FORMAT_ARRAY& RTFormatArray,
	DXGI_FORMAT& DSVFormat)
{
	RTFormatArray.NumRenderTargets = PsoInit.ComputeNumValidRenderTargets();

	for (uint32 RTIdx = 0; RTIdx < PsoInit.RenderTargetsEnabled; ++RTIdx)
	{
		checkSlow(PsoInit.RenderTargetFormats[RTIdx] == PF_Unknown || GPixelFormats[PsoInit.RenderTargetFormats[RTIdx]].Supported);

		DXGI_FORMAT PlatformFormat = (DXGI_FORMAT)GPixelFormats[PsoInit.RenderTargetFormats[RTIdx]].PlatformFormat;
		ETextureCreateFlags Flags = PsoInit.RenderTargetFlags[RTIdx];

		RTFormatArray.RTFormats[RTIdx] = D3D12RHI::FindShaderResourceDXGIFormat( GetPlatformTextureResourceFormat(PlatformFormat, Flags), EnumHasAnyFlags(Flags, TexCreate_SRGB) );
	}

	checkSlow(PsoInit.DepthStencilTargetFormat == PF_Unknown || GPixelFormats[PsoInit.DepthStencilTargetFormat].Supported);

	DXGI_FORMAT PlatformFormat = (DXGI_FORMAT)GPixelFormats[PsoInit.DepthStencilTargetFormat].PlatformFormat;

	DSVFormat = D3D12RHI::FindDepthStencilDXGIFormat( GetPlatformTextureResourceFormat(PlatformFormat, PsoInit.DepthStencilTargetFlag) );
}

} // namespace D3D12RHI

// Returns the given format as a string. Unsupported formats are treated as DXGI_FORMAT_UNKNOWN.
const TCHAR* LexToString(DXGI_FORMAT Format);

#if (PLATFORM_WINDOWS || PLATFORM_HOLOLENS)

#ifndef DXGI_PRESENT_ALLOW_TEARING
#define DXGI_PRESENT_ALLOW_TEARING          0x00000200UL
#define DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING  2048

#endif



#define EMBED_DXGI_ERROR_LIST(PerEntry, Terminator)	\
	PerEntry(DXGI_ERROR_UNSUPPORTED) Terminator \
	PerEntry(DXGI_ERROR_NOT_CURRENT) Terminator \
	PerEntry(DXGI_ERROR_MORE_DATA) Terminator \
	PerEntry(DXGI_ERROR_MODE_CHANGE_IN_PROGRESS) Terminator \
	PerEntry(DXGI_ERROR_ALREADY_EXISTS) Terminator \
	PerEntry(DXGI_ERROR_SESSION_DISCONNECTED) Terminator \
	PerEntry(DXGI_ERROR_ACCESS_DENIED) Terminator \
	PerEntry(DXGI_ERROR_NON_COMPOSITED_UI) Terminator \
	PerEntry(DXGI_ERROR_CACHE_FULL) Terminator \
	PerEntry(DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) Terminator \
	PerEntry(DXGI_ERROR_CACHE_CORRUPT) Terminator \
	PerEntry(DXGI_ERROR_WAIT_TIMEOUT) Terminator \
	PerEntry(DXGI_ERROR_FRAME_STATISTICS_DISJOINT) Terminator \
	PerEntry(DXGI_ERROR_DYNAMIC_CODE_POLICY_VIOLATION) Terminator \
	PerEntry(DXGI_ERROR_REMOTE_OUTOFMEMORY) Terminator \
	PerEntry(DXGI_ERROR_ACCESS_LOST) Terminator



#endif //(PLATFORM_WINDOWS || PLATFORM_HOLOLENS)
