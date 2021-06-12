// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHI.h: Render Hardware Interface definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/ContainersFwd.h"
#include "Stats/Stats.h"
#include "RHIDefinitions.h"
#include "Containers/StaticArray.h"
#include "Containers/StringFwd.h"

#ifndef RHI_COMMAND_LIST_DEBUG_TRACES
#define RHI_COMMAND_LIST_DEBUG_TRACES 0
#endif

class FResourceArrayInterface;
class FResourceBulkDataInterface;

/** Alignment of the shader parameters struct is required to be 16-byte boundaries. */
#define SHADER_PARAMETER_STRUCT_ALIGNMENT 16

/** The alignment in bytes between elements of array shader parameters. */
#define SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT 16

// RHICreateUniformBuffer assumes C++ constant layout matches the shader layout when extracting float constants, yet the C++ struct contains pointers.  
// Enforce a min size of 64 bits on pointer types in uniform buffer structs to guarantee layout matching between languages.
#define SHADER_PARAMETER_POINTER_ALIGNMENT sizeof(uint64)
static_assert(sizeof(void*) <= SHADER_PARAMETER_POINTER_ALIGNMENT, "The alignment of pointer needs to match the largest pointer.");


/** RHI Logging. */
RHI_API DECLARE_LOG_CATEGORY_EXTERN(LogRHI,Log,VeryVerbose);

/**
 * RHI configuration settings.
 */

namespace RHIConfig
{
	RHI_API bool ShouldSaveScreenshotAfterProfilingGPU();
	RHI_API bool ShouldShowProfilerAfterProfilingGPU();
	RHI_API float GetGPUHitchThreshold();
}

/**
 * RHI globals.
 */

/** True if the render hardware has been initialized. */
extern RHI_API bool GIsRHIInitialized;

class RHI_API FRHICommandList;

/**
 * RHI capabilities.
 */


/** The maximum number of mip-maps that a texture can contain. 	*/
extern	RHI_API int32		GMaxTextureMipCount;

/** Does the RHI implements CopyToTexture() with FRHICopyTextureInfo::NumMips > 1 */
extern RHI_API bool GRHISupportsCopyToTextureMultipleMips;

/** true if this platform has quad buffer stereo support. */
extern RHI_API bool GSupportsQuadBufferStereo;

/** true if the RHI supports textures that may be bound as both a render target and a shader resource. */
extern RHI_API bool GSupportsRenderDepthTargetableShaderResources;

// The maximum feature level and shader platform available on this system
// GRHIFeatureLevel and GRHIShaderPlatform have been deprecated. There is no longer a current featurelevel/shaderplatform that
// should be used for all rendering, rather a specific set for each view.
extern RHI_API ERHIFeatureLevel::Type GMaxRHIFeatureLevel;
extern RHI_API EShaderPlatform GMaxRHIShaderPlatform;

/** true if the RHI supports SRVs */
extern RHI_API bool GSupportsResourceView;

/** true if the RHI supports Draw Indirect */
extern RHI_API bool GRHISupportsDrawIndirect;

/** Whether the RHI can send commands to the device context from multiple threads. Used in the GPU readback to avoid stalling the RHI threads. */
extern RHI_API bool GRHISupportsMultithreading;

/** 
 * only set if RHI has the information (after init of the RHI and only if RHI has that information, never changes after that)
 * e.g. "NVIDIA GeForce GTX 670"
 */
extern RHI_API FString GRHIAdapterName;
extern RHI_API FString GRHIAdapterInternalDriverVersion;
extern RHI_API FString GRHIAdapterUserDriverVersion;
extern RHI_API FString GRHIAdapterDriverDate;
extern RHI_API uint32 GRHIDeviceId;
extern RHI_API uint32 GRHIDeviceRevision;

// 0 means not defined yet, use functions like IsRHIDeviceAMD() to access
extern RHI_API uint32 GRHIVendorId;

// true if the RHI supports Pixel Shader UAV
extern RHI_API bool GRHISupportsPixelShaderUAVs;

// to trigger GPU specific optimizations and fallbacks
RHI_API bool IsRHIDeviceAMD();

// to trigger GPU specific optimizations and fallbacks
RHI_API bool IsRHIDeviceIntel();

// to trigger GPU specific optimizations and fallbacks
RHI_API bool IsRHIDeviceNVIDIA();

// helper to convert GRHIVendorId into a printable string, or "Unknown" if unknown.
RHI_API const TCHAR* RHIVendorIdToString();

// helper to convert VendorId into a printable string, or "Unknown" if unknown.
RHI_API const TCHAR* RHIVendorIdToString(EGpuVendorId VendorId);

// helper to return the shader language version for the given shader platform.
RHI_API uint32 RHIGetShaderLanguageVersion(const FStaticShaderPlatform Platform);

// helper to check that the shader platform supports tessellation.
RHI_API bool RHISupportsTessellation(const FStaticShaderPlatform Platform);

// helper to check that the shader platform supports writing to UAVs from pixel shaders.
RHI_API bool RHISupportsPixelShaderUAVs(const FStaticShaderPlatform Platform);

// helper to check that the shader platform supports creating a UAV off an index buffer.
RHI_API bool RHISupportsIndexBufferUAVs(const FStaticShaderPlatform Platform);

// helper to check if a preview feature level has been requested.
RHI_API bool RHIGetPreviewFeatureLevel(ERHIFeatureLevel::Type& PreviewFeatureLevelOUT);

// helper to check if preferred EPixelFormat is supported, return one if it is not
RHI_API EPixelFormat RHIPreferredPixelFormatHint(EPixelFormat PreferredPixelFormat);

// helper to check which resource type should be used for clear (UAV) replacement shaders.
RHI_API int32 RHIGetPreferredClearUAVRectPSResourceType(const FStaticShaderPlatform Platform);

inline bool RHISupportsInstancedStereo(const FStaticShaderPlatform Platform)
{
	return Platform == EShaderPlatform::SP_PCD3D_SM5 || Platform == EShaderPlatform::SP_METAL_SM5 || Platform == EShaderPlatform::SP_METAL_SM5_NOTESS
		|| Platform == EShaderPlatform::SP_PCD3D_ES3_1 || FDataDrivenShaderPlatformInfo::GetSupportsInstancedStereo(Platform);
}

inline bool RHISupportsMultiView(const FStaticShaderPlatform Platform)
{
	return ((Platform == EShaderPlatform::SP_METAL_SM5 || Platform == SP_METAL_SM5_NOTESS))
		|| FDataDrivenShaderPlatformInfo::GetSupportsMultiView(Platform);
}

inline bool RHISupportsMSAA(const FStaticShaderPlatform Platform)
{
	// @todo platplug: Maybe this should become bDisallowMSAA to default of 0 is a better default (since now MSAA is opt-out more than opt-in) 
	return FDataDrivenShaderPlatformInfo::GetSupportsMSAA(Platform);
}

inline bool RHISupportsBufferLoadTypeConversion(const FStaticShaderPlatform Platform)
{
	return !IsMetalPlatform(Platform);
}

/** Whether the platform supports reading from volume textures (does not cover rendering to volume textures). */
inline bool RHISupportsVolumeTextures(const FStaticFeatureLevel FeatureLevel)
{
	return FeatureLevel >= ERHIFeatureLevel::SM5;
}

inline bool RHISupportsVertexShaderLayer(const FStaticShaderPlatform Platform)
{
	return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5) && IsMetalPlatform(Platform) && IsPCPlatform(Platform);
}

/** Return true if and only if the GPU support rendering to volume textures (2D Array, 3D) is guaranteed supported for a target platform.
	if PipelineVolumeTextureLUTSupportGuaranteedAtRuntime is true then it is guaranteed that GSupportsVolumeTextureRendering is true at runtime.
*/
inline bool RHIVolumeTextureRenderingSupportGuaranteed(const FStaticShaderPlatform Platform)
{
	return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5)
		&& (!IsMetalPlatform(Platform) || RHISupportsVertexShaderLayer(Platform)) // For Metal only shader platforms & versions that support vertex-shader-layer can render to volume textures - this is a compile/cook time check.
		&& !IsOpenGLPlatform(Platform);		// Apparently, some OpenGL 3.3 cards support SM4 but can't render to volume textures
}

inline bool RHISupports4ComponentUAVReadWrite(const FStaticShaderPlatform Platform)
{
	// Must match usf PLATFORM_SUPPORTS_4COMPONENT_UAV_READ_WRITE
	// D3D11 does not support multi-component loads from a UAV: "error X3676: typed UAV loads are only allowed for single-component 32-bit element types"
	return IsMetalPlatform(Platform) 
		|| FDataDrivenShaderPlatformInfo::GetSupports4ComponentUAVReadWrite(Platform);
}

/** Whether Manual Vertex Fetch is supported for the specified shader platform.
	Shader Platform must not use the mobile renderer, and for Metal, the shader language must be at least 2. */
inline bool RHISupportsManualVertexFetch(const FStaticShaderPlatform InShaderPlatform)
{
	bool bIsMetalMobilePlatform = IsMetalMobilePlatform(InShaderPlatform);
	bool bIsUnsupportedGL = IsOpenGLPlatform(InShaderPlatform);

	return (!bIsUnsupportedGL && !IsMobilePlatform(InShaderPlatform) && !bIsMetalMobilePlatform)
		|| FDataDrivenShaderPlatformInfo::GetSupportsManualVertexFetch(InShaderPlatform);
}

/** 
 * Returns true if SV_VertexID contains BaseVertexIndex passed to the draw call, false if shaders must manually construct an absolute VertexID.
 */
inline bool RHISupportsAbsoluteVertexID(const EShaderPlatform InShaderPlatform)
{
	return IsVulkanPlatform(InShaderPlatform) || IsVulkanMobilePlatform(InShaderPlatform);
}

/** Can this platform compile ray tracing shaders (regardless of project settings).
 *  To use at runtime, also check GRHISupportsRayTracing and r.RayTracing CVar (see IsRayTracingEnabled() helper).
 **/
inline RHI_API bool RHISupportsRayTracingShaders(const FStaticShaderPlatform Platform)
{
	return FDataDrivenShaderPlatformInfo::GetSupportsRayTracing(Platform);
}

/** Can this platform compile shaders that use shader model 6.0 wave intrinsics.
 *  To use such shaders at runtime, also check GRHISupportsWaveOperations.
 **/
inline RHI_API bool RHISupportsWaveOperations(const FStaticShaderPlatform Platform)
{
	return FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(Platform);
}

/** True if the given shader platform supports a render target write mask */
inline bool RHISupportsRenderTargetWriteMask(const FStaticShaderPlatform Platform)
{
	return FDataDrivenShaderPlatformInfo::GetSupportsRenderTargetWriteMask(Platform);
}

// Wrapper for GRHI## global variables, allows values to be overridden for mobile preview modes.
template <typename TValueType>
class TRHIGlobal
{
public:
	explicit TRHIGlobal(const TValueType& InValue) : Value(InValue) {}

	TRHIGlobal& operator=(const TValueType& InValue) 
	{
		Value = InValue; 
		return *this;
	}

#if WITH_EDITOR
	inline void SetPreviewOverride(const TValueType& InValue)
	{
		PreviewValue = InValue;
	}

	inline operator TValueType() const
	{ 
		return PreviewValue.IsSet() ? GetPreviewValue() : Value;
	}
#else
	inline operator TValueType() const { return Value; }
#endif

private:
	TValueType Value;
#if WITH_EDITOR
	TOptional<TValueType> PreviewValue;
	TValueType GetPreviewValue() const { return PreviewValue.GetValue(); }
#endif
};

#if WITH_EDITOR
template<>
inline int32 TRHIGlobal<int32>::GetPreviewValue() const 
{
	// ensure the preview values are subsets of RHI functionality.
	return FMath::Min(PreviewValue.GetValue(), Value);
}
template<>
inline bool TRHIGlobal<bool>::GetPreviewValue() const
{
	// ensure the preview values are subsets of RHI functionality.
	return PreviewValue.GetValue() && Value;
}
#endif

