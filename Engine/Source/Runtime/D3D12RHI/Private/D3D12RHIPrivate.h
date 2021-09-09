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
#include "RHI.h"
#include "GPUProfiler.h"
#include "ShaderCore.h"

DECLARE_LOG_CATEGORY_EXTERN(LogD3D12RHI, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogD3D12GapRecorder, Log, All);

#include "D3D12RHI.h"
#include "D3D12RHICommon.h"

#if PLATFORM_WINDOWS
#include "Windows/D3D12RHIBasePrivate.h"
#elif PLATFORM_HOLOLENS
#include "HoloLens/D3D12RHIBasePrivate.h"
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
#include "D3D12TimedIntervalQuery.h"
#include "D3D12DescriptorCache.h"
#include "D3D12StateCachePrivate.h"
typedef FD3D12StateCacheBase FD3D12StateCache;
#include "D3D12Allocation.h"
#include "D3D12CommandContext.h"
#include "D3D12Stats.h"
#include "D3D12Device.h"
#include "D3D12Adapter.h"

// Definitions.
#define USE_D3D12RHI_RESOURCE_STATE_TRACKING 1	// Fully relying on the engine's resource barriers is a work in progress. For now, continue to use the D3D12 RHI's resource state tracking.

#define EXECUTE_DEBUG_COMMAND_LISTS 0
#define ENABLE_PLACED_RESOURCES 0 // Disabled due to a couple of NVidia bugs related to placed resources. Works fine on Intel
#define NAME_OBJECTS !(UE_BUILD_SHIPPING || UE_BUILD_TEST)	// Name objects in all builds except shipping
#define LOG_PSO_CREATES (0 && STATS)	// Logs Create Pipeline State timings (also requires STATS)

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

#define DEFAULT_CONTEXT_UPLOAD_POOL_SIZE (8 * 1024 * 1024)
#define DEFAULT_CONTEXT_UPLOAD_POOL_MAX_ALLOC_SIZE (4 * 1024 * 1024)
#define DEFAULT_CONTEXT_UPLOAD_POOL_ALIGNMENT (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
#define TEXTURE_POOL_SIZE (8 * 1024 * 1024)

#define MAX_GPU_BREADCRUMB_DEPTH 1024

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

struct FD3D12UpdateTexture3DData
{
	FD3D12ResourceLocation* UploadHeapResourceLocation;
	bool bComputeShaderCopy;
};

/** Forward declare the context for the AMD AGS utility library. */
struct AGSContext;

PRAGMA_DISABLE_DEPRECATION_WARNINGS

/** The interface which is implemented by the dynamically bound RHI. */
class FD3D12DynamicRHI : public FDynamicRHI
{
	friend class FD3D12CommandContext;

	static FD3D12DynamicRHI* SingleD3DRHI;

public:

	static D3D12RHI_API FD3D12DynamicRHI* GetD3DRHI() { return SingleD3DRHI; }

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

