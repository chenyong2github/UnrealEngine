// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredShadingRenderer.cpp: Top level rendering loop for deferred shading
=============================================================================*/

#include "DeferredShadingRenderer.h"
#include "VelocityRendering.h"
#include "AtmosphereRendering.h"
#include "SkyAtmosphereRendering.h"
#include "ScenePrivate.h"
#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "CompositionLighting/CompositionLighting.h"
#include "FXSystem.h"
#include "OneColorShader.h"
#include "CompositionLighting/PostProcessDeferredDecals.h"
#include "CompositionLighting/PostProcessAmbientOcclusion.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "GlobalDistanceField.h"
#include "PostProcess/PostProcessing.h"
#include "DistanceFieldAtlas.h"
#include "EngineModule.h"
#include "SceneViewExtension.h"
#include "GPUSkinCache.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "RendererModule.h"
#include "VT/VirtualTextureSystem.h"
#include "GPUScene.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "ScreenSpaceRayTracing.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"
#include "ShaderPrint.h"

static TAutoConsoleVariable<int32> CVarStencilForLODDither(
	TEXT("r.StencilForLODDither"),
	0,
	TEXT("Whether to use stencil tests in the prepass, and depth-equal tests in the base pass to implement LOD dithering.\n")
	TEXT("If disabled, LOD dithering will be done through clip() instructions in the prepass and base pass, which disables EarlyZ.\n")
	TEXT("Forces a full prepass when enabled."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

TAutoConsoleVariable<int32> CVarCustomDepthOrder(
	TEXT("r.CustomDepth.Order"),
	1,	
	TEXT("When CustomDepth (and CustomStencil) is getting rendered\n")
	TEXT("  0: Before GBuffer (can be more efficient with AsyncCompute, allows using it in DBuffer pass, no GBuffer blending decals allow GBuffer compression)\n")
	TEXT("  1: After Base Pass (default)"),
	ECVF_RenderThreadSafe);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
static TAutoConsoleVariable<int32> CVarVisualizeTexturePool(
	TEXT("r.VisualizeTexturePool"),
	0,
	TEXT("Allows to enable the visualize the texture pool (currently only on console).\n")
	TEXT(" 0: off (default)\n")
	TEXT(" 1: on"),
	ECVF_Cheat | ECVF_RenderThreadSafe);
#endif

static TAutoConsoleVariable<int32> CVarClearCoatNormal(
	TEXT("r.ClearCoatNormal"),
	0,
	TEXT("0 to disable clear coat normal.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarIrisNormal(
	TEXT("r.IrisNormal"),
	0,
	TEXT("0 to disable iris normal.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on"),
	ECVF_ReadOnly);

int32 GbEnableAsyncComputeTranslucencyLightingVolumeClear = 1;
static FAutoConsoleVariableRef CVarEnableAsyncComputeTranslucencyLightingVolumeClear(
	TEXT("r.EnableAsyncComputeTranslucencyLightingVolumeClear"),
	GbEnableAsyncComputeTranslucencyLightingVolumeClear,
	TEXT("Whether to clear the translucency lighting volume using async compute.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability
);

int32 GDoPrepareDistanceFieldSceneAfterRHIFlush = 1;
static FAutoConsoleVariableRef CVarDoPrepareDistanceFieldSceneAfterRHIFlush(
	TEXT("r.DoPrepareDistanceFieldSceneAfterRHIFlush"),
	GDoPrepareDistanceFieldSceneAfterRHIFlush,
	TEXT("If true, then do the distance field scene after the RHI sync and flush. Improves pipelining."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarParallelBasePass(
	TEXT("r.ParallelBasePass"),
	1,
	TEXT("Toggles parallel base pass rendering. Parallel rendering must be enabled for this to have an effect."),
	ECVF_RenderThreadSafe
);

static int32 GRayTracing = 0;
static TAutoConsoleVariable<int32> CVarRayTracing(
	TEXT("r.RayTracing"),
	GRayTracing,
	TEXT("0 to disable ray tracing.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

static int32 GForceAllRayTracingEffects = -1;
static TAutoConsoleVariable<int32> CVarForceAllRayTracingEffects(
	TEXT("r.RayTracing.ForceAllRayTracingEffects"),
	GForceAllRayTracingEffects,
	TEXT("Force all ray tracing effects ON/OFF.\n")
	TEXT(" -1: Do not force (default) \n")
	TEXT(" 0: All ray tracing effects disabled\n")
	TEXT(" 1: All ray tracing effects enabled"),
	ECVF_RenderThreadSafe);

static int32 GRayTracingExcludeDecals = 0;
static FAutoConsoleVariableRef CRayTracingExcludeDecals(
	TEXT("r.RayTracing.ExcludeDecals"),
	GRayTracingExcludeDecals,
	TEXT("A toggle that modifies the inclusion of decals in the ray tracing BVH.\n")
	TEXT(" 0: Decals included in the ray tracing BVH (default)\n")
	TEXT(" 1: Decals excluded from the ray tracing BVH"),
	ECVF_RenderThreadSafe);

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarForceBlackVelocityBuffer(
	TEXT("r.Test.ForceBlackVelocityBuffer"), 0,
	TEXT("Force the velocity buffer to have no motion vector for debugging purpose."),
	ECVF_RenderThreadSafe);
#endif


DECLARE_CYCLE_STAT(TEXT("PostInitViews FlushDel"), STAT_PostInitViews_FlushDel, STATGROUP_InitViews);
DECLARE_CYCLE_STAT(TEXT("InitViews Intentional Stall"), STAT_InitViews_Intentional_Stall, STATGROUP_InitViews);

DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer UpdateDownsampledDepthSurface"), STAT_FDeferredShadingSceneRenderer_UpdateDownsampledDepthSurface, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Render Init"), STAT_FDeferredShadingSceneRenderer_Render_Init, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Render ServiceLocalQueue"), STAT_FDeferredShadingSceneRenderer_Render_ServiceLocalQueue, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer DistanceFieldAO Init"), STAT_FDeferredShadingSceneRenderer_DistanceFieldAO_Init, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer FGlobalDynamicVertexBuffer Commit"), STAT_FDeferredShadingSceneRenderer_FGlobalDynamicVertexBuffer_Commit, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer FXSystem PreRender"), STAT_FDeferredShadingSceneRenderer_FXSystem_PreRender, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer AllocGBufferTargets"), STAT_FDeferredShadingSceneRenderer_AllocGBufferTargets, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ClearLPVs"), STAT_FDeferredShadingSceneRenderer_ClearLPVs, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer DBuffer"), STAT_FDeferredShadingSceneRenderer_DBuffer, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer SetAndClearViewGBuffer"), STAT_FDeferredShadingSceneRenderer_SetAndClearViewGBuffer, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ClearGBufferAtMaxZ"), STAT_FDeferredShadingSceneRenderer_ClearGBufferAtMaxZ, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ResolveDepth After Basepass"), STAT_FDeferredShadingSceneRenderer_ResolveDepth_After_Basepass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Resolve After Basepass"), STAT_FDeferredShadingSceneRenderer_Resolve_After_Basepass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer FXSystem PostRenderOpaque"), STAT_FDeferredShadingSceneRenderer_FXSystem_PostRenderOpaque, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer AfterBasePass"), STAT_FDeferredShadingSceneRenderer_AfterBasePass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Lighting"), STAT_FDeferredShadingSceneRenderer_Lighting, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderLightShaftOcclusion"), STAT_FDeferredShadingSceneRenderer_RenderLightShaftOcclusion, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderAtmosphere"), STAT_FDeferredShadingSceneRenderer_RenderAtmosphere, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderFog"), STAT_FDeferredShadingSceneRenderer_RenderFog, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderLightShaftBloom"), STAT_FDeferredShadingSceneRenderer_RenderLightShaftBloom, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderFinish"), STAT_FDeferredShadingSceneRenderer_RenderFinish, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ViewExtensionPostRenderBasePass"), STAT_FDeferredShadingSceneRenderer_ViewExtensionPostRenderBasePass, STATGROUP_SceneRendering);

DECLARE_GPU_STAT_NAMED(RayTracingTLAS, TEXT("Ray Tracing Top Level Acceleration Structure"));
DECLARE_GPU_STAT(Postprocessing);
DECLARE_GPU_STAT(VisibilityCommands);
DECLARE_GPU_STAT(RenderDeferredLighting);
DECLARE_GPU_STAT(AllocateRendertargets);
DECLARE_GPU_STAT(FrameRenderFinish);
DECLARE_GPU_STAT(SortLights);
DECLARE_GPU_STAT(PostRenderOpsFX);
DECLARE_GPU_STAT(HZB);
DECLARE_GPU_STAT_NAMED(Unaccounted, TEXT("[unaccounted]"));

const TCHAR* GetDepthPassReason(bool bDitheredLODTransitionsUseStencil, EShaderPlatform ShaderPlatform)
{
	if (IsForwardShadingEnabled(ShaderPlatform))
	{
		return TEXT("(Forced by ForwardShading)");
	}

	bool bDBufferAllowed = IsUsingDBuffers(ShaderPlatform);

	if (bDBufferAllowed)
	{
		return TEXT("(Forced by DBuffer)");
	}

	if (bDitheredLODTransitionsUseStencil)
	{
		return TEXT("(Forced by StencilLODDither)");
	}

	return TEXT("");
}

/*-----------------------------------------------------------------------------
	FDeferredShadingSceneRenderer
-----------------------------------------------------------------------------*/

FDeferredShadingSceneRenderer::FDeferredShadingSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer)
	: FSceneRenderer(InViewFamily, HitProxyConsumer)
	, EarlyZPassMode(Scene ? Scene->EarlyZPassMode : DDM_None)
	, bEarlyZPassMovable(Scene ? Scene->bEarlyZPassMovable : false)
	, bClusteredShadingLightsInLightGrid(false)
{
	static const auto StencilLODDitherCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.StencilForLODDither"));
	bDitheredLODTransitionsUseStencil = StencilLODDitherCVar->GetValueOnAnyThread() != 0;

	// Shader complexity requires depth only pass to display masked material cost correctly
	if (ViewFamily.UseDebugViewPS() && ViewFamily.GetDebugViewShaderMode() != DVSM_OutputMaterialTextureScales)
	{
		EarlyZPassMode = DDM_AllOpaque;
		bEarlyZPassMovable = true;
	}
}

float GetSceneColorClearAlpha()
{
	// Scene color alpha is used during scene captures and planar reflections.  1 indicates background should be shown, 0 indicates foreground is fully present.
	return 1.0f;
}

/** 
* Clears view where Z is still at the maximum value (ie no geometry rendered)
*/
void FDeferredShadingSceneRenderer::ClearGBufferAtMaxZ(FRHICommandList& RHICmdList)
{
	// Assumes BeginRenderingSceneColor() has been called before this function
	check(RHICmdList.IsInsideRenderPass());
	SCOPED_DRAW_EVENT(RHICmdList, ClearGBufferAtMaxZ);

	// Clear the G Buffer render targets
	const bool bClearBlack = Views[0].Family->EngineShowFlags.ShaderComplexity || Views[0].Family->EngineShowFlags.StationaryLightOverlap;
	const float ClearAlpha = GetSceneColorClearAlpha();
	const FLinearColor ClearColor = bClearBlack ? FLinearColor(0, 0, 0, ClearAlpha) : FLinearColor(Views[0].BackgroundColor.R, Views[0].BackgroundColor.G, Views[0].BackgroundColor.B, ClearAlpha);
	FLinearColor ClearColors[MaxSimultaneousRenderTargets] = 
		{ClearColor, FLinearColor(0.5f,0.5f,0.5f,0), FLinearColor(0,0,0,1), FLinearColor(0,0,0,0), FLinearColor(0,1,1,1), FLinearColor(1,1,1,1), FLinearColor::Transparent, FLinearColor::Transparent};

	uint32 NumActiveRenderTargets = FSceneRenderTargets::Get(RHICmdList).GetNumGBufferTargets();
	
	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<TOneColorVS<true> > VertexShader(ShaderMap);
	FOneColorPS* PixelShader = NULL; 

	// Assume for now all code path supports SM4, otherwise render target numbers are changed
	switch(NumActiveRenderTargets)
	{
	case 5:
		{
			TShaderMapRef<TOneColorPixelShaderMRT<5> > MRTPixelShader(ShaderMap);
			PixelShader = *MRTPixelShader;
		}
		break;
	case 6:
		{
			TShaderMapRef<TOneColorPixelShaderMRT<6> > MRTPixelShader(ShaderMap);
			PixelShader = *MRTPixelShader;
		}
		break;
	default:
	case 1:
		{
			TShaderMapRef<TOneColorPixelShaderMRT<1> > MRTPixelShader(ShaderMap);
			PixelShader = *MRTPixelShader;
		}
		break;
	}

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// Opaque rendering, depth test but no depth writes
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	VertexShader->SetDepthParameter(RHICmdList, float(ERHIZBuffer::FarPlane));

	// Clear each viewport by drawing background color at MaxZ depth
	for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
	{
		SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("ClearView%d"), ViewIndex);

		FViewInfo& View = Views[ViewIndex];

		// Set viewport for this view
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);

		// Setup PS
		PixelShader->SetColors(RHICmdList, ClearColors, NumActiveRenderTargets);

		RHICmdList.SetStreamSource(0, GClearVertexBuffer.VertexBufferRHI, 0);
		// Render quad
		RHICmdList.DrawPrimitive(0, 2, 1);
	}
}