/** true if the GPU is AMD's Pre-GCN architecture */
extern RHI_API bool GRHIDeviceIsAMDPreGCNArchitecture;

/** true if PF_G8 render targets are supported */
extern RHI_API TRHIGlobal<bool> GSupportsRenderTargetFormat_PF_G8;

/** true if PF_FloatRGBA render targets are supported */
extern RHI_API TRHIGlobal<bool> GSupportsRenderTargetFormat_PF_FloatRGBA;

/** true if mobile framebuffer fetch is supported */
extern RHI_API bool GSupportsShaderFramebufferFetch;

/** true if mobile depth & stencil fetch is supported */
extern RHI_API bool GSupportsShaderDepthStencilFetch;

/** true if RQT_AbsoluteTime is supported by RHICreateRenderQuery */
extern RHI_API bool GSupportsTimestampRenderQueries;

/** true if RQT_AbsoluteTime is supported by RHICreateRenderQuery */
extern RHI_API bool GRHISupportsGPUTimestampBubblesRemoval;

/** true if RHIGetGPUFrameCycles removes CPu generated bubbles. */
extern RHI_API bool GRHISupportsFrameCyclesBubblesRemoval;

/** true if the GPU supports hidden surface removal in hardware. */
extern RHI_API bool GHardwareHiddenSurfaceRemoval;

/** true if the RHI supports asynchronous creation of texture resources */
extern RHI_API bool GRHISupportsAsyncTextureCreation;

/** true if the RHI supports quad topology (PT_QuadList). */
extern RHI_API bool GRHISupportsQuadTopology;

/** true if the RHI supports rectangular topology (PT_RectList). */
extern RHI_API bool GRHISupportsRectTopology;

/** true if the RHI supports primitive shaders. */
extern RHI_API bool GRHISupportsPrimitiveShaders;

/** true if the RHI supports 64 bit uint atomics. */
extern RHI_API bool GRHISupportsAtomicUInt64;

/** Temporary. When OpenGL is running in a separate thread, it cannot yet do things like initialize shaders that are first discovered in a rendering task. It is doable, it just isn't done. */
extern RHI_API bool GSupportsParallelRenderingTasksWithSeparateRHIThread;

/** If an RHI is so slow, that it is the limiting factor for the entire frame, we can kick early to try to give it as much as possible. */
extern RHI_API bool GRHIThreadNeedsKicking;

/** If an RHI cannot do an unlimited number of occlusion queries without stalling and waiting for the GPU, this can be used to tune hte occlusion culler to try not to do that. */
extern RHI_API int32 GRHIMaximumReccommendedOustandingOcclusionQueries;

/** Some RHIs can only do visible or not occlusion queries. */
extern RHI_API bool GRHISupportsExactOcclusionQueries;

/** True if and only if the GPU support rendering to volume textures (2D Array, 3D). Some OpenGL 3.3 cards support SM4, but can't render to volume textures. */
extern RHI_API bool GSupportsVolumeTextureRendering;

/** True if the RHI supports separate blend states per render target. */
extern RHI_API bool GSupportsSeparateRenderTargetBlendState;

/** True if the RHI has artifacts with atlased CSM depths. */
extern RHI_API bool GRHINeedsUnatlasedCSMDepthsWorkaround;

/** true if the RHI supports 3D textures */
extern RHI_API bool GSupportsTexture3D;

/** true if the RHI supports mobile multi-view */
extern RHI_API bool GSupportsMobileMultiView;

/** true if the RHI supports image external */
extern RHI_API bool GSupportsImageExternal;

/** true if the RHI supports 256bit MRT */
extern RHI_API bool GSupportsWideMRT;

/** True if the RHI and current hardware supports supports depth bounds testing */
extern RHI_API bool GSupportsDepthBoundsTest;

/** True if the RHI supports explicit access to depth target HTile meta data. */
extern RHI_API bool GRHISupportsExplicitHTile;

/** True if the RHI supports explicit access to MSAA target FMask meta data. */
extern RHI_API bool GRHISupportsExplicitFMask;

/** True if the RHI supports resummarizing depth target HTile meta data. */
extern RHI_API bool GRHISupportsResummarizeHTile;

/** True if the RHI supports depth target unordered access views. */
extern RHI_API bool GRHISupportsDepthUAV;

/** True if the RHI and current hardware supports efficient AsyncCompute (by default we assume false and later we can enable this for more hardware) */
extern RHI_API bool GSupportsEfficientAsyncCompute;

/** True if the RHI supports getting the result of occlusion queries when on a thread other than the render thread */
extern RHI_API bool GSupportsParallelOcclusionQueries;

/** true if the RHI supports aliasing of transient resources */
extern RHI_API bool GSupportsTransientResourceAliasing;

/** true if the RHI requires a valid RT bound during UAV scatter operation inside the pixel shader */
extern RHI_API bool GRHIRequiresRenderTargetForPixelShaderUAVs;

/** true if the RHI supports unordered access view format aliasing */
extern RHI_API bool GRHISupportsUAVFormatAliasing;

/** true if the pointer returned by Lock is a persistent direct pointer to gpu memory */
extern RHI_API bool GRHISupportsDirectGPUMemoryLock;

/** The minimum Z value in clip space for the RHI. */
extern RHI_API float GMinClipZ;

/** The sign to apply to the Y axis of projection matrices. */
extern RHI_API float GProjectionSignY;

/** Does this RHI need to wait for deletion of resources due to ref counting. */
extern RHI_API bool GRHINeedsExtraDeletionLatency;

/** Allow opt-out default RHI resource deletion latency for streaming textures */
extern RHI_API bool GRHIForceNoDeletionLatencyForStreamingTextures;

/** The maximum size allowed for a computeshader dispatch. */
extern RHI_API TRHIGlobal<int32> GMaxComputeDispatchDimension;

/** If true, then avoiding loading shader code and instead force the "native" path, which sends a library and a hash instead. */
extern RHI_API bool GRHILazyShaderCodeLoading;

/** If true, then it is possible to turn on GRHILazyShaderCodeLoading. */
extern RHI_API bool GRHISupportsLazyShaderCodeLoading;

/** The maximum size to allow for the shadow depth buffer in the X dimension.  This must be larger or equal to GMaxShadowDepthBufferSizeY. */
extern RHI_API TRHIGlobal<int32> GMaxShadowDepthBufferSizeX;
/** The maximum size to allow for the shadow depth buffer in the Y dimension. */
extern RHI_API TRHIGlobal<int32> GMaxShadowDepthBufferSizeY;

/** The maximum size allowed for 2D textures in both dimensions. */
extern RHI_API TRHIGlobal<int32> GMaxTextureDimensions;

/** The maximum size allowed for 2D textures in both dimensions. */
extern RHI_API TRHIGlobal<int64> GMaxBufferDimensions;

/** The maximum size allowed for Shared Compute Memory. */
extern RHI_API TRHIGlobal<int64> GMaxComputeSharedMemory;

/** The maximum size allowed for 3D textures in all three dimensions. */
extern RHI_API TRHIGlobal<int32> GMaxVolumeTextureDimensions;

/** Whether RW texture buffers are supported */
extern RHI_API bool GRHISupportsRWTextureBuffers;

FORCEINLINE uint64 GetMaxBufferDimension()
{
	return GMaxBufferDimensions;
}

FORCEINLINE uint64 GetMaxComputeSharedMemory()
{
	return GMaxComputeSharedMemory;
}

FORCEINLINE uint32 GetMax2DTextureDimension()
{
	return GMaxTextureDimensions;
}

/** The maximum size allowed for cube textures. */
extern RHI_API TRHIGlobal<int32> GMaxCubeTextureDimensions;
FORCEINLINE uint32 GetMaxCubeTextureDimension()
{
	return GMaxCubeTextureDimensions;
}

/** The Maximum number of layers in a 1D or 2D texture array. */
extern RHI_API int32 GMaxTextureArrayLayers;
FORCEINLINE uint32 GetMaxTextureArrayLayers()
{
	return GMaxTextureArrayLayers;
}

extern RHI_API int32 GMaxTextureSamplers;
FORCEINLINE uint32 GetMaxTextureSamplers()
{
	return GMaxTextureSamplers;
}

/** The maximum work group invocations allowed for compute shader. */
extern RHI_API TRHIGlobal<int32> GMaxWorkGroupInvocations;
FORCEINLINE uint32 GetMaxWorkGroupInvocations()
{
	return GMaxWorkGroupInvocations;
}

/** true if we are running with the NULL RHI */
extern RHI_API bool GUsingNullRHI;

/**
 *	The size to check against for Draw*UP call vertex counts.
 *	If greater than this value, the draw call will not occur.
 */
extern RHI_API int32 GDrawUPVertexCheckCount;
/**
 *	The size to check against for Draw*UP call index counts.
 *	If greater than this value, the draw call will not occur.
 */
extern RHI_API int32 GDrawUPIndexCheckCount;

/** true for each VET that is supported. One-to-one mapping with EVertexElementType */
extern RHI_API class FVertexElementTypeSupportInfo GVertexElementTypeSupport;

#include "MultiGPU.h"

/** Whether the next frame should profile the GPU. */
extern RHI_API bool GTriggerGPUProfile;

/** Whether we are profiling GPU hitches. */
extern RHI_API bool GTriggerGPUHitchProfile;

/** Non-empty if we are performing a gpu trace. Also says where to place trace file. */
extern RHI_API FString GGPUTraceFileName;

/** True if the RHI supports texture streaming */
extern RHI_API bool GRHISupportsTextureStreaming;
/** Amount of memory allocated by textures. In kilobytes. */
extern RHI_API volatile int32 GCurrentTextureMemorySize;
/** Amount of memory allocated by rendertargets. In kilobytes. */
extern RHI_API volatile int32 GCurrentRendertargetMemorySize;
/** Current texture streaming pool size, in bytes. 0 means unlimited. */
extern RHI_API int64 GTexturePoolSize;

/** In percent. If non-zero, the texture pool size is a percentage of GTotalGraphicsMemory. */
extern RHI_API int32 GPoolSizeVRAMPercentage;

/** Some simple runtime stats, reset on every call to RHIBeginFrame */
/** Num draw calls & primitives on previous frame (accurate on any thread)*/
extern RHI_API int32 GNumDrawCallsRHI[MAX_NUM_GPUS];
extern RHI_API int32 GNumPrimitivesDrawnRHI[MAX_NUM_GPUS];

/** Num draw calls and primitives this frame (only accurate on RenderThread) */
extern RHI_API int32 GCurrentNumDrawCallsRHI[MAX_NUM_GPUS];
extern RHI_API int32(*GCurrentNumDrawCallsRHIPtr)[MAX_NUM_GPUS];
extern RHI_API int32 GCurrentNumPrimitivesDrawnRHI[MAX_NUM_GPUS];

/** Whether or not the RHI can handle a non-zero BaseVertexIndex - extra SetStreamSource calls will be needed if this is false */
extern RHI_API bool GRHISupportsBaseVertexIndex;

/** True if the RHI supports copying cubemap faces using CopyToResolveTarget */
extern RHI_API bool GRHISupportsResolveCubemapFaces;

/** Whether or not the RHI can handle a non-zero FirstInstance to DrawIndexedPrimitive and friends - extra SetStreamSource calls will be needed if this is false */
extern RHI_API bool GRHISupportsFirstInstance;

/** Whether or not the RHI can handle dynamic resolution or not. */
extern RHI_API bool GRHISupportsDynamicResolution;

/** Whether or not the RHI supports ray tracing on current hardware (acceleration structure building and new ray tracing-specific shader types). */
extern RHI_API bool GRHISupportsRayTracing;

/** Whether or not the RHI supports adding new shaders to an existing RT PSO. */
extern RHI_API bool GRHISupportsRayTracingPSOAdditions;

/** Whether or not the RHI supports async building ray tracing acceleration structures. */
extern RHI_API bool GRHISupportsRayTracingAsyncBuildAccelerationStructure;