	virtual FD3D12CommandContext* CreateCommandContext(FD3D12Device* InParent, bool InIsDefaultContext, bool InIsAsyncComputeContext);
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
	virtual FHullShaderRHIRef RHICreateHullShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override;
	virtual FDomainShaderRHIRef RHICreateDomainShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override;
	virtual FGeometryShaderRHIRef RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override;
	virtual FComputeShaderRHIRef RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash) override;
	virtual FGPUFenceRHIRef RHICreateGPUFence(const FName& Name) final override;
	virtual FStagingBufferRHIRef RHICreateStagingBuffer() final override;
	virtual void* RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI) final override;
    virtual void RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer) final override;
	virtual FBoundShaderStateRHIRef RHICreateBoundShaderState(FRHIVertexDeclaration* VertexDeclaration, FRHIVertexShader* VertexShader, FRHIHullShader* HullShader, FRHIDomainShader* DomainShader, FRHIPixelShader* PixelShader, FRHIGeometryShader* GeometryShader) final override;
	virtual FGraphicsPipelineStateRHIRef RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer) final override;
	virtual TRefCountPtr<FRHIComputePipelineState> RHICreateComputePipelineState(FRHIComputeShader* ComputeShader) final override;
	virtual void RHICreateTransition(FRHITransition* Transition, ERHIPipeline SrcPipelines, ERHIPipeline DstPipelines, ERHICreateTransitionFlags CreateFlags, TArrayView<const FRHITransitionInfo> Infos) final override;
	virtual void RHIReleaseTransition(FRHITransition* Transition) final override;
	virtual FUniformBufferRHIRef RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation) final override;
	virtual void RHIUpdateUniformBuffer(FRHIUniformBuffer* UniformBufferRHI, const void* Contents) final override;
	virtual FIndexBufferRHIRef RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual void* RHILockIndexBuffer(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer, uint32 Offset, uint32 Size, EResourceLockMode LockMode) final override;
	virtual void RHIUnlockIndexBuffer(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer) final override;
	virtual void RHITransferIndexBufferUnderlyingResource(FRHIIndexBuffer* DestIndexBuffer, FRHIIndexBuffer* SrcIndexBuffer) final override;
	virtual FVertexBufferRHIRef RHICreateVertexBuffer(uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual void* RHILockVertexBuffer(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) final override;
	virtual void RHIUnlockVertexBuffer(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer) final override;
	virtual void RHICopyVertexBuffer(FRHIVertexBuffer* SourceBuffer, FRHIVertexBuffer* DestBuffer) final override;
	virtual void RHITransferVertexBufferUnderlyingResource(FRHIVertexBuffer* DestVertexBuffer, FRHIVertexBuffer* SrcVertexBuffer) final override;
	virtual FStructuredBufferRHIRef RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual void* RHILockStructuredBuffer(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) final override;
	virtual void RHIUnlockStructuredBuffer(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBuffer, uint8 Format) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBuffer, uint8 Format) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBuffer) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(const FShaderResourceViewInitializer& Initializer) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIIndexBuffer* Buffer) final override;
	virtual void RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) final override;
	virtual void RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIIndexBuffer* IndexBuffer) final override;
	virtual uint64 RHICalcTexture2DPlatformSize(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags InFlags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) override;
	virtual uint64 RHICalcTexture3DPlatformSize(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags InFlags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) final override;
	virtual uint64 RHICalcTextureCubePlatformSize(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags InFlags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) final override;
	virtual uint64 RHIGetMinimumAlignmentForBufferBackedSRV(EPixelFormat Format) final override;
	virtual void RHIGetTextureMemoryStats(FTextureMemoryStats& OutStats) final override;
	virtual bool RHIGetTextureMemoryVisualizeData(FColor* TextureData, int32 SizeX, int32 SizeY, int32 Pitch, int32 PixelSize) final override;
	virtual FTextureReferenceRHIRef RHICreateTextureReference(FLastRenderTimeContainer* LastRenderTime) final override;
	virtual FTexture2DRHIRef RHICreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override;
	virtual FTexture2DRHIRef RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, void** InitialMipData, uint32 NumInitialMips) final override;
	virtual void RHICopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D) final override;
	virtual FTexture2DArrayRHIRef RHICreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override;
	virtual FTexture3DRHIRef RHICreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override;
	virtual void RHIGetResourceInfo(FRHITexture* Ref, FRHIResourceInfo& OutInfo) override;
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
	virtual FTextureCubeRHIRef RHICreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override;
	virtual FTextureCubeRHIRef RHICreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override;
	virtual void* RHILockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override;
	virtual void RHIUnlockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail) final override;
	virtual void RHIBindDebugLabelName(FRHITexture* Texture, const TCHAR* Name) final override;
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
	UE_DEPRECATED(4.25, "RHIAliasTextureResources now takes references to FTextureRHIRef objects as parameters")
	virtual void RHIAliasTextureResources(FRHITexture* DestTexture, FRHITexture* SrcTexture) final override;
	UE_DEPRECATED(4.25, "RHICreateAliasedTexture now takes a reference to an FTextureRHIRef object")
	virtual FTextureRHIRef RHICreateAliasedTexture(FRHITexture* SourceTexture) final override;
	virtual void RHIAliasTextureResources(FTextureRHIRef& DestTexture, FTextureRHIRef& SrcTexture) final override;
	virtual FTextureRHIRef RHICreateAliasedTexture(FTextureRHIRef& SourceTexture) final override;
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

