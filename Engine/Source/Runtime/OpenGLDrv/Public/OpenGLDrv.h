// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLDrv.h: Public OpenGL RHI definitions.
=============================================================================*/

#pragma once

// Dependencies.

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "GPUProfiler.h"
#include "RenderResource.h"
#include "Templates/EnableIf.h"

// @todo platplug: Replace all of these includes with a call to COMPILED_PLATFORM_HEADER(OpenGLDrvPrivate.h)
//TODO: Move these to OpenGLDrvPrivate.h
#if PLATFORM_WINDOWS
	#include "Runtime/OpenGLDrv/Private/Windows/OpenGLWindows.h"
#elif PLATFORM_LINUX
	#include "Runtime/OpenGLDrv/Private/Linux/OpenGLLinux.h"
#elif PLATFORM_LUMIN
	// these guys will self-select
	#include "Lumin/LuminESDeferredOpenGL.h"
	#include "Lumin/LuminOpenGL.h"
	#include "Lumin/LuminGL4.h"
#elif PLATFORM_ANDROIDESDEFERRED
	#include "Android/AndroidESDeferredOpenGL.h"
#elif PLATFORM_ANDROID
	#include "Android/AndroidOpenGL.h"
#else
#include COMPILED_PLATFORM_HEADER(OpenGLDrvPrivate.h)
#endif

// Define here so don't have to do platform filtering
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

#define OPENGL_USE_BINDABLE_UNIFORMS 0
#define OPENGL_USE_BLIT_FOR_BACK_BUFFER 1

// OpenGL RHI public headers.
#include "OpenGLUtil.h"
#include "OpenGLState.h"
#include "RenderUtils.h"

#define FOpenGLCachedUniformBuffer_Invalid 0xFFFFFFFF

class FOpenGLDynamicRHI;
class FResourceBulkDataInterface;
struct Rect;

template<class T> struct TOpenGLResourceTraits;

// This class has multiple inheritance but really FGPUTiming is a static class
class FOpenGLBufferedGPUTiming : public FGPUTiming
{
public:

	/**
	 * Constructor.
	 *
	 * @param InOpenGLRHI			RHI interface
	 * @param InBufferSize		Number of buffered measurements
	 */
	FOpenGLBufferedGPUTiming(class FOpenGLDynamicRHI* InOpenGLRHI, int32 BufferSize);

	void	StartTiming();

	/**
	 * End a GPU timing measurement.
	 * The timing for this particular measurement will be resolved at a later time by the GPU.
	 */
	void	EndTiming();

	/**
	 * Retrieves the most recently resolved timing measurement.
	 * The unit is the same as for FPlatformTime::Cycles(). Returns 0 if there are no resolved measurements.
	 *
	 * @return	Value of the most recently resolved timing, or 0 if no measurements have been resolved by the GPU yet.
	 */
	uint64	GetTiming(bool bGetCurrentResultsAndBlock = false);

	void InitResources();
	void ReleaseResources();


private:

	/**
	 * Initializes the static variables, if necessary.
	 */
	static void PlatformStaticInitialize(void* UserData);

	/** RHI interface */
	FOpenGLDynamicRHI*					OpenGLRHI;
	/** Number of timestamps created in 'StartTimestamps' and 'EndTimestamps'. */
	int32								BufferSize;
	/** Current timing being measured on the CPU. */
	int32								CurrentTimestamp;
	/** Number of measurements in the buffers (0 - BufferSize). */
	int32								NumIssuedTimestamps;
	/** Timestamps for all StartTimings. */
	TArray<FOpenGLRenderQuery *>		StartTimestamps;
	/** Timestamps for all EndTimings. */
	TArray<FOpenGLRenderQuery *>		EndTimestamps;
	/** Whether we are currently timing the GPU: between StartTiming() and EndTiming(). */
	bool								bIsTiming;
};

/**
  * Used to track whether a period was disjoint on the GPU, which means GPU timings are invalid.
  * OpenGL lacks this concept at present, so the class is just a placeholder
  * Timings are all assumed to be non-disjoint
  */
class FOpenGLDisjointTimeStampQuery
{
public:
	FOpenGLDisjointTimeStampQuery(class FOpenGLDynamicRHI* InOpenGLRHI=NULL);

	void Init(class FOpenGLDynamicRHI* InOpenGLRHI)
	{
		OpenGLRHI = InOpenGLRHI;
		InitResources();
	}

	void StartTracking();
	void EndTracking();
	bool IsResultValid();
	bool GetResult(uint64* OutResult=NULL);
	static uint64 GetTimingFrequency()
	{
		return 1000000000ull;
	}
	static bool IsSupported()
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		return FOpenGL::SupportsDisjointTimeQueries();
#endif
	}

	void InitResources();
	void ReleaseResources();


private:
	bool	bIsResultValid;
	GLuint	DisjointQuery;
	uint64	Context;

	FOpenGLDynamicRHI* OpenGLRHI;
};

/** A single perf event node, which tracks information about a appBeginDrawEvent/appEndDrawEvent range. */
class FOpenGLEventNode : public FGPUProfilerEventNode
{
public:

	FOpenGLEventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent, class FOpenGLDynamicRHI* InRHI)
	:	FGPUProfilerEventNode(InName, InParent)
	,	Timing(InRHI, 1)
	{
		// Initialize Buffered timestamp queries 
		Timing.InitResources();
	}

	virtual ~FOpenGLEventNode()
	{
		Timing.ReleaseResources();
	}

	/** 
	 * Returns the time in ms that the GPU spent in this draw event.  
	 * This blocks the CPU if necessary, so can cause hitching.
	 */
	float GetTiming() override;

	virtual void StartTiming() override
	{
		Timing.StartTiming();
	}

	virtual void StopTiming() override
	{
		Timing.EndTiming();
	}

	FOpenGLBufferedGPUTiming Timing;
};

/** An entire frame of perf event nodes, including ancillary timers. */
class FOpenGLEventNodeFrame : public FGPUProfilerEventNodeFrame
{
public:
	FOpenGLEventNodeFrame(class FOpenGLDynamicRHI* InRHI) :
		FGPUProfilerEventNodeFrame(),
		RootEventTiming(InRHI, 1),
		DisjointQuery(InRHI)
	{
	  RootEventTiming.InitResources();
	  DisjointQuery.InitResources();
	}

	~FOpenGLEventNodeFrame()
	{

		RootEventTiming.ReleaseResources();
		DisjointQuery.ReleaseResources();
	}

	/** Start this frame of per tracking */
	void StartFrame() override;

	/** End this frame of per tracking, but do not block yet */
	void EndFrame() override;

	/** Calculates root timing base frequency (if needed by this RHI) */
	virtual float GetRootTimingResults() override;

	virtual void LogDisjointQuery() override;

	/** Timer tracking inclusive time spent in the root nodes. */
	FOpenGLBufferedGPUTiming RootEventTiming;

	/** Disjoint query tracking whether the times reported by DumpEventTree are reliable. */
	FOpenGLDisjointTimeStampQuery DisjointQuery;
};

/** 
 * Encapsulates GPU profiling logic and data. 
 * There's only one global instance of this struct so it should only contain global data, nothing specific to a frame.
 */
struct FOpenGLGPUProfiler : public FGPUProfiler
{
	/** Used to measure GPU time per frame. */
	FOpenGLBufferedGPUTiming FrameTiming;