/** Whether or not the RHI supports the AMD Hit Token extension. */
extern RHI_API bool GRHISupportsRayTracingAMDHitToken;

/** Whether or not the RHI supports shader wave operations (shader model 6.0). */
extern RHI_API bool GRHISupportsWaveOperations;

/** Specifies the minimum and maximum number of lanes in the SIMD wave that this GPU can support. I.e. 32 on NVIDIA, 64 on AMD. Values are in range [4..128]. */
extern RHI_API int32 GRHIMinimumWaveSize;
extern RHI_API int32 GRHIMaximumWaveSize;

/** Whether or not the RHI supports an RHI thread.
Requirements for RHI thread
* Microresources (those in RHIStaticStates.h) need to be able to be created by any thread at any time and be able to work with a radically simplified rhi resource lifecycle. CreateSamplerState, CreateRasterizerState, CreateDepthStencilState, CreateBlendState
* CreateUniformBuffer needs to be threadsafe
* GetRenderQueryResult should be threadsafe, but this isn't required. If it isn't threadsafe, then you need to flush yourself in the RHI
* GetViewportBackBuffer and AdvanceFrameForGetViewportBackBuffer need to be threadsafe and need to support the fact that the render thread has a different concept of "current backbuffer" than the RHI thread. Without an RHIThread this is moot due to the next two items.
* AdvanceFrameForGetViewportBackBuffer needs be added as an RHI method and this needs to work with GetViewportBackBuffer to give the render thread the right back buffer even though many commands relating to the beginning and end of the frame are queued.
* BeginDrawingViewport, and 5 or so other frame advance methods are queued with an RHIThread. Without an RHIThread, these just flush internally.
***/
extern RHI_API bool GRHISupportsRHIThread;
/* as above, but we run the commands on arbitrary task threads */
extern RHI_API bool GRHISupportsRHIOnTaskThread;

/** Whether or not the RHI supports parallel RHIThread executes / translates
Requirements:
* RHICreateBoundShaderState & RHICreateGraphicsPipelineState is threadsafe and GetCachedBoundShaderState must not be used. GetCachedBoundShaderState_Threadsafe has a slightly different protocol.
***/
extern RHI_API bool GRHISupportsParallelRHIExecute;

/** Whether or not the RHI can perform MSAA sample load. */
extern RHI_API bool GRHISupportsMSAADepthSampleAccess;

/** Whether or not the RHI can render to the backbuffer with a custom depth/stencil surface bound. */
extern RHI_API bool GRHISupportsBackBufferWithCustomDepthStencil;

/** Whether or not HDR is currently enabled */
extern RHI_API bool GRHIIsHDREnabled;

/** Whether the present adapter/display offers HDR output capabilities. */
extern RHI_API bool GRHISupportsHDROutput;

/** Whether VRS (in all flavors) is currently enabled (separate from whether it's supported/available). */
extern RHI_API bool GRHIVariableRateShadingEnabled;

/** Whether attachment (image-based) VRS is currently enabled (separate from whether it's supported/available). */
extern RHI_API bool GRHIAttachmentVariableRateShadingEnabled;

/** Whether or not the RHI can support per-draw Variable Rate Shading. */
extern RHI_API bool GRHISupportsPipelineVariableRateShading;

/** Whether or not the RHI can support image-based Variable Rate Shading. */
extern RHI_API bool GRHISupportsAttachmentVariableRateShading;

/** Whether or not the RHI can support complex combiner operatations between per-draw (pipeline) VRS and image VRS. */
extern RHI_API bool GRHISupportsComplexVariableRateShadingCombinerOps;

/** Whether or not the RHI can support shading rate attachments as array textures. */
extern RHI_API bool GRHISupportsVariableRateShadingAttachmentArrayTextures;

/** Maximum tile width in a screen space texture that can be used to drive Variable Rate Shading. */
extern RHI_API int32 GRHIVariableRateShadingImageTileMaxWidth;

/** Maximum tile height in a screen space texture that can be used to drive Variable Rate Shading. */
extern RHI_API int32 GRHIVariableRateShadingImageTileMaxHeight;

/** Minimum tile width in a screen space texture that can be used to drive Variable Rate Shading. */
extern RHI_API int32 GRHIVariableRateShadingImageTileMinWidth;

/** Minimum tile height in a screen space texture that can be used to drive Variable Rate Shading. */
extern RHI_API int32 GRHIVariableRateShadingImageTileMinHeight;

/** Data type contained in a shading-rate image for image-based Variable Rate Shading. */
extern RHI_API EVRSImageDataType GRHIVariableRateShadingImageDataType;

/** Image format for the shading rate image for image-based Variable Rate Shading. */
extern RHI_API EPixelFormat GRHIVariableRateShadingImageFormat;

/** Whether Variable Rate Shading deferred shading rate texture update is supported. */
extern RHI_API bool GRHISupportsLateVariableRateShadingUpdate;

/** Format used for the backbuffer when outputting to a HDR display. */
extern RHI_API EPixelFormat GRHIHDRDisplayOutputFormat;

/** Counter incremented once on each frame present. Used to support game thread synchronization with swap chain frame flips. */
extern RHI_API uint64 GRHIPresentCounter;

/** True if the RHI supports setting the render target array index from any shader stage */
extern RHI_API bool GRHISupportsArrayIndexFromAnyShader;

/** True if the pipeline file cache can be used with this RHI */
extern RHI_API bool GRHISupportsPipelineFileCache;

/** Information about a pixel format. */
struct FPixelFormatInfo
{
	const TCHAR*	Name;
	int32			BlockSizeX,
					BlockSizeY,
					BlockSizeZ,
					BlockBytes,
					NumComponents;
	/** Platform specific token, e.g. D3DFORMAT with D3DDrv										*/
	uint32			PlatformFormat;
	/** Whether the texture format is supported on the current platform/ rendering combination	*/
	bool			Supported;
	EPixelFormat	UnrealFormat;
};

extern RHI_API FPixelFormatInfo GPixelFormats[PF_MAX];		// Maps members of EPixelFormat to a FPixelFormatInfo describing the format.

//
//	CalculateImageBytes
//

extern RHI_API SIZE_T CalculateImageBytes(uint32 SizeX,uint32 SizeY,uint32 SizeZ,uint8 Format);

/** Called once per frame only from within an RHI. */
extern RHI_API void RHIPrivateBeginFrame();


RHI_API FName LegacyShaderPlatformToShaderFormat(EShaderPlatform Platform);
RHI_API EShaderPlatform ShaderFormatToLegacyShaderPlatform(FName ShaderFormat);
RHI_API FName ShaderPlatformToPlatformName(EShaderPlatform Platform);

/**
 * Adjusts a projection matrix to output in the correct clip space for the
 * current RHI. Unreal projection matrices follow certain conventions and
 * need to be patched for some RHIs. All projection matrices should be adjusted
 * before being used for rendering!
 */
inline FMatrix AdjustProjectionMatrixForRHI(const FMatrix& InProjectionMatrix)
{
	FScaleMatrix ClipSpaceFixScale(FVector(1.0f, GProjectionSignY, 1.0f - GMinClipZ));
	FTranslationMatrix ClipSpaceFixTranslate(FVector(0.0f, 0.0f, GMinClipZ));	
	return InProjectionMatrix * ClipSpaceFixScale * ClipSpaceFixTranslate;
}

/** Set runtime selection of mobile feature level preview. */
RHI_API void RHISetMobilePreviewFeatureLevel(ERHIFeatureLevel::Type MobilePreviewFeatureLevel);

/** Current shader platform. */


/** Finds a corresponding ERHIFeatureLevel::Type given an FName, or returns false if one could not be found. */
extern RHI_API bool GetFeatureLevelFromName(FName Name, ERHIFeatureLevel::Type& OutFeatureLevel);

/** Creates a string for the given feature level. */
extern RHI_API void GetFeatureLevelName(ERHIFeatureLevel::Type InFeatureLevel, FString& OutName);

/** Creates an FName for the given feature level. */
extern RHI_API void GetFeatureLevelName(ERHIFeatureLevel::Type InFeatureLevel, FName& OutName);


/** Table for finding out which shader platform corresponds to a given feature level for this RHI. */
extern RHI_API EShaderPlatform GShaderPlatformForFeatureLevel[ERHIFeatureLevel::Num];

/** Get the shader platform associated with the supplied feature level on this machine */
inline EShaderPlatform GetFeatureLevelShaderPlatform(const FStaticFeatureLevel InFeatureLevel)
{
	return GShaderPlatformForFeatureLevel[InFeatureLevel];
}

/** Stringifies EShaderPlatform */
extern RHI_API FString LexToString(EShaderPlatform Platform);

/** Stringifies ERHIFeatureLevel */
extern RHI_API FString LexToString(ERHIFeatureLevel::Type Level);

/** Finds a corresponding ERHIShadingPath::Type given an FName, or returns false if one could not be found. */
extern RHI_API bool GetShadingPathFromName(FName Name, ERHIShadingPath::Type& OutShadingPath);

/** Creates a string for the given shading path. */
extern RHI_API void GetShadingPathName(ERHIShadingPath::Type InShadingPath, FString& OutName);

/** Creates an FName for the given shading path. */
extern RHI_API void GetShadingPathName(ERHIShadingPath::Type InShadingPath, FName& OutName);


enum class ERHIPipeline : uint8
{
	Graphics = 1 << 0,
	AsyncCompute = 1 << 1,

	All = Graphics | AsyncCompute,
	Num = 2
};
ENUM_CLASS_FLAGS(ERHIPipeline)

enum class EResourceTransitionPipeline
{
	EGfxToCompute     UE_DEPRECATED(4.26, "The RHI resource barrier API has been refactored. Use the new RHITransition API. EResourceTransitionPipeline has been replaced with ERHIPipeline bitmask."),
	EComputeToGfx     UE_DEPRECATED(4.26, "The RHI resource barrier API has been refactored. Use the new RHITransition API. EResourceTransitionPipeline has been replaced with ERHIPipeline bitmask."),
	EGfxToGfx         UE_DEPRECATED(4.26, "The RHI resource barrier API has been refactored. Use the new RHITransition API. EResourceTransitionPipeline has been replaced with ERHIPipeline bitmask."),
	EComputeToCompute UE_DEPRECATED(4.26, "The RHI resource barrier API has been refactored. Use the new RHITransition API. EResourceTransitionPipeline has been replaced with ERHIPipeline bitmask.")
};

enum class ERHIAccess
{
	// Used when the previous state of a resource is not known,
	// which implies we have to flush all GPU caches etc.
	Unknown = 0,

	// Read states
	CPURead             = 1 <<  0,
	Present             = 1 <<  1,
	IndirectArgs        = 1 <<  2,
	VertexOrIndexBuffer = 1 <<  3,
	SRVCompute          = 1 <<  4,
	SRVGraphics         = 1 <<  5,
	CopySrc             = 1 <<  6,
	ResolveSrc          = 1 <<  7,
	DSVRead				= 1 <<  8,

	// Read-write states
	UAVCompute          = 1 <<  9,
	UAVGraphics         = 1 << 10,
	RTV                 = 1 << 11,
	CopyDest            = 1 << 12,
	ResolveDst          = 1 << 13,
	DSVWrite            = 1 << 14,

	// Custom (special) states:
	ShadingRateSource	= 1 << 15,

	Last = DSVWrite,
	None = Unknown,
	Mask = (Last << 1) - 1,

	// A mask of the two possible SRV states
	SRVMask = SRVCompute | SRVGraphics,

	// A mask of the two possible UAV states
	UAVMask = UAVCompute | UAVGraphics,

	// A mask of all bits representing read-only states which cannot be combined with other write states.
	ReadOnlyExclusiveMask = CPURead | Present | IndirectArgs | VertexOrIndexBuffer | SRVGraphics | SRVCompute | CopySrc | ResolveSrc,