#if WITH_MGPU
	virtual IRHICommandContextContainer* RHIGetCommandContextContainer(int32 Index, int32 Num, FRHIGPUMask GPUMask)final override;
#endif

	// FD3D12DynamicRHI interface.
	virtual uint32 GetDebugFlags();
	virtual ID3D12CommandQueue* RHIGetD3DCommandQueue();
	virtual FTexture2DRHIRef RHICreateTexture2DFromResource(EPixelFormat Format, ETextureCreateFlags TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D12Resource* Resource);
	virtual FTexture2DRHIRef RHICreateTexture2DArrayFromResource(EPixelFormat Format, ETextureCreateFlags TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D12Resource* Resource);
	virtual FTextureCubeRHIRef RHICreateTextureCubeFromResource(EPixelFormat Format, ETextureCreateFlags TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D12Resource* Resource);

	//
	// The Following functions are the _RenderThread version of the above functions. They allow the RHI to control the thread synchronization for greater efficiency.
	// These will be un-commented as they are implemented.
	//

	virtual FVertexShaderRHIRef CreateVertexShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreateVertexShader(Code, Hash);
	}

	virtual FGeometryShaderRHIRef CreateGeometryShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreateGeometryShader(Code, Hash);
	}

	virtual FHullShaderRHIRef CreateHullShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreateHullShader(Code, Hash);
	}

	virtual FDomainShaderRHIRef CreateDomainShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreateDomainShader(Code, Hash);
	}

	virtual FPixelShaderRHIRef CreatePixelShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreatePixelShader(Code, Hash);
	}

	virtual FComputeShaderRHIRef CreateComputeShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash) override final
	{
		return RHICreateComputeShader(Code, Hash);
	}

	virtual FVertexBufferRHIRef CreateVertexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override final;
	virtual FStructuredBufferRHIRef CreateStructuredBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override final;
	virtual FVertexBufferRHIRef CreateAndLockVertexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer) override final;
	virtual FIndexBufferRHIRef CreateAndLockIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer) override final;
	// virtual FTexture2DRHIRef AsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus) override final;
	// virtual ETextureReallocationStatus FinalizeAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted) override final;
	// virtual ETextureReallocationStatus CancelAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted) override final;
	virtual FIndexBufferRHIRef CreateIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override final;
	virtual void UpdateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData) override final;
	virtual void* LockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush = true) override final;
	virtual void UnlockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush = true) override final;

	virtual FTexture2DRHIRef AsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus);
	virtual ETextureReallocationStatus FinalizeAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
	{
		return RHIFinalizeAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
	}
	virtual ETextureReallocationStatus CancelAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted)\
	{
		return RHICancelAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
	}

	virtual FTexture2DRHIRef RHICreateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override;
	virtual FTexture2DArrayRHIRef RHICreateTexture2DArray_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override;
	virtual FTexture3DRHIRef RHICreateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer) override final;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel) override final;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint8 Format) override final;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo) override final;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) override final;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer) override final;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceViewWriteMask_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D) override final;

	virtual FShaderResourceViewRHIRef CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) override final;
	virtual FShaderResourceViewRHIRef CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* Buffer) override final;
	virtual FShaderResourceViewRHIRef CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer) override final;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer) override final;
	virtual FTextureCubeRHIRef RHICreateTextureCube_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override;
	virtual FTextureCubeRHIRef RHICreateTextureCubeArray_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) override;

	virtual FRenderQueryRHIRef RHICreateRenderQuery_RenderThread(class FRHICommandListImmediate& RHICmdList, ERenderQueryType QueryType) override final
	{
		return RHICreateRenderQuery(QueryType);
	}

	virtual void RHICopySubTextureRegion(FRHITexture2D* SourceTexture, FRHITexture2D* DestinationTexture, FBox2D SourceBox, FBox2D DestinationBox) final override;

	void RHICalibrateTimers() override;

