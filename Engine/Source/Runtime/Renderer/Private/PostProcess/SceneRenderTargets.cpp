// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRenderTargets.cpp: Scene render target implementation.
=============================================================================*/

#include "PostProcess/SceneRenderTargets.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "SceneRenderTargetParameters.h"
#include "SceneTextureParameters.h"
#include "VelocityRendering.h"
#include "RendererModule.h"
#include "LightPropagationVolume.h"
#include "ScenePrivate.h"
#include "HdrCustomResolveShaders.h"
#include "WideCustomResolveShaders.h"
#include "ClearQuad.h"
#include "RenderUtils.h"
#include "RendererInterface.h"
#include "PipelineStateCache.h"
#include "OneColorShader.h"
#include "ResolveShader.h"
#include "EngineGlobals.h"
#include "UnrealEngine.h"
#include "StereoRendering.h"
#include "StereoRenderTargetManager.h"
#include "VT/VirtualTextureSystem.h"
#include "VT/VirtualTextureFeedback.h"
#include "VisualizeTexture.h"
#include "GpuDebugRendering.h"
#include "IHeadMountedDisplayModule.h"

static TAutoConsoleVariable<int32> CVarRSMResolution(
	TEXT("r.LPV.RSMResolution"),
	360,
	TEXT("Reflective Shadow Map resolution (used for LPV) - higher values result in less aliasing artifacts, at the cost of performance"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarNoGBufferDClear(
	TEXT("r.NoGBufferDClear"),
	0,
	TEXT("Do not clear GBuffer D"),
	ECVF_RenderThreadSafe);

/*-----------------------------------------------------------------------------
FSceneRenderTargets
-----------------------------------------------------------------------------*/

int32 GDownsampledOcclusionQueries = 0;
static FAutoConsoleVariableRef CVarDownsampledOcclusionQueries(
	TEXT("r.DownsampledOcclusionQueries"),
	GDownsampledOcclusionQueries,
	TEXT("Whether to issue occlusion queries to a downsampled depth buffer"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarSceneTargetsResizeMethod(
	TEXT("r.SceneRenderTargetResizeMethod"),
	0,
	TEXT("Control the scene render target resize method:\n")
	TEXT("(This value is only used in game mode and on windowing platforms unless 'r.SceneRenderTargetsResizingMethodForceOverride' is enabled.)\n")
	TEXT("0: Resize to match requested render size (Default) (Least memory use, can cause stalls when size changes e.g. ScreenPercentage)\n")
	TEXT("1: Fixed to screen resolution.\n")
	TEXT("2: Expands to encompass the largest requested render dimension. (Most memory use, least prone to allocation stalls.)"),	
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarSceneTargetsResizeMethodForceOverride(
	TEXT("r.SceneRenderTargetResizeMethodForceOverride"),
	0,
	TEXT("Forces 'r.SceneRenderTargetResizeMethod' to be respected on all configurations.\n")
	TEXT("0: Disabled.\n")
	TEXT("1: Enabled.\n"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarCustomDepth(
	TEXT("r.CustomDepth"),
	1,
	TEXT("0: feature is disabled\n")
	TEXT("1: feature is enabled, texture is created on demand\n")
	TEXT("2: feature is enabled, texture is not released until required (should be the project setting if the feature should not stall)\n")
	TEXT("3: feature is enabled, stencil writes are enabled, texture is not released until required (should be the project setting if the feature should not stall)"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarMobileCustomDepthDownSample(
	TEXT("r.Mobile.CustomDepthDownSample"),
	0,
	TEXT("Perform Mobile CustomDepth at HalfRes \n ")
	TEXT("0: Off (default)\n ")
	TEXT("1: On \n "),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMSAACount(
	TEXT("r.MSAACount"),
	4,
	TEXT("Number of MSAA samples to use with the forward renderer.  Only used when MSAA is enabled in the rendering project settings.\n")
	TEXT("0: MSAA disabled (Temporal AA enabled)\n")
	TEXT("1: MSAA disabled\n")
	TEXT("2: Use 2x MSAA\n")
	TEXT("4: Use 4x MSAA")
	TEXT("8: Use 8x MSAA"),
	ECVF_RenderThreadSafe | ECVF_Scalability
	);

static TAutoConsoleVariable<int32> CVarMobileMSAA(
	TEXT("r.MobileMSAA"),
	1,
	TEXT("Use MSAA instead of Temporal AA on mobile:\n")
	TEXT("1: Use Temporal AA (MSAA disabled)\n")
	TEXT("2: Use 2x MSAA (Temporal AA disabled)\n")
	TEXT("4: Use 4x MSAA (Temporal AA disabled)\n")
	TEXT("8: Use 8x MSAA (Temporal AA disabled)"),
	ECVF_RenderThreadSafe | ECVF_Scalability
	);

static TAutoConsoleVariable<int32> CVarGBufferFormat(
	TEXT("r.GBufferFormat"),
	1,
	TEXT("Defines the memory layout used for the GBuffer.\n")
	TEXT("(affects performance, mostly through bandwidth, quality of normals and material attributes).\n")
	TEXT(" 0: lower precision (8bit per component, for profiling)\n")
	TEXT(" 1: low precision (default)\n")
	TEXT(" 3: high precision normals encoding\n")
	TEXT(" 5: high precision"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDefaultBackBufferPixelFormat(
	TEXT("r.DefaultBackBufferPixelFormat"),
	4,
	TEXT("Defines the default back buffer pixel format.\n")
	TEXT(" 0: 8bit RGBA\n")
	TEXT(" 1: 16bit RGBA\n")
	TEXT(" 2: Float RGB\n")
	TEXT(" 3: Float RGBA\n")
	TEXT(" 4: 10bit RGB, 2bit Alpha\n"),
	ECVF_ReadOnly);

int32 GAllowCustomMSAAResolves = 1;
static FAutoConsoleVariableRef CVarAllowCustomResolves(
   TEXT("r.MSAA.AllowCustomResolves"),
   GAllowCustomMSAAResolves,
   TEXT("Whether to use builtin HW resolve or allow custom shader MSAA resolves"),
   ECVF_RenderThreadSafe
   );

int32 GVirtualTextureFeedbackFactor = 16;
static FAutoConsoleVariableRef CVarVirtualTextureFeedbackFactor(
	TEXT("r.vt.FeedbackFactor"),
	GVirtualTextureFeedbackFactor,
	TEXT("The size of the VT feedback buffer is calculated by dividing the render resolution by this factor.")
	TEXT("The value set here is rounded up to the nearest power of two before use."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly /*Read-only as shaders are compiled with this value*/
);

/** The global render targets used for scene rendering. */
static TGlobalResource<FSceneRenderTargets> SceneRenderTargetsSingleton;

extern int32 GUseTranslucentLightingVolumes;

FSceneRenderTargets& FSceneRenderTargets::Get(FRHIComputeCommandList& RHICmdList)
{
	if (RHICmdList.IsImmediate() || RHICmdList.IsImmediateAsyncCompute())
	{
		check(IsInRenderingThread() && !RHICmdList.GetRenderThreadContext(FRHICommandListBase::ERenderThreadContext::SceneRenderTargets)
			&& !FTaskGraphInterface::Get().IsThreadProcessingTasks(ENamedThreads::GetRenderThread_Local())); // if we are processing tasks on the local queue, it is assumed this are in support of async tasks, which cannot use the current state of the render targets. This can be relaxed if needed.
		return SceneRenderTargetsSingleton;
	}
	else
	{
		FSceneRenderTargets* SceneContext = (FSceneRenderTargets*)RHICmdList.GetRenderThreadContext(FRHICommandListBase::ERenderThreadContext::SceneRenderTargets);
		if (!SceneContext)
		{
			return SceneRenderTargetsSingleton;
		}
		return *SceneContext;
	}
}

FSceneRenderTargets& FSceneRenderTargets::GetGlobalUnsafe()
{
	check(IsInRenderingThread());
	return SceneRenderTargetsSingleton;
}

FSceneRenderTargets& FSceneRenderTargets::Get_FrameConstantsOnly()
{
	return SceneRenderTargetsSingleton;
}

 FSceneRenderTargets* FSceneRenderTargets::CreateSnapshot(const FViewInfo& InView)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FSceneRenderTargets_CreateSnapshot);
	check(IsInRenderingThread() && FMemStack::Get().GetNumMarks() == 1); // we do not want this popped before the end of the scene and it better be the scene allocator
	FSceneRenderTargets* NewSnapshot = new (FMemStack::Get()) FSceneRenderTargets(InView, *this);
	check(NewSnapshot->bSnapshot);
	Snapshots.Add(NewSnapshot);
	return NewSnapshot;
}

void FSceneRenderTargets::SetSnapshotOnCmdList(FRHICommandList& TargetCmdList)
{
	check(bSnapshot);
	TargetCmdList.SetRenderThreadContext(this, FRHICommandListBase::ERenderThreadContext::SceneRenderTargets);
}

void FSceneRenderTargets::DestroyAllSnapshots()
{
	if (Snapshots.Num())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FSceneRenderTargets_DestroyAllSnapshots);
		check(IsInRenderingThread());
		for (auto Snapshot : Snapshots)
		{
			Snapshot->~FSceneRenderTargets();
		}
		Snapshots.Reset();
		GRenderTargetPool.DestructSnapshots();
	}
}



template <size_t N>
static void SnapshotArray(TRefCountPtr<IPooledRenderTarget> (&Dest)[N], const TRefCountPtr<IPooledRenderTarget> (&Src)[N])
{
	for (int32 Index = 0; Index < N; Index++)
	{
		Dest[Index] = GRenderTargetPool.MakeSnapshot(Src[Index]);
	}
}

template <uint32 N>
static void SnapshotArray(TArray<TRefCountPtr<IPooledRenderTarget>, TInlineAllocator<N>>(&Dest), const TArray<TRefCountPtr<IPooledRenderTarget>, TInlineAllocator<N>>(&Src))
{
	Dest.SetNum(Src.Num());
	for (int32 Index = 0; Index < Src.Num(); Index++)
	{
		Dest[Index] = GRenderTargetPool.MakeSnapshot(Src[Index]);
	}
}

FSceneRenderTargets::FSceneRenderTargets(const FViewInfo& View, const FSceneRenderTargets& SnapshotSource)
	: LightAccumulation(GRenderTargetPool.MakeSnapshot(SnapshotSource.LightAccumulation))
	, DirectionalOcclusion(GRenderTargetPool.MakeSnapshot(SnapshotSource.DirectionalOcclusion))
	, SceneDepthZ(GRenderTargetPool.MakeSnapshot(SnapshotSource.SceneDepthZ))	
	, SceneVelocity(GRenderTargetPool.MakeSnapshot(SnapshotSource.SceneVelocity))
	, SmallDepthZ(GRenderTargetPool.MakeSnapshot(SnapshotSource.SmallDepthZ))
	, GBufferA(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferA))
	, GBufferB(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferB))
	, GBufferC(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferC))
	, GBufferD(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferD))
	, GBufferE(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferE))
	, GBufferF(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferF))
	, SceneDepthAux(GRenderTargetPool.MakeSnapshot(SnapshotSource.SceneDepthAux))
	, DBufferA(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferA))
	, DBufferB(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferB))
	, DBufferC(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferC))
	, DBufferMask(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferMask))
	, ScreenSpaceAO(GRenderTargetPool.MakeSnapshot(SnapshotSource.ScreenSpaceAO))
	, ScreenSpaceGTAOHorizons(GRenderTargetPool.MakeSnapshot(SnapshotSource.ScreenSpaceGTAOHorizons))
	, QuadOverdrawBuffer(GRenderTargetPool.MakeSnapshot(SnapshotSource.QuadOverdrawBuffer))
	, CustomDepth(GRenderTargetPool.MakeSnapshot(SnapshotSource.CustomDepth))
	, MobileCustomDepth(GRenderTargetPool.MakeSnapshot(SnapshotSource.MobileCustomDepth))
	, MobileCustomStencil(GRenderTargetPool.MakeSnapshot(SnapshotSource.MobileCustomStencil))
	, CustomStencilSRV(SnapshotSource.CustomStencilSRV)
	, SkySHIrradianceMap(GRenderTargetPool.MakeSnapshot(SnapshotSource.SkySHIrradianceMap))
	, EditorPrimitivesColor(GRenderTargetPool.MakeSnapshot(SnapshotSource.EditorPrimitivesColor))
	, EditorPrimitivesDepth(GRenderTargetPool.MakeSnapshot(SnapshotSource.EditorPrimitivesDepth))
	, bScreenSpaceAOIsValid(SnapshotSource.bScreenSpaceAOIsValid)
	, bCustomDepthIsValid(SnapshotSource.bCustomDepthIsValid)
	, GBufferRefCount(SnapshotSource.GBufferRefCount)
	, ThisFrameNumber(SnapshotSource.ThisFrameNumber)
	, CurrentDesiredSizeIndex(SnapshotSource.CurrentDesiredSizeIndex)
	, BufferSize(SnapshotSource.BufferSize)
	, LastStereoSize(SnapshotSource.LastStereoSize)
	, SmallColorDepthDownsampleFactor(SnapshotSource.SmallColorDepthDownsampleFactor)
	, bUseDownsizedOcclusionQueries(SnapshotSource.bUseDownsizedOcclusionQueries)
	, CurrentGBufferFormat(SnapshotSource.CurrentGBufferFormat)
	, CurrentSceneColorFormat(SnapshotSource.CurrentSceneColorFormat)
	, CurrentMobileSceneColorFormat(SnapshotSource.CurrentMobileSceneColorFormat)
	, bAllowStaticLighting(SnapshotSource.bAllowStaticLighting)
	, CurrentMaxShadowResolution(SnapshotSource.CurrentMaxShadowResolution)
	, CurrentRSMResolution(SnapshotSource.CurrentRSMResolution)
	, CurrentTranslucencyLightingVolumeDim(SnapshotSource.CurrentTranslucencyLightingVolumeDim)
	, CurrentMSAACount(SnapshotSource.CurrentMSAACount)
	, CurrentMinShadowResolution(SnapshotSource.CurrentMinShadowResolution)
	, bCurrentLightPropagationVolume(SnapshotSource.bCurrentLightPropagationVolume)
	, CurrentFeatureLevel(SnapshotSource.CurrentFeatureLevel)
	, CurrentShadingPath(SnapshotSource.CurrentShadingPath)
	, bRequireSceneColorAlpha(SnapshotSource.bRequireSceneColorAlpha)
	, bAllocateVelocityGBuffer(SnapshotSource.bAllocateVelocityGBuffer)
	, bSnapshot(true)
	, DefaultColorClear(SnapshotSource.DefaultColorClear)
	, DefaultDepthClear(SnapshotSource.DefaultDepthClear)
	, bHMDAllocatedDepthTarget(SnapshotSource.bHMDAllocatedDepthTarget)
	, bKeepDepthContent(SnapshotSource.bKeepDepthContent)
	, bRequireMultiView(SnapshotSource.bRequireMultiView)
{
	FMemory::Memcpy(LargestDesiredSizes, SnapshotSource.LargestDesiredSizes);
#if PREVENT_RENDERTARGET_SIZE_THRASHING
	FMemory::Memcpy(HistoryFlags, SnapshotSource.HistoryFlags, sizeof(SnapshotSource.HistoryFlags));
#endif
	SnapshotArray(SceneColor, SnapshotSource.SceneColor);
	SnapshotArray(ReflectionColorScratchCubemap, SnapshotSource.ReflectionColorScratchCubemap);
	SnapshotArray(DiffuseIrradianceScratchCubemap, SnapshotSource.DiffuseIrradianceScratchCubemap);
	SnapshotArray(TranslucencyLightingVolumeAmbient, SnapshotSource.TranslucencyLightingVolumeAmbient);
	SnapshotArray(TranslucencyLightingVolumeDirectional, SnapshotSource.TranslucencyLightingVolumeDirectional);
}