	/** Measuring GPU frame time with a disjoint query. */
	static const int MAX_GPUFRAMEQUERIES = 4;
	FOpenGLDisjointTimeStampQuery DisjointGPUFrameTimeQuery[MAX_GPUFRAMEQUERIES];
	int CurrentGPUFrameQueryIndex;

	class FOpenGLDynamicRHI* OpenGLRHI;
	// count the number of beginframe calls without matching endframe calls.
	int32 NestedFrameCount;
	bool bIntialized;

	uint32 ExternalGPUTime;

	/** GPU hitch profile histories */
	TIndirectArray<FOpenGLEventNodeFrame> GPUHitchEventNodeFrames;

	FOpenGLGPUProfiler(class FOpenGLDynamicRHI* InOpenGLRHI)
	:	FGPUProfiler()
	,	FrameTiming(InOpenGLRHI, 4)
	,	CurrentGPUFrameQueryIndex(0)
	,	OpenGLRHI(InOpenGLRHI)
	,	NestedFrameCount(0)
	,	bIntialized(false)
	,	ExternalGPUTime(0)
	{
	}

	void InitResources()
	{
		FrameTiming.InitResources();
		for (int32 Index = 0; Index < MAX_GPUFRAMEQUERIES; ++Index)
		{
			DisjointGPUFrameTimeQuery[Index].Init(OpenGLRHI);
		}
	}

	virtual FGPUProfilerEventNode* CreateEventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent) override
	{
		FOpenGLEventNode* EventNode = new FOpenGLEventNode(InName, InParent, OpenGLRHI);
		return EventNode;
	}

	void Cleanup();

	virtual void PushEvent(const TCHAR* Name, FColor Color) override;
	virtual void PopEvent() override;

	void BeginFrame(class FOpenGLDynamicRHI* InRHI);
	void EndFrame();
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS

/** The interface which is implemented by the dynamically bound RHI. */
class OPENGLDRV_API FOpenGLDynamicRHI  final : public FDynamicRHI, public IRHICommandContextPSOFallback
{
public:

	friend class FOpenGLViewport;
#if PLATFORM_ANDROIDESDEFERRED // Flithy hack to workaround radr://16011763
	friend class FOpenGLTextureBase;
#endif

	/** Initialization constructor. */
	FOpenGLDynamicRHI();

	/** Destructor */
	~FOpenGLDynamicRHI() {}

	// FDynamicRHI interface.
	virtual void Init();
	virtual void PostInit();

	virtual void Shutdown();
	virtual const TCHAR* GetName() override { return TEXT("OpenGL"); }

	// If using a Proxy object return the contained GL object rather than the proxy itself.
	template<typename TRHIType>
	static FORCEINLINE typename TEnableIf<TIsGLProxyObject<typename TOpenGLResourceTraits<TRHIType>::TConcreteType>::Value, typename TOpenGLResourceTraits<TRHIType>::TConcreteType::ContainedGLType*>::Type ResourceCast(TRHIType* Resource)
	{	
		auto GLProxy = static_cast<typename TOpenGLResourceTraits<TRHIType>::TConcreteType*>(Resource);
		// rhi resource can be null.
		return GLProxy ? GLProxy->GetGLResourceObject() : nullptr;
	}

	template<typename TRHIType>
	static FORCEINLINE typename TEnableIf<!TIsGLProxyObject<typename TOpenGLResourceTraits<TRHIType>::TConcreteType>::Value, typename TOpenGLResourceTraits<TRHIType>::TConcreteType*>::Type ResourceCast(TRHIType* Resource)
	{
		CheckRHITFence(static_cast<typename TOpenGLResourceTraits<TRHIType>::TConcreteType*>(Resource));
		return static_cast<typename TOpenGLResourceTraits<TRHIType>::TConcreteType*>(Resource);
	}

	template<typename TRHIType>
	static FORCEINLINE typename TEnableIf<!TIsGLProxyObject<typename TOpenGLResourceTraits<TRHIType>::TConcreteType>::Value, typename TOpenGLResourceTraits<TRHIType>::TConcreteType*>::Type ResourceCast_Unfenced(TRHIType* Resource)
	{
		return static_cast<typename TOpenGLResourceTraits<TRHIType>::TConcreteType*>(Resource);
	}