#if D3D12_RHI_RAYTRACING

	virtual FRayTracingGeometryRHIRef RHICreateRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer) final override;
	virtual FRayTracingSceneRHIRef RHICreateRayTracingScene(const FRayTracingSceneInitializer& Initializer) final override;
	virtual FRayTracingShaderRHIRef RHICreateRayTracingShader(TArrayView<const uint8> Code, const FSHAHash& Hash, EShaderFrequency ShaderFrequency) final override;
	virtual FRayTracingPipelineStateRHIRef RHICreateRayTracingPipelineState(const FRayTracingPipelineStateInitializer& Initializer) final override;

#endif //D3D12_RHI_RAYTRACING

	bool CheckGpuHeartbeat() const override;

	bool RHIRequiresComputeGenerateMips() const override { return true; };

	bool IsQuadBufferStereoEnabled() const;
	void DisableQuadBufferStereo();

	static int32 GetResourceBarrierBatchSizeLimit();

	void* LockBuffer(FRHICommandListImmediate* RHICmdList, FD3D12Buffer* Buffer, uint32 BufferSize, uint32 BufferUsage, uint32 Offset, uint32 Size, EResourceLockMode LockMode);
	void UnlockBuffer(FRHICommandListImmediate* RHICmdList, FD3D12Buffer* Buffer, uint32 BufferUsage);

	static inline bool ShouldDeferBufferLockOperation(FRHICommandListImmediate* RHICmdList)
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

	void UpdateBuffer(FD3D12Resource* Dest, uint32 DestOffset, FD3D12Resource* Source, uint32 SourceOffset, uint32 NumBytes);

#if UE_BUILD_DEBUG	
	uint32 SubmissionLockStalls;
	uint32 DrawCount;
	uint64 PresentCount;
#endif

	inline void UpdataTextureMemorySize(int64 TextureSizeInKiloBytes) { FPlatformAtomics::InterlockedAdd(&GCurrentTextureMemorySize, TextureSizeInKiloBytes); }

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

	static inline bool IsTransitionNeeded(D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES& After)
	{
		check(Before != D3D12_RESOURCE_STATE_CORRUPT && After != D3D12_RESOURCE_STATE_CORRUPT);
		check(Before != D3D12_RESOURCE_STATE_TBD && After != D3D12_RESOURCE_STATE_TBD);

		// Depth write is actually a suitable for read operations as a "normal" depth buffer.
		if ((Before == D3D12_RESOURCE_STATE_DEPTH_WRITE) && (After == D3D12_RESOURCE_STATE_DEPTH_READ))
		{
			return false;
		}

		// COMMON is an oddball state that doesn't follow the RESOURE_STATE pattern of 
		// having exactly one bit set so we need to special case these
		if (After == D3D12_RESOURCE_STATE_COMMON)
		{
			// The resource state tracking code in FD3D12CommandContext::RHITransitionResources forces all EReadable transitions
			// to go through the COMMON state right now, so we can end up with some COMMON -> COMMON transitions which can be
			// skipped. Once that is fixed or removed, we shouldn't get here anymore if we're already in the COMMON state,
			// so we can simply return true and let the ensure in FD3D12CommandListHandle::AddTransitionBarrier catch bad usage.
			return (Before != D3D12_RESOURCE_STATE_COMMON);
		}

		// We should avoid doing read-to-read state transitions. But when we do, we should avoid turning off already transitioned bits,
		// e.g. VERTEX_BUFFER -> SHADER_RESOURCE is turned into VERTEX_BUFFER -> VERTEX_BUFFER | SHADER_RESOURCE.
		// This reduces the number of resource transitions and ensures automatic states from resource bindings get properly combined.
		D3D12_RESOURCE_STATES Combined = Before | After;
		if ((Combined & (D3D12_RESOURCE_STATE_GENERIC_READ | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)) == Combined)
		{
			After = Combined;
		}

		return Before != After;
	}

	/** Transition a resource's state based on a Render target view */
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12RenderTargetView* pView, D3D12_RESOURCE_STATES after)
	{
#if USE_D3D12RHI_RESOURCE_STATE_TRACKING
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
			TransitionResource(hCommandList, pResource, after, desc.Texture2D.MipSlice);
			break;

		case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
		{
			// Multiple subresources to transition
			TransitionResource(hCommandList, pResource, after, pView->GetViewSubresourceSubset());
			break;
		}

		default:
			check(false);	// Need to update this code to include the view type
			break;
		}