inline const TCHAR* GetSceneColorTargetName(EShadingPath ShadingPath)
{
	const TCHAR* SceneColorNames[(uint32)EShadingPath::Num] =
	{ 
		TEXT("SceneColorMobile"), 
		TEXT("SceneColorDeferred")
	};
	check((uint32)ShadingPath < UE_ARRAY_COUNT(SceneColorNames));
	return SceneColorNames[(uint32)ShadingPath];
}

#if PREVENT_RENDERTARGET_SIZE_THRASHING

namespace ERenderTargetHistory
{
	enum Type
	{
		RTH_SceneCapture		= 0x1,
		RTH_ReflectionCapture	= 0x2,
		RTH_HighresScreenshot	= 0x4,
		RTH_MaskAll				= 0x7,
	};
}

static inline void UpdateHistoryFlags(uint8& Flags, bool bIsSceneCapture, bool bIsReflectionCapture, bool bIsHighResScreenShot)
{
	Flags |= bIsSceneCapture ? ERenderTargetHistory::RTH_SceneCapture : 0;
	Flags |= bIsReflectionCapture ? ERenderTargetHistory::RTH_ReflectionCapture : 0;
	Flags |= bIsHighResScreenShot ? ERenderTargetHistory::RTH_HighresScreenshot : 0;
}

template <uint32 NumEntries>
static bool AnyCaptureRenderedRecently(const uint8* HistoryFlags, uint8 Mask)
{
	uint8 Result = 0;
	for (uint32 Idx = 0; Idx < NumEntries; ++Idx)
	{
		Result |= (HistoryFlags[Idx] & Mask);
	}
	return Result != 0;
}

#define UPDATE_HISTORY_FLAGS(Flags, bIsSceneCapture, bIsReflectionCapture, bIsHighResScreenShot) UpdateHistoryFlags(Flags, bIsSceneCapture, bIsReflectionCapture, bIsHighResScreenShot)
#define ANY_CAPTURE_RENDERED_RECENTLY(HistoryFlags, NumEntries) AnyCaptureRenderedRecently<NumEntries>(HistoryFlags, ERenderTargetHistory::RTH_MaskAll)
#define ANY_HIGHRES_CAPTURE_RENDERED_RECENTLY(HistoryFlags, NumEntries) AnyCaptureRenderedRecently<NumEntries>(HistoryFlags, ERenderTargetHistory::RTH_HighresScreenshot)

#else

#define UPDATE_HISTORY_FLAGS(Flags, bIsSceneCapture, bIsReflectionCapture, bIsHighResScreenShot)
#define ANY_CAPTURE_RENDERED_RECENTLY(HistoryFlags, NumEntries) (false)
#define ANY_HIGHRES_CAPTURE_RENDERED_RECENTLY(HistoryFlags, NumEntries) (false)
#endif

FIntPoint FSceneRenderTargets::ComputeDesiredSize(const FSceneViewFamily& ViewFamily)
{
	enum ESizingMethods { RequestedSize, ScreenRes, Grow, VisibleSizingMethodsCount };
	ESizingMethods SceneTargetsSizingMethod = Grow;

	bool bIsSceneCapture = false;
	bool bIsReflectionCapture = false;
	bool bIsVRScene = false;

	for (int32 ViewIndex = 0, ViewCount = ViewFamily.Views.Num(); ViewIndex < ViewCount; ++ViewIndex)
	{
		const FSceneView* View = ViewFamily.Views[ViewIndex];

		bIsSceneCapture |= View->bIsSceneCapture;
		bIsReflectionCapture |= View->bIsReflectionCapture;
		bIsVRScene |= (IStereoRendering::IsStereoEyeView(*View) && GEngine->XRSystem.IsValid());
	}

	FIntPoint DesiredBufferSize = FIntPoint::ZeroValue;
	FIntPoint DesiredFamilyBufferSize = FSceneRenderer::GetDesiredInternalBufferSize(ViewFamily);

	{
		bool bUseResizeMethodCVar = true;

		if (CVarSceneTargetsResizeMethodForceOverride.GetValueOnRenderThread() != 1)
		{
			if (!FPlatformProperties::SupportsWindowedMode() || bIsVRScene)
			{
				if (bIsVRScene)
				{
					if (!bIsSceneCapture && !bIsReflectionCapture)
					{
						// If this isn't a scene capture, and it's a VR scene, and the size has changed since the last time we
						// rendered a VR scene (or this is the first time), use the requested size method.
						if (DesiredFamilyBufferSize.X != LastStereoSize.X || DesiredFamilyBufferSize.Y != LastStereoSize.Y)
						{
							LastStereoSize = DesiredFamilyBufferSize;
							SceneTargetsSizingMethod = RequestedSize;
							UE_LOG(LogRenderer, Warning, TEXT("Resizing VR buffer to %d by %d"), DesiredFamilyBufferSize.X, DesiredFamilyBufferSize.Y);
						}
						else
						{
							// Otherwise use the grow method.
							SceneTargetsSizingMethod = Grow;
						}
					}
					else
					{
						// If this is a scene capture, and it's smaller than the VR view size, then don't re-allocate buffers, just use the "grow" method.
						// If it's bigger than the VR view, then log a warning, and use resize method.
						if (DesiredFamilyBufferSize.X > LastStereoSize.X || DesiredFamilyBufferSize.Y > LastStereoSize.Y)
						{
							if (LastStereoSize.X > 0 && bIsSceneCapture)
							{
								static bool DisplayedCaptureSizeWarning = false;
								if (!DisplayedCaptureSizeWarning)
								{
									DisplayedCaptureSizeWarning = true;
									UE_LOG(LogRenderer, Warning, TEXT("Scene capture of %d by %d is larger than the current VR target. If this is deliberate for a capture that is being done for multiple frames, consider the performance and memory implications. To disable this warning and ensure optimal behavior with this path, set r.SceneRenderTargetResizeMethod to 2, and r.SceneRenderTargetResizeMethodForceOverride to 1."), DesiredFamilyBufferSize.X, DesiredFamilyBufferSize.Y);
								}
							}
							SceneTargetsSizingMethod = RequestedSize;
						}
						else
						{
							SceneTargetsSizingMethod = Grow;
						}
					}
				}
				else
				{
					// Force ScreenRes on non windowed platforms.
					SceneTargetsSizingMethod = RequestedSize;
				}
				bUseResizeMethodCVar = false;
			}
			else if (GIsEditor)
			{
				// Always grow scene render targets in the editor.
				SceneTargetsSizingMethod = Grow;
				bUseResizeMethodCVar = false;
			}
		}

		if (bUseResizeMethodCVar)
		{
			// Otherwise use the setting specified by the console variable.
			SceneTargetsSizingMethod = (ESizingMethods)FMath::Clamp(CVarSceneTargetsResizeMethod.GetValueOnRenderThread(), 0, (int32)VisibleSizingMethodsCount);
		}
	}

	switch (SceneTargetsSizingMethod)
	{
		case RequestedSize:
			DesiredBufferSize = DesiredFamilyBufferSize;
			break;

		case ScreenRes:
			DesiredBufferSize = FIntPoint(GSystemResolution.ResX, GSystemResolution.ResY);
			break;

		case Grow:
			DesiredBufferSize = FIntPoint(
				FMath::Max((int32)GetBufferSizeXY().X, DesiredFamilyBufferSize.X),
				FMath::Max((int32)GetBufferSizeXY().Y, DesiredFamilyBufferSize.Y));
			break;

		default:
			checkNoEntry();
	}

	const uint32 FrameNumber = ViewFamily.FrameNumber;
	if (ThisFrameNumber != FrameNumber)
	{
		ThisFrameNumber = FrameNumber;
		if (++CurrentDesiredSizeIndex == FrameSizeHistoryCount)
		{
			CurrentDesiredSizeIndex -= FrameSizeHistoryCount;
		}
		// this allows the BufferSize to shrink each frame (in game)
		LargestDesiredSizes[CurrentDesiredSizeIndex] = FIntPoint::ZeroValue;
#if PREVENT_RENDERTARGET_SIZE_THRASHING
		HistoryFlags[CurrentDesiredSizeIndex] = 0;
#endif
	}

	// this allows The BufferSize to not grow below the SceneCapture requests (happen before scene rendering, in the same frame with a Grow request)
	FIntPoint& LargestDesiredSizeThisFrame = LargestDesiredSizes[CurrentDesiredSizeIndex];
	LargestDesiredSizeThisFrame = LargestDesiredSizeThisFrame.ComponentMax(DesiredBufferSize);
	bool bIsHighResScreenshot = GIsHighResScreenshot;
	UPDATE_HISTORY_FLAGS(HistoryFlags[CurrentDesiredSizeIndex], bIsSceneCapture, bIsReflectionCapture, bIsHighResScreenshot);

	// we want to shrink the buffer but as we can have multiple scenecaptures per frame we have to delay that a frame to get all size requests
	// Don't save buffer size in history while making high-res screenshot.
	// We have to use the requested size when allocating an hmd depth target to ensure it matches the hmd allocated render target size.
	bool bAllowDelayResize = !GIsHighResScreenshot && !bIsVRScene;

	// Don't consider the history buffer when the aspect ratio changes, the existing buffers won't make much sense at all.
	// This prevents problems when orientation changes on mobile in particular.
	// bIsReflectionCapture is explicitly checked on all platforms to prevent aspect ratio change detection from forcing the immediate buffer resize.
	// This ensures that 1) buffers are not resized spuriously during reflection rendering 2) all cubemap faces use the same render target size.
	if (bAllowDelayResize && !bIsReflectionCapture && !ANY_CAPTURE_RENDERED_RECENTLY(HistoryFlags, FrameSizeHistoryCount))
	{
		const bool bAspectRatioChanged =
			!BufferSize.Y ||
			!FMath::IsNearlyEqual(
				(float)BufferSize.X / BufferSize.Y,
				(float)DesiredBufferSize.X / DesiredBufferSize.Y);

		if (bAspectRatioChanged)
		{
			bAllowDelayResize = false;

			// At this point we're assuming a simple output resize and forcing a hard swap so clear the history.
			// If we don't the next frame will fail this check as the allocated aspect ratio will match the new
			// frame's forced size so we end up looking through the history again, finding the previous old size
			// and reallocating. Only after a few frames can the results actually settle when the history clears 
			for (int32 i = 0; i < FrameSizeHistoryCount; ++i)
			{
				LargestDesiredSizes[i] = FIntPoint::ZeroValue;
#if PREVENT_RENDERTARGET_SIZE_THRASHING
				HistoryFlags[i] = 0;
#endif
			}
		}
	}
	const bool bAnyHighresScreenshotRecently = ANY_HIGHRES_CAPTURE_RENDERED_RECENTLY(HistoryFlags, FrameSizeHistoryCount);
	if(bAnyHighresScreenshotRecently != GIsHighResScreenshot)
	{
		bAllowDelayResize = false;
	}

	if(bAllowDelayResize)
	{
		for (int32 i = 0; i < FrameSizeHistoryCount; ++i)
		{
			DesiredBufferSize = DesiredBufferSize.ComponentMax(LargestDesiredSizes[i]);
		}
	}

	return DesiredBufferSize;
}

uint16 FSceneRenderTargets::GetNumSceneColorMSAASamples(ERHIFeatureLevel::Type InFeatureLevel, bool bRendererSupportMSAA/* = true*/)
{
	uint16 NumSamples = 1;

	if (InFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		static IConsoleVariable* CVarDefaultAntiAliasing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AntiAliasing"));
		EAntiAliasingMethod Method = (EAntiAliasingMethod)CVarDefaultAntiAliasing->GetInt();

		if (IsForwardShadingEnabled(GetFeatureLevelShaderPlatform(InFeatureLevel)) && Method == AAM_MSAA)
		{
			NumSamples = FMath::Max(1, CVarMSAACount.GetValueOnRenderThread());
		}
	}
	else
	{
		NumSamples = CVarMobileMSAA.GetValueOnRenderThread();

		static uint16 PlatformMaxSampleCount = GDynamicRHI->RHIGetPlatformTextureMaxSampleCount();
		NumSamples = FMath::Min(NumSamples, PlatformMaxSampleCount);
	}

	if ((NumSamples != 1 && NumSamples != 2 && NumSamples != 4 && NumSamples != 8) || !bRendererSupportMSAA)
	{
		NumSamples = 1;

		static bool bWarned = false;

		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogRenderer, Warning, TEXT("Requested %d samples for MSAA, but this is not supported; falling back to 1 sample"), NumSamples);
		}
	}

	if (NumSamples > 1 && !RHISupportsMSAA(GShaderPlatformForFeatureLevel[InFeatureLevel]))
	{
		NumSamples = 1;

		static bool bWarned = false;

		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogRenderer, Log, TEXT("MSAA requested but the platform doesn't support MSAA, falling back to Temporal AA"));
		}
	}

	return NumSamples;
}