/** Render the TexturePool texture */
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void FDeferredShadingSceneRenderer::RenderVisualizeTexturePool(FRHICommandListImmediate& RHICmdList)
{
	TRefCountPtr<IPooledRenderTarget> VisualizeTexturePool;

	/** Resolution for the texture pool visualizer texture. */
	enum
	{
		TexturePoolVisualizerSizeX = 280,
		TexturePoolVisualizerSizeY = 140,
	};

	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(TexturePoolVisualizerSizeX, TexturePoolVisualizerSizeY), PF_B8G8R8A8, FClearValueBinding::None, TexCreate_None, TexCreate_None, false));
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, VisualizeTexturePool, TEXT("VisualizeTexturePool"));
	
	uint32 Pitch;
	FColor* TextureData = (FColor*)RHICmdList.LockTexture2D((FTexture2DRHIRef&)VisualizeTexturePool->GetRenderTargetItem().ShaderResourceTexture, 0, RLM_WriteOnly, Pitch, false);
	if(TextureData)
	{
		// clear with grey to get reliable background color
		FMemory::Memset(TextureData, 0x88, TexturePoolVisualizerSizeX * TexturePoolVisualizerSizeY * 4);
		RHICmdList.GetTextureMemoryVisualizeData(TextureData, TexturePoolVisualizerSizeX, TexturePoolVisualizerSizeY, Pitch, 4096);
	}

	RHICmdList.UnlockTexture2D((FTexture2DRHIRef&)VisualizeTexturePool->GetRenderTargetItem().ShaderResourceTexture, 0, false);

	FIntPoint RTExtent = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY();

	FVector2D Tex00 = FVector2D(0, 0);
	FVector2D Tex11 = FVector2D(1, 1);

//todo	VisualizeTexture(*VisualizeTexturePool, ViewFamily.RenderTarget, FIntRect(0, 0, RTExtent.X, RTExtent.Y), RTExtent, 1.0f, 0.0f, 0.0f, Tex00, Tex11, 1.0f, false);
}
#endif

/** 
* Finishes the view family rendering.
*/
void FDeferredShadingSceneRenderer::RenderFinish(FRHICommandListImmediate& RHICmdList)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		if(CVarVisualizeTexturePool.GetValueOnRenderThread())
		{
			RenderVisualizeTexturePool(RHICmdList);
		}
	}

#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	FSceneRenderer::RenderFinish(RHICmdList);

	// Some RT should be released as early as possible to allow sharing of that memory for other purposes.
	// SceneColor is be released in tone mapping, if not we want to get access to the HDR scene color after this pass so we keep it.
	// This becomes even more important with some limited VRam (XBoxOne).
	FSceneRenderTargets::Get(RHICmdList).SetLightAttenuation(0);
}

void BuildHZB( FRDGBuilder& GraphBuilder, FViewInfo& View );

/** 
* Renders the view family. 
*/

DEFINE_STAT(STAT_CLM_PrePass);
DECLARE_CYCLE_STAT(TEXT("FXPreRender"), STAT_CLM_FXPreRender, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterPrePass"), STAT_CLM_AfterPrePass, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("BasePass"), STAT_CLM_BasePass, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterBasePass"), STAT_CLM_AfterBasePass, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Lighting"), STAT_CLM_Lighting, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterLighting"), STAT_CLM_AfterLighting, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Translucency"), STAT_CLM_Translucency, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("RenderDistortion"), STAT_CLM_RenderDistortion, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterTranslucency"), STAT_CLM_AfterTranslucency, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("RenderDistanceFieldLighting"), STAT_CLM_RenderDistanceFieldLighting, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("LightShaftBloom"), STAT_CLM_LightShaftBloom, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("PostProcessing"), STAT_CLM_PostProcessing, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Velocity"), STAT_CLM_Velocity, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterVelocity"), STAT_CLM_AfterVelocity, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("TranslucentVelocity"), STAT_CLM_TranslucentVelocity, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterTranslucentVelocity"), STAT_CLM_AfterTranslucentVelocity, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("RenderFinish"), STAT_CLM_RenderFinish, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterFrame"), STAT_CLM_AfterFrame, STATGROUP_CommandListMarkers);

FGraphEventRef FDeferredShadingSceneRenderer::TranslucencyTimestampQuerySubmittedFence[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames + 1];
FGlobalDynamicIndexBuffer FDeferredShadingSceneRenderer::DynamicIndexBufferForInitViews;
FGlobalDynamicIndexBuffer FDeferredShadingSceneRenderer::DynamicIndexBufferForInitShadows;
FGlobalDynamicVertexBuffer FDeferredShadingSceneRenderer::DynamicVertexBufferForInitViews;
FGlobalDynamicVertexBuffer FDeferredShadingSceneRenderer::DynamicVertexBufferForInitShadows;
TGlobalResource<FGlobalDynamicReadBuffer> FDeferredShadingSceneRenderer::DynamicReadBufferForInitShadows;
TGlobalResource<FGlobalDynamicReadBuffer> FDeferredShadingSceneRenderer::DynamicReadBufferForInitViews;

/**
 * Returns true if the depth Prepass needs to run
 */
static FORCEINLINE bool NeedsPrePass(const FDeferredShadingSceneRenderer* Renderer)
{
	return (RHIHasTiledGPU(Renderer->ViewFamily.GetShaderPlatform()) == false) && 
		(Renderer->EarlyZPassMode != DDM_None || Renderer->bEarlyZPassMovable != 0);
}

bool FDeferredShadingSceneRenderer::RenderHzb(FRHICommandListImmediate& RHICmdList)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SCOPED_GPU_STAT(RHICmdList, HZB);

	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, SceneContext.GetSceneDepthSurface());

	static const auto ICVarHZBOcc = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HZBOcclusion"));
	bool bHZBOcclusion = ICVarHZBOcc->GetInt() != 0;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		FSceneViewState* ViewState = (FSceneViewState*)View.State;

		const bool bSSR  = ShouldRenderScreenSpaceReflections(View);
		const bool bSSAO = ShouldRenderScreenSpaceAmbientOcclusion(View);
		const bool bSSGI = ShouldRenderScreenSpaceDiffuseIndirect(View);

		if (bSSAO || bHZBOcclusion || bSSR || bSSGI)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			{
				RDG_EVENT_SCOPE(GraphBuilder, "BuildHZB(ViewId=%d)", ViewIndex);
				BuildHZB(GraphBuilder, Views[ViewIndex]);
			}
			GraphBuilder.Execute();
		}

		if (bHZBOcclusion && ViewState && ViewState->HZBOcclusionTests.GetNum() != 0)
		{
			check(ViewState->HZBOcclusionTests.IsValidFrame(ViewState->OcclusionFrameCounter));

			SCOPED_DRAW_EVENT(RHICmdList, HZB);
			ViewState->HZBOcclusionTests.Submit(RHICmdList, View);
		}
	}

	//async ssao only requires HZB and depth as inputs so get started ASAP
	if (CanOverlayRayTracingOutput(Views[0]) && GCompositionLighting.CanProcessAsyncSSAO(Views))
	{
		GCompositionLighting.ProcessAsyncSSAO(RHICmdList, Views);
	}

	return bHZBOcclusion;
}

void FDeferredShadingSceneRenderer::RenderOcclusion(FRHICommandListImmediate& RHICmdList)
{		
	check(RHICmdList.IsOutsideRenderPass());

	SCOPED_GPU_STAT(RHICmdList, HZB);

	{
		// Update the quarter-sized depth buffer with the current contents of the scene depth texture.
		// This needs to happen before occlusion tests, which makes use of the small depth buffer.
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_UpdateDownsampledDepthSurface);
		UpdateDownsampledDepthSurface(RHICmdList);
	}
		
	// Issue occlusion queries
	// This is done after the downsampled depth buffer is created so that it can be used for issuing queries
	BeginOcclusionTests(RHICmdList, true);
}

void FDeferredShadingSceneRenderer::FinishOcclusion(FRHICommandListImmediate& RHICmdList)
{
	// Hint to the RHI to submit commands up to this point to the GPU if possible.  Can help avoid CPU stalls next frame waiting
	// for these query results on some platforms.
	RHICmdList.SubmitCommandsHint();
}
// The render thread is involved in sending stuff to the RHI, so we will periodically service that queue
void ServiceLocalQueue()
{
	SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Render_ServiceLocalQueue);
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GetRenderThread_Local());

	if (IsRunningRHIInSeparateThread())
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}
}

// @return 0/1
static int32 GetCustomDepthPassLocation()
{		
	return FMath::Clamp(CVarCustomDepthOrder.GetValueOnRenderThread(), 0, 1);
}

void FDeferredShadingSceneRenderer::PrepareDistanceFieldScene(FRHICommandListImmediate& RHICmdList, bool bSplitDispatch)
{
	if (ShouldPrepareDistanceFieldScene())
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderDFAO);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_DistanceFieldAO_Init);
		GDistanceFieldVolumeTextureAtlas.UpdateAllocations();
		UpdateGlobalDistanceFieldObjectBuffers(RHICmdList);
		if (bSplitDispatch)
		{
			RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		}
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			Views[ViewIndex].HeightfieldLightingViewInfo.SetupVisibleHeightfields(Views[ViewIndex], RHICmdList);

			if (ShouldPrepareGlobalDistanceField())
			{
				float OcclusionMaxDistance = Scene->DefaultMaxDistanceFieldOcclusionDistance;

				// Use the skylight's max distance if there is one
				if (Scene->SkyLight && Scene->SkyLight->bCastShadows && !Scene->SkyLight->bWantsStaticShadowing)
				{
					OcclusionMaxDistance = Scene->SkyLight->OcclusionMaxDistance;
				}

				UpdateGlobalDistanceFieldVolume(RHICmdList, Views[ViewIndex], Scene, OcclusionMaxDistance, Views[ViewIndex].GlobalDistanceFieldInfo);
			}
		}
		if (!bSplitDispatch)
		{
			RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		}
	}
}

#if RHI_RAYTRACING