	virtual FRasterizerStateRHIRef RHICreateRasterizerState(const FRasterizerStateInitializerRHI& Initializer) final override;
	virtual FDepthStencilStateRHIRef RHICreateDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer) final override;
	virtual FBlendStateRHIRef RHICreateBlendState(const FBlendStateInitializerRHI& Initializer) final override;
	virtual FVertexDeclarationRHIRef RHICreateVertexDeclaration(const FVertexDeclarationElementList& Elements) final override;

	virtual FPixelShaderRHIRef RHICreatePixelShader(const TArray<uint8>& Code) final override;
	virtual FVertexShaderRHIRef RHICreateVertexShader(const TArray<uint8>& Code) final override;
	virtual FHullShaderRHIRef RHICreateHullShader(const TArray<uint8>& Code) final override;
	virtual FDomainShaderRHIRef RHICreateDomainShader(const TArray<uint8>& Code) final override;
	virtual FGeometryShaderRHIRef RHICreateGeometryShader(const TArray<uint8>& Code) final override;
	virtual FComputeShaderRHIRef RHICreateComputeShader(const TArray<uint8>& Code) final override;

	virtual FPixelShaderRHIRef RHICreatePixelShader(FRHIShaderLibrary* Library, FSHAHash Hash) final override;
	virtual FVertexShaderRHIRef RHICreateVertexShader(FRHIShaderLibrary* Library, FSHAHash Hash) final override;
	virtual FHullShaderRHIRef RHICreateHullShader(FRHIShaderLibrary* Library, FSHAHash Hash) final override;
	virtual FDomainShaderRHIRef RHICreateDomainShader(FRHIShaderLibrary* Library, FSHAHash Hash) final override;
	virtual FGeometryShaderRHIRef RHICreateGeometryShader(FRHIShaderLibrary* Library, FSHAHash Hash) final override;
	virtual FComputeShaderRHIRef RHICreateComputeShader(FRHIShaderLibrary* Library, FSHAHash Hash) final override;

	virtual FUniformBufferRHIRef RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation) final override;
	virtual void RHIUpdateUniformBuffer(FRHIUniformBuffer* UniformBufferRHI, const void* Contents) final override;
	virtual FIndexBufferRHIRef RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual void* LockIndexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer, uint32 Offset, uint32 Size, EResourceLockMode LockMode) final override;
	virtual void UnlockIndexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer) final override;
	virtual FVertexBufferRHIRef RHICreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual void* LockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) final override;
	virtual void UnlockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer) final override;
	virtual void RHICopyVertexBuffer(FRHIVertexBuffer* SourceBuffer, FRHIVertexBuffer* DestBuffer) final override;
	virtual FStructuredBufferRHIRef RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual void* LockStructuredBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) final override;
	virtual void UnlockStructuredBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBuffer, uint8 Format) final override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBuffer, uint8 Format) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBuffer) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIIndexBuffer* Buffer) final override;
	virtual void RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) final override;
	virtual uint64 RHICalcTexture2DPlatformSize(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) final override;
	virtual uint64 RHICalcTexture3DPlatformSize(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) final override;
	virtual uint64 RHICalcTextureCubePlatformSize(uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) final override;
	virtual void RHIGetTextureMemoryStats(FTextureMemoryStats& OutStats) final override;
	virtual bool RHIGetTextureMemoryVisualizeData(FColor* TextureData,int32 SizeX,int32 SizeY,int32 Pitch,int32 PixelSize) final override;
	virtual FTextureReferenceRHIRef RHICreateTextureReference(FLastRenderTimeContainer* LastRenderTime) final override;
	virtual void RHIUpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture) final override;
	virtual FTexture2DRHIRef RHICreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual FTexture2DRHIRef RHICreateTextureExternal2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual FTexture2DRHIRef RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 Flags, void** InitialMipData, uint32 NumInitialMips) final override;
	virtual void RHICopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D) final override;
	virtual FTexture2DArrayRHIRef RHICreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual FTexture3DRHIRef RHICreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual void RHIGetResourceInfo(FRHITexture* Ref, FRHIResourceInfo& OutInfo) final override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo) final override;
	virtual void RHIGenerateMips(FRHITexture* Texture) final override;
	virtual uint32 RHIComputeMemorySize(FRHITexture* TextureRHI) final override;
	virtual FTexture2DRHIRef RHIAsyncReallocateTexture2D(FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus) final override;
	virtual ETextureReallocationStatus RHIFinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted) final override;
	virtual ETextureReallocationStatus RHICancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted) final override;
	virtual void* RHILockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override;
	virtual void RHIUnlockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail) final override;
	virtual void* RHILockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override;
	virtual void RHIUnlockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, bool bLockWithinMiptail) final override;
	virtual void RHIUpdateTexture2D(FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData) final override;
	virtual void RHIUpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData) final override;
	virtual FTextureCubeRHIRef RHICreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual FTextureCubeRHIRef RHICreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override;
	virtual void* RHILockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override;
	virtual void RHIUnlockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail) final override;
	virtual void RHIBindDebugLabelName(FRHITexture* Texture, const TCHAR* Name) final override;
	virtual void RHIReadSurfaceData(FRHITexture* Texture,FIntRect Rect,TArray<FColor>& OutData,FReadSurfaceDataFlags InFlags) final override;
	virtual void RHIReadSurfaceData(FRHITexture* Texture, FIntRect Rect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags) final override;
	virtual void RHIMapStagingSurface(FRHITexture* Texture, FRHIGPUFence* Fence, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex = 0) final override;
	virtual void RHIUnmapStagingSurface(FRHITexture* Texture, uint32 GPUIndex = 0) final override;
	virtual void RHIReadSurfaceFloatData(FRHITexture* Texture,FIntRect Rect,TArray<FFloat16Color>& OutData,ECubeFace CubeFace,int32 ArrayIndex,int32 MipIndex) final override;
	virtual void RHIRead3DSurfaceFloatData(FRHITexture* Texture,FIntRect Rect,FIntPoint ZMinMax,TArray<FFloat16Color>& OutData) final override;
	virtual FRenderQueryRHIRef RHICreateRenderQuery(ERenderQueryType QueryType) final override;
	virtual bool RHIGetRenderQueryResult(FRHIRenderQuery* RenderQuery, uint64& OutResult, bool bWait, uint32 GPUIndex = INDEX_NONE) final override;
	virtual FTexture2DRHIRef RHIGetViewportBackBuffer(FRHIViewport* Viewport) final override;
	UE_DEPRECATED(4.25, "RHIAliasTextureResources now takes references to FTextureRHIRef objects as parameters")
	virtual void RHIAliasTextureResources(FRHITexture* DestTexture, FRHITexture* SrcTexture) final override;
	UE_DEPRECATED(4.25, "RHICreateAliasedTexture now takes a reference to an FTextureRHIRef object")
	virtual FTextureRHIRef RHICreateAliasedTexture(FRHITexture* SourceTexture) final override;
	virtual void RHIAdvanceFrameForGetViewportBackBuffer(FRHIViewport* Viewport) final override;
	virtual void RHIAcquireThreadOwnership() final override;
	virtual void RHIReleaseThreadOwnership() final override;
	virtual void RHIFlushResources() final override;
	virtual uint32 RHIGetGPUFrameCycles(uint32 GPUIndex = 0) final override;
	virtual FViewportRHIRef RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat) final override;
	virtual void RHIResizeViewport(FRHIViewport* Viewport, uint32 SizeX, uint32 SizeY, bool bIsFullscreen) final override;
	virtual EPixelFormat RHIPreferredPixelFormatHint(EPixelFormat PreferredPixelFormat) final override;
	virtual void RHITick(float DeltaTime) final override;
	virtual void RHIBlockUntilGPUIdle() final override;
	virtual void RHISubmitCommandsAndFlushGPU() final override;
	virtual void RHIPollOcclusionQueries() final override;
	virtual bool RHIGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate) final override;
	virtual void RHIGetSupportedResolution(uint32& Width, uint32& Height) final override;
	virtual void RHIVirtualTextureSetFirstMipInMemory(FRHITexture2D* Texture, uint32 FirstMip) final override;
	virtual void RHIVirtualTextureSetFirstMipVisible(FRHITexture2D* Texture, uint32 FirstMip) final override;
	virtual void RHIExecuteCommandList(FRHICommandList* CmdList) final override;
	virtual void* RHIGetNativeDevice() final override;
	virtual void* RHIGetNativeInstance() final override;
	virtual class IRHICommandContext* RHIGetDefaultContext() final override;
	virtual class IRHICommandContextContainer* RHIGetCommandContextContainer(int32 Index, int32 Num) final override;

	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName) final override;
	virtual void RHIEndRenderPass() final override;
	virtual void RHINextSubpass() final override;
	
	virtual void RHISetComputeShader(FRHIComputeShader* ComputeShader) final override;
	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) final override;
	virtual void RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHIAutomaticCacheFlushAfterComputeShader(bool bEnable) final override;
	virtual void RHIFlushComputeShaderCache() final override;
	virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) final override;
	virtual void RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values) final override;
	virtual void RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values) final override;
	virtual void RHICopyToResolveTarget(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FResolveParams& ResolveParams) final override;
	virtual void RHIBeginRenderQuery(FRHIRenderQuery* RenderQuery) final override;
	virtual void RHIEndRenderQuery(FRHIRenderQuery* RenderQuery) final override;
	virtual void RHISubmitCommandsHint() final override;
	virtual void RHIBeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI) final override;
	virtual void RHIEndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync) final override;
	virtual void RHIBeginFrame() final override;
	virtual void RHIEndFrame() final override;
	virtual void RHIBeginScene() final override;
	virtual void RHIEndScene() final override;
	virtual void RHISetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBuffer, uint32 Offset) final override;
	virtual void RHISetRasterizerState(FRHIRasterizerState* NewState) final override;
	virtual void RHISetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ) final override;
	virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) final override;
	virtual void RHISetBoundShaderState(FRHIBoundShaderState* BoundShaderState) final override;
	virtual void RHISetShaderTexture(FRHIGraphicsShader* Shader, uint32 TextureIndex, FRHITexture* NewTexture) final override;
	virtual void RHISetShaderTexture(FRHIComputeShader* PixelShader, uint32 TextureIndex, FRHITexture* NewTexture) final override;
	virtual void RHISetShaderSampler(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHISamplerState* NewState) final override;
	virtual void RHISetShaderSampler(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHISamplerState* NewState) final override;
	virtual void RHISetUAVParameter(FRHIPixelShader* PixelShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV) final override;
	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV) final override;
	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount) final override;
	virtual void RHISetShaderResourceViewParameter(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override;
	virtual void RHISetGlobalUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers) final override;
	virtual void RHISetShaderUniformBuffer(FRHIGraphicsShader* Shader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override;
	virtual void RHISetShaderParameter(FRHIGraphicsShader* Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FRHIComputeShader* ComputeShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetDepthStencilState(FRHIDepthStencilState* NewState, uint32 StencilRef) final override;
	virtual void RHISetStencilRef(uint32 StencilRef) final override;
	virtual void RHISetBlendState(FRHIBlendState* NewState, const FLinearColor& BlendFactor) final override;
	virtual void RHISetBlendFactor(const FLinearColor& BlendFactor) final override
	{
		// Currently ignored as well as on RHISetBlendState()...
	}
	virtual void RHISetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets, const FRHIDepthRenderTargetView* NewDepthStencilTarget) final override;
	virtual void RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo) final override;
	virtual void RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) final override;
	virtual void RHIDrawPrimitiveIndirect(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHIDrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances) final override;
	virtual void RHIDrawIndexedPrimitive(FRHIIndexBuffer* IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances) final override;
	virtual void RHIDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBuffer, FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHIEnableDepthBoundsTest(bool bEnable) final override;
	virtual void RHISetDepthBounds(float MinDepth, float MaxDepth) final override;
	virtual void RHIPushEvent(const TCHAR* Name, FColor Color) final override;
	virtual void RHIPopEvent() final override;
	virtual void RHIInvalidateCachedState() final override;
	virtual void RHIDiscardRenderTargets(bool Depth,bool Stencil,uint32 ColorBitMask) final override;

	virtual void RHICopySubTextureRegion(FRHITexture2D* SourceTexture, FRHITexture2D* DestinationTexture, FBox2D SourceBox, FBox2D DestinationBox) final override;
	// FIXME: Broken on Android for cubemaps
	//virtual void RHICopyTexture(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FRHICopyTextureInfo& CopyInfo) final override;

	// FOpenGLDynamicRHI interface.
	virtual FTexture2DRHIRef RHICreateTexture2DFromResource(EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, uint32 NumSamplesTileMem, const FClearValueBinding& ClearValueBinding, GLuint Resource, uint32 Flags);
	virtual FTexture2DRHIRef RHICreateTexture2DArrayFromResource(EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, uint32 NumSamplesTileMem, const FClearValueBinding& ClearValueBinding, GLuint Resource, uint32 Flags);
	virtual FTextureCubeRHIRef RHICreateTextureCubeFromResource(EPixelFormat Format, uint32 Size, bool bArray, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, uint32 NumSamplesTileMem, const FClearValueBinding& ClearValueBinding, GLuint Resource, uint32 Flags);

	// Inline copy
	virtual void RHICopyToStagingBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIStagingBuffer* DestinationStagingBufferRHI, uint32 InOffset, uint32 InNumBytes) final override;
	virtual void RHIWriteGPUFence(FRHIGPUFence* FenceRHI) final override;
	virtual void* RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI) final override;
	virtual void RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer) final override;

	virtual FRenderQueryPoolRHIRef RHICreateRenderQueryPool(ERenderQueryType QueryType, uint32 NumQueries = UINT32_MAX) final override;
	virtual FStagingBufferRHIRef RHICreateStagingBuffer() final override;
	virtual FGPUFenceRHIRef RHICreateGPUFence(const FName &Name) final override;

	virtual bool RHIRequiresComputeGenerateMips() const override;

	void Cleanup();

	void PurgeFramebufferFromCaches(GLuint Framebuffer);
	void OnVertexBufferDeletion(GLuint VertexBufferResource);
	void OnIndexBufferDeletion(GLuint IndexBufferResource);
	void OnPixelBufferDeletion(GLuint PixelBufferResource);
	void OnUniformBufferDeletion(GLuint UniformBufferResource,uint32 AllocatedSize,bool bStreamDraw);
	void OnProgramDeletion(GLint ProgramResource);
	void InvalidateTextureResourceInCache(GLuint Resource);
	void InvalidateUAVResourceInCache(GLuint Resource);
	/** Set a resource on texture target of a specific real OpenGL stage. Goes through cache to eliminate redundant calls. */
	FORCEINLINE void CachedSetupTextureStage(FOpenGLContextState& ContextState, GLint TextureIndex, GLenum Target, GLuint Resource, GLint BaseMip, GLint NumMips)
	{
		FTextureStage& TextureState = ContextState.Textures[TextureIndex];
		const bool bSameTarget = (TextureState.Target == Target);
		const bool bSameResource = (TextureState.Resource == Resource);

		if (bSameTarget && bSameResource)
		{
			// Nothing changed, no need to update
			return;
		}
		CachedSetupTextureStageInner(ContextState, TextureIndex, Target, Resource, BaseMip, NumMips);
	}

	void CachedSetupTextureStageInner(FOpenGLContextState& ContextState, GLint TextureIndex, GLenum Target, GLuint Resource, GLint BaseMip, GLint NumMips);
	void CachedSetupUAVStage(FOpenGLContextState& ContextState, GLint UAVIndex, GLenum Format, GLuint Resource, bool bLayered, GLint Layer);
	void UpdateSRV(FOpenGLShaderResourceView* SRV);
	FOpenGLContextState& GetContextStateForCurrentContext(bool bAssertIfInvalid = true);

	FORCEINLINE void CachedBindArrayBuffer( FOpenGLContextState& ContextState, GLuint Buffer )
	{
		VERIFY_GL_SCOPE();
		if( ContextState.ArrayBufferBound != Buffer )
		{
			glBindBuffer( GL_ARRAY_BUFFER, Buffer );
			ContextState.ArrayBufferBound = Buffer;
		}
	}

	void CachedBindElementArrayBuffer( FOpenGLContextState& ContextState, GLuint Buffer )
	{
		VERIFY_GL_SCOPE();
		if( ContextState.ElementArrayBufferBound != Buffer )
		{
			glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, Buffer );
			ContextState.ElementArrayBufferBound = Buffer;
		}
	}

	void CachedBindPixelUnpackBuffer( FOpenGLContextState& ContextState, GLuint Buffer )
	{
		VERIFY_GL_SCOPE();

		if( ContextState.PixelUnpackBufferBound != Buffer )
		{
			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, Buffer );
			ContextState.PixelUnpackBufferBound = Buffer;
		}
	}

	void CachedBindUniformBuffer( FOpenGLContextState& ContextState, GLuint Buffer )
	{
		VERIFY_GL_SCOPE();
		check(IsInRenderingThread()||IsInRHIThread());
		if( ContextState.UniformBufferBound != Buffer )
		{
			glBindBuffer( GL_UNIFORM_BUFFER, Buffer );
			ContextState.UniformBufferBound = Buffer;
		}
	}

	bool IsUniformBufferBound( FOpenGLContextState& ContextState, GLuint Buffer ) const
	{
		return ( ContextState.UniformBufferBound == Buffer );
	}

	/** Add query to Queries list upon its creation. */
	void RegisterQuery( FOpenGLRenderQuery* Query );

	/** Remove query from Queries list upon its deletion. */
	void UnregisterQuery( FOpenGLRenderQuery* Query );

	/** Inform all queries about the need to recreate themselves after OpenGL context they're in gets deleted. */
	void InvalidateQueries();

	void BeginRenderQuery_OnThisThread(FOpenGLRenderQuery* Query);
	void EndRenderQuery_OnThisThread(FOpenGLRenderQuery* Query);
	void GetRenderQueryResult_OnThisThread(FOpenGLRenderQuery* Query, bool bWait);



	FOpenGLSamplerState* GetPointSamplerState() const { return (FOpenGLSamplerState*)PointSamplerState.GetReference(); }

	FRHITexture* CreateOpenGLTexture(uint32 SizeX, uint32 SizeY, bool CubeTexture, bool ArrayTexture, bool bIsExternal, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 ArraySize, uint32 Flags, const FClearValueBinding& InClearValue, FResourceBulkDataInterface* BulkData = NULL);
	
	FRHITexture* CreateOpenGLRHITextureOnly(const uint32 SizeX, const uint32 SizeY, const bool bCubeTexture, const bool bArrayTexture, const bool bIsExternal, uint8& Format, uint32& NumMips, uint32& NumSamples, const uint32 ArraySize, uint32& Flags, const FClearValueBinding& InClearValue, FResourceBulkDataInterface* BulkData = nullptr);
	FRHITexture* CreateTexture2DAliased(FOpenGLTexture2D* SourceTexture);
	FRHITexture* CreateTexture2DArrayAliased(FOpenGLTexture2DArray* SourceTexture);
	FRHITexture* CreateTextureCubeAliased(FOpenGLTextureCube* SourceTexture);

	void InitializeGLTexture(FRHITexture* Texture, uint32 SizeX, const uint32 SizeY, const bool bCubeTexture, const bool bArrayTexture, const bool bIsExternal, const uint8 Format, const uint32 NumMips, const uint32 NumSamples, const uint32 ArraySize, const uint32 Flags, const FClearValueBinding& InClearValue, FResourceBulkDataInterface* BulkData = nullptr);

	void* GetOpenGLCurrentContextHandle();

	void SetCustomPresent(class FRHICustomPresent* InCustomPresent);