	// A mask of all bits representing read-only states which may be combined with other write states.
	ReadOnlyMask = ReadOnlyExclusiveMask | DSVRead | ShadingRateSource,

	// A mask of all bits representing readable states which may also include writable states.
	ReadableMask = ReadOnlyMask | UAVMask,

	// A mask of all bits representing write-only states which cannot be combined with other read states.
	WriteOnlyExclusiveMask = RTV | CopyDest | ResolveDst,

	// A mask of all bits representing write-only states which may be combined with other read states.
	WriteOnlyMask = WriteOnlyExclusiveMask | DSVWrite,

	// A mask of all bits representing writable states which may also include readable states.
	WritableMask = WriteOnlyMask | UAVMask,


	// ------------------------------------------
	// Legacy states

	// These are for compatibility with the legacy RHITransitionResources API implementation. Do not use these when writing new rendering code/features.
	EReadable    = ReadOnlyMask, // "Generic read"
	EWritable    = WritableMask, // "Generic write"
	ERWBarrier   = CopySrc | CopyDest | SRVCompute | SRVGraphics | UAVCompute | UAVGraphics, // Mostly for UAVs. Transition to read/write state and always insert a resource barrier.
	ERWNoBarrier = ERWBarrier    // Mostly for UAVs. Indicates we want R/W access and do not require synchronization for the duration of the RW state.  The initial transition from writable->RWNoBarrier and readable->RWNoBarrier still requires a sync.

	//-------------------------------------------
};
ENUM_CLASS_FLAGS(ERHIAccess)

/** Mask of read states that can be used together for textures. */
extern RHI_API ERHIAccess GRHITextureReadAccessMask;

// Helper namespace to maintain compatibility with old renderer code for one engine release.
namespace EResourceTransitionAccess
{
	UE_DEPRECATED(4.26, "The RHI resource barrier API has been refactored. Use the RHITransition API and ERHIAccess to specify explicit previous/next states during resource transitions.")
	static constexpr const ERHIAccess EReadable    = ERHIAccess::EReadable;

	UE_DEPRECATED(4.26, "The RHI resource barrier API has been refactored. Use the RHITransition API and ERHIAccess to specify explicit previous/next states during resource transitions.")
	static constexpr const ERHIAccess EWritable    = ERHIAccess::EWritable;

	UE_DEPRECATED(4.26, "The RHI resource barrier API has been refactored. Use the RHITransition API and ERHIAccess to specify explicit previous/next states during resource transitions.")
	static constexpr const ERHIAccess ERWBarrier   = ERHIAccess::ERWBarrier;

	UE_DEPRECATED(4.26, "The RHI resource barrier API has been refactored. Use the RHITransition API and ERHIAccess to specify explicit previous/next states during resource transitions.")
	static constexpr const ERHIAccess ERWNoBarrier = ERHIAccess::ERWNoBarrier;
};


/** to customize the RHIReadSurfaceData() output */
class FReadSurfaceDataFlags
{
public:
	// @param InCompressionMode defines the value input range that is mapped to output range
	// @param InCubeFace defined which cubemap side is used, only required for cubemap content, then it needs to be a valid side
	FReadSurfaceDataFlags(ERangeCompressionMode InCompressionMode = RCM_UNorm, ECubeFace InCubeFace = CubeFace_MAX) 
		:CubeFace(InCubeFace), CompressionMode(InCompressionMode)
	{
	}

	ECubeFace GetCubeFace() const
	{
		checkSlow(CubeFace <= CubeFace_NegZ);
		return CubeFace;
	}

	ERangeCompressionMode GetCompressionMode() const
	{
		return CompressionMode;
	}

	void SetLinearToGamma(bool Value)
	{
		bLinearToGamma = Value;
	}

	bool GetLinearToGamma() const
	{
		return bLinearToGamma;
	}

	void SetOutputStencil(bool Value)
	{
		bOutputStencil = Value;
	}

	bool GetOutputStencil() const
	{
		return bOutputStencil;
	}

	void SetMip(uint8 InMipLevel)
	{
		MipLevel = InMipLevel;
	}

	uint8 GetMip() const
	{
		return MipLevel;
	}	

	void SetMaxDepthRange(float Value)
	{
		MaxDepthRange = Value;
	}

	float ComputeNormalizedDepth(float DeviceZ) const
	{
		return FMath::Abs(ConvertFromDeviceZ(DeviceZ) / MaxDepthRange);
	}

	void SetGPUIndex(uint32 InGPUIndex)
	{
		GPUIndex = InGPUIndex;
	}

	uint32 GetGPUIndex() const
	{
		return GPUIndex;
	}

	void SetArrayIndex(int32 InArrayIndex)
	{
		ArrayIndex = InArrayIndex;
	}

	int32 GetArrayIndex() const
	{
		return ArrayIndex;
	}

private:

	// @return SceneDepth
	float ConvertFromDeviceZ(float DeviceZ) const
	{
		DeviceZ = FMath::Min(DeviceZ, 1 - Z_PRECISION);

		// for depth to linear conversion
		const FVector2D InvDeviceZToWorldZ(0.1f, 0.1f);

		return 1.0f / (DeviceZ * InvDeviceZToWorldZ.X - InvDeviceZToWorldZ.Y);
	}

	ECubeFace CubeFace = CubeFace_MAX;
	ERangeCompressionMode CompressionMode = RCM_UNorm;
	bool bLinearToGamma = true;	
	float MaxDepthRange = 16000.0f;
	bool bOutputStencil = false;
	uint8 MipLevel = 0;
	int32 ArrayIndex = 0;
	uint32 GPUIndex = 0;
};

/** Info for supporting the vertex element types */
class FVertexElementTypeSupportInfo
{
public:
	FVertexElementTypeSupportInfo() { for(int32 i=0; i<VET_MAX; i++) ElementCaps[i]=true; }
	FORCEINLINE bool IsSupported(EVertexElementType ElementType) { return ElementCaps[ElementType]; }
	FORCEINLINE void SetSupported(EVertexElementType ElementType,bool bIsSupported) { ElementCaps[ElementType]=bIsSupported; }
private:
	/** cap bit set for each VET. One-to-one mapping based on EVertexElementType */
	bool ElementCaps[VET_MAX];
};

struct FVertexElement
{
	uint8 StreamIndex;
	uint8 Offset;
	TEnumAsByte<EVertexElementType> Type;
	uint8 AttributeIndex;
	uint16 Stride;
	/**
	 * Whether to use instance index or vertex index to consume the element.  
	 * eg if bUseInstanceIndex is 0, the element will be repeated for every instance.
	 */
	uint16 bUseInstanceIndex;

	FVertexElement() {}
	FVertexElement(uint8 InStreamIndex,uint8 InOffset,EVertexElementType InType,uint8 InAttributeIndex,uint16 InStride,bool bInUseInstanceIndex = false):
		StreamIndex(InStreamIndex),
		Offset(InOffset),
		Type(InType),
		AttributeIndex(InAttributeIndex),
		Stride(InStride),
		bUseInstanceIndex(bInUseInstanceIndex)
	{}
	/**
	* Suppress the compiler generated assignment operator so that padding won't be copied.
	* This is necessary to get expected results for code that zeros, assigns and then CRC's the whole struct.
	*/
	void operator=(const FVertexElement& Other)
	{
		StreamIndex = Other.StreamIndex;
		Offset = Other.Offset;
		Type = Other.Type;
		AttributeIndex = Other.AttributeIndex;
		Stride = Other.Stride;
		bUseInstanceIndex = Other.bUseInstanceIndex;
	}

	friend FArchive& operator<<(FArchive& Ar,FVertexElement& Element)
	{
		Ar << Element.StreamIndex;
		Ar << Element.Offset;
		Ar << Element.Type;
		Ar << Element.AttributeIndex;
		Ar << Element.Stride;
		Ar << Element.bUseInstanceIndex;
		return Ar;
	}
	RHI_API FString ToString() const;
	RHI_API void FromString(const FString& Src);
	RHI_API void FromString(const FStringView& Src);
};

typedef TArray<FVertexElement,TFixedAllocator<MaxVertexElementCount> > FVertexDeclarationElementList;

/** RHI representation of a single stream out element. */
//#todo-RemoveStreamOut
struct FStreamOutElement
{
	/** Index of the output stream from the geometry shader. */
	uint32 Stream;

	/** Semantic name of the output element as defined in the geometry shader.  This should not contain the semantic number. */
	const ANSICHAR* SemanticName;

	/** Semantic index of the output element as defined in the geometry shader.  For example "TEXCOORD5" in the shader would give a SemanticIndex of 5. */
	uint32 SemanticIndex;

	/** Start component index of the shader output element to stream out. */
	uint8 StartComponent;

	/** Number of components of the shader output element to stream out. */
	uint8 ComponentCount;

	/** Stream output target slot, corresponding to the streams set by RHISetStreamOutTargets. */
	uint8 OutputSlot;

	FStreamOutElement() {}
	FStreamOutElement(uint32 InStream, const ANSICHAR* InSemanticName, uint32 InSemanticIndex, uint8 InComponentCount, uint8 InOutputSlot) :
		Stream(InStream),
		SemanticName(InSemanticName),
		SemanticIndex(InSemanticIndex),
		StartComponent(0),
		ComponentCount(InComponentCount),
		OutputSlot(InOutputSlot)
	{}
};

//#todo-RemoveStreamOut
typedef TArray<FStreamOutElement,TFixedAllocator<MaxVertexElementCount> > FStreamOutElementList;

struct FSamplerStateInitializerRHI
{
	FSamplerStateInitializerRHI() {}
	FSamplerStateInitializerRHI(
		ESamplerFilter InFilter,
		ESamplerAddressMode InAddressU = AM_Wrap,
		ESamplerAddressMode InAddressV = AM_Wrap,
		ESamplerAddressMode InAddressW = AM_Wrap,
		float InMipBias = 0,
		int32 InMaxAnisotropy = 0,
		float InMinMipLevel = 0,
		float InMaxMipLevel = FLT_MAX,
		uint32 InBorderColor = 0,
		/** Only supported in D3D11 */
		ESamplerCompareFunction InSamplerComparisonFunction = SCF_Never
		)
	:	Filter(InFilter)
	,	AddressU(InAddressU)
	,	AddressV(InAddressV)
	,	AddressW(InAddressW)
	,	MipBias(InMipBias)
	,	MinMipLevel(InMinMipLevel)
	,	MaxMipLevel(InMaxMipLevel)
	,	MaxAnisotropy(InMaxAnisotropy)
	,	BorderColor(InBorderColor)
	,	SamplerComparisonFunction(InSamplerComparisonFunction)
	{
	}
	TEnumAsByte<ESamplerFilter> Filter = SF_Point;
	TEnumAsByte<ESamplerAddressMode> AddressU = AM_Wrap;
	TEnumAsByte<ESamplerAddressMode> AddressV = AM_Wrap;
	TEnumAsByte<ESamplerAddressMode> AddressW = AM_Wrap;
	float MipBias = 0.0f;
	/** Smallest mip map level that will be used, where 0 is the highest resolution mip level. */
	float MinMipLevel = 0.0f;
	/** Largest mip map level that will be used, where 0 is the highest resolution mip level. */
	float MaxMipLevel = FLT_MAX;
	int32 MaxAnisotropy = 0;
	uint32 BorderColor = 0;
	TEnumAsByte<ESamplerCompareFunction> SamplerComparisonFunction = SCF_Never;


	RHI_API friend uint32 GetTypeHash(const FSamplerStateInitializerRHI& Initializer);
	RHI_API friend bool operator== (const FSamplerStateInitializerRHI& A, const FSamplerStateInitializerRHI& B);
};

struct FRasterizerStateInitializerRHI
{
	TEnumAsByte<ERasterizerFillMode> FillMode;
	TEnumAsByte<ERasterizerCullMode> CullMode;
	float DepthBias;
	float SlopeScaleDepthBias;
	bool bAllowMSAA;
	bool bEnableLineAA;
	