bool FDeferredShadingSceneRenderer::GatherRayTracingWorldInstances(FRHICommandListImmediate& RHICmdList)
{
	if (!IsRayTracingEnabled())
	{
		return false;
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_GenerateVisibleRayTracingMeshCommands);
		RayTracingCollector.ClearViewMeshArrays();
		TArray<int> DynamicMeshBatchStartOffset;
		TArray<int> VisibleDrawCommandStartOffset;

		TArray<FPrimitiveUniformShaderParameters> DummyDynamicPrimitiveShaderData;

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			DynamicMeshBatchStartOffset.Add(0);
			VisibleDrawCommandStartOffset.Add(0);
			View.RayTracingGeometryInstances.Reserve(Scene->Primitives.Num());

			RayTracingCollector.AddViewMeshArrays(
				&View,
				&View.RayTracedDynamicMeshElements,
				&View.SimpleElementCollector,
				&DummyDynamicPrimitiveShaderData,
				ViewFamily.GetFeatureLevel(),
				&DynamicIndexBufferForInitViews,
				&DynamicVertexBufferForInitViews,
				&DynamicReadBufferForInitViews
				);

			View.DynamicRayTracingMeshCommandStorage.RayTracingMeshCommands.Reserve(Scene->Primitives.Num());
			View.VisibleRayTracingMeshCommands.Reserve(Scene->Primitives.Num());
		}

		FViewInfo& ReferenceView = Views[0];

		ReferenceView.RayTracingMeshResourceCollector = MakeUnique<FRayTracingMeshResourceCollector>(
			Scene->GetFeatureLevel(),
			&DynamicIndexBufferForInitViews,
			&DynamicVertexBufferForInitViews,
			&DynamicReadBufferForInitViews);

		FRayTracingMaterialGatheringContext MaterialGatheringContext
		{
			Scene,
			&ReferenceView,
			ViewFamily,
			*ReferenceView.RayTracingMeshResourceCollector
		};

		int32 BroadIndex = 0;
		for (int PrimitiveIndex = 0; PrimitiveIndex < Scene->PrimitiveSceneProxies.Num(); PrimitiveIndex++)
		{
			while (PrimitiveIndex >= int(Scene->TypeOffsetTable[BroadIndex].Offset))
			{
				BroadIndex++;
			}

			FPrimitiveSceneInfo* SceneInfo = Scene->Primitives[PrimitiveIndex];

			if (!SceneInfo->bIsRayTracingRelevant)
			{
				//skip over unsupported SceneProxies (warning don't make IsRayTracingRelevant data dependent other than the vtable)
				PrimitiveIndex = Scene->TypeOffsetTable[BroadIndex].Offset - 1;
				continue;
			}

			if (!SceneInfo->bIsVisibleInRayTracing)
			{
				continue;
			}

			uint8 RayTracedMeshElementsMask = 0;
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				FViewInfo& View = Views[ViewIndex];
				if (!View.State)// || View.RayTracingRenderMode == ERayTracingRenderMode::Disabled)
				{
					continue;
				}

				if (View.bIsReflectionCapture && !SceneInfo->bIsVisibleInReflectionCaptures)
				{
					continue;
				}

				//#dxr_todo UE-68621  The Raytracing codepath does not support Showflags since data moved to the SceneInfo. 
				//Touching the SceneProxy to determine this would simply cost too much
				if (SceneInfo->bShouldRenderInMainPass && SceneInfo->bDrawInGame)
				{
					if (SceneInfo->bIsRayTracingStaticRelevant && View.Family->EngineShowFlags.StaticMeshes)
					{
						static const auto ICVarStaticMeshLODDistanceScale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.StaticMeshLODDistanceScale"));
						float LODScale = ICVarStaticMeshLODDistanceScale->GetFloat() * View.LODDistanceFactor;

						const FPrimitiveBounds& Bounds = Scene->PrimitiveBounds[PrimitiveIndex];
						const FPrimitiveSceneInfo* RESTRICT PrimitiveSceneInfo = Scene->Primitives[PrimitiveIndex];

						FLODMask LODToRender;

						const int8 CurFirstLODIdx = PrimitiveSceneInfo->Proxy->GetCurrentFirstLODIdx_RenderThread();
						check(CurFirstLODIdx >= 0);

						float MeshScreenSizeSquared = 0;
						int32 ForcedLODLevel = GetCVarForceLOD();
						if (SceneInfo->bIsUsingCustomLODRules)
						{
							FPrimitiveSceneProxy* SceneProxy = Scene->PrimitiveSceneProxies[PrimitiveIndex];
							LODToRender = SceneProxy->GetCustomLOD(View, View.LODDistanceFactor, ForcedLODLevel, MeshScreenSizeSquared);
							LODToRender.ClampToFirstLOD(CurFirstLODIdx);
						}
						else
						{
							LODToRender = ComputeLODForMeshes(SceneInfo->StaticMeshRelevances, View, Bounds.BoxSphereBounds.Origin, Bounds.BoxSphereBounds.SphereRadius, ForcedLODLevel, MeshScreenSizeSquared, CurFirstLODIdx, LODScale, false);
						}

						FRayTracingGeometryRHIRef RayTracingGeometryInstance = SceneInfo->GetStaticRayTracingGeometryInstance(LODToRender.GetRayTracedLOD());
						if (!RayTracingGeometryInstance.IsValid())
						{
							continue;
						}

						const int NewInstanceIndex = View.RayTracingGeometryInstances.Num();
						uint8 NewInstanceMask = 0;
						bool bAllSegmentsOpaque = true;
						bool bAnySegmentsCastShadow = false;
						bool bAnySegmentsDecal = false;

						uint32 LODIndex = LODToRender.GetRayTracedLOD();
						// Sometimes LODIndex is out of range because it is clamped by ClampToFirstLOD, like the requested LOD is being streamed in and hasn't been available
						// According to InitViews, we should hide the static mesh instance
						if (SceneInfo->CachedRayTracingMeshCommandIndicesPerLOD.IsValidIndex(LODIndex))
						{
							const auto& CachedRayTracingMeshCommandIndices = SceneInfo->CachedRayTracingMeshCommandIndicesPerLOD[LODIndex];
							for (auto CommandIndex : CachedRayTracingMeshCommandIndices)
							{
								if (CommandIndex >= 0)
								{
									FVisibleRayTracingMeshCommand NewVisibleMeshCommand;

									NewVisibleMeshCommand.RayTracingMeshCommand = &Scene->CachedRayTracingMeshCommands.RayTracingMeshCommands[CommandIndex];
									NewVisibleMeshCommand.InstanceIndex = NewInstanceIndex;

									View.VisibleRayTracingMeshCommands.Add(NewVisibleMeshCommand);
									VisibleDrawCommandStartOffset[ViewIndex]++;

									NewInstanceMask |= NewVisibleMeshCommand.RayTracingMeshCommand->InstanceMask;
									bAllSegmentsOpaque &= NewVisibleMeshCommand.RayTracingMeshCommand->bOpaque;
									bAnySegmentsCastShadow |= NewVisibleMeshCommand.RayTracingMeshCommand->bCastRayTracedShadows;
									bAnySegmentsDecal |= NewVisibleMeshCommand.RayTracingMeshCommand->bDecal;
								}
								else
								{
									// CommandIndex == -1 indicates that the mesh batch has been filtered by FRayTracingMeshProcessor (like the shadow depth pass batch)
									// Do nothing in this case
								}
							}

							if (GRayTracingExcludeDecals && bAnySegmentsDecal)
							{
								continue;
							}

							NewInstanceMask |= bAnySegmentsCastShadow ? RAY_TRACING_MASK_SHADOW : 0;

							// When no cached command is found, NewInstanceMask == 0 and the instance is effectively filtered out
							FRayTracingGeometryInstance RayTracingInstance = { RayTracingGeometryInstance };
							RayTracingInstance.Transform = Scene->PrimitiveTransforms[PrimitiveIndex];
							RayTracingInstance.UserData = (uint32)PrimitiveIndex;
							RayTracingInstance.Mask = NewInstanceMask;
							RayTracingInstance.bForceOpaque = bAllSegmentsOpaque;
							View.RayTracingGeometryInstances.Add(RayTracingInstance);
						}
					}
					else if (View.Family->EngineShowFlags.SkeletalMeshes)
					{
						RayTracedMeshElementsMask |= 1 << ViewIndex;
					}
				}
			}

			if (RayTracedMeshElementsMask != 0)
			{
				FPrimitiveSceneProxy* SceneProxy = Scene->PrimitiveSceneProxies[PrimitiveIndex];
				TArray<FRayTracingInstance> RayTracingInstances;
				SceneProxy->GetDynamicRayTracingInstances(MaterialGatheringContext, RayTracingInstances);

				{
					for (auto DynamicRayTracingGeometryUpdate : MaterialGatheringContext.DynamicRayTracingGeometriesToUpdate)
					{
						Scene->GetRayTracingDynamicGeometryCollection()->AddDynamicMeshBatchForGeometryUpdate(
							Scene,
							&ReferenceView,
							SceneProxy,
							DynamicRayTracingGeometryUpdate
						);
					}

					MaterialGatheringContext.DynamicRayTracingGeometriesToUpdate.Reset();
				}

				if (RayTracingInstances.Num() > 0)
				{
					for (FRayTracingInstance& Instance : RayTracingInstances)
					{
						FRayTracingGeometryInstance RayTracingInstance = { Instance.Geometry->RayTracingGeometryRHI };
						RayTracingInstance.UserData = (uint32)PrimitiveIndex;
						RayTracingInstance.Mask = Instance.Mask;
						RayTracingInstance.bForceOpaque = Instance.bForceOpaque;

						check(Instance.Materials.Num() == Instance.Geometry->Initializer.Segments.Num() || (Instance.Geometry->Initializer.Segments.Num() == 0 && Instance.Materials.Num() == 1));

						for (const FMatrix& InstanceTransform : Instance.InstanceTransforms)
						{
							RayTracingInstance.Transform = InstanceTransform;

							uint32 InstanceIndex = ReferenceView.RayTracingGeometryInstances.Add(RayTracingInstance);

							for (int32 ViewIndex = 1; ViewIndex < Views.Num(); ViewIndex++)
							{
								Views[ViewIndex].RayTracingGeometryInstances.Add(RayTracingInstance);
							}

							for (int32 SegmentIndex = 0; SegmentIndex < Instance.Materials.Num(); SegmentIndex++)
							{
								FMeshBatch& MeshBatch = Instance.Materials[SegmentIndex];
								FDynamicRayTracingMeshCommandContext CommandContext(ReferenceView.DynamicRayTracingMeshCommandStorage, ReferenceView.VisibleRayTracingMeshCommands, SegmentIndex, InstanceIndex);
								FRayTracingMeshProcessor RayTracingMeshProcessor(&CommandContext, Scene, &ReferenceView);

								RayTracingMeshProcessor.AddMeshBatch(MeshBatch, 1, SceneProxy);
							}
						}
					}
				}
			}
		}
	}

	return true;
}

bool FDeferredShadingSceneRenderer::DispatchRayTracingWorldUpdates(FRHICommandListImmediate& RHICmdList)
{
	if (!IsRayTracingEnabled())
	{
		return false;
	}

	SCOPED_GPU_STAT(RHICmdList, RayTracingTLAS);

	Scene->GetRayTracingDynamicGeometryCollection()->DispatchUpdates(RHICmdList);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		SET_DWORD_STAT(STAT_RayTracingInstances, View.RayTracingGeometryInstances.Num());
		FRayTracingSceneInitializer Initializer;
		Initializer.Instances = View.RayTracingGeometryInstances;
		Initializer.ShaderSlotsPerGeometrySegment = RAY_TRACING_NUM_SHADER_SLOTS;
		View.RayTracingScene.RayTracingSceneRHI = RHICreateRayTracingScene(Initializer);
		RHICmdList.BuildAccelerationStructure(View.RayTracingScene.RayTracingSceneRHI);

		// #dxr_todo: UE-72565: refactor ray tracing effects to not be member functions of DeferredShadingRenderer. register each effect at startup and just loop over them automatically to gather all required shaders
		TArray<FRHIRayTracingShader*> RayGenShaders;
		PrepareRayTracingReflections(View, RayGenShaders);
		PrepareRayTracingShadows(View, RayGenShaders);
		PrepareRayTracingAmbientOcclusion(View, RayGenShaders);
		PrepareRayTracingSkyLight(View, RayGenShaders);
		PrepareRayTracingRectLight(View, RayGenShaders);
		PrepareRayTracingGlobalIllumination(View, RayGenShaders);
		PrepareRayTracingTranslucency(View, RayGenShaders);
		PrepareRayTracingDebug(View, RayGenShaders);
		PreparePathTracing(View, RayGenShaders);

		if (RayGenShaders.Num())
		{
			auto DefaultHitShader = View.ShaderMap->GetShader<FOpaqueShadowHitGroup>()->GetRayTracingShader();

			View.RayTracingMaterialPipeline = BindRayTracingMaterialPipeline(RHICmdList, View,
				RayGenShaders,
				DefaultHitShader
			);
		}
	}

	return true;
}