#define RHITHREAD_GLTRACE 1
#if RHITHREAD_GLTRACE 
	#define RHITHREAD_GLTRACE_BLOCKING QUICK_SCOPE_CYCLE_COUNTER(STAT_OGLRHIThread_Flush);
//#define RHITHREAD_GLTRACE_BLOCKING FPlatformMisc::LowLevelOutputDebugStringf(TEXT("FLUSHING %s!\n"), ANSI_TO_TCHAR(__FUNCTION__))
//#define RHITHREAD_GLTRACE_BLOCKING UE_LOG(LogRHI, Warning,TEXT("FLUSHING %s!\n"), ANSI_TO_TCHAR(__FUNCTION__));
#else
	#define RHITHREAD_GLTRACE_BLOCKING 
#endif
#define RHITHREAD_GLCOMMAND_PROLOGUE() auto GLCommand= [&]() {

#define RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(x) };\
		if (RHICmdList.Bypass() ||  !IsRunningRHIInSeparateThread() || IsInRHIThread())\
		{\
			return GLCommand();\
		}\
		else\
		{\
			x ReturnValue = (x)0;\
			ALLOC_COMMAND_CL(RHICmdList, FRHICommandGLCommand)([&ReturnValue, GLCommand = MoveTemp(GLCommand)]() { ReturnValue = GLCommand(); }); \
			RHITHREAD_GLTRACE_BLOCKING;\
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);\
			return ReturnValue;\
		}\