	friend FArchive& operator<<(FArchive& Ar,FRasterizerStateInitializerRHI& RasterizerStateInitializer)
	{
		Ar << RasterizerStateInitializer.FillMode;
		Ar << RasterizerStateInitializer.CullMode;
		Ar << RasterizerStateInitializer.DepthBias;
		Ar << RasterizerStateInitializer.SlopeScaleDepthBias;
		Ar << RasterizerStateInitializer.bAllowMSAA;
		Ar << RasterizerStateInitializer.bEnableLineAA;
		return Ar;
	}

	RHI_API friend uint32 GetTypeHash(const FRasterizerStateInitializerRHI& Initializer);
	RHI_API friend bool operator== (const FRasterizerStateInitializerRHI& A, const FRasterizerStateInitializerRHI& B);
};

struct FDepthStencilStateInitializerRHI
{
	bool bEnableDepthWrite;
	TEnumAsByte<ECompareFunction> DepthTest;

	bool bEnableFrontFaceStencil;
	TEnumAsByte<ECompareFunction> FrontFaceStencilTest;
	TEnumAsByte<EStencilOp> FrontFaceStencilFailStencilOp;
	TEnumAsByte<EStencilOp> FrontFaceDepthFailStencilOp;
	TEnumAsByte<EStencilOp> FrontFacePassStencilOp;
	bool bEnableBackFaceStencil;
	TEnumAsByte<ECompareFunction> BackFaceStencilTest;
	TEnumAsByte<EStencilOp> BackFaceStencilFailStencilOp;
	TEnumAsByte<EStencilOp> BackFaceDepthFailStencilOp;
	TEnumAsByte<EStencilOp> BackFacePassStencilOp;
	uint8 StencilReadMask;
	uint8 StencilWriteMask;

	FDepthStencilStateInitializerRHI(
		bool bInEnableDepthWrite = true,
		ECompareFunction InDepthTest = CF_LessEqual,
		bool bInEnableFrontFaceStencil = false,
		ECompareFunction InFrontFaceStencilTest = CF_Always,
		EStencilOp InFrontFaceStencilFailStencilOp = SO_Keep,
		EStencilOp InFrontFaceDepthFailStencilOp = SO_Keep,
		EStencilOp InFrontFacePassStencilOp = SO_Keep,
		bool bInEnableBackFaceStencil = false,
		ECompareFunction InBackFaceStencilTest = CF_Always,
		EStencilOp InBackFaceStencilFailStencilOp = SO_Keep,
		EStencilOp InBackFaceDepthFailStencilOp = SO_Keep,
		EStencilOp InBackFacePassStencilOp = SO_Keep,
		uint8 InStencilReadMask = 0xFF,
		uint8 InStencilWriteMask = 0xFF
		)
	: bEnableDepthWrite(bInEnableDepthWrite)
	, DepthTest(InDepthTest)
	, bEnableFrontFaceStencil(bInEnableFrontFaceStencil)
	, FrontFaceStencilTest(InFrontFaceStencilTest)
	, FrontFaceStencilFailStencilOp(InFrontFaceStencilFailStencilOp)
	, FrontFaceDepthFailStencilOp(InFrontFaceDepthFailStencilOp)
	, FrontFacePassStencilOp(InFrontFacePassStencilOp)
	, bEnableBackFaceStencil(bInEnableBackFaceStencil)
	, BackFaceStencilTest(InBackFaceStencilTest)
	, BackFaceStencilFailStencilOp(InBackFaceStencilFailStencilOp)
	, BackFaceDepthFailStencilOp(InBackFaceDepthFailStencilOp)
	, BackFacePassStencilOp(InBackFacePassStencilOp)
	, StencilReadMask(InStencilReadMask)
	, StencilWriteMask(InStencilWriteMask)
	{}
	
	friend FArchive& operator<<(FArchive& Ar,FDepthStencilStateInitializerRHI& DepthStencilStateInitializer)
	{
		Ar << DepthStencilStateInitializer.bEnableDepthWrite;
		Ar << DepthStencilStateInitializer.DepthTest;
		Ar << DepthStencilStateInitializer.bEnableFrontFaceStencil;
		Ar << DepthStencilStateInitializer.FrontFaceStencilTest;
		Ar << DepthStencilStateInitializer.FrontFaceStencilFailStencilOp;
		Ar << DepthStencilStateInitializer.FrontFaceDepthFailStencilOp;
		Ar << DepthStencilStateInitializer.FrontFacePassStencilOp;
		Ar << DepthStencilStateInitializer.bEnableBackFaceStencil;
		Ar << DepthStencilStateInitializer.BackFaceStencilTest;
		Ar << DepthStencilStateInitializer.BackFaceStencilFailStencilOp;
		Ar << DepthStencilStateInitializer.BackFaceDepthFailStencilOp;
		Ar << DepthStencilStateInitializer.BackFacePassStencilOp;
		Ar << DepthStencilStateInitializer.StencilReadMask;
		Ar << DepthStencilStateInitializer.StencilWriteMask;
		return Ar;
	}

	RHI_API friend uint32 GetTypeHash(const FDepthStencilStateInitializerRHI& Initializer);
	RHI_API friend bool operator== (const FDepthStencilStateInitializerRHI& A, const FDepthStencilStateInitializerRHI& B);
	
	RHI_API FString ToString() const;
	RHI_API void FromString(const FString& Src);
	RHI_API void FromString(const FStringView& Src);
};

class FBlendStateInitializerRHI
{
public:

	struct FRenderTarget
	{
		enum
		{
			NUM_STRING_FIELDS = 7
		};
		TEnumAsByte<EBlendOperation> ColorBlendOp;
		TEnumAsByte<EBlendFactor> ColorSrcBlend;
		TEnumAsByte<EBlendFactor> ColorDestBlend;
		TEnumAsByte<EBlendOperation> AlphaBlendOp;
		TEnumAsByte<EBlendFactor> AlphaSrcBlend;
		TEnumAsByte<EBlendFactor> AlphaDestBlend;
		TEnumAsByte<EColorWriteMask> ColorWriteMask;
		
		FRenderTarget(
			EBlendOperation InColorBlendOp = BO_Add,
			EBlendFactor InColorSrcBlend = BF_One,
			EBlendFactor InColorDestBlend = BF_Zero,
			EBlendOperation InAlphaBlendOp = BO_Add,
			EBlendFactor InAlphaSrcBlend = BF_One,
			EBlendFactor InAlphaDestBlend = BF_Zero,
			EColorWriteMask InColorWriteMask = CW_RGBA
			)
		: ColorBlendOp(InColorBlendOp)
		, ColorSrcBlend(InColorSrcBlend)
		, ColorDestBlend(InColorDestBlend)
		, AlphaBlendOp(InAlphaBlendOp)
		, AlphaSrcBlend(InAlphaSrcBlend)
		, AlphaDestBlend(InAlphaDestBlend)
		, ColorWriteMask(InColorWriteMask)
		{}
		
		friend FArchive& operator<<(FArchive& Ar,FRenderTarget& RenderTarget)
		{
			Ar << RenderTarget.ColorBlendOp;
			Ar << RenderTarget.ColorSrcBlend;
			Ar << RenderTarget.ColorDestBlend;
			Ar << RenderTarget.AlphaBlendOp;
			Ar << RenderTarget.AlphaSrcBlend;
			Ar << RenderTarget.AlphaDestBlend;
			Ar << RenderTarget.ColorWriteMask;
			return Ar;
		}
		
		RHI_API FString ToString() const;
		RHI_API void FromString(const TArray<FString>& Parts, int32 Index);
		RHI_API void FromString(TArrayView<const FStringView> Parts);
	};

	FBlendStateInitializerRHI() {}

	FBlendStateInitializerRHI(const FRenderTarget& InRenderTargetBlendState, bool bInUseAlphaToCoverage = false)
	:	bUseIndependentRenderTargetBlendStates(false)
	,	bUseAlphaToCoverage(bInUseAlphaToCoverage)
	{
		RenderTargets[0] = InRenderTargetBlendState;
	}

	template<uint32 NumRenderTargets>
	FBlendStateInitializerRHI(const TStaticArray<FRenderTarget,NumRenderTargets>& InRenderTargetBlendStates, bool bInUseAlphaToCoverage = false)
	:	bUseIndependentRenderTargetBlendStates(NumRenderTargets > 1)
	,	bUseAlphaToCoverage(bInUseAlphaToCoverage)
	{
		static_assert(NumRenderTargets <= MaxSimultaneousRenderTargets, "Too many render target blend states.");

		for(uint32 RenderTargetIndex = 0;RenderTargetIndex < NumRenderTargets;++RenderTargetIndex)
		{
			RenderTargets[RenderTargetIndex] = InRenderTargetBlendStates[RenderTargetIndex];
		}
	}

	TStaticArray<FRenderTarget,MaxSimultaneousRenderTargets> RenderTargets;
	bool bUseIndependentRenderTargetBlendStates;
	bool bUseAlphaToCoverage;
	
	friend FArchive& operator<<(FArchive& Ar,FBlendStateInitializerRHI& BlendStateInitializer)
	{
		Ar << BlendStateInitializer.RenderTargets;
		Ar << BlendStateInitializer.bUseIndependentRenderTargetBlendStates;
		Ar << BlendStateInitializer.bUseAlphaToCoverage;
		return Ar;
	}

	RHI_API friend uint32 GetTypeHash(const FBlendStateInitializerRHI::FRenderTarget& RenderTarget);
	RHI_API friend bool operator== (const FBlendStateInitializerRHI::FRenderTarget& A, const FBlendStateInitializerRHI::FRenderTarget& B);
	
	RHI_API friend uint32 GetTypeHash(const FBlendStateInitializerRHI& Initializer);
	RHI_API friend bool operator== (const FBlendStateInitializerRHI& A, const FBlendStateInitializerRHI& B);
	
	RHI_API FString ToString() const;
	RHI_API void FromString(const FString& Src);
	RHI_API void FromString(const FStringView& Src);
};

/**
 *	Screen Resolution
 */
struct FScreenResolutionRHI
{
	uint32	Width;
	uint32	Height;
	uint32	RefreshRate;
};

/**
 *	Viewport bounds structure to set multiple view ports for the geometry shader
 *  (needs to be 1:1 to the D3D11 structure)
 */
struct FViewportBounds
{
	float	TopLeftX;
	float	TopLeftY;
	float	Width;
	float	Height;
	float	MinDepth;
	float	MaxDepth;

	FViewportBounds() {}

	FViewportBounds(float InTopLeftX, float InTopLeftY, float InWidth, float InHeight, float InMinDepth = 0.0f, float InMaxDepth = 1.0f)
		:TopLeftX(InTopLeftX), TopLeftY(InTopLeftY), Width(InWidth), Height(InHeight), MinDepth(InMinDepth), MaxDepth(InMaxDepth)
	{
	}
};


typedef TArray<FScreenResolutionRHI>	FScreenResolutionArray;

struct FVRamAllocation
{
	FVRamAllocation(uint32 InAllocationStart = 0, uint32 InAllocationSize = 0)
		: AllocationStart(InAllocationStart)
		, AllocationSize(InAllocationSize)
	{
	}

	bool IsValid() const { return AllocationSize > 0; }

	// in bytes
	uint32 AllocationStart;
	// in bytes
	uint32 AllocationSize;
};

struct FRHIResourceInfo
{
	FVRamAllocation VRamAllocation;
};

enum class EClearBinding
{
	ENoneBound, //no clear color associated with this target.  Target will not do hardware clears on most platforms
	EColorBound, //target has a clear color bound.  Clears will use the bound color, and do hardware clears.
	EDepthStencilBound, //target has a depthstencil value bound.  Clears will use the bound values and do hardware clears.
};


enum class EColorSpaceAndEOTF
{
	EUnknown = 0,

	EColorSpace_Rec709  = 1,		// Color Space Uses Rec 709  Primaries
	EColorSpace_Rec2020 = 2,		// Color Space Uses Rec 2020 Primaries
	EColorSpace_DCIP3   = 3,		// Color Space Uses DCI-P3   Primaries
	EEColorSpace_MASK   = 0xf,