#endif // RHI_RAYTRACING

extern bool IsLpvIndirectPassRequired(const FViewInfo& View);

static TAutoConsoleVariable<float> CVarStallInitViews(
	TEXT("CriticalPathStall.AfterInitViews"),
	0.0f,
	TEXT("Sleep for the given time after InitViews. Time is given in ms. This is a debug option used for critical path analysis and forcing a change in the critical path."));

void FDeferredShadingSceneRenderer::Render(FRHICommandListImmediate& RHICmdList)
{
	check(RHICmdList.IsOutsideRenderPass());

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderOther);

	PrepareViewRectsForRendering();

	if (Scene->HasSkyAtmosphere() && ShouldRenderSkyAtmosphere(Scene->SkyAtmosphere, Scene->GetShaderPlatform()))
	{
		for (int32 LightIndex = 0; LightIndex < NUM_ATMOSPHERE_LIGHTS; ++LightIndex)
		{
			if (Scene->AtmosphereLights[LightIndex])
			{
				Scene->GetSkyAtmosphereSceneInfo()->PrepareSunLightProxy(*Scene->AtmosphereLights[LightIndex]);
			}
		}
	}
	else if (Scene->AtmosphereLights[0] && Scene->HasAtmosphericFog())
	{
		// Only one atmospheric light at one time.
		Scene->GetAtmosphericFogSceneInfo()->PrepareSunLightProxy(*Scene->AtmosphereLights[0]);
	}
	else
	{
		Scene->ResetAtmosphereLightsProperties();
	}

	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_Render, FColor::Emerald);

#if WITH_MGPU
	const FRHIGPUMask RenderTargetGPUMask = (GNumExplicitGPUsForRendering > 1 && ViewFamily.RenderTarget) ? ViewFamily.RenderTarget->GetGPUMask(RHICmdList) : FRHIGPUMask::GPU0();
	ComputeViewGPUMasks(RenderTargetGPUMask);
#endif // WITH_MGPU

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	
	//make sure all the targets we're going to use will be safely writable.
	GRenderTargetPool.TransitionTargetsWritable(RHICmdList);

	// this way we make sure the SceneColor format is the correct one and not the one from the end of frame before
	SceneContext.ReleaseSceneColor();

	const bool bDBuffer = !ViewFamily.EngineShowFlags.ShaderComplexity && ViewFamily.EngineShowFlags.Decals && IsUsingDBuffers(ShaderPlatform);

	WaitOcclusionTests(RHICmdList);

	if (!ViewFamily.EngineShowFlags.Rendering)
	{
		return;
	}
	SCOPED_DRAW_EVENT(RHICmdList, Scene);

	// Anything rendered inside Render() which isn't accounted for will fall into this stat
	// This works because child stat events do not contribute to their parents' times (see GPU_STATS_CHILD_TIMES_INCLUDED)
	SCOPED_GPU_STAT(RHICmdList, Unaccounted);
	
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Render_Init);
		SCOPED_GPU_STAT(RHICmdList, AllocateRendertargets);

		// Initialize global system textures (pass-through if already initialized).
		GSystemTextures.InitializeTextures(RHICmdList, FeatureLevel);

		// Allocate the maximum scene render target space for the current view family.
		SceneContext.Allocate(RHICmdList, this);
	}

	const bool bUseVirtualTexturing = UseVirtualTexturing(FeatureLevel);
	if (bUseVirtualTexturing)
	{
		// AllocateResources needs to be called before RHIBeginScene
		FVirtualTextureSystem::Get().AllocateResources(RHICmdList, FeatureLevel);
		FVirtualTextureSystem::Get().CallPendingCallbacks();
	}

	const bool bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;

	// Use readonly depth in the base pass if we have a full depth prepass
	const bool bAllowReadonlyDepthBasePass = EarlyZPassMode == DDM_AllOpaque
		&& !ViewFamily.EngineShowFlags.ShaderComplexity
		&& !ViewFamily.UseDebugViewPS()
		&& !bIsWireframe
		&& !ViewFamily.EngineShowFlags.LightMapDensity;

	const FExclusiveDepthStencil::Type BasePassDepthStencilAccess = 
		bAllowReadonlyDepthBasePass
		? FExclusiveDepthStencil::DepthRead_StencilWrite 
		: FExclusiveDepthStencil::DepthWrite_StencilWrite;

	FGraphEventArray UpdateViewCustomDataEvents;
	FILCUpdatePrimTaskData ILCTaskData;

	// Find the visible primitives.
	RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	
	bool bDoInitViewAftersPrepass = false;
	{
		SCOPED_GPU_STAT(RHICmdList, VisibilityCommands);
		bDoInitViewAftersPrepass = InitViews(RHICmdList, BasePassDepthStencilAccess, ILCTaskData, UpdateViewCustomDataEvents);
	}

#if !UE_BUILD_SHIPPING
	if (CVarStallInitViews.GetValueOnRenderThread() > 0.0f)
	{
		SCOPE_CYCLE_COUNTER(STAT_InitViews_Intentional_Stall);
		FPlatformProcess::Sleep(CVarStallInitViews.GetValueOnRenderThread() / 1000.0f);
	}
#endif

#if RHI_RAYTRACING
	// Gather mesh instances, shaders, resources, parameters, etc. and build ray tracing acceleration structure
	GatherRayTracingWorldInstances(RHICmdList);

	if (Views[0].RayTracingRenderMode != ERayTracingRenderMode::PathTracing)
	{
		extern ENGINE_API float GAveragePathTracedMRays;
		GAveragePathTracedMRays = 0.0f;
	}
#endif // RHI_RAYTRACING

	if (GRHICommandList.UseParallelAlgorithms())
	{
		// there are dynamic attempts to get this target during parallel rendering
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			Views[ViewIndex].GetEyeAdaptation(RHICmdList);
		}	
	}

	if (GDoPrepareDistanceFieldSceneAfterRHIFlush && (GRHINeedsExtraDeletionLatency || !GRHICommandList.Bypass()))
	{
		// we will probably stall on occlusion queries, so might as well have the RHI thread and GPU work while we wait.
		SCOPE_CYCLE_COUNTER(STAT_PostInitViews_FlushDel);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
	}


	UpdateGPUScene(RHICmdList, *Scene);

	if (bUseVirtualTexturing)
	{
		Scene->FlushDirtyRuntimeVirtualTextures();
		FVirtualTextureSystem::Get().Update(RHICmdList, FeatureLevel);
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		ShaderPrint::BeginView(RHICmdList, Views[ViewIndex]);
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		UploadDynamicPrimitiveShaderDataForView(RHICmdList, *Scene, Views[ViewIndex]);
	}	
	
	if (!bDoInitViewAftersPrepass)
	{
		bool bSplitDispatch = !GDoPrepareDistanceFieldSceneAfterRHIFlush;
		PrepareDistanceFieldScene(RHICmdList, bSplitDispatch);
	}

	if (!GDoPrepareDistanceFieldSceneAfterRHIFlush && (GRHINeedsExtraDeletionLatency || !GRHICommandList.Bypass()))
	{
		// we will probably stall on occlusion queries, so might as well have the RHI thread and GPU work while we wait.
		SCOPE_CYCLE_COUNTER(STAT_PostInitViews_FlushDel);
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
	}

	static const auto ClearMethodCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ClearSceneMethod"));
	bool bRequiresRHIClear = true;
	bool bRequiresFarZQuadClear = false;

	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);
	bool bCanOverlayRayTracingOutput = CanOverlayRayTracingOutput(Views[0]);// #dxr_todo: UE-72557 multi-view case
	
	const bool bRenderDeferredLighting = ViewFamily.EngineShowFlags.Lighting
		&& FeatureLevel >= ERHIFeatureLevel::SM4
		&& ViewFamily.EngineShowFlags.DeferredLighting
		&& bUseGBuffer
		&& bCanOverlayRayTracingOutput;

	bool bComputeLightGrid = false;
	// Simple forward shading doesn't support local lights. No need to compute light grid
	if (!IsSimpleForwardShadingEnabled(ShaderPlatform))
	{
		if (bUseGBuffer)
		{
			bComputeLightGrid = bRenderDeferredLighting;
		}
		else
		{
			bComputeLightGrid = ViewFamily.EngineShowFlags.Lighting;
		}

		bComputeLightGrid |= (
			ShouldRenderVolumetricFog() ||
			ViewFamily.ViewMode != VMI_Lit);
	}

	if (ClearMethodCVar)
	{
		int32 ClearMethod = ClearMethodCVar->GetValueOnRenderThread();

		if (ClearMethod == 0 && !ViewFamily.EngineShowFlags.Game)
		{
			// Do not clear the scene only if the view family is in game mode.
			ClearMethod = 1;
		}

		switch (ClearMethod)
		{
		case 0: // No clear
			{
				bRequiresRHIClear = false;
				bRequiresFarZQuadClear = false;
				break;
			}
		
		case 1: // RHICmdList.Clear
			{
				bRequiresRHIClear = true;
				bRequiresFarZQuadClear = false;
				break;
			}

		case 2: // Clear using far-z quad
			{
				bRequiresFarZQuadClear = true;
				bRequiresRHIClear = false;
				break;
			}
		}
	}

	// Always perform a full buffer clear for wireframe, shader complexity view mode, and stationary light overlap viewmode.
	if (bIsWireframe || ViewFamily.EngineShowFlags.ShaderComplexity || ViewFamily.EngineShowFlags.StationaryLightOverlap)
	{
		bRequiresRHIClear = true;
	}

	// force using occ queries for wireframe if rendering is parented or frozen in the first view
	check(Views.Num());
	#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
		const bool bIsViewFrozen = false;
		const bool bHasViewParent = false;
	#else
		const bool bIsViewFrozen = Views[0].State && ((FSceneViewState*)Views[0].State)->bIsFrozen;
		const bool bHasViewParent = Views[0].State && ((FSceneViewState*)Views[0].State)->HasViewParent();
	#endif

	
	const bool bIsOcclusionTesting = DoOcclusionQueries(FeatureLevel) && (!bIsWireframe || bIsViewFrozen || bHasViewParent);

	// Dynamic vertex and index buffers need to be committed before rendering.
	GEngine->GetPreRenderDelegate().Broadcast();
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_FGlobalDynamicVertexBuffer_Commit);
		DynamicIndexBufferForInitViews.Commit();
		DynamicVertexBufferForInitViews.Commit();
		DynamicReadBufferForInitViews.Commit();

		if (!bDoInitViewAftersPrepass)
		{
			DynamicVertexBufferForInitShadows.Commit();
			DynamicIndexBufferForInitShadows.Commit();
			DynamicReadBufferForInitShadows.Commit();
		}
	}

	// Only update the GPU particle simulation for the main view
	//@todo - this is needed because the GPU particle simulation is updated within a frame render.  Simulation should happen outside of a visible frame rendering.
	// This also causes GPU particles to be one frame behind in scene captures and planar reflections.
	const bool bAllowGPUParticleSceneUpdate = !Views[0].bIsPlanarReflection && !Views[0].bIsSceneCapture && !Views[0].bIsReflectionCapture;

	// Notify the FX system that the scene is about to be rendered.
	bool bDoFXPrerender = Scene->FXSystem && Views.IsValidIndex(0);

	if (bDoFXPrerender)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_FXSystem_PreRender);
		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_FXPreRender));
		Scene->FXSystem->PreRender(RHICmdList, &Views[0].GlobalDistanceFieldInfo.ParameterData, bAllowGPUParticleSceneUpdate);
	}

	bool bDidAfterTaskWork = false;
	auto AfterTasksAreStarted = [&bDidAfterTaskWork, bDoInitViewAftersPrepass, this, &RHICmdList, &ILCTaskData, &UpdateViewCustomDataEvents, bDoFXPrerender]()
	{
		if (!bDidAfterTaskWork)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_AfterPrepassTasksWork);
			bDidAfterTaskWork = true; // only do this once
			if (bDoInitViewAftersPrepass)
			{
				InitViewsPossiblyAfterPrepass(RHICmdList, ILCTaskData, UpdateViewCustomDataEvents);
				PrepareDistanceFieldScene(RHICmdList, false);

				{
					SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_FGlobalDynamicVertexBuffer_Commit);
					DynamicVertexBufferForInitShadows.Commit();
					DynamicIndexBufferForInitShadows.Commit();
					DynamicReadBufferForInitShadows.Commit();
				}

				ServiceLocalQueue();
			}
		}
	};

	if (FGPUSkinCache* GPUSkinCache = Scene->GetGPUSkinCache())
	{
		GPUSkinCache->TransitionAllToReadable(RHICmdList);
	}

	// Before starting the render, all async task for the Custom data must be completed
	if (UpdateViewCustomDataEvents.Num() > 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_AsyncUpdateViewCustomData_Wait);
		FTaskGraphInterface::Get().WaitUntilTasksComplete(UpdateViewCustomDataEvents, ENamedThreads::GetRenderThread());
	}

	// Generate the Sky/Atmosphere look up tables
	const bool bShouldRenderSkyAtmosphere = ShouldRenderSkyAtmosphere(Scene->GetSkyAtmosphereSceneInfo(), Scene->GetShaderPlatform());
	if (bShouldRenderSkyAtmosphere)
	{
		RenderSkyAtmosphereLookUpTables(RHICmdList);
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	// The Z-prepass

	// Draw the scene pre-pass / early z pass, populating the scene depth buffer and HiZ
	GRenderTargetPool.AddPhaseEvent(TEXT("EarlyZPass"));
	const bool bNeedsPrePass = NeedsPrePass(this);
	bool bDepthWasCleared;
	if (bNeedsPrePass)
	{
		bDepthWasCleared = RenderPrePass(RHICmdList, AfterTasksAreStarted);
	}
	else
	{
		// we didn't do the prepass, but we still want the HMD mask if there is one
		AfterTasksAreStarted();
		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_PrePass));
		bDepthWasCleared = RenderPrePassHMD(RHICmdList);
	}
	check(bDidAfterTaskWork);
	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_AfterPrePass));
	ServiceLocalQueue();