#define RHITHREAD_GLCOMMAND_EPILOGUE_GET_RETURN(x) };\
		x ReturnValue = (x)0;\
		if (RHICmdList.Bypass() ||  !IsRunningRHIInSeparateThread() || IsInRHIThread() )\
		{\
			ReturnValue = GLCommand();\
		}\
		else\
		{\
			ALLOC_COMMAND_CL(RHICmdList, FRHICommandGLCommand)([&ReturnValue, GLCommand = MoveTemp(GLCommand)]() { ReturnValue = GLCommand(); }); \
			RHITHREAD_GLTRACE_BLOCKING;\
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);\
		}\


#define RHITHREAD_GLCOMMAND_EPILOGUE() };\
		if (ShouldRunGLRenderContextOpOnThisThread(RHICmdList))\
		{\
			return GLCommand();\
		}\
		else\
		{\
			ALLOC_COMMAND_CL(RHICmdList, FRHICommandGLCommand)( GLCommand ); \
			RHITHREAD_GLTRACE_BLOCKING;\
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);\
		}\

#define RHITHREAD_GLCOMMAND_EPILOGUE_NORETURN() };\
		if (ShouldRunGLRenderContextOpOnThisThread(RHICmdList))\
		{\
			GLCommand();\
		}\
		else\
		{\
			ALLOC_COMMAND_CL(RHICmdList, FRHICommandGLCommand)( GLCommand ); \
			RHITHREAD_GLTRACE_BLOCKING;\
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);\
		}\

	struct FTextureLockTracker
	{
		struct FLockParams
		{
			void* RHIBuffer;
			void* Buffer;
			uint32 MipIndex;
			uint32 BufferSize;
			uint32 Stride;
			EResourceLockMode LockMode;

			FORCEINLINE_DEBUGGABLE FLockParams(void* InRHIBuffer, void* InBuffer, uint32 InMipIndex, uint32 InStride, uint32 InBufferSize, EResourceLockMode InLockMode)
				: RHIBuffer(InRHIBuffer)
				, Buffer(InBuffer)
				, MipIndex(InMipIndex)
				, BufferSize(InBufferSize)
				, Stride(InStride)
				, LockMode(InLockMode)
			{
			}
		};
		TArray<FLockParams, TInlineAllocator<16> > OutstandingLocks;
		uint32 TotalMemoryOutstanding;

		FTextureLockTracker()
		{
			TotalMemoryOutstanding = 0;
		}

		FORCEINLINE_DEBUGGABLE void Lock(void* RHIBuffer, void* Buffer, uint32 MipIndex, uint32 Stride, uint32 SizeRHI, EResourceLockMode LockMode)
		{
//#if DO_CHECK
			for (auto& Parms : OutstandingLocks)
			{
				check(Parms.RHIBuffer != RHIBuffer || Parms.MipIndex != MipIndex);
			}
//#endif
			OutstandingLocks.Add(FLockParams(RHIBuffer, Buffer, MipIndex, Stride, SizeRHI, LockMode));
			TotalMemoryOutstanding += SizeRHI;
		}

		FORCEINLINE_DEBUGGABLE FLockParams Unlock(void* RHIBuffer, uint32 MipIndex)
		{
			for (int32 Index = 0; Index < OutstandingLocks.Num(); Index++)
			{
				FLockParams& CurrentLock = OutstandingLocks[Index];
				if (CurrentLock.RHIBuffer == RHIBuffer && CurrentLock.MipIndex == MipIndex)
				{
					FLockParams Result = OutstandingLocks[Index];
					OutstandingLocks.RemoveAtSwap(Index, 1, false);
					TotalMemoryOutstanding -= Result.BufferSize;
					return Result;
				}
			}
			check(!"Mismatched RHI buffer locks.");
			return FLockParams(nullptr, nullptr, 0, 0, 0, RLM_WriteOnly);
		}
	};

	virtual void* LockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush) final override;
	virtual void UnlockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush) final override;
	virtual void* RHILockTextureCubeFace_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override;
	virtual void RHIUnlockTextureCubeFace_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail) final override;

	virtual FIndexBufferRHIRef CreateIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) final override;

	virtual FVertexBufferRHIRef CreateVertexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) final override
	{
		return this->RHICreateVertexBuffer(Size, InUsage, CreateInfo);
	}

	virtual FStructuredBufferRHIRef CreateStructuredBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) final override
	{
		RHITHREAD_GLCOMMAND_PROLOGUE();
		return this->RHICreateStructuredBuffer(Stride, Size, InUsage, CreateInfo);
		RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(FStructuredBufferRHIRef);
	}

	virtual FShaderResourceViewRHIRef CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) final override
	{
		return this->RHICreateShaderResourceView(VertexBuffer, Stride, Format);
	}

	virtual FShaderResourceViewRHIRef CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* Buffer) final override
	{
		return this->RHICreateShaderResourceView(Buffer);
	}

	virtual FTextureReferenceRHIRef RHICreateTextureReference_RenderThread(class FRHICommandListImmediate& RHICmdList, FLastRenderTimeContainer* LastRenderTime) final override
	{
		// threadsafe, doesn't really do anything
		return this->RHICreateTextureReference(LastRenderTime);
	}


	virtual FTexture2DRHIRef RHICreateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override
	{
		const bool bCubeTexture = false;
		const bool bArrayTexture = false;
		const bool bIsExternal = false;
		const uint32 ArraySize = 1;
		FOpenGLTexture2D* Texture2D = (FOpenGLTexture2D*)CreateOpenGLRHITextureOnly(SizeX, SizeY, bCubeTexture, bArrayTexture, bIsExternal, Format, NumMips, NumSamples, ArraySize, Flags, CreateInfo.ClearValueBinding, CreateInfo.BulkData);
		Texture2D->CreationFence.Reset();
		RunOnGLRenderContextThread([=]()
		{
			// Fill in the GL resources.
			InitializeGLTexture(Texture2D, SizeX, SizeY, bCubeTexture, bArrayTexture, bIsExternal, Format, NumMips, NumSamples, ArraySize, Flags, CreateInfo.ClearValueBinding, CreateInfo.BulkData);
			Texture2D->CreationFence.WriteAssertFence();
		});
		Texture2D->CreationFence.SetRHIThreadFence();
		return Texture2D;
	}

	virtual FTexture2DRHIRef RHICreateTextureExternal2D_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override
	{
		RHITHREAD_GLCOMMAND_PROLOGUE();
		return this->RHICreateTextureExternal2D(SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
		RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(FTexture2DRHIRef);
	}

	virtual FTexture2DArrayRHIRef RHICreateTexture2DArray_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override
	{
		RHITHREAD_GLCOMMAND_PROLOGUE();
		return this->RHICreateTexture2DArray(SizeX, SizeY, SizeZ, Format, NumMips, NumSamples, Flags, CreateInfo);
		RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(FTexture2DArrayRHIRef);
	}

	virtual FTexture3DRHIRef RHICreateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override
	{
		RHITHREAD_GLCOMMAND_PROLOGUE();
		return this->RHICreateTexture3D(SizeX, SizeY, SizeZ, Format, NumMips, Flags, CreateInfo);
		RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(FTexture3DRHIRef);
	}

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer) final override
	{
		RHITHREAD_GLCOMMAND_PROLOGUE();
		return this->RHICreateUnorderedAccessView(StructuredBuffer, bUseUAVCounter, bAppendBuffer);
		RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(FUnorderedAccessViewRHIRef);
	}

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel) final override
	{
		RHITHREAD_GLCOMMAND_PROLOGUE();
		return this->RHICreateUnorderedAccessView(Texture, MipLevel);
		RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(FUnorderedAccessViewRHIRef);
	}

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint8 Format) final override
	{
		RHITHREAD_GLCOMMAND_PROLOGUE();
		return this->RHICreateUnorderedAccessView(VertexBuffer, Format);
		RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(FUnorderedAccessViewRHIRef);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo) final override
	{
		return this->RHICreateShaderResourceView(Texture, CreateInfo);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) final override
	{
		return this->RHICreateShaderResourceView(VertexBuffer, Stride, Format);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* Buffer) final override
	{
		return this->RHICreateShaderResourceView(Buffer);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer) final override
	{
		return this->RHICreateShaderResourceView(StructuredBuffer);
	}

	virtual FTextureCubeRHIRef RHICreateTextureCube_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override
	{
		const bool bCubeTexture = true;
		const bool bArrayTexture = false;
		const bool bIsExternal = false;
		const uint32 ArraySize = 1;
		uint32 NumSamples = 1;
		FOpenGLTextureCube* TextureCube = (FOpenGLTextureCube*)CreateOpenGLRHITextureOnly(Size, Size, bCubeTexture, bArrayTexture, bIsExternal, Format, NumMips, NumSamples, ArraySize, Flags, CreateInfo.ClearValueBinding, CreateInfo.BulkData);
		TextureCube->CreationFence.Reset();
		RunOnGLRenderContextThread([=]()
		{
			// Fill in the GL resources.
			InitializeGLTexture(TextureCube, Size, Size, bCubeTexture, bArrayTexture, bIsExternal, Format, NumMips, NumSamples, ArraySize, Flags, CreateInfo.ClearValueBinding, CreateInfo.BulkData);
			TextureCube->CreationFence.WriteAssertFence();
		});
		TextureCube->CreationFence.SetRHIThreadFence();
		return TextureCube;
	}

	virtual FTextureCubeRHIRef RHICreateTextureCubeArray_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) final override
	{
		RHITHREAD_GLCOMMAND_PROLOGUE();
		return this->RHICreateTextureCubeArray(Size, ArraySize, Format, NumMips, Flags, CreateInfo);
		RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(FTextureCubeRHIRef);
	}

	virtual FRenderQueryRHIRef RHICreateRenderQuery_RenderThread(class FRHICommandListImmediate& RHICmdList, ERenderQueryType QueryType) final override
	{
		return this->RHICreateRenderQuery(QueryType);
	}

	virtual FTexture2DRHIRef AsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus) final override;

	virtual ETextureReallocationStatus FinalizeAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted) final override
	{
		return this->RHIFinalizeAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
	}

	virtual ETextureReallocationStatus CancelAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted) final override
	{
		return this->RHICancelAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
	}

	virtual FVertexShaderRHIRef CreateVertexShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) final override
	{
		return this->RHICreateVertexShader(Code);
	}

	virtual FPixelShaderRHIRef CreatePixelShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) final override
	{
		return this->RHICreatePixelShader(Code);
	}

	virtual FGeometryShaderRHIRef CreateGeometryShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) final override
	{
		return this->RHICreateGeometryShader(Code);
	}

	virtual FComputeShaderRHIRef CreateComputeShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) final override
	{
		return this->RHICreateComputeShader(Code);
	}

	virtual FHullShaderRHIRef CreateHullShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) final override
	{
		return this->RHICreateHullShader(Code);
	}

	virtual FSamplerStateRHIRef RHICreateSamplerState(const FSamplerStateInitializerRHI& Initializer) final override;

	virtual void UpdateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData) final override
	{
		GDynamicRHI->RHIUpdateTexture2D(Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
	}

	virtual void UpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData) final override
	{
		RHITHREAD_GLCOMMAND_PROLOGUE();
		GDynamicRHI->RHIUpdateTexture3D(Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
		RHITHREAD_GLCOMMAND_EPILOGUE();
	}

	virtual void RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState) final override
	{
		FRHIGraphicsPipelineStateFallBack* FallbackGraphicsState = static_cast<FRHIGraphicsPipelineStateFallBack*>(GraphicsState);

		auto& PsoInit = FallbackGraphicsState->Initializer;

		RHISetBoundShaderState(
			RHICreateBoundShaderState_internal(
				PsoInit.BoundShaderState.VertexDeclarationRHI,
				PsoInit.BoundShaderState.VertexShaderRHI,
				TESSELLATION_SHADER(PsoInit.BoundShaderState.HullShaderRHI),
				TESSELLATION_SHADER(PsoInit.BoundShaderState.DomainShaderRHI),
				PsoInit.BoundShaderState.PixelShaderRHI,
				GEOMETRY_SHADER(PsoInit.BoundShaderState.GeometryShaderRHI),
				PsoInit.bFromPSOFileCache
			).GetReference()
		);

		RHISetDepthStencilState(FallbackGraphicsState->Initializer.DepthStencilState, 0);
		RHISetRasterizerState(FallbackGraphicsState->Initializer.RasterizerState);
		RHISetBlendState(FallbackGraphicsState->Initializer.BlendState, FLinearColor(1.0f, 1.0f, 1.0f));
		if (GSupportsDepthBoundsTest)
		{
			RHIEnableDepthBoundsTest(FallbackGraphicsState->Initializer.bDepthBounds);
		}

		ApplyGlobalUniformBuffers(PsoInit.BoundShaderState.VertexShaderRHI, ResourceCast(PsoInit.BoundShaderState.VertexShaderRHI));
		ApplyGlobalUniformBuffers(PsoInit.BoundShaderState.HullShaderRHI, ResourceCast(PsoInit.BoundShaderState.HullShaderRHI));
		ApplyGlobalUniformBuffers(PsoInit.BoundShaderState.DomainShaderRHI, ResourceCast(PsoInit.BoundShaderState.DomainShaderRHI));
		ApplyGlobalUniformBuffers(PsoInit.BoundShaderState.GeometryShaderRHI, ResourceCast(PsoInit.BoundShaderState.GeometryShaderRHI));
		ApplyGlobalUniformBuffers(PsoInit.BoundShaderState.PixelShaderRHI, ResourceCast(PsoInit.BoundShaderState.PixelShaderRHI));

		// Store the PSO's primitive (after since IRHICommandContext::RHISetGraphicsPipelineState sets the BSS)
		PrimitiveType = PsoInit.PrimitiveType;
	}

	FBoundShaderStateRHIRef RHICreateBoundShaderState_internal(
		FRHIVertexDeclaration* VertexDeclarationRHI,
		FRHIVertexShader* VertexShaderRHI,
		FRHIHullShader* HullShaderRHI,
		FRHIDomainShader* DomainShaderRHI,
		FRHIPixelShader* PixelShaderRHI,
		FRHIGeometryShader* GeometryShaderRHI,
		bool FromPSOFileCache
	)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		RHITHREAD_GLCOMMAND_PROLOGUE()
			return RHICreateBoundShaderState_OnThisThread(VertexDeclarationRHI,
				VertexShaderRHI,
				HullShaderRHI,
				DomainShaderRHI,
				PixelShaderRHI,
				GeometryShaderRHI,
				FromPSOFileCache);
		RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(FBoundShaderStateRHIRef);
	}

	virtual FBoundShaderStateRHIRef RHICreateBoundShaderState(
		FRHIVertexDeclaration* VertexDeclarationRHI,
		FRHIVertexShader* VertexShaderRHI,
		FRHIHullShader* HullShaderRHI,
		FRHIDomainShader* DomainShaderRHI,
		FRHIPixelShader* PixelShaderRHI,
		FRHIGeometryShader* GeometryShaderRHI
	) final override
	{
		return RHICreateBoundShaderState_internal(
			VertexDeclarationRHI,
			VertexShaderRHI,
			HullShaderRHI,
			DomainShaderRHI,
			PixelShaderRHI,
			GeometryShaderRHI,
			false);
	}

	class FOpenGLLinkedProgram* GetLinkedComputeProgram(FRHIComputeShader* ComputeShaderRHI);

	FBoundShaderStateRHIRef RHICreateBoundShaderState_OnThisThread(FRHIVertexDeclaration* VertexDeclaration, FRHIVertexShader* VertexShader, FRHIHullShader* HullShader, FRHIDomainShader* DomainShader, FRHIPixelShader* PixelShader, FRHIGeometryShader* GeometryShader, bool FromPSOFileCache);
	void RHIPerFrameRHIFlushComplete();

	FOpenGLGPUProfiler& GetGPUProfilingData() {
		return GPUProfilingData;
	}
	