#endif // USE_D3D12RHI_RESOURCE_STATE_TRACKING
	}

	/** Transition a resource's state based on a Depth stencil view's desc flags */
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12DepthStencilView* pView)
	{
#if USE_D3D12RHI_RESOURCE_STATE_TRACKING
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
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_DEPTH_WRITE, pView->GetDepthOnlyViewSubresourceSubset());
		}

		if (bStencilIsWritable)
		{
			TransitionResource(hCommandList, pResource, D3D12_RESOURCE_STATE_DEPTH_WRITE, pView->GetStencilOnlyViewSubresourceSubset());
		}
#endif // USE_D3D12RHI_RESOURCE_STATE_TRACKING
	}

	/** Transition a resource's state based on a Depth stencil view */
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12DepthStencilView* pView, D3D12_RESOURCE_STATES after)
	{
#if USE_D3D12RHI_RESOURCE_STATE_TRACKING
		FD3D12Resource* pResource = pView->GetResource();

		const D3D12_DEPTH_STENCIL_VIEW_DESC &desc = pView->GetDesc();
		switch (desc.ViewDimension)
		{
		case D3D12_DSV_DIMENSION_TEXTURE2D:
		case D3D12_DSV_DIMENSION_TEXTURE2DMS:
			if (pResource->GetPlaneCount() > 1)
			{
				// Multiple subresources to transtion
				TransitionResource(hCommandList, pResource, after, pView->GetViewSubresourceSubset());
				break;
			}
			else
			{
				// Only one subresource to transition
				check(pResource->GetPlaneCount() == 1);
				TransitionResource(hCommandList, pResource, after, desc.Texture2D.MipSlice);
			}
			break;

		case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
		case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
		{
			// Multiple subresources to transtion
			TransitionResource(hCommandList, pResource, after, pView->GetViewSubresourceSubset());
			break;
		}

		default:
			check(false);	// Need to update this code to include the view type
			break;
		}
#endif // USE_D3D12RHI_RESOURCE_STATE_TRACKING
	}

	/** Transition a resource's state based on a Unordered access view */
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12UnorderedAccessView* pView, D3D12_RESOURCE_STATES after)
	{
#if USE_D3D12RHI_RESOURCE_STATE_TRACKING
		FD3D12Resource* pResource = pView->GetResource();

		const D3D12_UNORDERED_ACCESS_VIEW_DESC &desc = pView->GetDesc();
		switch (desc.ViewDimension)
		{
		case D3D12_UAV_DIMENSION_BUFFER:
			TransitionResource(hCommandList, pResource, after, 0);
			break;

		case D3D12_UAV_DIMENSION_TEXTURE2D:
			// Only one subresource to transition
			TransitionResource(hCommandList, pResource, after, desc.Texture2D.MipSlice);
			break;

		case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
		{
			// Multiple subresources to transtion
			TransitionResource(hCommandList, pResource, after, pView->GetViewSubresourceSubset());
			break;
		}
		case D3D12_UAV_DIMENSION_TEXTURE3D:
		{
			// Multiple subresources to transtion
			TransitionResource(hCommandList, pResource, after, pView->GetViewSubresourceSubset());
			break;
		}

		default:
			check(false);	// Need to update this code to include the view type
			break;
		}
#endif // USE_D3D12RHI_RESOURCE_STATE_TRACKING
	}

	/** Transition a resource's state based on a Shader resource view */
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12ShaderResourceView* pView, D3D12_RESOURCE_STATES after)
	{
#if USE_D3D12RHI_RESOURCE_STATE_TRACKING
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
			TransitionResource(hCommandList, pResource, after, subresourceSubset);
			break;
		}

		case D3D12_SRV_DIMENSION_BUFFER:
		{
			if (pResource->GetHeapType() == D3D12_HEAP_TYPE_DEFAULT)
			{
				// Transition the resource
				TransitionResource(hCommandList, pResource, after, subresourceSubset);
			}
			break;
		}
		}