	EEOTF_Linear		= 1 << 4,   // Transfer Function Uses Linear Encoding
	EEOTF_sRGB			= 2 << 4,	// Transfer Function Uses sRGB Encoding
	EEOTF_PQ			= 3 << 4,	// Transfer Function Uses PQ Encoding
	EEOTF_MASK			= 0xf << 4,

	ERec709_sRGB		= EColorSpace_Rec709  | EEOTF_sRGB,
	ERec709_Linear		= EColorSpace_Rec709  | EEOTF_Linear,
	
	ERec2020_PQ			= EColorSpace_Rec2020 | EEOTF_PQ,
	ERec2020_Linear		= EColorSpace_Rec2020 | EEOTF_Linear,
	
	EDCIP3_PQ			= EColorSpace_DCIP3 | EEOTF_PQ,
	EDCIP3_Linear		= EColorSpace_DCIP3 | EEOTF_Linear,
	
};


struct FClearValueBinding
{
	struct DSVAlue
	{
		float Depth;
		uint32 Stencil;
	};

	FClearValueBinding()
		: ColorBinding(EClearBinding::EColorBound)
	{
		Value.Color[0] = 0.0f;
		Value.Color[1] = 0.0f;
		Value.Color[2] = 0.0f;
		Value.Color[3] = 0.0f;
	}

	FClearValueBinding(EClearBinding NoBinding)
		: ColorBinding(NoBinding)
	{
		check(ColorBinding == EClearBinding::ENoneBound);
	}

	explicit FClearValueBinding(const FLinearColor& InClearColor)
		: ColorBinding(EClearBinding::EColorBound)
	{
		Value.Color[0] = InClearColor.R;
		Value.Color[1] = InClearColor.G;
		Value.Color[2] = InClearColor.B;
		Value.Color[3] = InClearColor.A;
	}

	explicit FClearValueBinding(float DepthClearValue, uint32 StencilClearValue = 0)
		: ColorBinding(EClearBinding::EDepthStencilBound)
	{
		Value.DSValue.Depth = DepthClearValue;
		Value.DSValue.Stencil = StencilClearValue;
	}

	FLinearColor GetClearColor() const
	{
		ensure(ColorBinding == EClearBinding::EColorBound);
		return FLinearColor(Value.Color[0], Value.Color[1], Value.Color[2], Value.Color[3]);
	}

	void GetDepthStencil(float& OutDepth, uint32& OutStencil) const
	{
		ensure(ColorBinding == EClearBinding::EDepthStencilBound);
		OutDepth = Value.DSValue.Depth;
		OutStencil = Value.DSValue.Stencil;
	}

	bool operator==(const FClearValueBinding& Other) const
	{
		if (ColorBinding == Other.ColorBinding)
		{
			if (ColorBinding == EClearBinding::EColorBound)
			{
				return
					Value.Color[0] == Other.Value.Color[0] &&
					Value.Color[1] == Other.Value.Color[1] &&
					Value.Color[2] == Other.Value.Color[2] &&
					Value.Color[3] == Other.Value.Color[3];

			}
			if (ColorBinding == EClearBinding::EDepthStencilBound)
			{
				return
					Value.DSValue.Depth == Other.Value.DSValue.Depth &&
					Value.DSValue.Stencil == Other.Value.DSValue.Stencil;
			}
			return true;
		}
		return false;
	}

	EClearBinding ColorBinding;

	union ClearValueType
	{
		float Color[4];
		DSVAlue DSValue;
	} Value;

	// common clear values
	static RHI_API const FClearValueBinding None;
	static RHI_API const FClearValueBinding Black;
	static RHI_API const FClearValueBinding BlackMaxAlpha;
	static RHI_API const FClearValueBinding White;
	static RHI_API const FClearValueBinding Transparent;
	static RHI_API const FClearValueBinding DepthOne;
	static RHI_API const FClearValueBinding DepthZero;
	static RHI_API const FClearValueBinding DepthNear;
	static RHI_API const FClearValueBinding DepthFar;	
	static RHI_API const FClearValueBinding Green;
	static RHI_API const FClearValueBinding DefaultNormal8Bit;
};

struct FRHIResourceCreateInfo
{
	FRHIResourceCreateInfo()
		: BulkData(nullptr)
		, ResourceArray(nullptr)
		, ClearValueBinding(FLinearColor::Transparent)
		, GPUMask(FRHIGPUMask::All())
		, bWithoutNativeResource(false)
		, DebugName(nullptr)
		, ExtData(0)
	{}

	// for CreateTexture calls
	FRHIResourceCreateInfo(FResourceBulkDataInterface* InBulkData)
		: FRHIResourceCreateInfo()
	{
		BulkData = InBulkData;
	}

	// for CreateVertexBuffer/CreateStructuredBuffer calls
	FRHIResourceCreateInfo(FResourceArrayInterface* InResourceArray)
		: FRHIResourceCreateInfo()
	{
		ResourceArray = InResourceArray;
	}

	FRHIResourceCreateInfo(const FClearValueBinding& InClearValueBinding)
		: FRHIResourceCreateInfo()
	{
		ClearValueBinding = InClearValueBinding;
	}

	FRHIResourceCreateInfo(const TCHAR* InDebugName)
		: FRHIResourceCreateInfo()
	{
		DebugName = InDebugName;
	}

	FRHIResourceCreateInfo(uint32 InExtData)
		: FRHIResourceCreateInfo()
	{
		ExtData = InExtData;
	}

	// for CreateTexture calls
	FResourceBulkDataInterface* BulkData;
	// for CreateVertexBuffer/CreateStructuredBuffer calls
	FResourceArrayInterface* ResourceArray;

	// for binding clear colors to render targets.
	FClearValueBinding ClearValueBinding;

	// set of GPUs on which to create the resource
	FRHIGPUMask GPUMask;

	// whether to create an RHI object with no underlying resource
	bool bWithoutNativeResource;
	const TCHAR* DebugName;

	// optional data that would have come from an offline cooker or whatever - general purpose
	uint32 ExtData;
};

enum ERHITextureSRVOverrideSRGBType
{
	SRGBO_Default,
	SRGBO_ForceDisable,
};

struct FRHITextureSRVCreateInfo
{
	explicit FRHITextureSRVCreateInfo(uint8 InMipLevel = 0u, uint8 InNumMipLevels = 1u, uint8 InFormat = PF_Unknown)
		: Format(InFormat)
		, MipLevel(InMipLevel)
		, NumMipLevels(InNumMipLevels)
		, SRGBOverride(SRGBO_Default)
		, FirstArraySlice(0)
		, NumArraySlices(0)
	{}

	explicit FRHITextureSRVCreateInfo(uint8 InMipLevel, uint8 InNumMipLevels, uint32 InFirstArraySlice, uint32 InNumArraySlices, uint8 InFormat = PF_Unknown)
		: Format(InFormat)
		, MipLevel(InMipLevel)
		, NumMipLevels(InNumMipLevels)
		, SRGBOverride(SRGBO_Default)
		, FirstArraySlice(InFirstArraySlice)
		, NumArraySlices(InNumArraySlices)
	{}

	/** View the texture with a different format. Leave as PF_Unknown to use original format. Useful when sampling stencil */
	uint8 Format;

	/** Specify the mip level to use. Useful when rendering to one mip while sampling from another */
	uint8 MipLevel;

	/** Create a view to a single, or multiple mip levels */
	uint8 NumMipLevels;

	/** Potentially override the texture's sRGB flag */
	ERHITextureSRVOverrideSRGBType SRGBOverride;

	/** Specify first array slice index. By default 0. */
	uint32 FirstArraySlice;

	/** Specify number of array slices. If FirstArraySlice and NumArraySlices are both zero, the SRV is created for all array slices. By default 0. */
	uint32 NumArraySlices;

	FORCEINLINE bool operator==(const FRHITextureSRVCreateInfo& Other)const
	{
		return (
			Format == Other.Format &&
			MipLevel == Other.MipLevel &&
			NumMipLevels == Other.NumMipLevels &&
			SRGBOverride == Other.SRGBOverride &&
			FirstArraySlice == Other.FirstArraySlice &&
			NumArraySlices == Other.NumArraySlices);
	}

	FORCEINLINE bool operator!=(const FRHITextureSRVCreateInfo& Other)const
	{
		return !(*this == Other);
	}
};

FORCEINLINE uint32 GetTypeHash(const FRHITextureSRVCreateInfo& Var)
{
	uint32 Hash0 = uint32(Var.Format) | uint32(Var.MipLevel) << 8 | uint32(Var.NumMipLevels) << 16 | uint32(Var.SRGBOverride) << 24;
	return HashCombine(HashCombine(GetTypeHash(Hash0), GetTypeHash(Var.FirstArraySlice)), GetTypeHash(Var.NumArraySlices));
}

struct FResolveRect
{
	int32 X1;
	int32 Y1;
	int32 X2;
	int32 Y2;

	// e.g. for a a full 256 x 256 area starting at (0, 0) it would be 
	// the values would be 0, 0, 256, 256
	FResolveRect(int32 InX1=-1, int32 InY1=-1, int32 InX2=-1, int32 InY2=-1)
	:	X1(InX1)
	,	Y1(InY1)
	,	X2(InX2)
	,	Y2(InY2)
	{}

	FResolveRect(const FResolveRect& Other)
		: X1(Other.X1)
		, Y1(Other.Y1)
		, X2(Other.X2)
		, Y2(Other.Y2)
	{}

	explicit FResolveRect(FIntRect Other)
		: X1(Other.Min.X)
		, Y1(Other.Min.Y)
		, X2(Other.Max.X)
		, Y2(Other.Max.Y)
	{}

	bool operator==(FResolveRect Other) const
	{
		return X1 == Other.X1 && Y1 == Other.Y1 && X2 == Other.X2 && Y2 == Other.Y2;
	}

	bool operator!=(FResolveRect Other) const
	{
		return !(*this == Other);
	}

	bool IsValid() const
	{
		return X1 >= 0 && Y1 >= 0 && X2 - X1 > 0 && Y2 - Y1 > 0;
	}
};

struct FResolveParams
{
	/** used to specify face when resolving to a cube map texture */
	ECubeFace CubeFace;
	/** resolve RECT bounded by [X1,Y1]..[X2,Y2]. Or -1 for fullscreen */
	FResolveRect Rect;
	FResolveRect DestRect;
	/** The mip index to resolve in both source and dest. */
	int32 MipIndex;
	/** Array index to resolve in the source. */
	int32 SourceArrayIndex;
	/** Array index to resolve in the dest. */
	int32 DestArrayIndex;
	/** States to transition to at the end of the resolve operation. */
	ERHIAccess SourceAccessFinal = ERHIAccess::EReadable;
	ERHIAccess DestAccessFinal = ERHIAccess::EReadable;

	/** constructor */
	FResolveParams(
		const FResolveRect& InRect = FResolveRect(), 
		ECubeFace InCubeFace = CubeFace_PosX,
		int32 InMipIndex = 0,
		int32 InSourceArrayIndex = 0,
		int32 InDestArrayIndex = 0,
		const FResolveRect& InDestRect = FResolveRect())
		:	CubeFace(InCubeFace)
		,	Rect(InRect)
		,	DestRect(InDestRect)
		,	MipIndex(InMipIndex)
		,	SourceArrayIndex(InSourceArrayIndex)
		,	DestArrayIndex(InDestArrayIndex)
	{}

	FORCEINLINE FResolveParams(const FResolveParams& Other)
		: CubeFace(Other.CubeFace)
		, Rect(Other.Rect)
		, DestRect(Other.DestRect)
		, MipIndex(Other.MipIndex)
		, SourceArrayIndex(Other.SourceArrayIndex)
		, DestArrayIndex(Other.DestArrayIndex)
		, SourceAccessFinal(Other.SourceAccessFinal)
		, DestAccessFinal(Other.DestAccessFinal)
	{}
};