private:

	/** Counter incremented each time RHIBeginScene is called. */
	uint32 SceneFrameCounter;

	/** Value used to detect when resource tables need to be recached. INDEX_NONE means always recache. */
	uint32 ResourceTableFrameCounter;

	/** RHI device state, independent of underlying OpenGL context used */
	FOpenGLRHIState						PendingState;
	FOpenGLStreamedVertexBufferArray	DynamicVertexBuffers;
	FOpenGLStreamedIndexBufferArray		DynamicIndexBuffers;
	FSamplerStateRHIRef					PointSamplerState;

	/** A list of all viewport RHIs that have been created. */
	TArray<FOpenGLViewport*> Viewports;
	TRefCountPtr<FOpenGLViewport>		DrawingViewport;
	bool								bRevertToSharedContextAfterDrawingViewport;

	bool								bIsRenderingContextAcquired;

	EPrimitiveType						PrimitiveType = PT_Num;

	/** A history of the most recently used bound shader states, used to keep transient bound shader states from being recreated for each use. */
	TGlobalResource< TBoundShaderStateHistory<10000> > BoundShaderStateHistory;

	/** Per-context state caching */
	FOpenGLContextState InvalidContextState;
	FOpenGLContextState	SharedContextState;
	FOpenGLContextState	RenderingContextState;
	// Cached context type on BeginScene
	int32 BeginSceneContextType;

	template <typename TRHIShader, typename TRHIProxyShader>
	void ApplyGlobalUniformBuffers(TRHIShader* Shader, TRHIProxyShader* ProxyShader)
	{
		if (ProxyShader)
		{
			check(Shader);
			::ApplyGlobalUniformBuffers(this, Shader, ProxyShader->StaticSlots, ProxyShader->Bindings.ShaderResourceTable.ResourceTableLayoutHashes, GlobalUniformBuffers);
		}
	}

	TArray<FRHIUniformBuffer*> GlobalUniformBuffers;

	/** Cached mip-limits for textures when ARB_texture_view is unavailable */
	TMap<GLuint, TPair<GLenum, GLenum>> TextureMipLimits;

	/** Underlying platform-specific data */
	struct FPlatformOpenGLDevice* PlatformDevice;

	/** Query list. This is used to inform queries they're no longer valid when OpenGL context they're in gets released from another thread. */
	TArray<FOpenGLRenderQuery*> Queries;

	/** A critical section to protect modifications and iteration over Queries list */
	FCriticalSection QueriesListCriticalSection;

	FOpenGLGPUProfiler GPUProfilingData;
	friend FOpenGLGPUProfiler;