#if RHI_RAYTRACING
	// Must be done after FGlobalDynamicVertexBuffer::Get().Commit() for dynamic geometries to be updated
	DispatchRayTracingWorldUpdates(RHICmdList);
#endif

	// Z-Prepass End

	checkSlow(RHICmdList.IsOutsideRenderPass());

	const bool bShouldRenderVelocities = ShouldRenderVelocities();
	const bool bBasePassCanOutputVelocity = FVelocityRendering::BasePassCanOutputVelocity(FeatureLevel);
	const bool bUseSelectiveBasePassOutputs = IsUsingSelectiveBasePassOutputs(ShaderPlatform);

	SceneContext.ResolveSceneDepthTexture(RHICmdList, FResolveRect(0, 0, FamilySize.X, FamilySize.Y));
	SceneContext.ResolveSceneDepthToAuxiliaryTexture(RHICmdList);

	// NOTE: The ordering of the lights is used to select sub-sets for different purposes, e.g., those that support clustered deferred.
	FSortedLightSetSceneInfo SortedLightSet;
	
	{
		SCOPED_GPU_STAT(RHICmdList, SortLights);
		GatherAndSortLights(SortedLightSet);
		ComputeLightGrid(RHICmdList, bComputeLightGrid, SortedLightSet);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_AllocGBufferTargets);
		SceneContext.PreallocGBufferTargets(); // Even if !bShouldRenderVelocities, the velocity buffer must be bound because it's a compile time option for the shader.
		SceneContext.AllocGBufferTargets(RHICmdList);
	}	

	checkSlow(RHICmdList.IsOutsideRenderPass());

	// Early occlusion queries
	const bool bOcclusionBeforeBasePass = (EarlyZPassMode == EDepthDrawingMode::DDM_AllOccluders) || (EarlyZPassMode == EDepthDrawingMode::DDM_AllOpaque);

	if (bOcclusionBeforeBasePass)
	{
		SCOPED_GPU_STAT(RHICmdList, HZB);

		if (bIsOcclusionTesting)
		{
			RenderOcclusion(RHICmdList);
		}

		bool bUseHzbOcclusion = RenderHzb(RHICmdList);
		
		if (bUseHzbOcclusion || bIsOcclusionTesting)
		{
			FinishOcclusion(RHICmdList);
		}
		if (bIsOcclusionTesting)
		{
			FenceOcclusionTests(RHICmdList);
		}
	}

	ServiceLocalQueue();
	// End early occlusion queries

	checkSlow(RHICmdList.IsOutsideRenderPass());

	// Early Shadow depth rendering
	if (bOcclusionBeforeBasePass)
	{
		// Before starting the shadow render, all async task for the shadow Custom data must be completed
		if (bDoInitViewAftersPrepass && UpdateViewCustomDataEvents.Num() > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_AsyncUpdateViewCustomData_Wait);
			FTaskGraphInterface::Get().WaitUntilTasksComplete(UpdateViewCustomDataEvents, ENamedThreads::GetRenderThread());
		}

		RenderShadowDepthMaps(RHICmdList);
		ServiceLocalQueue();
	}
	// End early Shadow depth rendering

	checkSlow(RHICmdList.IsOutsideRenderPass());

	// Clear LPVs for all views
	if (FeatureLevel >= ERHIFeatureLevel::SM5)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_ClearLPVs);
		ClearLPVs(RHICmdList);
		ServiceLocalQueue();
	}

	if(GetCustomDepthPassLocation() == 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass0);
		RenderCustomDepthPassAtLocation(RHICmdList, 0);
	}

	if (bOcclusionBeforeBasePass)
	{
		ComputeVolumetricFog(RHICmdList);
	}

	TRefCountPtr<IPooledRenderTarget> ForwardScreenSpaceShadowMask;

	if (IsForwardShadingEnabled(ShaderPlatform))
	{
		RenderForwardShadingShadowProjections(RHICmdList, ForwardScreenSpaceShadowMask);

		RenderIndirectCapsuleShadows(
			RHICmdList, 
			NULL, 
			NULL);
	}

	// only temporarily available after early z pass and until base pass
	check(!SceneContext.DBufferA);
	check(!SceneContext.DBufferB);
	check(!SceneContext.DBufferC);

	if (bDBuffer || IsForwardShadingEnabled(ShaderPlatform))
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_DBuffer);

		// e.g. DBuffer deferred decals
		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{	
			SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView,Views.Num() > 1, TEXT("View%d"), ViewIndex);
			FViewInfo& View = Views[ViewIndex];

			Scene->UniformBuffers.UpdateViewUniformBuffer(View);

			uint32 SSAOLevels = FSSAOHelper::ComputeAmbientOcclusionPassCount(View);
			// In deferred shader, the SSAO uses the GBuffer and must be executed after base pass. Otherwise, async compute runs the shader in RenderHzb()
			// In forward, if zprepass is off - as SSAO here requires a valid HZB buffer - disable SSAO
			if (!IsForwardShadingEnabled(ShaderPlatform) || !View.HZB.IsValid() || FSSAOHelper::IsAmbientOcclusionAsyncCompute(View, SSAOLevels))
			{
				SSAOLevels = 0;
			}

			GCompositionLighting.ProcessBeforeBasePass(RHICmdList, View, bDBuffer, SSAOLevels);
		}

		ServiceLocalQueue();
	}
	
	checkSlow(RHICmdList.IsOutsideRenderPass());

	if (bRenderDeferredLighting)
	{
		bool bShouldAllocateDeferredShadingPathRenderTargets = false;
		const char* str = SceneContext.ScreenSpaceAO ? "Allocated" : "Unallocated"; //ScreenSpaceAO is determining factor of detecting render target allocation
		for(int Index = 0; Index < (NumTranslucentVolumeRenderTargetSets * Views.Num()); ++Index)
		{
			if(!SceneContext.TranslucencyLightingVolumeAmbient[Index] || !SceneContext.TranslucencyLightingVolumeDirectional[Index])
			{
				
				ensureMsgf(SceneContext.TranslucencyLightingVolumeAmbient[Index], TEXT("%s%d is unallocated, Deferred Render Targets would be detected as: %s"), "TranslucencyLightingVolumeAmbient", Index, str);
				ensureMsgf(SceneContext.TranslucencyLightingVolumeDirectional[Index], TEXT("%s%d is unallocated, Deferred Render Targets would be detected as: %s"), "TranslucencyLightingVolumeDirectional", Index, str);
				bShouldAllocateDeferredShadingPathRenderTargets = true;
				break;
			}
		}

		if(bShouldAllocateDeferredShadingPathRenderTargets)
		{
			SceneContext.AllocateDeferredShadingPathRenderTargets(RHICmdList);
		}
		
		if (GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute)
		{
			ClearTranslucentVolumeLightingAsyncCompute(RHICmdList);
		}
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	bool bIsWireframeRenderpass = bIsWireframe && FSceneRenderer::ShouldCompositeEditorPrimitives(Views[0]);
	bool bRenderLightmapDensity = ViewFamily.EngineShowFlags.LightMapDensity && AllowDebugViewmodes();
	bool bRenderSkyAtmosphereEditorNotifications = ShouldRenderSkyAtmosphereEditorNotifications();
	bool bDoParallelBasePass = GRHICommandList.UseParallelAlgorithms() && CVarParallelBasePass.GetValueOnRenderThread();
	
	// BASE PASS AND GBUFFER SETUP
	// Gross logic to cover all the cases of special rendering modes + parallel dispatch
	// Clear the GBuffer render targets
	bool bIsGBufferCurrent = false;
	if (bRequiresRHIClear)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_SetAndClearViewGBuffer);
		bool bClearDepth = !bDepthWasCleared;
		 
		// if we didn't to the prepass above, then we will need to clear now, otherwise, it's already been cleared and rendered to
		ERenderTargetLoadAction ColorLoadAction = ERenderTargetLoadAction::ELoad;
		ERenderTargetLoadAction DepthLoadAction = bClearDepth ? ERenderTargetLoadAction::EClear : (!IsMetalPlatform(ShaderPlatform) ? ERenderTargetLoadAction::ENoAction : ERenderTargetLoadAction::ELoad);

		const bool bClearBlack = ViewFamily.EngineShowFlags.ShaderComplexity || ViewFamily.EngineShowFlags.StationaryLightOverlap;
		const float ClearAlpha = GetSceneColorClearAlpha();
		FLinearColor ClearColor = bClearBlack ? FLinearColor(0, 0, 0, ClearAlpha) : FLinearColor(Views[0].BackgroundColor.R, Views[0].BackgroundColor.G, Views[0].BackgroundColor.B, ClearAlpha);
		ColorLoadAction = ERenderTargetLoadAction::EClear;

		// The first time through we'll clear the Overdraw UAVs.
		SceneContext.BeginRenderingGBuffer(RHICmdList, ColorLoadAction, DepthLoadAction, BasePassDepthStencilAccess, ViewFamily.EngineShowFlags.ShaderComplexity, true, ClearColor);
		
		// If we are in wireframe mode or will go wide later this pass is just the clear.
		if (bIsWireframeRenderpass || bRenderSkyAtmosphereEditorNotifications || bDoParallelBasePass)
		{
			RHICmdList.EndRenderPass();
		}
		else
		{
		bIsGBufferCurrent = true;
		}
		ServiceLocalQueue();
	}

	if (bRenderSkyAtmosphereEditorNotifications)
	{
		RenderSkyAtmosphereEditorNotifications(RHICmdList);
	}

	// Wireframe mode requires bRequiresRHIClear to be true. 
	// Rendering will be very funny without it and the call to BeginRenderingGBuffer will call AllocSceneColor which is needed for the EditorPrimitives resolve.
	if (bIsWireframeRenderpass)
	{
		check(bRequiresRHIClear);

		// In Editor we want wire frame view modes to be MSAA for better quality. Resolve will be done with EditorPrimitives
		FRHIRenderPassInfo RPInfo(SceneContext.GetEditorPrimitivesColor(RHICmdList), ERenderTargetActions::Clear_Store);
		RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::ClearDepthStencil_StoreDepthStencil;
		RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SceneContext.GetEditorPrimitivesDepth(RHICmdList);
		RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
		RHICmdList.BeginRenderPass(RPInfo, TEXT("Wireframe"));

		// #todo-renderpasses In serial mode wireframe rendering only binds one target
		// In parallel the entire gbuffer is bound. This was the previous SetRenderTarget behavior, preserved here.
		// This is just a clear in the parallel case.
		if (bDoParallelBasePass)
		{
			RHICmdList.EndRenderPass();
		}
	}
	else if (!bIsGBufferCurrent && (!bDoParallelBasePass || bRenderLightmapDensity))
	{
		// Make sure we have began the renderpass
		ERenderTargetLoadAction DepthLoadAction = bDepthWasCleared ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::EClear;
		SceneContext.BeginRenderingGBuffer(RHICmdList, (!IsMetalPlatform(ShaderPlatform) ? ERenderTargetLoadAction::ENoAction : ERenderTargetLoadAction::ELoad), DepthLoadAction, BasePassDepthStencilAccess, ViewFamily.EngineShowFlags.ShaderComplexity);
	}
	// Wait for Async SSAO before rendering base pass with forward rendering
	if (IsForwardShadingEnabled(ShaderPlatform))
	{
		GCompositionLighting.GfxWaitForAsyncSSAO(RHICmdList);
	}

	GRenderTargetPool.AddPhaseEvent(TEXT("BasePass"));

	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_BasePass));
	RenderBasePass(RHICmdList, BasePassDepthStencilAccess, ForwardScreenSpaceShadowMask.GetReference(), bDoParallelBasePass, bRenderLightmapDensity);

	// Release forward screen space shadow mask right after base pass in forward rendering to free resources, such as FastVRAM
	if (IsForwardShadingEnabled(ShaderPlatform))
	{
		ForwardScreenSpaceShadowMask.SafeRelease();
	}

	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_AfterBasePass));
	ServiceLocalQueue();

	// If we ran parallel in the basepass there will be no renderpass at this point.
	if (bDoParallelBasePass && !bRenderLightmapDensity)
	{
		SceneContext.BeginRenderingGBuffer(RHICmdList, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, BasePassDepthStencilAccess, ViewFamily.EngineShowFlags.ShaderComplexity);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_ViewExtensionPostRenderBasePass);
		for (int32 ViewExt = 0; ViewExt < ViewFamily.ViewExtensions.Num(); ++ViewExt)
		{
			for (int32 ViewIndex = 0; ViewIndex < ViewFamily.Views.Num(); ++ViewIndex)
			{
				ViewFamily.ViewExtensions[ViewExt]->PostRenderBasePass_RenderThread(RHICmdList, Views[ViewIndex]);
			}
		}
	}

	// #todo-renderpasses Should this be further below?
	if (bRequiresFarZQuadClear)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_ClearGBufferAtMaxZ);
		// Clears view by drawing quad at maximum Z
		// TODO: if all the platforms have fast color clears, we can replace this with an RHICmdList.Clear.
		ClearGBufferAtMaxZ(RHICmdList);
		ServiceLocalQueue();

		bRequiresFarZQuadClear = false;
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Resolve_After_Basepass);
		// Will early return if simple forward
		SceneContext.FinishGBufferPassAndResolve(RHICmdList);
	}

	if (!bAllowReadonlyDepthBasePass)
	{
		SceneContext.ResolveSceneDepthTexture(RHICmdList, FResolveRect(0, 0, FamilySize.X, FamilySize.Y));
		SceneContext.ResolveSceneDepthToAuxiliaryTexture(RHICmdList);
	}

	// BASE PASS ENDS HERE.

	if (ViewFamily.EngineShowFlags.VisualizeLightCulling)
	{
		// clear out emissive and baked lighting (not too efficient but simple and only needed for this debug view)
		SceneContext.BeginRenderingSceneColor(RHICmdList);
		DrawClearQuad(RHICmdList, FLinearColor(0, 0, 0, 0));
		SceneContext.FinishRenderingSceneColor(RHICmdList);
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	SceneContext.DBufferA.SafeRelease();
	SceneContext.DBufferB.SafeRelease();
	SceneContext.DBufferC.SafeRelease();

	// only temporarily available after early z pass and until base pass
	check(!SceneContext.DBufferA);
	check(!SceneContext.DBufferB);
	check(!SceneContext.DBufferC);

	// #todo-renderpass Zfar clear was here. where should it really go?
	
	VisualizeVolumetricLightmap(RHICmdList);

	SceneContext.ResolveSceneDepthToAuxiliaryTexture(RHICmdList);

	// Occlusion after base pass
	if (!bOcclusionBeforeBasePass)
	{
		SCOPED_GPU_STAT(RHICmdList, HZB);
		// #todo-renderpasses Needs its own renderpass. Does this need more than the depth?
		if (bIsOcclusionTesting)
		{
			RenderOcclusion(RHICmdList);
		}

		bool bUseHzbOcclusion = RenderHzb(RHICmdList);

		if (bUseHzbOcclusion || bIsOcclusionTesting)
		{
			FinishOcclusion(RHICmdList);
		}
		if (bIsOcclusionTesting)
		{
			FenceOcclusionTests(RHICmdList);
		}
	}

	ServiceLocalQueue();
	// End occlusion after base

	checkSlow(RHICmdList.IsOutsideRenderPass());

	if (!bUseGBuffer)
	{		
		ResolveSceneColor(RHICmdList);
	}

	// Shadow and fog after base pass
	if (!bOcclusionBeforeBasePass)
	{
		// Before starting the shadow render, all async task for the shadow Custom data must be completed
		if (bDoInitViewAftersPrepass && UpdateViewCustomDataEvents.Num() > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_AsyncUpdateViewCustomData_Wait);
			FTaskGraphInterface::Get().WaitUntilTasksComplete(UpdateViewCustomDataEvents, ENamedThreads::GetRenderThread());
		}

		RenderShadowDepthMaps(RHICmdList);

		checkSlow(RHICmdList.IsOutsideRenderPass());

		ComputeVolumetricFog(RHICmdList);
		ServiceLocalQueue();
	}
	// End shadow and fog after base pass

	checkSlow(RHICmdList.IsOutsideRenderPass());

	if(GetCustomDepthPassLocation() == 1)
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(CustomDepthPass);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass1);
		RenderCustomDepthPassAtLocation(RHICmdList, 1);
	}

	ServiceLocalQueue();

	// If bBasePassCanOutputVelocity is set, basepass fully writes the velocity buffer unless bUseSelectiveBasePassOutputs is enabled.
	if (bShouldRenderVelocities && (!bBasePassCanOutputVelocity || bUseSelectiveBasePassOutputs))
	{
		// Render the velocities of movable objects
		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_Velocity));
		RenderVelocities(RHICmdList, SceneContext.SceneVelocity);
		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_AfterVelocity));
		ServiceLocalQueue();
	}