void FSceneRenderTargets::Allocate(FRHICommandListImmediate& RHICmdList, const FSceneRenderer* SceneRenderer)
{
	check(IsInRenderingThread());
	// ViewFamily setup wasn't complete
	check(SceneRenderer->ViewFamily.FrameNumber != UINT_MAX);

	const FSceneViewFamily& ViewFamily = SceneRenderer->ViewFamily;

	// If feature level has changed, release all previously allocated targets to the pool. If feature level has changed but
	const auto NewFeatureLevel = ViewFamily.Scene->GetFeatureLevel();
	CurrentShadingPath = ViewFamily.Scene->GetShadingPath();

	bRequireSceneColorAlpha = false;
	bRequireMultiView = ViewFamily.bRequireMultiView;

	for (int32 ViewIndex = 0; ViewIndex < ViewFamily.Views.Num(); ViewIndex++)
	{
		// Planar reflections and scene captures use scene color alpha to keep track of where content has been rendered, for compositing into a different scene later
		if (ViewFamily.Views[ViewIndex]->bIsPlanarReflection || ViewFamily.Views[ViewIndex]->bIsSceneCapture)
		{
			bRequireSceneColorAlpha = true;
		}
	}

	FIntPoint DesiredBufferSize = ComputeDesiredSize(ViewFamily);
	check(DesiredBufferSize.X > 0 && DesiredBufferSize.Y > 0);
	QuantizeSceneBufferSize(DesiredBufferSize, DesiredBufferSize);

	int GBufferFormat = CVarGBufferFormat.GetValueOnRenderThread();

	// Set default clear values
	if (CurrentShadingPath == EShadingPath::Mobile)
	{
		// On mobile the scene depth is calculated from the alpha component of the scene color
		// Use FarPlane for alpha to ensure un-rendered pixels have max depth...
		float DepthFar = (float)ERHIZBuffer::FarPlane;
		FClearValueBinding ClearColorMaxDepth = FClearValueBinding(FLinearColor(0.f, 0.f, 0.f, DepthFar));
		SetDefaultColorClear(ClearColorMaxDepth);
	}
	else
	{
		SetDefaultColorClear(FClearValueBinding::Black);
	}
	SetDefaultDepthClear(FClearValueBinding::DepthFar);

	int SceneColorFormat;
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SceneColorFormat"));

		SceneColorFormat = CVar->GetValueOnRenderThread();
	}

	EPixelFormat MobileSceneColorFormat = GetDesiredMobileSceneColorFormat();
		
	bool bNewAllowStaticLighting;
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));

		bNewAllowStaticLighting = CVar->GetValueOnRenderThread() != 0;
	}

	bool bDownsampledOcclusionQueries = GDownsampledOcclusionQueries != 0;

	int32 MaxShadowResolution = GetCachedScalabilityCVars().MaxShadowResolution;

	int32 RSMResolution = FMath::Clamp(CVarRSMResolution.GetValueOnRenderThread(), 1, 2048);

	if (ViewFamily.Scene->GetShadingPath() == EShadingPath::Mobile)
	{
		// ensure there is always enough space for mobile renderer's tiled shadow maps
		// by reducing the shadow map resolution.
		int32 MaxShadowDepthBufferDim = FMath::Max(GMaxShadowDepthBufferSizeX, GMaxShadowDepthBufferSizeY);
		if (MaxShadowResolution * 2 >  MaxShadowDepthBufferDim)
		{
			MaxShadowResolution = MaxShadowDepthBufferDim / 2;
		}
	}

	const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

	int32 MSAACount = GetNumSceneColorMSAASamples(NewFeatureLevel, SceneRenderer->SupportsMSAA());

	bool bLightPropagationVolume = UseLightPropagationVolumeRT(NewFeatureLevel);

	uint32 MinShadowResolution;
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.MinResolution"));

		MinShadowResolution = CVar->GetValueOnRenderThread();
	}

	if( (BufferSize.X != DesiredBufferSize.X) ||
		(BufferSize.Y != DesiredBufferSize.Y) ||
		(CurrentGBufferFormat != GBufferFormat) ||
		(CurrentSceneColorFormat != SceneColorFormat) ||
		(CurrentMobileSceneColorFormat != MobileSceneColorFormat) ||
		(bAllowStaticLighting != bNewAllowStaticLighting) ||
		(bUseDownsizedOcclusionQueries != bDownsampledOcclusionQueries) ||
		(CurrentMaxShadowResolution != MaxShadowResolution) ||
 		(CurrentRSMResolution != RSMResolution) ||
		(CurrentTranslucencyLightingVolumeDim != TranslucencyLightingVolumeDim) ||
		(CurrentMSAACount != MSAACount) ||
		(bCurrentLightPropagationVolume != bLightPropagationVolume) ||
		(CurrentMinShadowResolution != MinShadowResolution) ||
		(bCurrentRequireMultiView != bRequireMultiView))
	{
		CurrentGBufferFormat = GBufferFormat;
		CurrentSceneColorFormat = SceneColorFormat;
		CurrentMobileSceneColorFormat = MobileSceneColorFormat;
		bAllowStaticLighting = bNewAllowStaticLighting;
		bUseDownsizedOcclusionQueries = bDownsampledOcclusionQueries;
		CurrentMaxShadowResolution = MaxShadowResolution;
		CurrentRSMResolution = RSMResolution;
		CurrentTranslucencyLightingVolumeDim = TranslucencyLightingVolumeDim;
		CurrentMSAACount = MSAACount;
		CurrentMinShadowResolution = MinShadowResolution;
		bCurrentLightPropagationVolume = bLightPropagationVolume;
		bCurrentRequireMultiView = bRequireMultiView;

		// Reinitialize the render targets for the given size.
		SetBufferSize(DesiredBufferSize.X, DesiredBufferSize.Y);

		UE_LOG(LogRenderer, Log, TEXT("Reallocating scene render targets to support %ux%u Format %u NumSamples %u (Frame:%u)."), BufferSize.X, BufferSize.Y, (uint32)GetSceneColorFormat(NewFeatureLevel), CurrentMSAACount, ViewFamily.FrameNumber);

		UpdateRHI();
	}

	// Do allocation of render targets if they aren't available for the current shading path
	CurrentFeatureLevel = NewFeatureLevel;
	AllocateRenderTargets(RHICmdList, ViewFamily.Views.Num());
}

int32 FSceneRenderTargets::GetGBufferRenderTargets(const TRefCountPtr<IPooledRenderTarget>* OutRenderTargets[MaxSimultaneousRenderTargets], int32& OutVelocityRTIndex, int32& OutGBufferDIndex) const
{
	int32 MRTCount = 0;
	OutRenderTargets[MRTCount++] = &GetSceneColor();

	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);

	if (bUseGBuffer)
	{
		OutRenderTargets[MRTCount++] = &GBufferA;
		OutRenderTargets[MRTCount++] = &GBufferB;
		OutRenderTargets[MRTCount++] = &GBufferC;
	}

	// The velocity buffer needs to be bound before other optionnal rendertargets (when UseSelectiveBasePassOutputs() is true).
	// Otherwise there is an issue on some AMD hardware where the target does not get updated. Seems to be related to the velocity buffer format as it works fine with other targets.
	if (bAllocateVelocityGBuffer && !IsSimpleForwardShadingEnabled(ShaderPlatform))
	{
		OutVelocityRTIndex = MRTCount;
		check(OutVelocityRTIndex == 4 || (!bUseGBuffer && OutVelocityRTIndex == 1)); // As defined in BasePassPixelShader.usf
		OutRenderTargets[MRTCount++] = &SceneVelocity;
	}
	else
	{
		OutVelocityRTIndex = -1;
	}

	OutGBufferDIndex = INDEX_NONE;

	if (bUseGBuffer)
	{
		OutGBufferDIndex = MRTCount;
		OutRenderTargets[MRTCount++] = &GBufferD;

		if (bAllowStaticLighting)
		{
			check(MRTCount == (bAllocateVelocityGBuffer ? 6 : 5)); // As defined in BasePassPixelShader.usf
			OutRenderTargets[MRTCount++] = &GBufferE;
		}
	}

	check(MRTCount <= MaxSimultaneousRenderTargets);
	return MRTCount;
}

int32 FSceneRenderTargets::FillGBufferRenderPassInfo(ERenderTargetLoadAction ColorLoadAction, FRHIRenderPassInfo& OutRenderPassInfo, int32& OutVelocityRTIndex) const
{
	const TRefCountPtr<IPooledRenderTarget>* RenderTargets[MaxSimultaneousRenderTargets];
	int32 GBufferDIndex;
	int32 MRTCount = GetGBufferRenderTargets(RenderTargets, OutVelocityRTIndex, GBufferDIndex);

	for (int32 MRTIdx = 0; MRTIdx < MRTCount; ++MRTIdx)
	{
		if (MRTIdx == GBufferDIndex && !!CVarNoGBufferDClear.GetValueOnRenderThread())
		{
			OutRenderPassInfo.ColorRenderTargets[MRTIdx].Action = MakeRenderTargetActions(ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore);
		}
		else
		{
			OutRenderPassInfo.ColorRenderTargets[MRTIdx].Action = MakeRenderTargetActions(ColorLoadAction, ERenderTargetStoreAction::EStore);
		}
		OutRenderPassInfo.ColorRenderTargets[MRTIdx].RenderTarget = (*RenderTargets[MRTIdx])->GetTargetableRHI();
		OutRenderPassInfo.ColorRenderTargets[MRTIdx].ArraySlice = -1;
		OutRenderPassInfo.ColorRenderTargets[MRTIdx].MipIndex = 0;
	}

	return MRTCount;
}

int32 FSceneRenderTargets::GetGBufferRenderTargets(ERenderTargetLoadAction ColorLoadAction, FRHIRenderTargetView OutRenderTargets[MaxSimultaneousRenderTargets], int32& OutVelocityRTIndex) const
{
	const TRefCountPtr<IPooledRenderTarget>* RenderTargets[MaxSimultaneousRenderTargets];
	int32 GBufferDIndex;
	int32 MRTCount = GetGBufferRenderTargets(RenderTargets, OutVelocityRTIndex, GBufferDIndex);

	for (int32 MRTIdx = 0; MRTIdx < MRTCount; ++MRTIdx)
	{
		OutRenderTargets[MRTIdx] = FRHIRenderTargetView((*RenderTargets[MRTIdx])->GetTargetableRHI(), 0, -1, ColorLoadAction, ERenderTargetStoreAction::EStore);
	}

	return MRTCount;
}

int32 FSceneRenderTargets::GetGBufferRenderTargets(FRDGBuilder& GraphBuilder, ERenderTargetLoadAction ColorLoadAction, FRenderTargetBinding OutRenderTargets[MaxSimultaneousRenderTargets], int32& OutVelocityRTIndex) const
{
	const TRefCountPtr<IPooledRenderTarget>* RenderTargets[MaxSimultaneousRenderTargets];
	int32 GBufferDIndex;
	int32 MRTCount = GetGBufferRenderTargets(RenderTargets, OutVelocityRTIndex, GBufferDIndex);

	for (int32 MRTIdx = 0; MRTIdx < MRTCount; ++MRTIdx)
	{
		OutRenderTargets[MRTIdx] = FRenderTargetBinding(GraphBuilder.RegisterExternalTexture(*RenderTargets[MRTIdx], ERenderTargetTexture::Targetable), ColorLoadAction);
	}

	return MRTCount;
}

int32 FSceneRenderTargets::GetGBufferRenderTargets(FRDGBuilder& GraphBuilder, TStaticArray<FRDGTextureRef, MaxSimultaneousRenderTargets>& OutRenderTargets, int32& OutGBufferDIndex) const
{
	int32 OutVelocityRTIndex = -1;
	const TRefCountPtr<IPooledRenderTarget>* RenderTargets[MaxSimultaneousRenderTargets];
	const int32 Count = GetGBufferRenderTargets(RenderTargets, OutVelocityRTIndex, OutGBufferDIndex);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		OutRenderTargets[Index] = GraphBuilder.RegisterExternalTexture(*RenderTargets[Index], ERenderTargetTexture::Targetable);
	}
	return Count;
}

int32 FSceneRenderTargets::GetGBufferRenderTargets(FRDGBuilder& GraphBuilder, ERenderTargetLoadAction ColorLoadAction, FRenderTargetBindingSlots& OutRenderTargets) const
{
	int32 OutVelocityRTIndex = -1;
	const TRefCountPtr<IPooledRenderTarget>* RenderTargets[MaxSimultaneousRenderTargets];
	int32 GBufferDIndex;
	const int32 Count = GetGBufferRenderTargets(RenderTargets, OutVelocityRTIndex, GBufferDIndex);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		OutRenderTargets[Index] = FRenderTargetBinding(GraphBuilder.RegisterExternalTexture(*RenderTargets[Index], ERenderTargetTexture::Targetable), ColorLoadAction);
	}
	return Count;
}

FUnorderedAccessViewRHIRef FSceneRenderTargets::GetVirtualTextureFeedbackUAV() const
{
	return VirtualTextureFeedbackUAV.IsValid() ? VirtualTextureFeedbackUAV : GEmptyVertexBufferWithUAV->UnorderedAccessViewRHI;
}

int32 FSceneRenderTargets::GetVirtualTextureFeedbackScale()
{
	// Round to nearest power of two to ensure that shader maths is efficient and sampling sequence logic is simple.
	return FMath::RoundUpToPowerOfTwo(FMath::Max(GVirtualTextureFeedbackFactor, 1));
}

FIntPoint FSceneRenderTargets::GetVirtualTextureFeedbackBufferSize() const
{
	return FIntPoint::DivideAndRoundUp(BufferSize, FMath::Max(GetVirtualTextureFeedbackScale(), 1));
}