//	FOpenGLEventQuery FrameSyncEvent;

	FCriticalSection CustomPresentSection;
	TRefCountPtr<class FRHICustomPresent> CustomPresent;

	GLuint GetOpenGLFramebuffer(uint32 NumSimultaneousRenderTargets, FOpenGLTextureBase** RenderTargets, const uint32* ArrayIndices, const uint32* MipmapLevels, FOpenGLTextureBase* DepthStencilTarget);

	void InitializeStateResources();

	/** needs to be called before each dispatch call */
	

	void EnableVertexElementCached(FOpenGLContextState& ContextCache, GLuint AttributeIndex, const FOpenGLVertexElement &VertexElement, GLsizei Stride, void *Pointer, GLuint Buffer);
	void EnableVertexElementCachedZeroStride(FOpenGLContextState& ContextCache, GLuint AttributeIndex, const FOpenGLVertexElement &VertexElement, uint32 NumVertices, FOpenGLVertexBuffer* VertexBuffer);
	void SetupVertexArrays(FOpenGLContextState& ContextCache, uint32 BaseVertexIndex, FOpenGLStream* Streams, uint32 NumStreams, uint32 MaxVertices);
	void SetupVertexArraysVAB(FOpenGLContextState& ContextCache, uint32 BaseVertexIndex, FOpenGLStream* Streams, uint32 NumStreams, uint32 MaxVertices);
	void SetupVertexArraysUP(FOpenGLContextState& ContextState, void* Buffer, uint32 Stride);

	void SetupBindlessTextures( FOpenGLContextState& ContextState, const TArray<FOpenGLBindlessSamplerInfo> &Samplers );

	/** needs to be called before each draw call */
	void BindPendingFramebuffer( FOpenGLContextState& ContextState );
	void BindPendingShaderState( FOpenGLContextState& ContextState );
	void BindPendingComputeShaderState( FOpenGLContextState& ContextState, FOpenGLComputeShader* ComputeShader );
	void UpdateRasterizerStateInOpenGLContext( FOpenGLContextState& ContextState );
	void UpdateDepthStencilStateInOpenGLContext( FOpenGLContextState& ContextState );
	void UpdateScissorRectInOpenGLContext( FOpenGLContextState& ContextState );
	void UpdateViewportInOpenGLContext( FOpenGLContextState& ContextState );
	
	template <class ShaderType> void SetResourcesFromTables(const ShaderType* RESTRICT);
	FORCEINLINE void CommitGraphicsResourceTables()
	{
		if (PendingState.bAnyDirtyGraphicsUniformBuffers)
		{
			CommitGraphicsResourceTablesInner();
		}
	}
	void CommitGraphicsResourceTablesInner();
	void CommitComputeResourceTables(FOpenGLComputeShader* ComputeShader);
	void CommitNonComputeShaderConstants();
	void CommitNonComputeShaderConstantsSlowPath();
	void CommitNonComputeShaderConstantsFastPath(FOpenGLLinkedProgram* LinkedProgram);
	void CommitComputeShaderConstants(FOpenGLComputeShader* ComputeShader);
	void SetPendingBlendStateForActiveRenderTargets( FOpenGLContextState& ContextState );
	
	void SetupTexturesForDraw( FOpenGLContextState& ContextState);
	template <typename StateType>
	void SetupTexturesForDraw( FOpenGLContextState& ContextState, const StateType& ShaderState, int32 MaxTexturesNeeded);

	void SetupUAVsForDraw(FOpenGLContextState& ContextState, const FOpenGLComputeShader* ComputeShader, int32 MaxUAVsNeeded);

	void RHIClearMRT(bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);