#if !UE_BUILD_SHIPPING
	if (CVarForceBlackVelocityBuffer.GetValueOnRenderThread())
	{
		SceneContext.SceneVelocity = GSystemTextures.BlackDummy;
	}
#endif
	checkSlow(RHICmdList.IsOutsideRenderPass());
#if RHI_RAYTRACING
	TRefCountPtr<IPooledRenderTarget> SkyLightRT;
	TRefCountPtr<IPooledRenderTarget> SkyLightHitDistanceRT;
	TRefCountPtr<IPooledRenderTarget> GlobalIlluminationRT;

	const bool bRayTracingEnabled = IsRayTracingEnabled();
	if (bRayTracingEnabled && bCanOverlayRayTracingOutput)
	{		
		RenderRayTracingSkyLight(RHICmdList, SkyLightRT, SkyLightHitDistanceRT);
		RenderRayTracingGlobalIllumination(RHICmdList, GlobalIlluminationRT); 
		RenderRayTracingAmbientOcclusion(RHICmdList, SceneContext.ScreenSpaceAO);
	}
#endif // RHI_RAYTRACING
	checkSlow(RHICmdList.IsOutsideRenderPass());

	// Copy lighting channels out of stencil before deferred decals which overwrite those values
	CopyStencilToLightingChannelTexture(RHICmdList);

	checkSlow(RHICmdList.IsOutsideRenderPass());

	if(!IsForwardShadingEnabled(ShaderPlatform))
	{
		GCompositionLighting.GfxWaitForAsyncSSAO(RHICmdList);
	}
	else
	{
		// Release SSAO texture and HZB texture earlier to free resources, such as FastVRAM.
		SceneContext.ScreenSpaceAO.SafeRelease();
		SceneContext.bScreenSpaceAOIsValid = false;

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);
			FViewInfo& View = Views[ViewIndex];

			View.HZB.SafeRelease();
		}
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	// Pre-lighting composition lighting stage
	// e.g. deferred decals, SSAO
	if (FeatureLevel >= ERHIFeatureLevel::SM4)
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(AfterBasePass);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_AfterBasePass);

		GRenderTargetPool.AddPhaseEvent(TEXT("AfterBasePass"));
		if (!IsForwardShadingEnabled(ShaderPlatform))
		{
			SceneContext.ResolveSceneDepthTexture(RHICmdList, FResolveRect(0, 0, FamilySize.X, FamilySize.Y));
			SceneContext.ResolveSceneDepthToAuxiliaryTexture(RHICmdList);
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);

			Scene->UniformBuffers.UpdateViewUniformBuffer(Views[ViewIndex]);

			GCompositionLighting.ProcessAfterBasePass(RHICmdList, Views[ViewIndex]);
		}
		ServiceLocalQueue();
	}

	// TODO: Could entirely remove this by using STENCIL_SANDBOX_BIT in ShadowRendering.cpp and DistanceFieldSurfaceCacheLighting.cpp
	if (!IsForwardShadingEnabled(ShaderPlatform))
	{
		// Clear stencil to 0 now that deferred decals are done using what was setup in the base pass
		// Shadow passes and other users of stencil assume it is cleared to 0 going in
		FRHIRenderPassInfo RPInfo(SceneContext.GetSceneDepthSurface(),
			EDepthStencilTargetActions::ClearStencilDontLoadDepth_StoreStencilNotDepth);
		RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilWrite;
		RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearStencilFromBasePass"));
		RHICmdList.EndRenderPass();

		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, SceneContext.GetSceneDepthSurface());
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	// Render lighting.
	if (bRenderDeferredLighting)
	{
		SCOPED_GPU_STAT(RHICmdList, RenderDeferredLighting);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderLighting);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Lighting);

		GRenderTargetPool.AddPhaseEvent(TEXT("Lighting"));

		RenderDiffuseIndirectAndAmbientOcclusion(RHICmdList);

		// These modulate the scenecolor output from the basepass, which is assumed to be indirect lighting
		RenderIndirectCapsuleShadows(
			RHICmdList, 
			SceneContext.GetSceneColorSurface(), 
			SceneContext.bScreenSpaceAOIsValid ? SceneContext.ScreenSpaceAO->GetRenderTargetItem().TargetableTexture : NULL);

		TRefCountPtr<IPooledRenderTarget> DynamicBentNormalAO;
		// These modulate the scenecolor output from the basepass, which is assumed to be indirect lighting
		RenderDFAOAsIndirectShadowing(RHICmdList, SceneContext.SceneVelocity, DynamicBentNormalAO);

		// Clear the translucent lighting volumes before we accumulate
		if ((GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute) == false)
		{
			for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				ClearTranslucentVolumeLighting(RHICmdList, ViewIndex);
			}
			
		}

		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_Lighting));
		RenderLights(RHICmdList, SortedLightSet);
		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_AfterLighting));
		ServiceLocalQueue();

		checkSlow(RHICmdList.IsOutsideRenderPass());

		GRenderTargetPool.AddPhaseEvent(TEXT("AfterRenderLights"));

		for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			InjectAmbientCubemapTranslucentVolumeLighting(RHICmdList, Views[ViewIndex], ViewIndex);
		}
		ServiceLocalQueue();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			// Filter the translucency lighting volume now that it is complete
			FilterTranslucentVolumeLighting(RHICmdList, Views[ViewIndex], ViewIndex);
		}
		ServiceLocalQueue();

		checkSlow(RHICmdList.IsOutsideRenderPass());

		// Pre-lighting composition lighting stage
		// e.g. LPV indirect
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			FViewInfo& View = Views[ViewIndex]; 

			if(IsLpvIndirectPassRequired(View))
			{
				SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView,Views.Num() > 1, TEXT("View%d"), ViewIndex);

				GCompositionLighting.ProcessLpvIndirect(RHICmdList, View);
				ServiceLocalQueue();
			}
		}

		checkSlow(RHICmdList.IsOutsideRenderPass());

		// Render diffuse sky lighting and reflections that only operate on opaque pixels
		RenderDeferredReflectionsAndSkyLighting(RHICmdList, DynamicBentNormalAO, SceneContext.SceneVelocity);

		DynamicBentNormalAO = NULL;

		// SSS need the SceneColor finalized as an SRV.
		ResolveSceneColor(RHICmdList);

		ServiceLocalQueue();

		ComputeSubsurfaceShim(RHICmdList, Views);