uint32 FSceneRenderTargets::SampleVirtualTextureFeedbackSequence(uint32 FrameIndex)
{
	const uint32 TileSize = GetVirtualTextureFeedbackScale();
	const uint32 TileSizeLog2 = FMath::CeilLogTwo(TileSize);
	const uint32 SequenceSize = FMath::Square(TileSize);
	const uint32 PixelIndex = FrameIndex % SequenceSize;
	const uint32 PixelAddress = ReverseBits(PixelIndex) >> (32U - 2 * TileSizeLog2);
	const uint32 X = FMath::ReverseMortonCode2(PixelAddress);
	const uint32 Y = FMath::ReverseMortonCode2(PixelAddress >> 1);
	const uint32 PixelSequenceIndex = X + Y * TileSize;
	return PixelSequenceIndex;
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
int32 FSceneRenderTargets::GetQuadOverdrawUAVIndex(EShaderPlatform Platform, ERHIFeatureLevel::Type FeatureLevel)
{
	if (IsSimpleForwardShadingEnabled(Platform))
	{
		return 1;
	}
	else if (IsForwardShadingEnabled(Platform))
	{
		return FVelocityRendering::BasePassCanOutputVelocity(FeatureLevel) ? 2 : 1;
	}
	else // GBuffer
	{
		return FVelocityRendering::BasePassCanOutputVelocity(FeatureLevel) ? 7 : 6;
	}
}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

void FSceneRenderTargets::ClearQuadOverdrawUAV(FRDGBuilder& GraphBuilder)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	if (AllowDebugViewShaderMode(DVSM_QuadComplexity, ShaderPlatform, CurrentFeatureLevel))
	{
		if (QuadOverdrawBuffer.IsValid() && QuadOverdrawBuffer->GetRenderTargetItem().UAV.IsValid())
		{
			FRDGTextureRef QuadOverdrawTexture = GraphBuilder.RegisterExternalTexture(QuadOverdrawBuffer);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(QuadOverdrawTexture), FUintVector4(0, 0, 0, 0));
		}
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

FUnorderedAccessViewRHIRef FSceneRenderTargets::GetQuadOverdrawBufferUAV() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (QuadOverdrawBuffer.IsValid() && QuadOverdrawBuffer->GetRenderTargetItem().UAV.IsValid())
	{
		// ShaderPlatform should only be tested if QuadOverdrawBuffer is allocated, to ensure CurrentFeatureLevel is valid.
		const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
		if (AllowDebugViewShaderMode(DVSM_QuadComplexity, ShaderPlatform, CurrentFeatureLevel))
		{
			return QuadOverdrawBuffer->GetRenderTargetItem().UAV;
		}
	}
#endif
	return GBlackTextureWithUAV->UnorderedAccessViewRHI;
}

int32 FSceneRenderTargets::GetNumGBufferTargets() const
{
	int32 NumGBufferTargets = 1;

	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
		if (IsUsingGBuffers(ShaderPlatform))
		{
			// This needs to match TBasePassPixelShaderBaseType::ModifyCompilationEnvironment()
			NumGBufferTargets = bAllowStaticLighting ? 6 : 5;
		}

		if (bAllocateVelocityGBuffer)
		{
			++NumGBufferTargets;
		}
	}
	return NumGBufferTargets;
}

void FSceneRenderTargets::AllocSceneColor(FRHICommandList& RHICmdList)
{
	TRefCountPtr<IPooledRenderTarget>& SceneColorTarget = GetSceneColorForCurrentShadingPath();
	if (SceneColorTarget && 
		SceneColorTarget->GetRenderTargetItem().TargetableTexture->HasClearValue() && 
		!(SceneColorTarget->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultColorClear))
	{
		const FLinearColor CurrentClearColor = SceneColorTarget->GetRenderTargetItem().TargetableTexture->GetClearBinding().GetClearColor();
		const FLinearColor NewClearColor = DefaultColorClear.GetClearColor();
		UE_LOG(LogRenderer, Log, TEXT("Releasing previous color target to switch default clear from: %f %f %f %f to: %f %f %f %f"), 
			CurrentClearColor.R, 
			CurrentClearColor.G, 
			CurrentClearColor.B, 
			CurrentClearColor.A, 
			NewClearColor.R, 
			NewClearColor.G, 
			NewClearColor.B, 
			NewClearColor.A);
		SceneColorTarget.SafeRelease();
	}

	if (GetSceneColorForCurrentShadingPath())
	{
		return;
	}

	EPixelFormat SceneColorBufferFormat = GetSceneColorFormat();

	// Mobile non-mobileHDR is the only platform rendering to a true sRGB buffer natively
	bool MobileHWsRGB = IsMobileColorsRGB() && IsMobilePlatform(GShaderPlatformForFeatureLevel[CurrentFeatureLevel]);

	// Create the scene color.
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, SceneColorBufferFormat, DefaultColorClear, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
		Desc.Flags |= GFastVRamConfig.SceneColor;
		Desc.NumSamples = CurrentMSAACount;
		Desc.ArraySize = bRequireMultiView ? 2 : 1;
		Desc.bIsArray = bRequireMultiView;

		if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5 && Desc.NumSamples == 1)
		{
			// GCNPerformanceTweets.pdf Tip 37: Warning: Causes additional synchronization between draw calls when using a render target allocated with this flag, use sparingly
			Desc.TargetableFlags |= TexCreate_UAV;
		}
		Desc.Flags |= MobileHWsRGB ? TexCreate_SRGB : TexCreate_None;

		// By default do not transition to writeable because of possible multiple target states
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GetSceneColorForCurrentShadingPath(), GetSceneColorTargetName(CurrentShadingPath));
	}

	check(GetSceneColorForCurrentShadingPath());
}

void FSceneRenderTargets::ReleaseGBufferTargets()
{
	GBufferA.SafeRelease();
	GBufferB.SafeRelease();
	GBufferC.SafeRelease();
	GBufferD.SafeRelease();
	GBufferE.SafeRelease();
	GBufferF.SafeRelease();
	SceneVelocity.SafeRelease();
}

void FSceneRenderTargets::PreallocGBufferTargets()
{
	bAllocateVelocityGBuffer = FVelocityRendering::BasePassCanOutputVelocity(CurrentFeatureLevel);
}

EPixelFormat FSceneRenderTargets::GetGBufferAFormat() const
{
	// good to see the quality loss due to precision in the gbuffer
	const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= EGBufferFormat::Force16BitsPerChannel);
	// good to profile the impact of non 8 bit formats
	const bool bEnforce8BitPerChannel = (CurrentGBufferFormat == EGBufferFormat::Force8BitsPerChannel);

	EPixelFormat NormalGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_A2B10G10R10;

	if (bEnforce8BitPerChannel)
	{
		NormalGBufferFormat = PF_B8G8R8A8;
	}
	else if (CurrentGBufferFormat == EGBufferFormat::HighPrecisionNormals)
	{
		NormalGBufferFormat = PF_FloatRGBA;
	}

	return NormalGBufferFormat;
}

EPixelFormat FSceneRenderTargets::GetGBufferBFormat() const
{
	// good to see the quality loss due to precision in the gbuffer
	const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= EGBufferFormat::Force16BitsPerChannel);

	const EPixelFormat SpecularGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_B8G8R8A8;

	return SpecularGBufferFormat;
}

EPixelFormat FSceneRenderTargets::GetGBufferCFormat() const
{
	// good to see the quality loss due to precision in the gbuffer
	const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= EGBufferFormat::Force16BitsPerChannel);

	const EPixelFormat DiffuseGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_B8G8R8A8;

	return DiffuseGBufferFormat;
}

EPixelFormat FSceneRenderTargets::GetGBufferDFormat() const
{
	return PF_B8G8R8A8;
}

EPixelFormat FSceneRenderTargets::GetGBufferEFormat() const
{
	return PF_B8G8R8A8;
}

EPixelFormat FSceneRenderTargets::GetGBufferFFormat() const
{
	// good to see the quality loss due to precision in the gbuffer
	const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= EGBufferFormat::Force16BitsPerChannel);
	// good to profile the impact of non 8 bit formats
	const bool bEnforce8BitPerChannel = (CurrentGBufferFormat == EGBufferFormat::Force8BitsPerChannel);

	EPixelFormat NormalGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_B8G8R8A8;

	if (bEnforce8BitPerChannel)
	{
		NormalGBufferFormat = PF_B8G8R8A8;
	}
	else if (CurrentGBufferFormat == EGBufferFormat::HighPrecisionNormals)
	{
		NormalGBufferFormat = PF_FloatRGBA;
	}

	return NormalGBufferFormat;
}

void FSceneRenderTargets::AllocGBufferTargets(FRHICommandList& RHICmdList)
{
	AllocGBufferTargets(RHICmdList, TexCreate_None);
}

void FSceneRenderTargets::AllocGBufferTargets(FRHICommandList& RHICmdList, ETextureCreateFlags AddTargetableFlags)
{	
	// AdjustGBufferRefCount +1 doesn't match -1 (within the same frame)
	ensure(GBufferRefCount == 0);

	if (GBufferA)
	{
		// no work needed
		return;
	}

	// create GBuffer on demand so it can be shared with other pooled RT
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);
	const bool bCanReadGBufferUniforms = (bUseGBuffer || IsSimpleForwardShadingEnabled(ShaderPlatform)) && CurrentFeatureLevel >= ERHIFeatureLevel::SM5;
	if (bUseGBuffer)
	{
		// Create the world-space normal g-buffer.
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferAFormat(), FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource | AddTargetableFlags , false));
			Desc.Flags |= GFastVRamConfig.GBufferA;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferA, TEXT("GBufferA"));
		}

		// Create the specular color and power g-buffer.
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferBFormat(), FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource | AddTargetableFlags, false));
			Desc.Flags |= GFastVRamConfig.GBufferB;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferB, TEXT("GBufferB"));
		}

		// Create the diffuse color g-buffer.
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferCFormat(), FClearValueBinding::Transparent, TexCreate_SRGB, TexCreate_RenderTargetable | TexCreate_ShaderResource | AddTargetableFlags, false));
			Desc.Flags |= GFastVRamConfig.GBufferC;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferC, TEXT("GBufferC"));
		}

		// Create the mask g-buffer (e.g. SSAO, subsurface scattering, wet surface mask, skylight mask, ...).
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferDFormat(), FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource | AddTargetableFlags | (!!CVarNoGBufferDClear.GetValueOnRenderThread() ? TexCreate_NoFastClear : TexCreate_None), false));
			Desc.Flags |= GFastVRamConfig.GBufferD;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferD, TEXT("GBufferD"));
		}

		if (bAllowStaticLighting)
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferEFormat(), FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			Desc.Flags |= GFastVRamConfig.GBufferE;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferE, TEXT("GBufferE"));
		}

		// Some mobile platforms may need to store SceneDepth into color buffer
		if (IsMobilePlatform(ShaderPlatform) && MobileRequiresSceneDepthAux(ShaderPlatform))
		{
			float FarDepth = (float)ERHIZBuffer::FarPlane;
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_R32_FLOAT, FClearValueBinding(FLinearColor(FarDepth,FarDepth,FarDepth,FarDepth)), TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource | AddTargetableFlags, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneDepthAux, TEXT("SceneDepthAux"));
		}

		// otherwise we have a severe problem
		check(GBufferA);
	}

	if (bAllocateVelocityGBuffer)
	{
		FPooledRenderTargetDesc VelocityRTDesc = Translate(FVelocityRendering::GetRenderTargetDesc(ShaderPlatform));
		VelocityRTDesc.Flags |= GFastVRamConfig.GBufferVelocity;
		GRenderTargetPool.FindFreeElement(RHICmdList, VelocityRTDesc, SceneVelocity, TEXT("GBufferVelocity"));
	}

	GBufferRefCount = 1;
}

const TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetSceneColor() const
{
	if (!GetSceneColorForCurrentShadingPath())
	{
		return GSystemTextures.BlackDummy;
	}

	return GetSceneColorForCurrentShadingPath();
}

bool FSceneRenderTargets::IsSceneColorAllocated() const
{
	return GetSceneColorForCurrentShadingPath() != 0;
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetSceneColor()
{
	if (!GetSceneColorForCurrentShadingPath())
	{
		return GSystemTextures.BlackDummy;
	}

	return GetSceneColorForCurrentShadingPath();
}

void FSceneRenderTargets::SetSceneColor(IPooledRenderTarget* In)
{
	check(CurrentShadingPath < EShadingPath::Num);
	SceneColor[(int32)GetSceneColorFormatType()] = In;
}

void FSceneRenderTargets::AdjustGBufferRefCount(FRHICommandList& RHICmdList, int Delta)
{
	if (Delta > 0 && GBufferRefCount == 0)
	{
		AllocGBufferTargets(RHICmdList);
	}
	else
	{
		GBufferRefCount += Delta;

		if (GBufferRefCount == 0)
		{
			ReleaseGBufferTargets();
		}
	}	
}

void FSceneRenderTargets::BeginRenderingPrePass(FRHICommandList& RHICmdList, bool bPerformClear, bool bStencilClear)
{
	check(RHICmdList.IsOutsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, BeginRenderingPrePass);

	FTexture2DRHIRef DepthTarget = GetSceneDepthSurface();

	RHICmdList.Transition(FRHITransitionInfo(DepthTarget, ERHIAccess::Unknown, ERHIAccess::DSVWrite | ERHIAccess::DSVRead));
	
	// No color target bound for the prepass.
	FRHIRenderPassInfo RPInfo;
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DepthTarget;
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
	
	if (bPerformClear)
	{				
		// Clear the depth buffer.
		// Note, this is a reversed Z depth surface, so 0.0f is the far plane.
		ERenderTargetActions StencilAction = bStencilClear ? ERenderTargetActions::Clear_Store : ERenderTargetActions::Load_Store;
		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Clear_Store, StencilAction);
	}
	else
	{
		// Set the scene depth surface and a DUMMY buffer as color buffer
		// (as long as it's the same dimension as the depth buffer),	
		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_Store, ERenderTargetActions::Load_Store);
	}
		
	RHICmdList.BeginRenderPass(RPInfo, TEXT("BeginRenderingPrePass"));
}

void FSceneRenderTargets::FinishRenderingPrePass(FRHICommandListImmediate& RHICmdList)
{
	check(RHICmdList.IsInsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, FinishRenderingPrePass);
	RHICmdList.EndRenderPass();

	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneDepthZ);
}

void FSceneRenderTargets::CleanUpEditorPrimitiveTargets()
{
	EditorPrimitivesDepth.SafeRelease();
	EditorPrimitivesColor.SafeRelease();
}

int32 FSceneRenderTargets::GetEditorMSAACompositingSampleCount() const
{
	int32 Value = 1;

	// only supported on SM5 yet (SM4 doesn't have MSAA sample load functionality which makes it harder to implement)
	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5 && GRHISupportsMSAADepthSampleAccess)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MSAA.CompositingSampleCount"));

		Value = CVar->GetValueOnRenderThread();

		if(Value <= 1)
		{
			Value = 1;
		}
		else if(Value <= 2)
		{
			Value = 2;
		}
		else if(Value <= 4)
		{
			Value = 4;
		}
		else
		{
			Value = 8;
		}
	}

	return Value;
}