#endif // USE_D3D12RHI_RESOURCE_STATE_TRACKING
	}

	// Transition a specific subresource to the after state.
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12Resource* pResource, D3D12_RESOURCE_STATES after, uint32 subresource)
	{
#if USE_D3D12RHI_RESOURCE_STATE_TRACKING
		TransitionResourceWithTracking(hCommandList, pResource, after, subresource);
#endif // USE_D3D12RHI_RESOURCE_STATE_TRACKING
	}

	// Transition a subset of subresources to the after state.
	static inline void TransitionResource(FD3D12CommandListHandle& hCommandList, FD3D12Resource* pResource, D3D12_RESOURCE_STATES after, const CViewSubresourceSubset& subresourceSubset)
	{
#if USE_D3D12RHI_RESOURCE_STATE_TRACKING
		TransitionResourceWithTracking(hCommandList, pResource, after, subresourceSubset);
#endif // USE_D3D12RHI_RESOURCE_STATE_TRACKING
	}

	// Transition a subresource from current to a new state, using resource state tracking.
	static void TransitionResourceWithTracking(FD3D12CommandListHandle& hCommandList, FD3D12Resource* pResource, D3D12_RESOURCE_STATES after, uint32 subresource)
	{
#if USE_D3D12RHI_RESOURCE_STATE_TRACKING
		check(pResource);
		check(pResource->RequiresResourceStateTracking());

		check(!((after & (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) && (pResource->GetDesc().Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)));

#ifdef PLATFORM_SUPPORTS_RESOURCE_COMPRESSION
		after |= pResource->GetCompressedState();
#endif

		hCommandList.UpdateResidency(pResource);

		CResourceState& ResourceState = hCommandList.GetResourceState(pResource);
		if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES && !ResourceState.AreAllSubresourcesSame())
		{
			// Slow path. Want to transition the entire resource (with multiple subresources). But they aren't in the same state.

			const uint8 SubresourceCount = pResource->GetSubresourceCount();
			for (uint32 SubresourceIndex = 0; SubresourceIndex < SubresourceCount; SubresourceIndex++)
			{
				const D3D12_RESOURCE_STATES before = ResourceState.GetSubresourceState(SubresourceIndex);
				if (before == D3D12_RESOURCE_STATE_TBD)
				{
					// We need a pending resource barrier so we can setup the state before this command list executes
					hCommandList.AddPendingResourceBarrier(pResource, after, SubresourceIndex);
					ResourceState.SetSubresourceState(SubresourceIndex, after);
				}
				// We're not using IsTransitionNeeded() because we do want to transition even if 'after' is a subset of 'before'
				// This is so that we can ensure all subresources are in the same state, simplifying future barriers
				else if (before != after)
				{
					hCommandList.AddTransitionBarrier(pResource, before, after, SubresourceIndex);
					ResourceState.SetSubresourceState(SubresourceIndex, after);
				}
			}

			// The entire resource should now be in the after state on this command list (even if all barriers are pending)
			check(ResourceState.CheckResourceState(after));
			ResourceState.SetResourceState(after);
		}
		else
		{
			const D3D12_RESOURCE_STATES before = ResourceState.GetSubresourceState(subresource);
			if (before == D3D12_RESOURCE_STATE_TBD)
			{
				// We need a pending resource barrier so we can setup the state before this command list executes
				hCommandList.AddPendingResourceBarrier(pResource, after, subresource);
				ResourceState.SetSubresourceState(subresource, after);
			}
			else if (IsTransitionNeeded(before, after))
			{
				hCommandList.AddTransitionBarrier(pResource, before, after, subresource);
				ResourceState.SetSubresourceState(subresource, after);
			}
		}
#endif // USE_D3D12RHI_RESOURCE_STATE_TRACKING
	}

	// Transition subresources from current to a new state, using resource state tracking.
	static void TransitionResourceWithTracking(FD3D12CommandListHandle& hCommandList, FD3D12Resource* pResource, D3D12_RESOURCE_STATES after, const CViewSubresourceSubset& subresourceSubset)
	{
#if USE_D3D12RHI_RESOURCE_STATE_TRACKING
		check(pResource);
		check(pResource->RequiresResourceStateTracking());

		check(!((after & (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) && (pResource->GetDesc().Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)));

#ifdef PLATFORM_SUPPORTS_RESOURCE_COMPRESSION
		after |= pResource->GetCompressedState();
#endif

		hCommandList.UpdateResidency(pResource);
		D3D12_RESOURCE_STATES before;
		const bool bIsWholeResource = subresourceSubset.IsWholeResource();
		CResourceState& ResourceState = hCommandList.GetResourceState(pResource);
		if (bIsWholeResource && ResourceState.AreAllSubresourcesSame())
		{
			// Fast path. Transition the entire resource from one state to another.
			before = ResourceState.GetSubresourceState(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			if (before == D3D12_RESOURCE_STATE_TBD)
			{
				// We need a pending resource barrier so we can setup the state before this command list executes
				hCommandList.AddPendingResourceBarrier(pResource, after, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
				ResourceState.SetResourceState(after);
			}
			else if (IsTransitionNeeded(before, after))
			{
				hCommandList.AddTransitionBarrier(pResource, before, after, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
				ResourceState.SetResourceState(after);
			}
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
					// IsTransitionNeeded can change the after state if it's read-only and the current state already contains other read-only bits. We don't want to propagate
					// those bits to other subresources, so we'll save the original value.
					D3D12_RESOURCE_STATES ActualAfter = after;

					before = ResourceState.GetSubresourceState(SubresourceIndex);
					if (before == D3D12_RESOURCE_STATE_TBD)
					{
						// We need a pending resource barrier so we can setup the state before this command list executes
						hCommandList.AddPendingResourceBarrier(pResource, after, SubresourceIndex);
						ResourceState.SetSubresourceState(SubresourceIndex, after);
					}
					else if (IsTransitionNeeded(before, ActualAfter))
					{
						hCommandList.AddTransitionBarrier(pResource, before, ActualAfter, SubresourceIndex);
						ResourceState.SetSubresourceState(SubresourceIndex, ActualAfter);
						// If IsTransitionNeeded() changed the destination state, this subresource will be in a different state compared to the previous subresources,
						// so bWholeResourceWasTransitionedToSameState cannot be true.
						if (ActualAfter != after)
						{
							bWholeResourceWasTransitionedToSameState = false;
						}
					}
					else
					{
						// Didn't need to transition the subresource.
						if (before != after)
						{
							bWholeResourceWasTransitionedToSameState = false;
						}
					}
				}
			}

			// If we just transtioned every subresource to the same state, lets update it's tracking so it's on a per-resource level
			if (bWholeResourceWasTransitionedToSameState)
			{
				// Sanity check to make sure all subresources are really in the 'after' state
				check(ResourceState.CheckResourceState(after));

				ResourceState.SetResourceState(after);
			}
		}
#endif // USE_D3D12RHI_RESOURCE_STATE_TRACKING
	}

public:

#if	PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
	virtual void* CreateVirtualTexture(ETextureCreateFlags InFlags, D3D12_RESOURCE_DESC& ResourceDesc, const struct FD3D12TextureLayout& TextureLayout, FD3D12Resource** ppResource, FPlatformMemory::FPlatformVirtualMemoryBlock& RawTextureBlock, D3D12_RESOURCE_STATES InitialUsage = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) = 0;
	virtual void DestroyVirtualTexture(ETextureCreateFlags InFlags, void* RawTextureMemory, FPlatformMemory::FPlatformVirtualMemoryBlock& RawTextureBlock, uint64 CommittedTextureSize) = 0;
#endif
	virtual bool HandleSpecialLock(void*& MemoryOut, uint32 MipIndex, uint32 ArrayIndex, uint32 InFlags, EResourceLockMode LockMode, const FD3D12TextureLayout& TextureLayout, void* RawTextureMemory, uint32& DestStride) { return false; }
	virtual bool HandleSpecialUnlock(FRHICommandListBase* RHICmdList, uint32 MipIndex, uint32 InFlags, const struct FD3D12TextureLayout& TextureLayout, void* RawTextureMemory) { return false; }

	FD3D12Adapter& GetAdapter(uint32_t Index = 0) { return *ChosenAdapters[Index]; }
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

protected:

	TArray<TSharedPtr<FD3D12Adapter>> ChosenAdapters;

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

	/** A buffer in system memory containing all zeroes of the specified size. */
	void* ZeroBuffer;
	uint32 ZeroBufferSize;

	template<typename BaseResourceType>
	TD3D12Texture2D<BaseResourceType>* CreateD3D12Texture2D(class FRHICommandListImmediate* RHICmdList, uint32 SizeX, uint32 SizeY, uint32 SizeZ, bool bTextureArray, bool CubeTexture, EPixelFormat Format,
		uint32 NumMips, uint32 NumSamples, ETextureCreateFlags InFlags, FRHIResourceCreateInfo& CreateInfo);

	FD3D12Texture3D* CreateD3D12Texture3D(class FRHICommandListImmediate* RHICmdList, uint32 SizeX, uint32 SizeY, uint32 SizeZ, EPixelFormat Format, uint32 NumMips, ETextureCreateFlags InFlags, FRHIResourceCreateInfo& CreateInfo);

	template<typename BaseResourceType>
	TD3D12Texture2D<BaseResourceType>* CreateTextureFromResource(bool bTextureArray, bool bCubeTexture, EPixelFormat Format, ETextureCreateFlags TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D12Resource* Resource);

	template<typename BaseResourceType>
	TD3D12Texture2D<BaseResourceType>* CreateAliasedD3D12Texture2D(TD3D12Texture2D<BaseResourceType>* SourceTexture);

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
	inline FD3D12Device* GetRHIDevice(uint32 GPUIndex)
	{
		return GetAdapter().GetDevice(GPUIndex);
	}

	HANDLE FlipEvent;

	const bool bAllowVendorDevice;
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
	virtual bool IsSupported() override;
	virtual FDynamicRHI* CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel = ERHIFeatureLevel::Num) override;

private:

#if USE_PIX && (PLATFORM_WINDOWS || PLATFORM_HOLOLENS)
	void* WindowsPixDllHandle = nullptr;
#endif // USE_PIX && (PLATFORM_WINDOWS || PLATFORM_HOLOLENS)

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
*	Class of a scoped resource barrier.
*	This class conditionally uses resource state tracking.
*	This should only be used with the Editor.
*/
class FConditionalScopeResourceBarrier
{
private:
	FD3D12CommandListHandle& hCommandList;
	FD3D12Resource* const pResource;
	D3D12_RESOURCE_STATES Current;
	const D3D12_RESOURCE_STATES Desired;
	const uint32 Subresource;
	bool bRestoreDefaultState;

public:
	explicit FConditionalScopeResourceBarrier(FD3D12CommandListHandle& hInCommandList, FD3D12Resource* pInResource, const D3D12_RESOURCE_STATES InDesired, const uint32 InSubresource)
		: hCommandList(hInCommandList)
		, pResource(pInResource)
		, Current(D3D12_RESOURCE_STATE_TBD)
		, Desired(InDesired)
		, Subresource(InSubresource)
		, bRestoreDefaultState(false)
	{
		// when we don't use resource state tracking, transition the resource (only if necessary)
		if (!pResource->RequiresResourceStateTracking())
		{
			Current = pResource->GetDefaultResourceState();
			if (Current != Desired)
			{
				// we will add a transition, we need to transition back to the default state when the scoped object dies : 
				bRestoreDefaultState = true;
				hCommandList.AddTransitionBarrier(pResource, Current, Desired, Subresource);
			}
		}
		else
		{
			FD3D12DynamicRHI::TransitionResource(hCommandList, pResource, Desired, Subresource);
		}
	}

	~FConditionalScopeResourceBarrier()
	{
		// Return the resource to it's default state if necessary : 
		if (bRestoreDefaultState)
		{
			hCommandList.AddTransitionBarrier(pResource, Desired, Current, Subresource);
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