#if RHI_RAYTRACING
		if (SkyLightRT)
		{
			CompositeRayTracingSkyLight(RHICmdList, SkyLightRT, SkyLightHitDistanceRT);
		}

		if (GlobalIlluminationRT)
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				CompositeGlobalIllumination(RHICmdList, Views[ViewIndex], GlobalIlluminationRT);
			}
		}
#endif // RHI_RAYTRACING
		ServiceLocalQueue();
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	FLightShaftsOutput LightShaftOutput;

	// Draw Lightshafts
	if (ViewFamily.EngineShowFlags.LightShafts)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLightShaftOcclusion);
		RenderLightShaftOcclusion(RHICmdList, LightShaftOutput);
		ServiceLocalQueue();
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	// Draw atmosphere
	if (bCanOverlayRayTracingOutput && ShouldRenderAtmosphere(ViewFamily))
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderAtmosphere);
		if (Scene->AtmosphericFog)
		{
			// Update RenderFlag based on LightShaftTexture is valid or not
			if (LightShaftOutput.LightShaftOcclusion)
			{
				Scene->AtmosphericFog->RenderFlag &= EAtmosphereRenderFlag::E_LightShaftMask;
			}
			else
			{
				Scene->AtmosphericFog->RenderFlag |= EAtmosphereRenderFlag::E_DisableLightShaft;
			}
#if WITH_EDITOR
			if (Scene->bIsEditorScene)
			{
				// Precompute Atmospheric Textures
				Scene->AtmosphericFog->PrecomputeTextures(RHICmdList, Views.GetData(), &ViewFamily);
			}
#endif
			RenderAtmosphere(RHICmdList, LightShaftOutput);
			ServiceLocalQueue();
		}
	}

	// Draw the sky atmosphere
	if (bShouldRenderSkyAtmosphere)
	{
		RenderSkyAtmosphere(RHICmdList);
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	GRenderTargetPool.AddPhaseEvent(TEXT("Fog"));

	// Draw fog.
	if (bCanOverlayRayTracingOutput && ShouldRenderFog(ViewFamily))
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderFog);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderFog);
		RenderFog(RHICmdList, LightShaftOutput);
		ServiceLocalQueue();
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	IRendererModule& RendererModule = GetRendererModule();
	if (RendererModule.HasPostOpaqueExtentions())
	{
		FSceneTexturesUniformParameters SceneTextureParameters;
		SetupSceneTextureUniformParameters(SceneContext, FeatureLevel, ESceneTextureSetupMode::SceneDepth | ESceneTextureSetupMode::GBuffers, SceneTextureParameters);
		TUniformBufferRef<FSceneTexturesUniformParameters> SceneTextureUniformBuffer = TUniformBufferRef<FSceneTexturesUniformParameters>::CreateUniformBufferImmediate(SceneTextureParameters, UniformBuffer_SingleFrame);

        SceneContext.BeginRenderingSceneColor(RHICmdList, (!IsMetalPlatform(ShaderPlatform) ? ESimpleRenderTargetMode::EUninitializedColorExistingDepth : ESimpleRenderTargetMode::EExistingColorAndDepth));
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewInfo& View = Views[ViewIndex];
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
			RendererModule.RenderPostOpaqueExtensions(View, RHICmdList, SceneContext, SceneTextureUniformBuffer);
		}
		SceneContext.FinishRenderingSceneColor(RHICmdList);
	}
	checkSlow(RHICmdList.IsOutsideRenderPass());
	// Unbind everything in case FX has to read.
	UnbindRenderTargets(RHICmdList);

	// Notify the FX system that opaque primitives have been rendered and we now have a valid depth buffer.
	if (Scene->FXSystem && Views.IsValidIndex(0) && bAllowGPUParticleSceneUpdate)
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderOpaqueFX);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_FXSystem_PostRenderOpaque);
		SCOPED_GPU_STAT(RHICmdList, PostRenderOpsFX);

		FSceneTexturesUniformParameters SceneTextureParameters;
		SetupSceneTextureUniformParameters(SceneContext, FeatureLevel, ESceneTextureSetupMode::SceneDepth | ESceneTextureSetupMode::GBuffers, SceneTextureParameters);
		TUniformBufferRef<FSceneTexturesUniformParameters> SceneTextureUniformBuffer = TUniformBufferRef<FSceneTexturesUniformParameters>::CreateUniformBufferImmediate(SceneTextureParameters, UniformBuffer_SingleFrame);

		Scene->FXSystem->PostRenderOpaque(
			RHICmdList,
			Views[0].ViewUniformBuffer,
			&FSceneTexturesUniformParameters::StaticStructMetadata,
			SceneTextureUniformBuffer.GetReference()
		);
		ServiceLocalQueue();
	}

	// No longer needed, release
	LightShaftOutput.LightShaftOcclusion = NULL;

	checkSlow(RHICmdList.IsOutsideRenderPass());

	if (bShouldRenderSkyAtmosphere)
	{
		// Debug the sky atmosphere. Critically rendered before translucency to avoid emissive leaking over visualization by writing depth. 
		// Alternative: render in post process chain as VisualizeHDR.
		RenderDebugSkyAtmosphere(RHICmdList);
	}

	GRenderTargetPool.AddPhaseEvent(TEXT("Translucency"));

	// Draw translucency.
	if (bCanOverlayRayTracingOutput && ViewFamily.EngineShowFlags.Translucency && !ViewFamily.EngineShowFlags.VisualizeLightCulling && !ViewFamily.UseDebugViewPS())
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderTranslucency);
		SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);

		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_Translucency));

#if RHI_RAYTRACING
		bool bAnyViewWithRaytracingTranslucency = false;
		for (int32 ViewIndex = 0, Num = Views.Num(); ViewIndex < Num; ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			bAnyViewWithRaytracingTranslucency = bAnyViewWithRaytracingTranslucency || ShouldRenderRayTracingTranslucency(View);
		}

		if (bAnyViewWithRaytracingTranslucency)
		{
			ResolveSceneColor(RHICmdList);
			RenderRayTracingTranslucency(RHICmdList);
		}
		else
#endif
		{
			// For now there is only one resolve for all translucency passes. This can be changed by enabling the resolve in RenderTranslucency()
			TRefCountPtr<IPooledRenderTarget> SceneColorCopy;
			ConditionalResolveSceneColorForTranslucentMaterials(RHICmdList, SceneColorCopy);

			// Disable UAV cache flushing so we have optimal VT feedback performance.
			RHICmdList.AutomaticCacheFlushAfterComputeShader(false);

			if (ViewFamily.AllowTranslucencyAfterDOF())
			{
				RenderTranslucency(RHICmdList, ETranslucencyPass::TPT_StandardTranslucency, SceneColorCopy);
				// Translucency after DOF is rendered now, but stored in the separate translucency RT for later use.
				RenderTranslucency(RHICmdList, ETranslucencyPass::TPT_TranslucencyAfterDOF, SceneColorCopy);
			}
			else // Otherwise render translucent primitives in a single bucket.
			{
				RenderTranslucency(RHICmdList, ETranslucencyPass::TPT_AllTranslucency, SceneColorCopy);
			}

			RHICmdList.AutomaticCacheFlushAfterComputeShader(true);
			RHICmdList.FlushComputeShaderCache();

			ServiceLocalQueue();

			static const auto DisableDistortionCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DisableDistortion"));
			const bool bAllowDistortion = DisableDistortionCVar->GetValueOnAnyThread() != 1;

			if (GetRefractionQuality(ViewFamily) > 0 && bAllowDistortion)
			{
				// To apply refraction effect by distorting the scene color.
				// After non separate translucency as that is considered at scene depth anyway
				// It allows skybox translucency (set to non separate translucency) to be refracted.
				RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_RenderDistortion));
				RenderDistortion(RHICmdList);
				ServiceLocalQueue();
			}

			RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_AfterTranslucency));
		}

		/*if (bShouldRenderVelocities)
		{
			// Render the velocities of movable objects
			RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_TranslucentVelocity));
			RenderVelocities(RHICmdList, VelocityRT);
			RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_AfterTranslucentVelocity));
			ServiceLocalQueue();
		}*/

		checkSlow(RHICmdList.IsOutsideRenderPass());

	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	if (bCanOverlayRayTracingOutput && ViewFamily.EngineShowFlags.LightShafts)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLightShaftBloom);
		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_LightShaftBloom));
		RenderLightShaftBloom(RHICmdList);
		ServiceLocalQueue();
	}

	if (bUseVirtualTexturing)
	{
		// No pass after this can make VT page requests
		SceneContext.VirtualTextureFeedback.TransferGPUToCPU(RHICmdList, Views[0].ViewRect);
	}

#if RHI_RAYTRACING
	if (bRayTracingEnabled)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (Views[ViewIndex].RayTracingRenderMode == ERayTracingRenderMode::PathTracing)
			{
				RenderPathTracing(RHICmdList, Views[ViewIndex]);
			}
			else if (Views[ViewIndex].RayTracingRenderMode == ERayTracingRenderMode::RayTracingDebug)
			{
				RenderRayTracingDebug(RHICmdList, Views[ViewIndex]);
			}
		}
	}
#endif

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
		RendererModule.RenderOverlayExtensions(View, RHICmdList, SceneContext);
	}

	if (ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO)
	{
		// Use the skylight's max distance if there is one, to be consistent with DFAO shadowing on the skylight
		const float OcclusionMaxDistance = Scene->SkyLight && !Scene->SkyLight->bWantsStaticShadowing ? Scene->SkyLight->OcclusionMaxDistance : Scene->DefaultMaxDistanceFieldOcclusionDistance;
		TRefCountPtr<IPooledRenderTarget> DummyOutput;
		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_RenderDistanceFieldLighting));
		RenderDistanceFieldLighting(RHICmdList, FDistanceFieldAOParameters(OcclusionMaxDistance), SceneContext.SceneVelocity, DummyOutput, false, ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO); 
		ServiceLocalQueue();
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	// Draw visualizations just before use to avoid target contamination
	if (ViewFamily.EngineShowFlags.VisualizeMeshDistanceFields || ViewFamily.EngineShowFlags.VisualizeGlobalDistanceField)
	{
		RenderMeshDistanceFieldVisualization(RHICmdList, FDistanceFieldAOParameters(Scene->DefaultMaxDistanceFieldOcclusionDistance));
		ServiceLocalQueue();
	}

	if (ViewFamily.EngineShowFlags.StationaryLightOverlap &&
		FeatureLevel >= ERHIFeatureLevel::SM4)
	{
		RenderStationaryLightOverlap(RHICmdList);
		ServiceLocalQueue();
	}

	// Resolve the scene color for post processing.
	ResolveSceneColor(RHICmdList);

	GetRendererModule().RenderPostResolvedSceneColorExtension(RHICmdList, SceneContext);

	CopySceneCaptureComponentToTarget(RHICmdList);

	// Finish rendering for each view.
	if (ViewFamily.bResolveScene)
	{
		SCOPED_DRAW_EVENT(RHICmdList, PostProcessing);
		SCOPED_GPU_STAT(RHICmdList, Postprocessing);

		SCOPE_CYCLE_COUNTER(STAT_FinishRenderViewTargetTime);

		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_PostProcessing));
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);

			if (ViewFamily.UseDebugViewPS())
			{
				DoDebugViewModePostProcessing(RHICmdList, Views[ViewIndex], SceneContext.SceneVelocity);
			}
			else
			{
				GPostProcessing.Process(RHICmdList, Views[ ViewIndex ], SceneContext.SceneVelocity);
			}
		}

		// End of frame, we don't need it anymore
		FSceneRenderTargets::Get(RHICmdList).FreeDownsampledTranslucencyDepth();

		// we rendered to it during the frame, seems we haven't made use of it, because it should be released
		check(!FSceneRenderTargets::Get(RHICmdList).SeparateTranslucencyRT);
	}
	else
	{
		// Release the original reference on the scene render targets
		SceneContext.AdjustGBufferRefCount(RHICmdList, -1);
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		ShaderPrint::EndView(Views[ViewIndex]);
	}