const FTexture2DRHIRef& FSceneRenderTargets::GetEditorPrimitivesColor(FRHICommandList& RHICmdList)
{
	const bool bIsValid = IsValidRef(EditorPrimitivesColor);

	if( !bIsValid || EditorPrimitivesColor->GetDesc().NumSamples != GetEditorMSAACompositingSampleCount() )
	{
		// If the target is does not match the MSAA settings it needs to be recreated
		InitEditorPrimitivesColor(RHICmdList);
	}

	return (const FTexture2DRHIRef&)EditorPrimitivesColor->GetRenderTargetItem().TargetableTexture;
}


const FTexture2DRHIRef& FSceneRenderTargets::GetEditorPrimitivesDepth(FRHICommandList& RHICmdList)
{
	const bool bIsValid = IsValidRef(EditorPrimitivesDepth);

	if (!bIsValid || (CurrentFeatureLevel >= ERHIFeatureLevel::SM5 && EditorPrimitivesDepth->GetDesc().NumSamples != GetEditorMSAACompositingSampleCount()) )
	{
		// If the target is does not match the MSAA settings it needs to be recreated
		InitEditorPrimitivesDepth(RHICmdList);
	}

	return (const FTexture2DRHIRef&)EditorPrimitivesDepth->GetRenderTargetItem().TargetableTexture;
}

void FSceneRenderTargets::InitEditorPrimitivesColor(FRHICommandList& RHICmdList)
{
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, 
		PF_B8G8R8A8,
		FClearValueBinding::Transparent,
		TexCreate_None, 
		TexCreate_ShaderResource | TexCreate_RenderTargetable,
		false));

	Desc.bForceSharedTargetAndShaderResource = true;

	Desc.NumSamples = GetEditorMSAACompositingSampleCount();

	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, EditorPrimitivesColor, TEXT("EditorPrimitivesColor"));
}

void FSceneRenderTargets::InitEditorPrimitivesDepth(FRHICommandList& RHICmdList)
{
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, 
		PF_DepthStencil,
		FClearValueBinding::DepthFar,
		TexCreate_None, 
		TexCreate_ShaderResource | TexCreate_DepthStencilTargetable,
		false));

	Desc.bForceSharedTargetAndShaderResource = true;

	Desc.NumSamples = GetEditorMSAACompositingSampleCount();

	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, EditorPrimitivesDepth, TEXT("EditorPrimitivesDepth"));
}

void FSceneRenderTargets::SetBufferSize(int32 InBufferSizeX, int32 InBufferSizeY)
{
	QuantizeSceneBufferSize(FIntPoint(InBufferSizeX, InBufferSizeY), BufferSize);
}

void FSceneRenderTargets::AllocateMobileRenderTargets(FRHICommandListImmediate& RHICmdList)
{
	// on mobile we don't do on demand allocation of SceneColor yet (in other platforms it's released in the Tonemapper Process())
	AllocSceneColor(RHICmdList);
	AllocateCommonDepthTargets(RHICmdList);
	AllocateVirtualTextureFeedbackBuffer(RHICmdList);
	AllocateDebugViewModeTargets(RHICmdList);
}

// This is a helper class. It generates and provides N names with
// sequentially incremented postfix starting from 0.
// Example: SomeName0, SomeName1, ..., SomeName117
class FIncrementalNamesHolder
{
public:
	FIncrementalNamesHolder(const TCHAR* const Name, uint32 Size)
	{
		check(Size > 0);
		Names.Empty(Size);
		for (uint32 i = 0; i < Size; ++i)
		{
			Names.Add(FString::Printf(TEXT("%s%d"), Name, i));
		}
	}

	const TCHAR* const operator[] (uint32 Idx) const
	{
		CA_SUPPRESS(6385);	// Doesn't like COM
		return *Names[Idx];
	}

private:
	TArray<FString> Names;
};

// for easier use of "VisualizeTexture"
static const TCHAR* const GetVolumeName(uint32 Id, bool bDirectional)
{
	constexpr uint32 MaxNames = 128;
	static FIncrementalNamesHolder Names(TEXT("TranslucentVolume"), MaxNames);
	static FIncrementalNamesHolder NamesDir(TEXT("TranslucentVolumeDir"), MaxNames);

	check(Id < MaxNames);

	CA_SUPPRESS(6385);	// Doesn't like COM
	return bDirectional ? NamesDir[Id] : Names[Id];
}

void FSceneRenderTargets::AllocateReflectionTargets(FRHICommandList& RHICmdList, int32 TargetSize)
{
	if (GSupportsRenderTargetFormat_PF_FloatRGBA)
	{
		const int32 NumReflectionCaptureMips = FMath::CeilLogTwo(TargetSize) + 1;

		if (ReflectionColorScratchCubemap[0] && ReflectionColorScratchCubemap[0]->GetRenderTargetItem().TargetableTexture->GetNumMips() != NumReflectionCaptureMips)
		{
			ReflectionColorScratchCubemap[0].SafeRelease();
			ReflectionColorScratchCubemap[1].SafeRelease();
		}

		// Reflection targets are shared between both mobile and deferred shading paths. If we have already allocated for one and are now allocating for the other,
		// we can skip these targets.
		bool bSharedReflectionTargetsAllocated = ReflectionColorScratchCubemap[0] != nullptr;

		if (!bSharedReflectionTargetsAllocated)
		{
			// We write to these cubemap faces individually during filtering
			ETextureCreateFlags CubeTexFlags = TexCreate_TargetArraySlicesIndependently
				| TexCreate_DisableDCC // todo: temporary disable to avoid DCC copy failure
				;

			{
				// Create scratch cubemaps for filtering passes
				FPooledRenderTargetDesc Desc2(FPooledRenderTargetDesc::CreateCubemapDesc(TargetSize, PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 10000, 0, 0)), CubeTexFlags, TexCreate_RenderTargetable, false, 1, NumReflectionCaptureMips));
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc2, ReflectionColorScratchCubemap[0], TEXT("ReflectionColorScratchCubemap0"), ERenderTargetTransience::NonTransient );
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc2, ReflectionColorScratchCubemap[1], TEXT("ReflectionColorScratchCubemap1"), ERenderTargetTransience::NonTransient );
			}

			extern int32 GDiffuseIrradianceCubemapSize;
			const int32 NumDiffuseIrradianceMips = FMath::CeilLogTwo(GDiffuseIrradianceCubemapSize) + 1;

			{
				FPooledRenderTargetDesc Desc2(FPooledRenderTargetDesc::CreateCubemapDesc(GDiffuseIrradianceCubemapSize, PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 10000, 0, 0)), CubeTexFlags, TexCreate_RenderTargetable, false, 1, NumDiffuseIrradianceMips));
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc2, DiffuseIrradianceScratchCubemap[0], TEXT("DiffuseIrradianceScratchCubemap0"), ERenderTargetTransience::NonTransient);
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc2, DiffuseIrradianceScratchCubemap[1], TEXT("DiffuseIrradianceScratchCubemap1"), ERenderTargetTransience::NonTransient);
			}

			{
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(FSHVector3::MaxSHBasis, 1), PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 10000, 0, 0)), TexCreate_None, TexCreate_RenderTargetable, false));
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SkySHIrradianceMap, TEXT("SkySHIrradianceMap"), ERenderTargetTransience::NonTransient);
			}
		}
	}
}

void FSceneRenderTargets::AllocateVirtualTextureFeedbackBuffer(FRHICommandList& RHICmdList)
{
	if (UseVirtualTexturing(CurrentFeatureLevel))
	{
		const FIntPoint FeedbackSize = GetVirtualTextureFeedbackBufferSize();
		const int32 FeedbackSizeBytes = FeedbackSize.X * FeedbackSize.Y * sizeof(uint32);

		FRHIResourceCreateInfo CreateInfo(TEXT("VirtualTextureFeedbackGPU"));
		VirtualTextureFeedback = RHICreateVertexBuffer(FeedbackSizeBytes, BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess | BUF_SourceCopy, CreateInfo);
		VirtualTextureFeedbackUAV = RHICreateUnorderedAccessView(VirtualTextureFeedback, PF_R32_UINT);
	}
}

void FSceneRenderTargets::AllocateDebugViewModeTargets(FRHICommandList& RHICmdList)
{
	// If the shader/quad complexity shader need a quad overdraw buffer to be bind, allocate it.
	if (AllowDebugViewShaderMode(DVSM_QuadComplexity, GetFeatureLevelShaderPlatform(CurrentFeatureLevel), CurrentFeatureLevel))
	{
		FIntPoint QuadOverdrawSize;
		QuadOverdrawSize.X = 2 * FMath::Max<uint32>((BufferSize.X + 1) / 2, 1); // The size is time 2 since left side is QuadDescriptor, and right side QuadComplexity.
		QuadOverdrawSize.Y = FMath::Max<uint32>((BufferSize.Y + 1) / 2, 1);

		FPooledRenderTargetDesc QuadOverdrawDesc = FPooledRenderTargetDesc::Create2DDesc(
			QuadOverdrawSize, 
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_None,
			TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
			false
			);

		GRenderTargetPool.FindFreeElement(RHICmdList, QuadOverdrawDesc, QuadOverdrawBuffer, TEXT("QuadOverdrawBuffer"));
	}
}

void FSceneRenderTargets::AllocateCommonDepthTargets(FRHICommandList& RHICmdList)
{
	const bool bStereo = GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
	IStereoRenderTargetManager* const StereoRenderTargetManager = bStereo ? GEngine->StereoRenderingDevice->GetRenderTargetManager() : nullptr;

	if (SceneDepthZ && (!(SceneDepthZ->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultDepthClear) || (StereoRenderTargetManager && StereoRenderTargetManager->NeedReAllocateDepthTexture(SceneDepthZ))))
	{
		uint32 StencilCurrent, StencilNew;
		float DepthCurrent, DepthNew;
		SceneDepthZ->GetRenderTargetItem().TargetableTexture->GetClearBinding().GetDepthStencil(DepthCurrent, StencilCurrent);
		DefaultDepthClear.GetDepthStencil(DepthNew, StencilNew);
		UE_LOG(LogRenderer, Log, TEXT("Releasing previous depth to switch default clear from depth: %f stencil: %u to depth: %f stencil: %u"), DepthCurrent, StencilCurrent, DepthNew, StencilNew);
		SceneDepthZ.SafeRelease();
	}

	if (!SceneDepthZ || GFastVRamConfig.bDirty)
	{
		enum EPixelFormat DepthFormat = PF_DepthStencil;
		if (bRequireMultiView && CurrentShadingPath == EShadingPath::Mobile)
		{
			DepthFormat = PF_D24;
		}

		FTexture2DRHIRef DepthTex, SRTex;
		bHMDAllocatedDepthTarget = StereoRenderTargetManager && StereoRenderTargetManager->AllocateDepthTexture(0, BufferSize.X, BufferSize.Y, DepthFormat, 1, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead, DepthTex, SRTex);

		// Allow UAV depth?
		const ETextureCreateFlags textureUAVCreateFlags = GRHISupportsDepthUAV ? TexCreate_UAV : TexCreate_None;

		// Create a texture to store the resolved scene depth, and a render-targetable surface to hold the unresolved scene depth.
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, DepthFormat, DefaultDepthClear, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead | textureUAVCreateFlags, false));
		Desc.NumSamples = CurrentMSAACount;
		Desc.Flags |= GFastVRamConfig.SceneDepth;
		Desc.ArraySize = bRequireMultiView ? 2 : 1;
		Desc.bIsArray = bRequireMultiView;

		if (!bKeepDepthContent)
		{
			Desc.TargetableFlags |= TexCreate_Memoryless;
		}

		// Only defer texture allocation if we're an HMD-allocated target, and we're not MSAA.
		const bool bDeferTextureAllocation = bHMDAllocatedDepthTarget && Desc.NumSamples == 1;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneDepthZ, TEXT("SceneDepthZ"), ERenderTargetTransience::Transient, bDeferTextureAllocation);

		if (SceneDepthZ && bHMDAllocatedDepthTarget)
		{
			const uint32 OldElementSize = SceneDepthZ->ComputeMemorySize();
		
			{
				FSceneRenderTargetItem& Item = SceneDepthZ->GetRenderTargetItem();

				// If SRT and texture are different (MSAA), only modify the resolve render target, to avoid creating a swapchain of MSAA textures
				if (Item.ShaderResourceTexture == Item.TargetableTexture)
				{
					Item.TargetableTexture = SRTex;
				}

				Item.ShaderResourceTexture = SRTex;

				// Reset all RDG views on the shader resource texture, which will be pointing at the old
				// shader resource texture. The VR texture should really be an untracked pool item. This
				// manual override is really dangerous and is going to get removed in the RDG conversion.
				auto& LocalSceneDepthZ = static_cast<FPooledRenderTarget&>(*SceneDepthZ);
				if (LocalSceneDepthZ.HasRDG())
				{
					LocalSceneDepthZ.InitRDG();
				}
				LocalSceneDepthZ.InitPassthroughRDG();
			}

			GRenderTargetPool.UpdateElementSize(SceneDepthZ, OldElementSize);
		}

		SceneStencilSRV.SafeRelease();
	}

	// We need to update the stencil SRV every frame if the depth target was allocated by an HMD.
	// TODO: This should be handled by the HMD depth target swap chain, but currently it only updates the depth SRV.
	if (bHMDAllocatedDepthTarget)
	{
		SceneStencilSRV.SafeRelease();
	}

	if (SceneDepthZ && !SceneStencilSRV)
	{
		SceneStencilSRV = RHICreateShaderResourceView((FTexture2DRHIRef&)SceneDepthZ->GetRenderTargetItem().TargetableTexture, 0, 1, PF_X24_G8);
	}
}