struct FRHICopyTextureInfo
{
	// Number of texels to copy. By default it will copy the whole resource if no size is specified.
	FIntVector Size = FIntVector::ZeroValue;

	// Position of the copy from the source texture/to destination texture
	FIntVector SourcePosition = FIntVector::ZeroValue;
	FIntVector DestPosition = FIntVector::ZeroValue;

	uint32 SourceSliceIndex = 0;
	uint32 DestSliceIndex = 0;
	uint32 NumSlices = 1;

	// Mips to copy and destination mips
	uint32 SourceMipIndex = 0;
	uint32 DestMipIndex = 0;
	uint32 NumMips = 1;
};

inline constexpr bool IsLegacyAccess(ERHIAccess Access)
{
	switch (Access)
	{
	case ERHIAccess::EReadable:
	case ERHIAccess::EWritable:
	case ERHIAccess::ERWBarrier:
		return true;
	}
	return false;
}

inline constexpr bool IsReadOnlyAccess(ERHIAccess Access)
{
	return EnumHasAnyFlags(Access, ERHIAccess::ReadOnlyMask) && !EnumHasAnyFlags(Access, ~ERHIAccess::ReadOnlyMask);
}

inline constexpr bool IsWriteOnlyAccess(ERHIAccess Access)
{
	return EnumHasAnyFlags(Access, ERHIAccess::WriteOnlyMask) && !EnumHasAnyFlags(Access, ~ERHIAccess::WriteOnlyMask);
}

inline constexpr bool IsWritableAccess(ERHIAccess Access)
{
	return EnumHasAnyFlags(Access, ERHIAccess::WritableMask);
}

inline constexpr bool IsReadableAccess(ERHIAccess Access)
{
	return EnumHasAnyFlags(Access, ERHIAccess::ReadableMask);
}

inline constexpr bool IsInvalidAccess(ERHIAccess Access)
{
	return
		((EnumHasAnyFlags(Access, ERHIAccess::ReadOnlyExclusiveMask) && EnumHasAnyFlags(Access, ERHIAccess::WritableMask)) ||
		 (EnumHasAnyFlags(Access, ERHIAccess::WriteOnlyExclusiveMask) && EnumHasAnyFlags(Access, ERHIAccess::ReadableMask))) &&
		!IsLegacyAccess(Access);
}

inline constexpr bool IsValidAccess(ERHIAccess Access)
{
	return !IsInvalidAccess(Access);
}

inline ERHIAccess RHIDecayResourceAccess(ERHIAccess AccessMask, ERHIAccess RequiredAccess, bool bAllowUAVOverlap)
{
	using T = __underlying_type(ERHIAccess);
	checkf((T(RequiredAccess) & (T(RequiredAccess) - 1)) == 0, TEXT("Only one required access bit may be set at once."));

	if (!bAllowUAVOverlap && EnumHasAnyFlags(RequiredAccess, ERHIAccess::UAVMask))
	{
		// UAV writes decay to no allowed resource access when overlaps are disabled. A barrier is always required after the dispatch/draw.
		return ERHIAccess::None;
	}
	
	// Handle DSV modes
	if (EnumHasAnyFlags(RequiredAccess, ERHIAccess::DSVWrite))
	{
		constexpr ERHIAccess CompatibleStates =
			ERHIAccess::DSVRead |
			ERHIAccess::DSVWrite;

		return AccessMask & CompatibleStates;
	}
	if (EnumHasAnyFlags(RequiredAccess, ERHIAccess::DSVRead))
	{
		constexpr ERHIAccess CompatibleStates =
			ERHIAccess::DSVRead |
			ERHIAccess::DSVWrite |
			ERHIAccess::SRVGraphics |
			ERHIAccess::SRVCompute;

		return AccessMask & CompatibleStates;
	}

	if (EnumHasAnyFlags(RequiredAccess, ERHIAccess::WritableMask))
	{
		// Decay to only 1 allowed state for all other writable states.
		return RequiredAccess; 
	}

	// Else, the state is readable. All readable states are compatible.
	return AccessMask;
}

enum class ERHICreateTransitionFlags
{
	None = 0,

	// Disables fencing between pipelines during the transition.
	NoFence = 1 << 0,

	// Indicates the transition will have no useful work between the Begin/End calls,
	// so should use a partial flush rather than a fence as this is more optimal.
	NoSplit = 1 << 1
};
ENUM_CLASS_FLAGS(ERHICreateTransitionFlags);

enum class EResourceTransitionFlags
{
	None                = 0,

	MaintainCompression = 1 << 0, // Specifies that the transition should not decompress the resource, allowing us to read a compressed resource directly in its compressed state.

	Last = MaintainCompression,
	Mask = (Last << 1) - 1
};
ENUM_CLASS_FLAGS(EResourceTransitionFlags);

RHI_API FString GetRHIAccessName(ERHIAccess Access);
RHI_API FString GetResourceTransitionFlagsName(EResourceTransitionFlags Flags);
RHI_API FString GetRHIPipelineName(ERHIPipeline Pipeline);

// The size in bytes of the storage required by the platform RHI for each resource transition.
extern RHI_API uint64 GRHITransitionPrivateData_SizeInBytes;
extern RHI_API uint64 GRHITransitionPrivateData_AlignInBytes;

struct FRHISubresourceRange
{
	static const uint32 kDepthPlaneSlice = 0;
	static const uint32 kStencilPlaneSlice = 1;
	static const uint32 kAllSubresources = TNumericLimits<uint32>::Max();

	uint32 MipIndex = kAllSubresources;
	uint32 ArraySlice = kAllSubresources;
	uint32 PlaneSlice = kAllSubresources;

	FRHISubresourceRange() = default;

	FRHISubresourceRange(
		uint32 InMipIndex,
		uint32 InArraySlice,
		uint32 InPlaneSlice)
		: MipIndex(InMipIndex)
		, ArraySlice(InArraySlice)
		, PlaneSlice(InPlaneSlice)
	{}

	inline bool IsAllMips() const
	{
		return MipIndex == kAllSubresources;
	}

	inline bool IsAllArraySlices() const
	{
		return ArraySlice == kAllSubresources;
	}

	inline bool IsAllPlaneSlices() const
	{
		return PlaneSlice == kAllSubresources;
	}

	inline bool IsWholeResource() const
	{
		return IsAllMips() && IsAllArraySlices() && IsAllPlaneSlices();
	}

	inline bool IgnoreDepthPlane() const
	{
		return PlaneSlice == kStencilPlaneSlice;
	}

	inline bool IgnoreStencilPlane() const
	{
		return PlaneSlice == kDepthPlaneSlice;
	}

	inline bool operator == (FRHISubresourceRange const& RHS) const
	{
		return MipIndex == RHS.MipIndex
			&& ArraySlice == RHS.ArraySlice
			&& PlaneSlice == RHS.PlaneSlice;
	}

	inline bool operator != (FRHISubresourceRange const& RHS) const
	{
		return !(*this == RHS);
	}
};

struct FRHITransitionInfo : public FRHISubresourceRange
{
	union
	{
		class FRHIResource* Resource = nullptr;
		class FRHITexture* Texture;
		class FRHIVertexBuffer* VertexBuffer;
		class FRHIIndexBuffer* IndexBuffer;
		class FRHIStructuredBuffer* StructuredBuffer;
		class FRHIUnorderedAccessView* UAV;
	};

	enum class EType : uint8
	{
		Unknown,
		Texture,
		VertexBuffer,
		IndexBuffer,
		StructuredBuffer,
		UAV
	} Type = EType::Unknown;

	ERHIAccess AccessBefore = ERHIAccess::Unknown;
	ERHIAccess AccessAfter = ERHIAccess::Unknown;
	EResourceTransitionFlags Flags = EResourceTransitionFlags::None;

	FRHITransitionInfo() = default;

	FRHITransitionInfo(
		class FRHITexture* InTexture,
		ERHIAccess InPreviousState,
		ERHIAccess InNewState,
		EResourceTransitionFlags InFlags = EResourceTransitionFlags::None,
		uint32 InMipIndex = kAllSubresources,
		uint32 InArraySlice = kAllSubresources,
		uint32 InPlaneSlice = kAllSubresources)
		: FRHISubresourceRange(InMipIndex, InArraySlice, InPlaneSlice)
		, Texture(InTexture)
		, Type(EType::Texture)
		, AccessBefore(InPreviousState)
		, AccessAfter(InNewState)
		, Flags(InFlags)
	{}

	FRHITransitionInfo(class FRHIUnorderedAccessView* InUAV, ERHIAccess InPreviousState, ERHIAccess InNewState, EResourceTransitionFlags InFlags = EResourceTransitionFlags::None)
		: UAV(InUAV)
		, Type(EType::UAV)
		, AccessBefore(InPreviousState)
		, AccessAfter(InNewState)
		, Flags(InFlags)
	{}

	inline bool operator == (FRHITransitionInfo const& RHS) const
	{
		return Resource == RHS.Resource
			&& Type == RHS.Type
			&& AccessBefore == RHS.AccessBefore
			&& AccessAfter == RHS.AccessAfter
			&& Flags == RHS.Flags
			&& FRHISubresourceRange::operator==(RHS);
	}

	inline bool operator != (FRHITransitionInfo const& RHS) const
	{
		return !(*this == RHS);
	}
};

#include "RHIValidationCommon.h"

// Opaque data structure used to represent a pending resource transition in the RHI.
struct FRHITransition
{
public:
	template <typename T>
	inline T* GetPrivateData()
	{
		checkSlow(sizeof(T) == GRHITransitionPrivateData_SizeInBytes && GRHITransitionPrivateData_AlignInBytes != 0);
		uintptr_t Addr = Align(uintptr_t(this + 1), GRHITransitionPrivateData_AlignInBytes);
		checkSlow(Addr + GRHITransitionPrivateData_SizeInBytes - (uintptr_t)this == GetTotalAllocationSize());
		return reinterpret_cast<T*>(Addr);
	}

	template <typename T>
	inline const T* GetPrivateData() const
	{
		return const_cast<FRHITransition*>(this)->GetPrivateData<T>();
	}

private:
	// Prevent copying and moving. Only pointers to these structures are allowed.
	FRHITransition(const FRHITransition&) = delete;
	FRHITransition(FRHITransition&&) = delete;

	// Private constructor. Memory for transitions is allocated manually with extra space at the tail of the structure for RHI use.
	FRHITransition(ERHIPipeline SrcPipelines, ERHIPipeline DstPipelines)
		: State(int8(SrcPipelines) | (int8(DstPipelines) << int32(ERHIPipeline::Num)))
#if DO_CHECK
		, AllowedSrc(SrcPipelines)
		, AllowedDst(DstPipelines)
#endif
	{}

	~FRHITransition()
	{}

	// Give private access to specific functions/RHI commands that need to allocate or control transitions.
	friend const FRHITransition* RHICreateTransition(ERHIPipeline, ERHIPipeline, ERHICreateTransitionFlags, TArrayView<const struct FRHITransitionInfo>);
	friend class FRHIComputeCommandList;
	friend struct FRHICommandBeginTransitions;
	friend struct FRHICommandEndTransitions;
	friend struct FRHICommandResourceTransition;

	static uint64 GetTotalAllocationSize()
	{
		// Allocate extra space at the end of this structure for private RHI use. This is determined by GRHITransitionPrivateData_SizeInBytes.
		return Align(sizeof(FRHITransition), FMath::Max(GRHITransitionPrivateData_AlignInBytes, 1ull)) + GRHITransitionPrivateData_SizeInBytes;
	}

	static uint64 GetAlignment()
	{
		return FMath::Max((uint64)alignof(FRHITransition), GRHITransitionPrivateData_AlignInBytes);
	}