#if WITH_MGPU
	DoCrossGPUTransfers(RHICmdList, RenderTargetGPUMask);
#endif

	//grab the new transform out of the proxies for next frame
	SceneContext.SceneVelocity.SafeRelease();

	// Invalidate the lighting channels
	SceneContext.LightingChannels.SafeRelease();


#if RHI_RAYTRACING
	// Release resources that were bound to the ray tracing scene to allow them to be immediately recycled.
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		if (View.RayTracingScene.RayTracingSceneRHI)
		{
			RHICmdList.ClearRayTracingBindings(View.RayTracingScene.RayTracingSceneRHI);
			View.RayTracingScene.RayTracingSceneRHI.SafeRelease();
		}
	}
#endif //  RHI_RAYTRACING

	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderFinish);
		SCOPED_GPU_STAT(RHICmdList, FrameRenderFinish);
		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_RenderFinish));
		RenderFinish(RHICmdList);
		RHICmdList.SetCurrentStat(GET_STATID(STAT_CLM_AfterFrame));
	}
	ServiceLocalQueue();
}

/** A simple pixel shader used on PC to read scene depth from scene color alpha and write it to a downsized depth buffer. */
class FDownsampleSceneDepthPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDownsampleSceneDepthPS,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	FDownsampleSceneDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		SceneTextureParameters.Bind(Initializer);
		ProjectionScaleBias.Bind(Initializer.ParameterMap,TEXT("ProjectionScaleBias"));
		SourceTexelOffsets01.Bind(Initializer.ParameterMap,TEXT("SourceTexelOffsets01"));
		SourceTexelOffsets23.Bind(Initializer.ParameterMap,TEXT("SourceTexelOffsets23"));
		UseMaxDepth.Bind(Initializer.ParameterMap, TEXT("UseMaxDepth"));
		SourceMaxUVParameter.Bind(Initializer.ParameterMap, TEXT("SourceMaxUV"));
	}
	FDownsampleSceneDepthPS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, bool bUseMaxDepth, FIntPoint ViewMax)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, GetPixelShader(), View.ViewUniformBuffer);
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		// Used to remap view space Z (which is stored in scene color alpha) into post projection z and w so we can write z/w into the downsized depth buffer
		const FVector2D ProjectionScaleBiasValue(View.ViewMatrices.GetProjectionMatrix().M[2][2], View.ViewMatrices.GetProjectionMatrix().M[3][2]);
		SetShaderValue(RHICmdList, GetPixelShader(), ProjectionScaleBias, ProjectionScaleBiasValue);
		SetShaderValue(RHICmdList, GetPixelShader(), UseMaxDepth, (bUseMaxDepth ? 1.0f : 0.0f));

		FIntPoint BufferSize = SceneContext.GetBufferSizeXY();

		const uint32 DownsampledBufferSizeX = BufferSize.X / SceneContext.GetSmallColorDepthDownsampleFactor();
		const uint32 DownsampledBufferSizeY = BufferSize.Y / SceneContext.GetSmallColorDepthDownsampleFactor();

		// Offsets of the four full resolution pixels corresponding with a low resolution pixel
		const FVector4 Offsets01(0.0f, 0.0f, 1.0f / DownsampledBufferSizeX, 0.0f);
		SetShaderValue(RHICmdList, GetPixelShader(), SourceTexelOffsets01, Offsets01);
		const FVector4 Offsets23(0.0f, 1.0f / DownsampledBufferSizeY, 1.0f / DownsampledBufferSizeX, 1.0f / DownsampledBufferSizeY);
		SetShaderValue(RHICmdList, GetPixelShader(), SourceTexelOffsets23, Offsets23);
		SceneTextureParameters.Set(RHICmdList, GetPixelShader(), View.FeatureLevel, ESceneTextureSetupMode::All);

		// Set MaxUV, so we won't sample outside of a valid texture region.
		FVector2D const SourceMaxUV((ViewMax.X - 0.5f) / BufferSize.X, (ViewMax.Y - 0.5f) / BufferSize.Y);
		SetShaderValue(RHICmdList, GetPixelShader(), SourceMaxUVParameter, SourceMaxUV);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << ProjectionScaleBias;
		Ar << SourceTexelOffsets01;
		Ar << SourceTexelOffsets23;
		Ar << SceneTextureParameters;
		Ar << UseMaxDepth;
		Ar << SourceMaxUVParameter;
		return bShaderHasOutdatedParameters;
	}

	FShaderParameter ProjectionScaleBias;
	FShaderParameter SourceTexelOffsets01;
	FShaderParameter SourceTexelOffsets23;
	FShaderParameter SourceMaxUVParameter;
	FSceneTextureShaderParameters SceneTextureParameters;
	FShaderParameter UseMaxDepth;
};

IMPLEMENT_SHADER_TYPE(,FDownsampleSceneDepthPS,TEXT("/Engine/Private/DownsampleDepthPixelShader.usf"),TEXT("Main"),SF_Pixel);

/** Updates the downsized depth buffer with the current full resolution depth buffer. */
void FDeferredShadingSceneRenderer::UpdateDownsampledDepthSurface(FRHICommandList& RHICmdList)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	if (SceneContext.UseDownsizedOcclusionQueries() && (FeatureLevel >= ERHIFeatureLevel::SM4))
	{
		RHICmdList.TransitionResource( EResourceTransitionAccess::EReadable, SceneContext.GetSceneDepthSurface() );

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			DownsampleDepthSurface(RHICmdList, SceneContext.GetSmallDepthSurface(), View, 1.0f / SceneContext.GetSmallColorDepthDownsampleFactor(), true);
		}
	}
}

/** Downsample the scene depth with a specified scale factor to a specified render target
*/
void FDeferredShadingSceneRenderer::DownsampleDepthSurface(FRHICommandList& RHICmdList, const FTexture2DRHIRef& RenderTarget, const FViewInfo& View, float ScaleFactor, bool bUseMaxDepth)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	FRHIRenderPassInfo RPInfo;
	RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil;
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = RenderTarget;
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
	RHICmdList.BeginRenderPass(RPInfo, TEXT("DownsampleDepth"));
	{
	SCOPED_DRAW_EVENT(RHICmdList, DownsampleDepth);

	// Set shaders and texture
	TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);
	TShaderMapRef<FDownsampleSceneDepthPS> PixelShader(View.ShaderMap);

	extern TGlobalResource<FFilterVertexDeclaration> GFilterVertexDeclaration;

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.BlendState = TStaticBlendState<CW_NONE>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*ScreenVertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	PixelShader->SetParameters(RHICmdList, View, bUseMaxDepth, View.ViewRect.Max);
	const uint32 DownsampledX = FMath::TruncToInt(View.ViewRect.Min.X * ScaleFactor);
	const uint32 DownsampledY = FMath::TruncToInt(View.ViewRect.Min.Y * ScaleFactor);
	const uint32 DownsampledSizeX = FMath::TruncToInt(View.ViewRect.Width() * ScaleFactor);
	const uint32 DownsampledSizeY = FMath::TruncToInt(View.ViewRect.Height() * ScaleFactor);

	RHICmdList.SetViewport(DownsampledX, DownsampledY, 0.0f, DownsampledX + DownsampledSizeX, DownsampledY + DownsampledSizeY, 1.0f);

	DrawRectangle(
		RHICmdList,
		0, 0,
		DownsampledSizeX, DownsampledSizeY,
		View.ViewRect.Min.X, View.ViewRect.Min.Y,
		View.ViewRect.Width(), View.ViewRect.Height(),
		FIntPoint(DownsampledSizeX, DownsampledSizeY),
		SceneContext.GetBufferSizeXY(),
		*ScreenVertexShader,
		EDRF_UseTriangleOptimization);
	}
	RHICmdList.EndRenderPass();
}

/** */
class FCopyStencilToLightingChannelsPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopyStencilToLightingChannelsPS,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("STENCIL_LIGHTING_CHANNELS_SHIFT"), STENCIL_LIGHTING_CHANNELS_BIT_ID);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_R16_UINT);
	}

	FCopyStencilToLightingChannelsPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		SceneStencilTexture.Bind(Initializer.ParameterMap,TEXT("SceneStencilTexture"));
	}
	FCopyStencilToLightingChannelsPS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, GetPixelShader(), View.ViewUniformBuffer);
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		SetSRVParameter(RHICmdList, GetPixelShader(), SceneStencilTexture, SceneContext.SceneStencilSRV);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SceneStencilTexture;
		return bShaderHasOutdatedParameters;
	}

	FShaderResourceParameter SceneStencilTexture;
};

IMPLEMENT_SHADER_TYPE(,FCopyStencilToLightingChannelsPS,TEXT("/Engine/Private/DownsampleDepthPixelShader.usf"),TEXT("CopyStencilToLightingChannelsPS"),SF_Pixel);

void FDeferredShadingSceneRenderer::CopyStencilToLightingChannelTexture(FRHICommandList& RHICmdList)
{
	bool bAnyViewUsesLightingChannels = false;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		bAnyViewUsesLightingChannels = bAnyViewUsesLightingChannels || View.bUsesLightingChannels;
	}

	if (bAnyViewUsesLightingChannels)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		SCOPED_DRAW_EVENT(RHICmdList, CopyStencilToLightingChannels);
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, SceneContext.GetSceneDepthTexture());

		SceneContext.AllocateLightingChannelTexture(RHICmdList);

		// Set the light attenuation surface as the render target, and the scene depth buffer as the depth-stencil surface.
		FRHIRenderPassInfo RPInfo(SceneContext.LightingChannels->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store);
		TransitionRenderPassTargets(RHICmdList, RPInfo);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyStencilToLightingChannel"));
		{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		extern TGlobalResource<FFilterVertexDeclaration> GFilterVertexDeclaration;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			// Set shaders and texture
			TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);
			TShaderMapRef<FCopyStencilToLightingChannelsPS> PixelShader(View.ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*ScreenVertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			PixelShader->SetParameters(RHICmdList, View);

			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Min.X + View.ViewRect.Width(), View.ViewRect.Min.Y + View.ViewRect.Height(), 1.0f);

			DrawRectangle(
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y,
				View.ViewRect.Width(), View.ViewRect.Height(),
				FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
				SceneContext.GetBufferSizeXY(),
				*ScreenVertexShader,
				EDRF_UseTriangleOptimization);
		}
		}
		RHICmdList.EndRenderPass();
		RHICmdList.CopyToResolveTarget(SceneContext.LightingChannels->GetRenderTargetItem().TargetableTexture, SceneContext.LightingChannels->GetRenderTargetItem().TargetableTexture, FResolveParams());
	}
	else
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		ensure(SceneContext.LightingChannels.IsValid() == false);
	}
}

#if RHI_RAYTRACING

int32 GetForceRayTracingEffectsCVarValue()
{
	if (IsRayTracingEnabled())
	{
		static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.ForceAllRayTracingEffects"));
		return CVar != nullptr ? CVar->GetInt() : -1;
	}
	else
	{
		return 0;
	}
}

bool CanOverlayRayTracingOutput(const FViewInfo& View)
{
	return (View.RayTracingRenderMode != ERayTracingRenderMode::PathTracing)
		&& (View.RayTracingRenderMode != ERayTracingRenderMode::RayTracingDebug);
}
#endif // RHI_RAYTRACING