public:
	/** Remember what RHI user wants set on a specific OpenGL texture stage, translating from Stage and TextureIndex for stage pair. */
	void InternalSetShaderTexture(FOpenGLTextureBase* Texture, FOpenGLShaderResourceView* SRV, GLint TextureIndex, GLenum Target, GLuint Resource, int NumMips, int LimitMip);
	void InternalSetShaderUAV(GLint UAVIndex, GLenum Format, GLuint Resource, bool bLayered, GLint Layer);
	void InternalSetSamplerStates(GLint TextureIndex, FOpenGLSamplerState* SamplerState);

private:

	void RegisterSharedShaderCodeDelegates();
	void UnregisterSharedShaderCodeDelegates();

	void SetupRecursiveResources();

	void ApplyTextureStage(FOpenGLContextState& ContextState, GLint TextureIndex, const FTextureStage& TextureStage, FOpenGLSamplerState* SamplerState);

	void ReadSurfaceDataRaw(FOpenGLContextState& ContextState, FRHITexture* TextureRHI,FIntRect Rect,TArray<uint8>& OutData, FReadSurfaceDataFlags InFlags);

	void BindUniformBufferBase(FOpenGLContextState& ContextState, int32 NumUniformBuffers, FUniformBufferRHIRef* BoundUniformBuffers, uint32 FirstUniformBuffer, bool ForceUpdate);

	void ClearCurrentFramebufferWithCurrentScissor(FOpenGLContextState& ContextState, int8 ClearType, int32 NumClearColors, const FLinearColor* ClearColorArray, float Depth, uint32 Stencil);

	void FreeZeroStrideBuffers();

	/** Remaps vertex attributes on devices where GL_MAX_VERTEX_ATTRIBS < 16 */
	FORCEINLINE uint32 RemapVertexAttrib(uint32 VertexAttributeIndex)
	{
		if (FOpenGL::NeedsVertexAttribRemapTable())
		{
			check(VertexAttributeIndex < UE_ARRAY_COUNT(PendingState.BoundShaderState->GetVertexShader()->Bindings.VertexAttributeRemap));
			VertexAttributeIndex = PendingState.BoundShaderState->GetVertexShader()->Bindings.VertexAttributeRemap[VertexAttributeIndex];
		}
		check(VertexAttributeIndex < NUM_OPENGL_VERTEX_STREAMS); // check that this attribute has remaped correctly.
		return VertexAttributeIndex;
	}

	FORCEINLINE uint32 RemapVertexAttrib(const FOpenGLShaderBindings& Bindings, uint32 VertexAttributeIndex)
	{
		if (FOpenGL::NeedsVertexAttribRemapTable())
		{
			check(VertexAttributeIndex < UE_ARRAY_COUNT(Bindings.VertexAttributeRemap));
			VertexAttributeIndex = Bindings.VertexAttributeRemap[VertexAttributeIndex];
		}
		check(VertexAttributeIndex < NUM_OPENGL_VERTEX_STREAMS); // check that this attribute has remaped correctly.
		return VertexAttributeIndex;
	}

	

	FTextureLockTracker GLLockTracker;

};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

/** Implements the OpenGLDrv module as a dynamic RHI providing module. */
class FOpenGLDynamicRHIModule : public IDynamicRHIModule
{
public:
	
	// IModuleInterface
	virtual bool SupportsDynamicReloading() override { return false; }

	// IDynamicRHIModule
	virtual bool IsSupported() override;

	virtual FDynamicRHI* CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel = ERHIFeatureLevel::Num) override;
};

extern ERHIFeatureLevel::Type GRequestedFeatureLevel;