	inline void MarkBegin(ERHIPipeline Pipeline) const
	{
		checkf(EnumHasAllFlags(Pipeline, AllowedSrc), TEXT("Transition is being used on a source pipeline that it wasn't created for."));

		int8 Mask = int8(Pipeline);
		int8 PreviousValue = FPlatformAtomics::InterlockedAnd(&State, ~Mask);
		checkf((PreviousValue & Mask) == Mask, TEXT("RHIBeginTransitions has been called twice on this transition for at least one pipeline."));

		if (PreviousValue == Mask)
		{
			Cleanup();
		}
	}

	inline void MarkEnd(ERHIPipeline Pipeline) const
	{
		checkf(EnumHasAllFlags(Pipeline, AllowedDst), TEXT("Transition is being used on a destination pipeline that it wasn't created for."));

		int8 Mask = int8(Pipeline) << int32(ERHIPipeline::Num);
		int8 PreviousValue = FPlatformAtomics::InterlockedAnd(&State, ~Mask);
		checkf((PreviousValue & Mask) == Mask, TEXT("RHIEndTransitions has been called twice on this transition for at least one pipeline."));

		if (PreviousValue == Mask)
		{
			Cleanup();
		}
	}

	inline void Cleanup() const;

	mutable int8 State;
	static_assert((int32(ERHIPipeline::Num) * 2) < (sizeof(State) * 8), "Not enough bits to hold pipeline state.");

#if DO_CHECK
	mutable ERHIPipeline AllowedSrc;
	mutable ERHIPipeline AllowedDst;
#endif

#if ENABLE_RHI_VALIDATION
	friend class FValidationRHI;
	friend class FValidationComputeContext;
	friend class FValidationContext;

	RHIValidation::FFence* Fence = nullptr;
	RHIValidation::FOperationsList PendingOperationsBegin;
	RHIValidation::FOperationsList PendingOperationsEnd;
#endif
};

/** specifies an update region for a texture */
struct FUpdateTextureRegion2D
{
	/** offset in texture */
	uint32 DestX;
	uint32 DestY;
	
	/** offset in source image data */
	int32 SrcX;
	int32 SrcY;
	
	/** size of region to copy */
	uint32 Width;
	uint32 Height;

	FUpdateTextureRegion2D()
	{}

	FUpdateTextureRegion2D(uint32 InDestX, uint32 InDestY, int32 InSrcX, int32 InSrcY, uint32 InWidth, uint32 InHeight)
	:	DestX(InDestX)
	,	DestY(InDestY)
	,	SrcX(InSrcX)
	,	SrcY(InSrcY)
	,	Width(InWidth)
	,	Height(InHeight)
	{}
};

/** specifies an update region for a texture */
struct FUpdateTextureRegion3D
{
	/** offset in texture */
	uint32 DestX;
	uint32 DestY;
	uint32 DestZ;

	/** offset in source image data */
	int32 SrcX;
	int32 SrcY;
	int32 SrcZ;

	/** size of region to copy */
	uint32 Width;
	uint32 Height;
	uint32 Depth;

	FUpdateTextureRegion3D()
	{}

	FUpdateTextureRegion3D(uint32 InDestX, uint32 InDestY, uint32 InDestZ, int32 InSrcX, int32 InSrcY, int32 InSrcZ, uint32 InWidth, uint32 InHeight, uint32 InDepth)
	:	DestX(InDestX)
	,	DestY(InDestY)
	,	DestZ(InDestZ)
	,	SrcX(InSrcX)
	,	SrcY(InSrcY)
	,	SrcZ(InSrcZ)
	,	Width(InWidth)
	,	Height(InHeight)
	,	Depth(InDepth)
	{}

	FUpdateTextureRegion3D(FIntVector InDest, FIntVector InSource, FIntVector InSourceSize)
		: DestX(InDest.X)
		, DestY(InDest.Y)
		, DestZ(InDest.Z)
		, SrcX(InSource.X)
		, SrcY(InSource.Y)
		, SrcZ(InSource.Z)
		, Width(InSourceSize.X)
		, Height(InSourceSize.Y)
		, Depth(InSourceSize.Z)
	{}
};

struct FRHIDispatchIndirectParameters
{
	uint32 ThreadGroupCountX;
	uint32 ThreadGroupCountY;
	uint32 ThreadGroupCountZ;
};

struct FRHIDrawIndirectParameters
{
	uint32 VertexCountPerInstance;
	uint32 InstanceCount;
	uint32 StartVertexLocation;
	uint32 StartInstanceLocation;
};

struct FRHIDrawIndexedIndirectParameters
{
	uint32 IndexCountPerInstance;
	uint32 InstanceCount;
	uint32 StartIndexLocation;
	int32 BaseVertexLocation;
	uint32 StartInstanceLocation;
};


struct FTextureMemoryStats
{
	// Hardware state (never change after device creation):

	// -1 if unknown, in bytes
	int64 DedicatedVideoMemory;
	// -1 if unknown, in bytes
	int64 DedicatedSystemMemory;
	// -1 if unknown, in bytes
	int64 SharedSystemMemory;
	// Total amount of "graphics memory" that we think we can use for all our graphics resources, in bytes. -1 if unknown.
	int64 TotalGraphicsMemory;

	// Size of allocated memory, in bytes
	int64 AllocatedMemorySize;
	// Size of the largest memory fragment, in bytes
	int64 LargestContiguousAllocation;
	// 0 if streaming pool size limitation is disabled, in bytes
	int64 TexturePoolSize;
	// Upcoming adjustments to allocated memory, in bytes (async reallocations)
	int32 PendingMemoryAdjustment;

	// defaults
	FTextureMemoryStats()
		: DedicatedVideoMemory(-1)
		, DedicatedSystemMemory(-1)
		, SharedSystemMemory(-1)
		, TotalGraphicsMemory(-1)
		, AllocatedMemorySize(0)
		, LargestContiguousAllocation(0)
		, TexturePoolSize(0)
		, PendingMemoryAdjustment(0)
	{
	}

	bool AreHardwareStatsValid() const
	{
		// pardon the redundancy, have a broken compiler (__EMSCRIPTEN__) that needs these types spelled out...
		return ((int64)DedicatedVideoMemory >= 0 && (int64)DedicatedSystemMemory >= 0 && (int64)SharedSystemMemory >= 0);
	}

	bool IsUsingLimitedPoolSize() const
	{
		return TexturePoolSize > 0;
	}

	int64 ComputeAvailableMemorySize() const
	{
		return FMath::Max(TexturePoolSize - AllocatedMemorySize, (int64)0);
	}
};

struct RHI_API FDrawCallCategoryName
{
	FDrawCallCategoryName() 
	{
		for (int32& Counter : Counters)
		{
			Counter = -1;
		}
	}

	FDrawCallCategoryName(FName InName)
		: Name(InName)
	{
		for (int32& Counter : Counters)
		{
			Counter = 0;
		}

		check(NumCategory < MAX_DRAWCALL_CATEGORY);
		if (NumCategory < MAX_DRAWCALL_CATEGORY)
		{
			Array[NumCategory] = this;
			NumCategory++;
		}
	}

	FName Name;
	int32 Counters[MAX_NUM_GPUS];

	static constexpr int32 MAX_DRAWCALL_CATEGORY = 256;
	static FDrawCallCategoryName* Array[MAX_DRAWCALL_CATEGORY];
	static int32 DisplayCounts[MAX_DRAWCALL_CATEGORY][MAX_NUM_GPUS]; // A backup of the counts that can be used to display on screen to avoid flickering.
	static int32 NumCategory;
};

// RHI counter stats.
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("DrawPrimitive calls"),STAT_RHIDrawPrimitiveCalls,STATGROUP_RHI,RHI_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Triangles drawn"),STAT_RHITriangles,STATGROUP_RHI,RHI_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Lines drawn"),STAT_RHILines,STATGROUP_RHI,RHI_API);

#if STATS
#define RHI_DRAW_CALL_INC_MGPU(GPUIndex) \
		INC_DWORD_STAT(STAT_RHIDrawPrimitiveCalls); \
		FPlatformAtomics::InterlockedIncrement(&(*GCurrentNumDrawCallsRHIPtr)[GPUIndex]);

#define RHI_DRAW_CALL_STATS_MGPU(GPUIndex,PrimitiveType,NumPrimitives) \
		RHI_DRAW_CALL_INC_MGPU(GPUIndex); \
		INC_DWORD_STAT_BY(STAT_RHITriangles,(uint32)(PrimitiveType != PT_LineList ? (NumPrimitives) : 0)); \
		INC_DWORD_STAT_BY(STAT_RHILines,(uint32)(PrimitiveType == PT_LineList ? (NumPrimitives) : 0)); \
		FPlatformAtomics::InterlockedAdd(&GCurrentNumPrimitivesDrawnRHI[GPUIndex], NumPrimitives);
#else
#define RHI_DRAW_CALL_INC_MGPU(GPUIndex) \
		FPlatformAtomics::InterlockedIncrement(&(*GCurrentNumDrawCallsRHIPtr)[GPUIndex]);

#define RHI_DRAW_CALL_STATS_MGPU(GPUIndex,PrimitiveType,NumPrimitives) \
		FPlatformAtomics::InterlockedAdd(&GCurrentNumPrimitivesDrawnRHI[GPUIndex], NumPrimitives); \
		FPlatformAtomics::InterlockedIncrement(&(*GCurrentNumDrawCallsRHIPtr)[GPUIndex]);
#endif

#define RHI_DRAW_CALL_INC() RHI_DRAW_CALL_INC_MGPU(0)
#define RHI_DRAW_CALL_STATS(PrimitiveType,NumPrimitives) RHI_DRAW_CALL_STATS_MGPU(0, PrimitiveType,NumPrimitives)


// RHI memory stats.
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Render target memory 2D"),STAT_RenderTargetMemory2D,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Render target memory 3D"),STAT_RenderTargetMemory3D,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Render target memory Cube"),STAT_RenderTargetMemoryCube,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Texture memory 2D"),STAT_TextureMemory2D,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Texture memory 3D"),STAT_TextureMemory3D,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Texture memory Cube"),STAT_TextureMemoryCube,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Uniform buffer memory"),STAT_UniformBufferMemory,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Index buffer memory"),STAT_IndexBufferMemory,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Vertex buffer memory"),STAT_VertexBufferMemory,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Structured buffer memory"),STAT_StructuredBufferMemory,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Pixel buffer memory"),STAT_PixelBufferMemory,STATGROUP_RHI,FPlatformMemory::MCR_GPU,RHI_API);


// RHI base resource types.
#include "RHIResources.h"
#include "DynamicRHI.h"

/** Initializes the RHI. */
extern RHI_API void RHIInit(bool bHasEditorToken);

/** Performs additional RHI initialization before the render thread starts. */
extern RHI_API void RHIPostInit(const TArray<uint32>& InPixelFormatByteWidth);

/** Shuts down the RHI. */
extern RHI_API void RHIExit();


// Panic delegate is called when when a fatal condition is encountered within RHI function.
DECLARE_DELEGATE_OneParam(FRHIPanicEvent, const FName&);
extern RHI_API FRHIPanicEvent& RHIGetPanicDelegate();

// RHI utility functions that depend on the RHI definitions.
#include "RHIUtilities.h"

inline void FRHITransition::Cleanup() const
{
	FRHITransition* Transition = const_cast<FRHITransition*>(this);
	RHIReleaseTransition(Transition);

	// Explicit destruction of the transition.
	Transition->~FRHITransition();
	FMemory::Free(Transition);
}

#if ENABLE_RHI_VALIDATION

inline void RHIValidation::FTracker::AddOp(const RHIValidation::FOperation& Op)
{
	if (GRHICommandList.Bypass() && CurrentList.Operations.Num() == 0)
	{
		auto& OpQueue = OpQueues[GetOpQueueIndex(Pipeline)];
		if (!EnumHasAllFlags(Op.Replay(OpQueue.bAllowAllUAVsOverlap), EReplayStatus::Waiting))
		{
			return;
		}
	}

	// @todo: improve performance. Use a fast linear allocator
	CurrentList.Operations.Add(Op);
}

#endif // ENABLE_RHI_VALIDATION