void FSceneRenderTargets::AllocateDeferredShadingPathRenderTargets(FRHICommandListImmediate& RHICmdList, const int32 NumViews)
{
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);

	AllocateCommonDepthTargets(RHICmdList);

	// Create a quarter-sized version of the scene depth.
	{
		FIntPoint SmallDepthZSize(FMath::Max<uint32>(BufferSize.X / SmallColorDepthDownsampleFactor, 1), FMath::Max<uint32>(BufferSize.Y / SmallColorDepthDownsampleFactor, 1));

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(SmallDepthZSize, PF_DepthStencil, FClearValueBinding::None, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, true));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SmallDepthZ, TEXT("SmallDepthZ"), ERenderTargetTransience::NonTransient);
	}

	// Create the required render targets if running Highend.
	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		// Create the screen space ambient occlusion buffer
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_G8, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			Desc.Flags |= GFastVRamConfig.ScreenSpaceAO;

			if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
			{
				// UAV is only needed to support "r.AmbientOcclusion.Compute"
				// todo: ideally this should be only UAV or RT, not both
				Desc.TargetableFlags |= TexCreate_UAV;
			}
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ScreenSpaceAO, TEXT("ScreenSpaceAO"), ERenderTargetTransience::NonTransient);
		}
		
		{
			const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

			// TODO: We can skip the and TLV allocations when rendering in forward shading mode
			ETextureCreateFlags TranslucencyTargetFlags = TexCreate_ShaderResource | TexCreate_RenderTargetable;
				
			if (!IsVulkanPlatform(ShaderPlatform))
			{
				TranslucencyTargetFlags |= TexCreate_ReduceMemoryWithTilingMode;
			}

			if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
			{
				TranslucencyTargetFlags |= TexCreate_UAV;
			}

			TranslucencyLightingVolumeAmbient.SetNum(NumViews * NumTranslucentVolumeRenderTargetSets);
			TranslucencyLightingVolumeDirectional.SetNum(NumViews * NumTranslucentVolumeRenderTargetSets);

			for (int32 RTSetIndex = 0; RTSetIndex < NumTranslucentVolumeRenderTargetSets * NumViews; RTSetIndex++)
			{
				GRenderTargetPool.FindFreeElement(
					RHICmdList,
					FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
						TranslucencyLightingVolumeDim,
						TranslucencyLightingVolumeDim,
						TranslucencyLightingVolumeDim,
						PF_FloatRGBA,
						FClearValueBinding::Transparent,
						TexCreate_None,
						TranslucencyTargetFlags,
						false,
						1,
						false)),
					TranslucencyLightingVolumeAmbient[RTSetIndex],
					GetVolumeName(RTSetIndex, false),
					ERenderTargetTransience::NonTransient
					);

				//Tests to catch UE-31578, UE-32536 and UE-22073 crash (Defferred Render Targets not being allocated)
				ensureMsgf(TranslucencyLightingVolumeAmbient[RTSetIndex], TEXT("Failed to allocate render target %s with dimension %i and flags %i"),
					GetVolumeName(RTSetIndex, false),
					TranslucencyLightingVolumeDim,
					TranslucencyTargetFlags);

				GRenderTargetPool.FindFreeElement(
					RHICmdList,
					FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
						TranslucencyLightingVolumeDim,
						TranslucencyLightingVolumeDim,
						TranslucencyLightingVolumeDim,
						PF_FloatRGBA,
						FClearValueBinding::Transparent,
						TexCreate_None,
						TranslucencyTargetFlags,
						false,
						1,
						false)),
					TranslucencyLightingVolumeDirectional[RTSetIndex],
					GetVolumeName(RTSetIndex, true),
					ERenderTargetTransience::NonTransient
					);

				//Tests to catch UE-31578, UE-32536 and UE-22073 crash
				ensureMsgf(TranslucencyLightingVolumeDirectional[RTSetIndex], TEXT("Failed to allocate render target %s with dimension %i and flags %i"),
					GetVolumeName(RTSetIndex, true),
					TranslucencyLightingVolumeDim,
					TranslucencyTargetFlags);
			}

			//these get bound even with the CVAR off, make sure they aren't full of garbage.
			if (!GUseTranslucentLightingVolumes)
			{
				ClearTranslucentVolumeLighting(RHICmdList, 0);
			}
		}
	}

	// LPV : Dynamic directional occlusion for diffuse and specular
	if(UseLightPropagationVolumeRT(CurrentFeatureLevel))
	{
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_R8G8, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DirectionalOcclusion, TEXT("DirectionalOcclusion"));
		}

		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
			{
				Desc.TargetableFlags |= TexCreate_UAV;
			}
			Desc.Flags |= GFastVRamConfig.LightAccumulation;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, LightAccumulation, TEXT("LightAccumulation"), ERenderTargetTransience::NonTransient);
		}
	}

	if (bAllocateVelocityGBuffer)
	{
		FPooledRenderTargetDesc VelocityRTDesc = Translate(FVelocityRendering::GetRenderTargetDesc(ShaderPlatform));
		VelocityRTDesc.Flags |= GFastVRamConfig.GBufferVelocity;
		GRenderTargetPool.FindFreeElement(RHICmdList, VelocityRTDesc, SceneVelocity, TEXT("GBufferVelocity"));
	}

	AllocateVirtualTextureFeedbackBuffer(RHICmdList);

	AllocateDebugViewModeTargets(RHICmdList);
}

void FSceneRenderTargets::AllocateAnisotropyTarget(FRHICommandListImmediate& RHICmdList)
{
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(
		BufferSize, 
		GetGBufferFFormat(), 
		FClearValueBinding({ 0.5f, 0.5f, 0.5f, 0.5f }), 
		TexCreate_None, 
		TexCreate_RenderTargetable | TexCreate_ShaderResource, 
		false));

	Desc.Flags |= GFastVRamConfig.GBufferF;
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferF, TEXT("GBufferF"));
}

EPixelFormat FSceneRenderTargets::GetDesiredMobileSceneColorFormat() const
{
	const EPixelFormat defaultLowpFormat = IHeadMountedDisplayModule::IsAvailable() && IHeadMountedDisplayModule::Get().IsStandaloneStereoOnlyDevice()
		? PF_R8G8B8A8 : PF_B8G8R8A8;
	EPixelFormat DefaultColorFormat = (!IsMobileHDR() || !GSupportsRenderTargetFormat_PF_FloatRGBA) ? defaultLowpFormat : PF_FloatRGBA;
	if (IsMobileDeferredShadingEnabled(GMaxRHIShaderPlatform))
	{
		DefaultColorFormat = PF_FloatR11G11B10;
	}
	check(GPixelFormats[DefaultColorFormat].Supported);

	EPixelFormat MobileSceneColorBufferFormat = DefaultColorFormat;
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.SceneColorFormat"));
	int32 MobileSceneColor = CVar->GetValueOnRenderThread();
	switch (MobileSceneColor)
	{
		case 1:
			MobileSceneColorBufferFormat = PF_FloatRGBA; break;
		case 2:
			MobileSceneColorBufferFormat = PF_FloatR11G11B10; break;
		case 3:
			MobileSceneColorBufferFormat = defaultLowpFormat; break;
		default:
		break;
	}

	return GPixelFormats[MobileSceneColorBufferFormat].Supported ? MobileSceneColorBufferFormat : DefaultColorFormat;
}

EPixelFormat FSceneRenderTargets::GetMobileSceneColorFormat() const
{
	return CurrentMobileSceneColorFormat;
}

void FSceneRenderTargets::ClearTranslucentVolumeLighting(FRHICommandListImmediate& RHICmdList, int32 ViewIndex)
{
	if (GSupportsVolumeTextureRendering)
	{
		// Clear all volume textures in the same draw with MRT, which is faster than individually
		static_assert(TVC_MAX == 2, "Only expecting two translucency lighting cascades.");
		static IConsoleVariable* CVarTranslucencyVolumeBlur =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.TranslucencyVolumeBlur"));
		static constexpr int32 Num3DTextures = NumTranslucentVolumeRenderTargetSets << 1;

		FRHITexture* RenderTargets[Num3DTextures];
		bool bUseTransLightingVolBlur = CVarTranslucencyVolumeBlur->GetInt() > 0;
		const int32 NumIterations = bUseTransLightingVolBlur ?
			NumTranslucentVolumeRenderTargetSets : NumTranslucentVolumeRenderTargetSets - 1;

		for (int32 Idx = 0; Idx < NumIterations; ++Idx)
		{
			RenderTargets[Idx << 1] = TranslucencyLightingVolumeAmbient[Idx + NumTranslucentVolumeRenderTargetSets * ViewIndex]->GetRenderTargetItem().TargetableTexture;
			RenderTargets[(Idx << 1) + 1] = TranslucencyLightingVolumeDirectional[Idx + NumTranslucentVolumeRenderTargetSets * ViewIndex]->GetRenderTargetItem().TargetableTexture;
		}

		static const FLinearColor ClearColors[Num3DTextures] = { FLinearColor::Transparent };

		if (bUseTransLightingVolBlur)
		{
			ClearVolumeTextures<Num3DTextures>(RHICmdList, CurrentFeatureLevel, RenderTargets, ClearColors);
		}
		else
		{
			ClearVolumeTextures<Num3DTextures - 2>(RHICmdList, CurrentFeatureLevel, RenderTargets, ClearColors);
		}
	}
}

/** Helper function that clears the given volume texture render targets. */
template<int32 NumRenderTargets>
void FSceneRenderTargets::ClearVolumeTextures(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors)
{
	check(!RHICmdList.IsInsideRenderPass());

	FRHIRenderPassInfo RPInfo(NumRenderTargets, RenderTargets, ERenderTargetActions::DontLoad_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);

	RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearVolumeTextures"));
	{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

	const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

	const FVolumeBounds VolumeBounds(TranslucencyLightingVolumeDim);
	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FWriteToSliceVS> VertexShader(ShaderMap);
	TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(ShaderMap);
	TOneColorPixelShaderMRT::FPermutationDomain PermutationVector;
	PermutationVector.Set<TOneColorPixelShaderMRT::TOneColorPixelShaderNumOutputs>(NumRenderTargets);
	TShaderMapRef<TOneColorPixelShaderMRT>PixelShader(ShaderMap, PermutationVector);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
#endif
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(TranslucencyLightingVolumeDim));
	if (GeometryShader.IsValid())
	{
		GeometryShader->SetParameters(RHICmdList, VolumeBounds.MinZ);
	}
	PixelShader->SetColors(RHICmdList, ClearColors, NumRenderTargets);

	RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
	}
	RHICmdList.EndRenderPass();

	FRHITransitionInfo SRVTransitions[NumRenderTargets];
	for (int RT = 0; RT < NumRenderTargets; ++RT)
	{
		SRVTransitions[RT] = FRHITransitionInfo(RenderTargets[RT], ERHIAccess::Unknown, ERHIAccess::SRVMask);
	}
	RHICmdList.Transition(MakeArrayView(SRVTransitions, NumRenderTargets));
}
template void FSceneRenderTargets::ClearVolumeTextures<1>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<2>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<3>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<4>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<5>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<6>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<7>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<8>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);

EPixelFormat FSceneRenderTargets::GetSceneColorFormat() const
{
	return GetSceneColorFormat(CurrentFeatureLevel);
}

EPixelFormat FSceneRenderTargets::GetSceneColorFormat(ERHIFeatureLevel::Type InFeatureLevel) const
{
	EPixelFormat SceneColorBufferFormat = PF_FloatRGBA;

	if (InFeatureLevel < ERHIFeatureLevel::SM5)
	{
		return GetMobileSceneColorFormat();
	}
	else
    {
	    switch(CurrentSceneColorFormat)
	    {
		    case 0:
			    SceneColorBufferFormat = PF_R8G8B8A8; break;
		    case 1:
			    SceneColorBufferFormat = PF_A2B10G10R10; break;
		    case 2:	
			    SceneColorBufferFormat = PF_FloatR11G11B10; break;
		    case 3:	
			    SceneColorBufferFormat = PF_FloatRGB; break;
		    case 4:
			    // default
			    break;
		    case 5:
			    SceneColorBufferFormat = PF_A32B32G32R32F; break;
	    }
    
		// Fallback in case the scene color selected isn't supported.
	    if (!GPixelFormats[SceneColorBufferFormat].Supported)
	    {
		    SceneColorBufferFormat = PF_FloatRGBA;
	    }

		if (bRequireSceneColorAlpha)
		{
			SceneColorBufferFormat = PF_FloatRGBA;
		}
	}

	return SceneColorBufferFormat;
}

void FSceneRenderTargets::AllocateRenderTargets(FRHICommandListImmediate& RHICmdList, const int32 NumViews)
{
	if (BufferSize.X > 0 && BufferSize.Y > 0 && IsAllocateRenderTargetsRequired())
	{
		if ((EShadingPath)CurrentShadingPath == EShadingPath::Mobile)
		{
			AllocateMobileRenderTargets(RHICmdList);
		}
		else
		{
			AllocateDeferredShadingPathRenderTargets(RHICmdList, NumViews);
		}
	}
	else if ((EShadingPath)CurrentShadingPath == EShadingPath::Mobile && SceneDepthZ)
	{
		// If the render targets are already allocated, but the keep depth content flag has changed,
		// we need to reallocate the depth buffer.
		uint32 DepthBufferFlags = SceneDepthZ->GetRenderTargetItem().TargetableTexture->GetFlags();
		bool bCurrentKeepDepthContent = (DepthBufferFlags & TexCreate_Memoryless) == 0;
		if (bCurrentKeepDepthContent != bKeepDepthContent)
		{
			SceneDepthZ.SafeRelease();
			// Make sure the old depth buffer is freed by flushing the target pool.
			GRenderTargetPool.FreeUnusedResources();
			AllocateCommonDepthTargets(RHICmdList);
		}
	}
}

void FSceneRenderTargets::ReleaseSceneColor()
{
	for (auto i = 0; i < (int32)ESceneColorFormatType::Num; ++i)
	{
		SceneColor[i].SafeRelease();
	}
	// Releases what might be part of a temporal history.
	{
		SceneDepthZ.SafeRelease();
		GBufferA.SafeRelease();
	}
}

void FSceneRenderTargets::ReleaseAllTargets()
{
	ReleaseGBufferTargets();

	ReleaseSceneColor();

	SceneDepthZ.SafeRelease();
	SceneStencilSRV.SafeRelease();
	SmallDepthZ.SafeRelease();
	DBufferA.SafeRelease();
	DBufferB.SafeRelease();
	DBufferC.SafeRelease();
	ScreenSpaceAO.SafeRelease();
	ScreenSpaceGTAOHorizons.SafeRelease();
	QuadOverdrawBuffer.SafeRelease();
	LightAccumulation.SafeRelease();
	DirectionalOcclusion.SafeRelease();
	CustomDepth.SafeRelease();
	MobileCustomDepth.SafeRelease();
	MobileCustomStencil.SafeRelease();
	CustomStencilSRV.SafeRelease();
	VirtualTextureFeedback.SafeRelease();
	VirtualTextureFeedbackUAV.SafeRelease();

	for (int32 i = 0; i < UE_ARRAY_COUNT(ReflectionColorScratchCubemap); i++)
	{
		ReflectionColorScratchCubemap[i].SafeRelease();
	}

	for (int32 i = 0; i < UE_ARRAY_COUNT(DiffuseIrradianceScratchCubemap); i++)
	{
		DiffuseIrradianceScratchCubemap[i].SafeRelease();
	}

	SkySHIrradianceMap.SafeRelease();

	ensure(TranslucencyLightingVolumeAmbient.Num() == TranslucencyLightingVolumeDirectional.Num());
	for (int32 RTSetIndex = 0; RTSetIndex < TranslucencyLightingVolumeAmbient.Num(); RTSetIndex++)
	{
		TranslucencyLightingVolumeAmbient[RTSetIndex].SafeRelease();
		TranslucencyLightingVolumeDirectional[RTSetIndex].SafeRelease();
	}

	EditorPrimitivesColor.SafeRelease();
	EditorPrimitivesDepth.SafeRelease();

	SceneDepthAux.SafeRelease();
}

void FSceneRenderTargets::ReleaseDynamicRHI()
{
	ReleaseAllTargets();
	GRenderTargetPool.FreeUnusedResources();
}

/** Returns the size of the shadow depth buffer, taking into account platform limitations and game specific resolution limits. */
FIntPoint FSceneRenderTargets::GetShadowDepthTextureResolution() const
{
	int32 MaxShadowRes = CurrentMaxShadowResolution;
	const FIntPoint ShadowBufferResolution(
			FMath::Clamp(MaxShadowRes,1,(int32)GMaxShadowDepthBufferSizeX),
			FMath::Clamp(MaxShadowRes,1,(int32)GMaxShadowDepthBufferSizeY));
	
	return ShadowBufferResolution;
}

FIntPoint FSceneRenderTargets::GetPreShadowCacheTextureResolution() const
{
	const FIntPoint ShadowDepthResolution = GetShadowDepthTextureResolution();
	// Higher numbers increase cache hit rate but also memory usage
	const int32 ExpandFactor = 2;

	static auto CVarPreShadowResolutionFactor = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.Shadow.PreShadowResolutionFactor"));

	float Factor = CVarPreShadowResolutionFactor->GetValueOnRenderThread();

	FIntPoint Ret;

	Ret.X = FMath::Clamp(FMath::TruncToInt(ShadowDepthResolution.X * Factor) * ExpandFactor, 1, (int32)GMaxShadowDepthBufferSizeX);
	Ret.Y = FMath::Clamp(FMath::TruncToInt(ShadowDepthResolution.Y * Factor) * ExpandFactor, 1, (int32)GMaxShadowDepthBufferSizeY);

	return Ret;
}

FIntPoint FSceneRenderTargets::GetTranslucentShadowDepthTextureResolution() const
{
	FIntPoint ShadowDepthResolution = GetShadowDepthTextureResolution();

	int32 Factor = GetTranslucentShadowDownsampleFactor();

	ShadowDepthResolution.X = FMath::Clamp(ShadowDepthResolution.X / Factor, 1, (int32)GMaxShadowDepthBufferSizeX);
	ShadowDepthResolution.Y = FMath::Clamp(ShadowDepthResolution.Y / Factor, 1, (int32)GMaxShadowDepthBufferSizeY);

	return ShadowDepthResolution;
}

const FTextureRHIRef& FSceneRenderTargets::GetSceneColorSurface() const							
{
	if (!GetSceneColorForCurrentShadingPath())
	{
		return GBlackTexture->TextureRHI;
	}

	return (const FTextureRHIRef&)GetSceneColor()->GetRenderTargetItem().TargetableTexture;
}

const FTextureRHIRef& FSceneRenderTargets::GetSceneColorTexture() const
{
	if (!GetSceneColorForCurrentShadingPath())
	{
		return GBlackTexture->TextureRHI;
	}

	return (const FTextureRHIRef&)GetSceneColor()->GetRenderTargetItem().ShaderResourceTexture; 
}

const FUnorderedAccessViewRHIRef& FSceneRenderTargets::GetSceneColorTextureUAV() const
{
	check(GetSceneColorForCurrentShadingPath());

	return (const FUnorderedAccessViewRHIRef&)GetSceneColor()->GetRenderTargetItem().UAV;
}

FCustomDepthTextures FSceneRenderTargets::RequestCustomDepth(FRDGBuilder& GraphBuilder, bool bPrimitives)
{
	FCustomDepthTextures CustomDepthTextures{};

	const int CustomDepthValue = CVarCustomDepth.GetValueOnRenderThread();
	const bool bWritesCustomStencilValues = IsCustomDepthPassWritingStencil(CurrentFeatureLevel);

	const bool bMobilePath = (CurrentFeatureLevel <= ERHIFeatureLevel::ES3_1);
	const int32 DownsampleFactor = bMobilePath && CVarMobileCustomDepthDownSample.GetValueOnRenderThread() > 0 ? 2 : 1;

	if ((CustomDepthValue == 1 && bPrimitives) || CustomDepthValue == 2 || bWritesCustomStencilValues)
	{
		CustomDepthTextures.CustomDepth = TryRegisterExternalTexture(GraphBuilder, CustomDepth);
		if (bMobilePath)
		{
			CustomDepthTextures.MobileCustomDepth = TryRegisterExternalTexture(GraphBuilder, MobileCustomDepth);
			CustomDepthTextures.MobileCustomStencil = TryRegisterExternalTexture(GraphBuilder, MobileCustomStencil);
		}

		const FIntPoint CustomDepthBufferSize = FIntPoint::DivideAndRoundUp(BufferSize, DownsampleFactor);

		bool bHasValidCustomDepth = (CustomDepthTextures.CustomDepth && CustomDepthBufferSize == CustomDepthTextures.CustomDepth->Desc.Extent && !GFastVRamConfig.bDirty);
		bool bHasValidCustomStencil;
		if (bMobilePath)
		{
			bHasValidCustomStencil = (CustomDepthTextures.MobileCustomStencil && CustomDepthBufferSize == CustomDepthTextures.MobileCustomStencil->Desc.Extent) &&
			                         // Use memory less when stencil writing is disabled and vice versa
			                         bWritesCustomStencilValues == ((CustomDepthTextures.MobileCustomStencil->Desc.Flags & TexCreate_Memoryless) == 0);
		}
		else
		{
			bHasValidCustomStencil = CustomStencilSRV.IsValid();
		}

		if (!(bHasValidCustomDepth && bHasValidCustomStencil))
		{
			// Skip depth decompression, custom depth doesn't benefit from it
			// Also disables fast clears, but typically only a small portion of custom depth is written to anyway
			ETextureCreateFlags CustomDepthFlags = GFastVRamConfig.CustomDepth | TexCreate_NoFastClear | TexCreate_DepthStencilTargetable | TexCreate_ShaderResource;
			if (bMobilePath)
			{
				CustomDepthFlags |= TexCreate_Memoryless;
			}

			// Todo: Could check if writes stencil here and create min viable target
			const FRDGTextureDesc CustomDepthDesc = FRDGTextureDesc::Create2D(CustomDepthBufferSize, PF_DepthStencil, FClearValueBinding::DepthFar, CustomDepthFlags);

			CustomDepthTextures.CustomDepth = GraphBuilder.CreateTexture(CustomDepthDesc, TEXT("CustomDepth"));
			ConvertToExternalTexture(GraphBuilder, CustomDepthTextures.CustomDepth, CustomDepth);
			
			if (bMobilePath)
			{
				const float DepthFar = (float)ERHIZBuffer::FarPlane;
				const FClearValueBinding DepthFarColor = FClearValueBinding(FLinearColor(DepthFar, DepthFar, DepthFar, DepthFar));

				ETextureCreateFlags MobileCustomDepthFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
				ETextureCreateFlags MobileCustomStencilFlags = MobileCustomDepthFlags;
				if (!bWritesCustomStencilValues)
				{
					MobileCustomStencilFlags |= TexCreate_Memoryless;
				}

				const FRDGTextureDesc MobileCustomDepthDesc = FRDGTextureDesc::Create2D(CustomDepthBufferSize, PF_R16F, DepthFarColor, MobileCustomDepthFlags);
				const FRDGTextureDesc MobileCustomStencilDesc = FRDGTextureDesc::Create2D(CustomDepthBufferSize, PF_G8, FClearValueBinding::Transparent, MobileCustomStencilFlags);

				CustomDepthTextures.MobileCustomDepth = GraphBuilder.CreateTexture(MobileCustomDepthDesc, TEXT("MobileCustomDepth"));
				CustomDepthTextures.MobileCustomStencil = GraphBuilder.CreateTexture(MobileCustomStencilDesc, TEXT("MobileCustomStencil"));

				ConvertToExternalTexture(GraphBuilder, CustomDepthTextures.MobileCustomDepth, MobileCustomDepth);
				ConvertToExternalTexture(GraphBuilder, CustomDepthTextures.MobileCustomStencil, MobileCustomStencil);

				CustomStencilSRV.SafeRelease();
			}
			else
			{
				CustomStencilSRV = RHICreateShaderResourceView((FTexture2DRHIRef&)CustomDepth->GetRenderTargetItem().TargetableTexture, 0, 1, PF_X24_G8);
			}
		}
	}

	return CustomDepthTextures;
}

bool FSceneRenderTargets::IsCustomDepthPassWritingStencil(ERHIFeatureLevel::Type InFeatureLevel)
{
	int32 CustomDepthValue = CVarCustomDepth.GetValueOnRenderThread();
	// Mobile uses "On Demand" for both Depth and Stencil textures
	if (CustomDepthValue == 3 || (CustomDepthValue == 1 && InFeatureLevel <= ERHIFeatureLevel::ES3_1))
	{
		return true;
	}
	return false;
}

/** Returns an index in the range [0, NumCubeShadowDepthSurfaces) given an input resolution. */
int32 FSceneRenderTargets::GetCubeShadowDepthZIndex(int32 ShadowResolution) const
{
	static auto CVarMinShadowResolution = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.MinResolution"));
	FIntPoint ObjectShadowBufferResolution = GetShadowDepthTextureResolution();

	// Use a lower resolution because cubemaps use a lot of memory
	ObjectShadowBufferResolution.X /= 2;
	ObjectShadowBufferResolution.Y /= 2;
	const int32 SurfaceSizes[NumCubeShadowDepthSurfaces] =
	{
		ObjectShadowBufferResolution.X,
		ObjectShadowBufferResolution.X / 2,
		ObjectShadowBufferResolution.X / 4,
		ObjectShadowBufferResolution.X / 8,
		CVarMinShadowResolution->GetValueOnRenderThread()
	};

	for (int32 SearchIndex = 0; SearchIndex < NumCubeShadowDepthSurfaces; SearchIndex++)
	{
		if (ShadowResolution >= SurfaceSizes[SearchIndex])
		{
			return SearchIndex;
		}
	}

	check(0);
	return 0;
}

/** Returns the appropriate resolution for a given cube shadow index. */
int32 FSceneRenderTargets::GetCubeShadowDepthZResolution(int32 ShadowIndex) const
{
	checkSlow(ShadowIndex >= 0 && ShadowIndex < NumCubeShadowDepthSurfaces);

	static auto CVarMinShadowResolution = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.MinResolution"));
	FIntPoint ObjectShadowBufferResolution = GetShadowDepthTextureResolution();

	// Use a lower resolution because cubemaps use a lot of memory
	ObjectShadowBufferResolution.X = FMath::Max(ObjectShadowBufferResolution.X / 2, 1);
	ObjectShadowBufferResolution.Y = FMath::Max(ObjectShadowBufferResolution.Y / 2, 1);
	const int32 SurfaceSizes[NumCubeShadowDepthSurfaces] =
	{
		ObjectShadowBufferResolution.X,
		FMath::Max(ObjectShadowBufferResolution.X / 2, 1),
		FMath::Max(ObjectShadowBufferResolution.X / 4, 1),
		FMath::Max(ObjectShadowBufferResolution.X / 8, 1),
		CVarMinShadowResolution->GetValueOnRenderThread()
	};
	return SurfaceSizes[ShadowIndex];
}

bool FSceneRenderTargets::AreRenderTargetClearsValid(ESceneColorFormatType InSceneColorFormatType) const
{
	switch (InSceneColorFormatType)
	{
	case ESceneColorFormatType::Mobile:
		{
			const TRefCountPtr<IPooledRenderTarget>& SceneColorTarget = GetSceneColorForCurrentShadingPath();
			const bool bColorValid = SceneColorTarget && (SceneColorTarget->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultColorClear);
			const bool bDepthValid = SceneDepthZ && (SceneDepthZ->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultDepthClear);
			return bColorValid && bDepthValid;
		}
	default:
		{
			return true;
		}
	}
}

bool FSceneRenderTargets::AreShadingPathRenderTargetsAllocated(ESceneColorFormatType InSceneColorFormatType) const
{
	switch (InSceneColorFormatType)
	{
	case ESceneColorFormatType::Mobile:
		{
			return (SceneColor[(int32)ESceneColorFormatType::Mobile] != nullptr);
		}
	case ESceneColorFormatType::HighEndWithAlpha:
		{
			return (SceneColor[(int32)ESceneColorFormatType::HighEndWithAlpha] != nullptr);
		}
	case ESceneColorFormatType::HighEnd:
		{
			return (SceneColor[(int32)ESceneColorFormatType::HighEnd] != nullptr);
		}
	default:
		{
			checkNoEntry();
			return false;
		}
	}
}

bool FSceneRenderTargets::IsAllocateRenderTargetsRequired() const
{
	return !AreShadingPathRenderTargetsAllocated(GetSceneColorFormatType()) || !AreRenderTargetClearsValid(GetSceneColorFormatType());
}

/*-----------------------------------------------------------------------------
FSceneTextureUniformParameters
-----------------------------------------------------------------------------*/

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(SceneTextures);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FSceneTextureUniformParameters, "SceneTexturesStruct", SceneTextures);

void SetupSceneTextureUniformParameters(
	FRDGBuilder* GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	const FSceneRenderTargets& SceneContext,
	ESceneTextureSetupMode SetupMode,
	FSceneTextureUniformParameters& SceneTextureParameters)
{
	const auto GetRDG = [&](const TRefCountPtr<IPooledRenderTarget>& PooledRenderTarget, ERDGTextureFlags Flags = ERDGTextureFlags::None)
	{
		return RegisterExternalOrPassthroughTexture(GraphBuilder, PooledRenderTarget, Flags);
	};

	FRDGTextureRef WhiteDefault2D = GetRDG(GSystemTextures.WhiteDummy);
	FRDGTextureRef BlackDefault2D = GetRDG(GSystemTextures.BlackDummy);
	FRDGTextureRef DepthDefault = GetRDG(GSystemTextures.DepthDummy);
	check(WhiteDefault2D && BlackDefault2D && DepthDefault);

	// Scene Color / Depth
	{
		SceneTextureParameters.SceneColorTexture = BlackDefault2D;
		SceneTextureParameters.SceneDepthTexture = DepthDefault;

		if (EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::SceneColor))
		{
			SceneTextureParameters.SceneColorTexture = GetRDG(SceneContext.GetSceneColor());
		}

		if (EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::SceneDepth) && SceneContext.SceneDepthZ)
		{
			SceneTextureParameters.SceneDepthTexture = GetRDG(SceneContext.SceneDepthZ);
		}
	}

	// GBuffer
	{
		const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(FeatureLevel);
		const bool bCanReadGBufferUniforms = IsUsingGBuffers(ShaderPlatform) || IsSimpleForwardShadingEnabled(ShaderPlatform);

		// Allocate the Gbuffer resource uniform buffer.
		SceneTextureParameters.GBufferATexture = bCanReadGBufferUniforms && EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::GBufferA) && SceneContext.GBufferA ? GetRDG(SceneContext.GBufferA) : BlackDefault2D;
		SceneTextureParameters.GBufferBTexture = bCanReadGBufferUniforms && EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::GBufferB) && SceneContext.GBufferB ? GetRDG(SceneContext.GBufferB) : BlackDefault2D;
		SceneTextureParameters.GBufferCTexture = bCanReadGBufferUniforms && EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::GBufferC) && SceneContext.GBufferC ? GetRDG(SceneContext.GBufferC) : BlackDefault2D;
		SceneTextureParameters.GBufferDTexture = bCanReadGBufferUniforms && EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::GBufferD) && SceneContext.GBufferD ? GetRDG(SceneContext.GBufferD) : BlackDefault2D;
		SceneTextureParameters.GBufferETexture = bCanReadGBufferUniforms && EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::GBufferE) && SceneContext.GBufferE ? GetRDG(SceneContext.GBufferE) : BlackDefault2D;
		SceneTextureParameters.GBufferFTexture = bCanReadGBufferUniforms && EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::GBufferF) && SceneContext.GBufferF ? GetRDG(SceneContext.GBufferF) : BlackDefault2D;
	}

	// Velocity
	{
		SceneTextureParameters.GBufferVelocityTexture = EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::SceneVelocity) && SceneContext.SceneVelocity ? GetRDG(SceneContext.SceneVelocity) : BlackDefault2D;
	}

	// SSAO
	{
		const bool bSetupSSAO = EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::SSAO);
		SceneTextureParameters.ScreenSpaceAOTexture = bSetupSSAO && SceneContext.bScreenSpaceAOIsValid && SceneContext.ScreenSpaceAO ? GetRDG(SceneContext.ScreenSpaceAO) : WhiteDefault2D;
	}

	// Custom Depth / Stencil
	{
		const bool bSetupCustomDepth = EnumHasAnyFlags(SetupMode, ESceneTextureSetupMode::CustomDepth);

		FRDGTextureRef CustomDepth = DepthDefault;
		FRHIShaderResourceView* CustomStencilSRV = GSystemTextures.StencilDummySRV;

		if (SceneContext.bCustomDepthIsValid)
		{
			check(SceneContext.CustomDepth && SceneContext.CustomStencilSRV);
			CustomDepth = GetRDG(SceneContext.CustomDepth);
			CustomStencilSRV = SceneContext.CustomStencilSRV;
		}

		SceneTextureParameters.CustomDepthTexture = CustomDepth;
		SceneTextureParameters.CustomStencilTexture = CustomStencilSRV;
	}

	SceneTextureParameters.PointClampSampler = TStaticSamplerState<SF_Point>::GetRHI();
}

void SetupSceneTextureUniformParameters(
	const FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	FSceneTextureUniformParameters& SceneTextureParameters)
{
	SetupSceneTextureUniformParameters(nullptr, FeatureLevel, SceneContext, SetupMode, SceneTextureParameters);
}

void SetupSceneTextureUniformParameters(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	FSceneTextureUniformParameters& SceneTextureParameters)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	SetupSceneTextureUniformParameters(&GraphBuilder, FeatureLevel, SceneContext, SetupMode, SceneTextureParameters);
}

TUniformBufferRef<FSceneTextureUniformParameters> CreateSceneTextureUniformBuffer(
	FRHIComputeCommandList& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode)
{
	SCOPED_NAMED_EVENT_TEXT("CreateSceneTextureUniformBuffer", FColor::Magenta);
	FSceneTextureUniformParameters SceneTextures;
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SetupSceneTextureUniformParameters(nullptr, FeatureLevel, SceneContext, SetupMode, SceneTextures);
	return TUniformBufferRef<FSceneTextureUniformParameters>::CreateUniformBufferImmediate(SceneTextures, EUniformBufferUsage::UniformBuffer_SingleFrame);
}

TRDGUniformBufferRef<FSceneTextureUniformParameters> CreateSceneTextureUniformBuffer(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode)
{
	FSceneTextureUniformParameters* SceneTextures = GraphBuilder.AllocParameters<FSceneTextureUniformParameters>();
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	SetupSceneTextureUniformParameters(&GraphBuilder, FeatureLevel, SceneContext, SetupMode, *SceneTextures);
	return GraphBuilder.CreateUniformBuffer(SceneTextures);
}

EMobileSceneTextureSetupMode Translate(ESceneTextureSetupMode InSetupMode)
{
	EMobileSceneTextureSetupMode OutSetupMode = EMobileSceneTextureSetupMode::None;
	if (EnumHasAnyFlags(InSetupMode, ESceneTextureSetupMode::GBuffers))
	{
		OutSetupMode |= EMobileSceneTextureSetupMode::SceneColor;
	}
	if (EnumHasAnyFlags(InSetupMode, ESceneTextureSetupMode::CustomDepth))
	{
		OutSetupMode |= EMobileSceneTextureSetupMode::CustomDepth;
	}
	return OutSetupMode;
}

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FMobileSceneTextureUniformParameters, "MobileSceneTextures", SceneTextures);

static void SetupMobileSceneTextureUniformParameters(
	FRDGBuilder* GraphBuilder,
	const FSceneRenderTargets& SceneContext,
	EMobileSceneTextureSetupMode SetupMode,
	FMobileSceneTextureUniformParameters& SceneTextureParameters)
{
	const auto GetRDG = [&](const TRefCountPtr<IPooledRenderTarget>& PooledRenderTarget)
	{
		return RegisterExternalOrPassthroughTexture(GraphBuilder, PooledRenderTarget);
	};

	FRDGTextureRef BlackDefault2D = GetRDG(GSystemTextures.BlackDummy);
	FRDGTextureRef DepthDefault = GetRDG(GSystemTextures.DepthDummy);

	const bool bUseSceneTextures = EnumHasAnyFlags(SetupMode, EMobileSceneTextureSetupMode::SceneColor);

	SceneTextureParameters.SceneColorTexture = BlackDefault2D;
	SceneTextureParameters.SceneColorTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	SceneTextureParameters.SceneDepthTexture = DepthDefault;
	SceneTextureParameters.SceneDepthTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	SceneTextureParameters.SceneVelocityTexture = BlackDefault2D;
	SceneTextureParameters.SceneVelocityTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	if (bUseSceneTextures)
	{
		SceneTextureParameters.SceneColorTexture = GetRDG(SceneContext.GetSceneColor());

		if (SceneContext.SceneDepthZ && (SceneContext.SceneDepthZ->GetDesc().Flags & TexCreate_Memoryless) == 0)
		{
			SceneTextureParameters.SceneDepthTexture = GetRDG(SceneContext.SceneDepthZ);
		}
	}

	// These are color textures on mobile, BlackDummy is equal to MaxDepth with HAS_INVERTED_Z_BUFFER
	FRDGTextureRef CustomDepth = BlackDefault2D;
	FRDGTextureRef CustomStencil = BlackDefault2D;

	const bool bUseCustomDepth = EnumHasAnyFlags(SetupMode, EMobileSceneTextureSetupMode::CustomDepth) && SceneContext.bCustomDepthIsValid;

	if (bUseCustomDepth)
	{
		if (SceneContext.MobileCustomDepth)
		{
			CustomDepth = GetRDG(SceneContext.MobileCustomDepth);
		}
		
		if (SceneContext.MobileCustomStencil && (SceneContext.MobileCustomStencil->GetDesc().Flags & TexCreate_Memoryless) == 0)
		{
			CustomStencil = GetRDG(SceneContext.MobileCustomStencil);
		}
	}

	SceneTextureParameters.CustomDepthTexture = CustomDepth;
	SceneTextureParameters.CustomDepthTextureSampler = TStaticSamplerState<>::GetRHI();
	SceneTextureParameters.MobileCustomStencilTexture = CustomStencil;
	SceneTextureParameters.MobileCustomStencilTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	SceneTextureParameters.VirtualTextureFeedbackUAV = SceneContext.GetVirtualTextureFeedbackUAV();

	// Mobile GBuffer
	{
		const bool bCanReadGBufferUniforms = IsMobileDeferredShadingEnabled(GMaxRHIShaderPlatform);

		// Allocate the Gbuffer resource uniform buffer.
		const FSceneRenderTargetItem& GBufferAToUse = bCanReadGBufferUniforms && SceneContext.GBufferA ? SceneContext.GBufferA->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferBToUse = bCanReadGBufferUniforms && SceneContext.GBufferB ? SceneContext.GBufferB->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferCToUse = bCanReadGBufferUniforms && SceneContext.GBufferC ? SceneContext.GBufferC->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferDToUse = bCanReadGBufferUniforms && SceneContext.GBufferD ? SceneContext.GBufferD->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		// SceneDepthAux is a color texture on mobile, BlackDummy is equal to MaxDepth with HAS_INVERTED_Z_BUFFER
		const FSceneRenderTargetItem& SceneDepthAuxToUse = bCanReadGBufferUniforms && SceneContext.SceneDepthAux ? SceneContext.SceneDepthAux->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();

		SceneTextureParameters.GBufferATexture = GBufferAToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferBTexture = GBufferBToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferCTexture = GBufferCToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferDTexture = GBufferDToUse.ShaderResourceTexture;
		SceneTextureParameters.SceneDepthAuxTexture = SceneDepthAuxToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferATextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferBTextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferCTextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferDTextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.SceneDepthAuxTextureSampler = TStaticSamplerState<>::GetRHI();
	}

	const bool bUseSceneVelocity = EnumHasAnyFlags(SetupMode, EMobileSceneTextureSetupMode::SceneVelocity);

	if (bUseSceneVelocity && SceneContext.SceneVelocity)
	{
		SceneTextureParameters.SceneVelocityTexture = GetRDG(SceneContext.SceneVelocity);
	}
}

void SetupMobileSceneTextureUniformParameters(
	const FSceneRenderTargets& SceneContext,
	EMobileSceneTextureSetupMode SetupMode,
	FMobileSceneTextureUniformParameters& SceneTextureParameters)
{
	SetupMobileSceneTextureUniformParameters(nullptr, SceneContext, SetupMode, SceneTextureParameters);
}

TUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBuffer(
	FRHIComputeCommandList& RHICmdList,
	EMobileSceneTextureSetupMode SetupMode)
{
	FMobileSceneTextureUniformParameters SceneTextures;
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SetupMobileSceneTextureUniformParameters(nullptr, SceneContext, SetupMode, SceneTextures);
	return TUniformBufferRef<FMobileSceneTextureUniformParameters>::CreateUniformBufferImmediate(SceneTextures, EUniformBufferUsage::UniformBuffer_SingleFrame);
}

TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBuffer(
	FRDGBuilder& GraphBuilder,
	EMobileSceneTextureSetupMode SetupMode)
{
	FMobileSceneTextureUniformParameters* SceneTextures = GraphBuilder.AllocParameters<FMobileSceneTextureUniformParameters>();
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	SetupMobileSceneTextureUniformParameters(&GraphBuilder, SceneContext, SetupMode, *SceneTextures);
	return GraphBuilder.CreateUniformBuffer(SceneTextures);
}

FSceneTextureShaderParameters CreateSceneTextureShaderParameters(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode)
{
	FSceneTextureShaderParameters Parameters;
	if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Deferred)
	{
		Parameters.SceneTextures = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SetupMode);
	}
	else if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
	{
		Parameters.MobileSceneTextures = CreateMobileSceneTextureUniformBuffer(GraphBuilder, Translate(SetupMode));
	}
	return Parameters;
}

TRefCountPtr<FRHIUniformBuffer> CreateSceneTextureUniformBufferDependentOnShadingPath(
	FRHIComputeCommandList& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode)
{
	if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Deferred)
	{
		return CreateSceneTextureUniformBuffer(RHICmdList, FeatureLevel, SetupMode);
	}
	else if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
	{
		return CreateMobileSceneTextureUniformBuffer(RHICmdList, Translate(SetupMode));
	}
	checkNoEntry();
	return nullptr;
}

/** Deprecated APIs */

bool IsSceneTexturesValid(FRHICommandListImmediate& RHICmdList)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	return SceneContext.IsShadingPathValid();
}

void SetupMobileSceneTextureUniformParameters(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	bool bSceneTexturesValid,
	bool bCustomDepthIsValid,
	FMobileSceneTextureUniformParameters& SceneTextureParameters)
{
	EMobileSceneTextureSetupMode SetupMode = EMobileSceneTextureSetupMode::None;
	if (bSceneTexturesValid)
	{
		SetupMode |= EMobileSceneTextureSetupMode::SceneColor;
	}
	if (bCustomDepthIsValid)
	{
		SetupMode |= EMobileSceneTextureSetupMode::CustomDepth;
	}
	SetupMobileSceneTextureUniformParameters(nullptr, SceneContext, SetupMode, SceneTextureParameters);
}

void SetupMobileSceneTextureUniformParameters(
	FRDGBuilder& GraphBuilder,
	EMobileSceneTextureSetupMode SetupMode,
	FMobileSceneTextureUniformParameters& SceneTextureParameters)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	SetupMobileSceneTextureUniformParameters(&GraphBuilder, SceneContext, SetupMode, SceneTextureParameters);
}
