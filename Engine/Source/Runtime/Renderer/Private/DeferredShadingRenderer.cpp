// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredShadingRenderer.cpp: Top level rendering loop for deferred shading
=============================================================================*/

#include "DeferredShadingRenderer.h"
#include "VelocityRendering.h"
#include "AtmosphereRendering.h"
#include "SingleLayerWaterRendering.h"
#include "SkyAtmosphereRendering.h"
#include "VolumetricCloudRendering.h"
#include "VolumetricRenderTarget.h"
#include "ScenePrivate.h"
#include "SceneOcclusion.h"
#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/PostProcessVisualizeCalibrationMaterial.h"
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
#include "VT/VirtualTextureFeedback.h"
#include "VT/VirtualTextureSystem.h"
#include "GPUScene.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "RayTracing/RayTracingLighting.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "ScreenSpaceRayTracing.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"
#include "ShaderPrint.h"
#include "GpuDebugRendering.h"
#include "GPUSortManager.h"
#include "HairStrands/HairStrandsRendering.h"
#include "PhysicsField/PhysicsFieldComponent.h"
#include "GPUSortManager.h"
#include "Rendering/NaniteResources.h"
#include "Rendering/NaniteStreamingManager.h"
#include "SceneTextureReductions.h"
#include "VirtualShadowMaps/VirtualShadowMapCacheManager.h"
#include "Strata/Strata.h"
#include "Lumen/Lumen.h"
#include "Experimental/Containers/SherwoodHashTable.h"

extern int32 GNaniteDebugFlags;
extern int32 GNaniteShowStats;

TAutoConsoleVariable<int32> CVarCustomDepthOrder(
	TEXT("r.CustomDepth.Order"),
	1,	
	TEXT("When CustomDepth (and CustomStencil) is getting rendered\n")
	TEXT("  0: Before GBuffer (can be more efficient with AsyncCompute, allows using it in DBuffer pass, no GBuffer blending decals allow GBuffer compression)\n")
	TEXT("  1: After Base Pass (default)"),
	ECVF_RenderThreadSafe);

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

static int32 GRayTracing = 0;
static TAutoConsoleVariable<int32> CVarRayTracing(
	TEXT("r.RayTracing"),
	GRayTracing,
	TEXT("0 to disable ray tracing.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

int32 GRayTracingUseTextureLod = 0;
static TAutoConsoleVariable<int32> CVarRayTracingTextureLod(
	TEXT("r.RayTracing.UseTextureLod"),
	GRayTracingUseTextureLod,
	TEXT("Enable automatic texture mip level selection in ray tracing material shaders.\n")
	TEXT(" 0: highest resolution mip level is used for all texture (default).\n")
	TEXT(" 1: texture LOD is approximated based on total ray length, output resolution and texel density at hit point (ray cone method)."),
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

static int32 GRayTracingSceneCaptures = -1;
static FAutoConsoleVariableRef CVarRayTracingSceneCaptures(
	TEXT("r.RayTracing.SceneCaptures"),
	GRayTracingSceneCaptures,
	TEXT("Enable ray tracing in scene captures.\n")
	TEXT(" -1: Use scene capture settings (default) \n")
	TEXT(" 0: off \n")
	TEXT(" 1: on"),
	ECVF_RenderThreadSafe);

static int32 GRayTracingExcludeDecals = 0;
static FAutoConsoleVariableRef CRayTracingExcludeDecals(
	TEXT("r.RayTracing.ExcludeDecals"),
	GRayTracingExcludeDecals,
	TEXT("A toggle that modifies the inclusion of decals in the ray tracing BVH.\n")
	TEXT(" 0: Decals included in the ray tracing BVH (default)\n")
	TEXT(" 1: Decals excluded from the ray tracing BVH"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingAsyncBuild(
	TEXT("r.RayTracing.AsyncBuild"),
	0,
	TEXT("Whether to build ray tracing acceleration structures on async compute queue.\n"),
	ECVF_RenderThreadSafe
);

static int32 GRayTracingParallelMeshBatchSetup = 1;
static FAutoConsoleVariableRef CRayTracingParallelMeshBatchSetup(
	TEXT("r.RayTracing.ParallelMeshBatchSetup"),
	GRayTracingParallelMeshBatchSetup,
	TEXT("Whether to setup ray tracing materials via parallel jobs."),
	ECVF_RenderThreadSafe);

static int32 GRayTracingParallelMeshBatchSize = 128;
static FAutoConsoleVariableRef CRayTracingParallelMeshBatchSize(
	TEXT("r.RayTracing.ParallelMeshBatchSize"),
	GRayTracingParallelMeshBatchSize,
	TEXT("Batch size for ray tracing materials parallel jobs."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRayTracingDynamicGeometryLastRenderTimeUpdateDistance(
	TEXT("r.RayTracing.DynamicGeometryLastRenderTimeUpdateDistance"),
	5000.0f,
	TEXT("Dynamic geometries within this distance will have their LastRenderTime updated, so that visibility based ticking (like skeletal mesh) can work when the component is not directly visible in the view (but reflected)."));

static TAutoConsoleVariable<int32> CVarRayTracingCulling(
	TEXT("r.RayTracing.Culling"),
	0,
	TEXT("Enable culling in ray tracing for objects that are behind the camera\n")
	TEXT(" 0: Culling disabled (default)\n")
	TEXT(" 1: Culling by distance and solid angle enabled"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRayTracingCullingRadius(
	TEXT("r.RayTracing.Culling.Radius"),
	10000.0f, 
	TEXT("Do camera culling for objects behind the camera outside of this radius in ray tracing effects (default = 10000 (100m))"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRayTracingCullingAngle(
	TEXT("r.RayTracing.Culling.Angle"),
	1.0f, 
	TEXT("Do camera culling for objects behind the camera with a projected angle smaller than this threshold in ray tracing effects (default = 5 degrees )"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingAutoInstance(
	TEXT("r.RayTracing.AutoInstance"),
	1,
	TEXT("Whether to auto instance static meshes\n"),
	ECVF_RenderThreadSafe
);

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarForceBlackVelocityBuffer(
	TEXT("r.Test.ForceBlackVelocityBuffer"), 0,
	TEXT("Force the velocity buffer to have no motion vector for debugging purpose."),
	ECVF_RenderThreadSafe);
#endif

namespace Lumen
{
	extern bool AnyLumenHardwareRayTracingPassEnabled();
}
namespace Nanite
{
	extern bool IsStatFilterActive(const FString& FilterName);
	extern void ListStatFilters(FSceneRenderer* SceneRenderer);
}

DECLARE_CYCLE_STAT(TEXT("PostInitViews FlushDel"), STAT_PostInitViews_FlushDel, STATGROUP_InitViews);
DECLARE_CYCLE_STAT(TEXT("InitViews Intentional Stall"), STAT_InitViews_Intentional_Stall, STATGROUP_InitViews);

DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer UpdateDownsampledDepthSurface"), STAT_FDeferredShadingSceneRenderer_UpdateDownsampledDepthSurface, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Render Init"), STAT_FDeferredShadingSceneRenderer_Render_Init, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Render ServiceLocalQueue"), STAT_FDeferredShadingSceneRenderer_Render_ServiceLocalQueue, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer DistanceFieldAO Init"), STAT_FDeferredShadingSceneRenderer_DistanceFieldAO_Init, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer FGlobalDynamicVertexBuffer Commit"), STAT_FDeferredShadingSceneRenderer_FGlobalDynamicVertexBuffer_Commit, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer FXSystem PreRender"), STAT_FDeferredShadingSceneRenderer_FXSystem_PreRender, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer AllocGBufferTargets"), STAT_FDeferredShadingSceneRenderer_AllocGBufferTargets, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer DBuffer"), STAT_FDeferredShadingSceneRenderer_DBuffer, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ResolveDepth After Basepass"), STAT_FDeferredShadingSceneRenderer_ResolveDepth_After_Basepass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Resolve After Basepass"), STAT_FDeferredShadingSceneRenderer_Resolve_After_Basepass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer FXSystem PostRenderOpaque"), STAT_FDeferredShadingSceneRenderer_FXSystem_PostRenderOpaque, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer AfterBasePass"), STAT_FDeferredShadingSceneRenderer_AfterBasePass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Lighting"), STAT_FDeferredShadingSceneRenderer_Lighting, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderLightShaftOcclusion"), STAT_FDeferredShadingSceneRenderer_RenderLightShaftOcclusion, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderAtmosphere"), STAT_FDeferredShadingSceneRenderer_RenderAtmosphere, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderSkyAtmosphere"), STAT_FDeferredShadingSceneRenderer_RenderSkyAtmosphere, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderFog"), STAT_FDeferredShadingSceneRenderer_RenderFog, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderLightShaftBloom"), STAT_FDeferredShadingSceneRenderer_RenderLightShaftBloom, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderFinish"), STAT_FDeferredShadingSceneRenderer_RenderFinish, STATGROUP_SceneRendering);

DECLARE_GPU_STAT_NAMED(RayTracingAS, TEXT("Ray Tracing Acceleration Structure Update/Refit"));
DECLARE_GPU_STAT_NAMED(RayTracingDynamicGeom, TEXT("Ray Tracing Dynamic Geometry Update"));

DECLARE_GPU_STAT(Postprocessing);
DECLARE_GPU_STAT(VisibilityCommands);
DECLARE_GPU_STAT(RenderDeferredLighting);
DECLARE_GPU_STAT(AllocateRendertargets);
DECLARE_GPU_STAT(FrameRenderFinish);
DECLARE_GPU_STAT(SortLights);
DECLARE_GPU_STAT(PostRenderOpsFX);
DECLARE_GPU_STAT(GPUSceneUpdate);
DECLARE_GPU_STAT_NAMED(Unaccounted, TEXT("[unaccounted]"));
DECLARE_GPU_DRAWCALL_STAT(WaterRendering);
DECLARE_GPU_STAT(HairRendering);
DEFINE_GPU_DRAWCALL_STAT(VirtualTextureUpdate);
DECLARE_GPU_STAT(UploadDynamicBuffers);
DECLARE_GPU_STAT(PostOpaqueExtensions);

CSV_DEFINE_CATEGORY(LightCount, true);

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
	, DepthPass(GetDepthPassInfo(Scene))
	, bAreLightsInLightGrid(false)
{}

/** 
* Renders the view family. 
*/

DEFINE_STAT(STAT_CLM_PrePass);
DECLARE_CYCLE_STAT(TEXT("FXPreRender"), STAT_CLM_FXPreRender, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterPrePass"), STAT_CLM_AfterPrePass, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Lighting"), STAT_CLM_Lighting, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterLighting"), STAT_CLM_AfterLighting, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("WaterPass"), STAT_CLM_WaterPass, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Translucency"), STAT_CLM_Translucency, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Distortion"), STAT_CLM_Distortion, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterTranslucency"), STAT_CLM_AfterTranslucency, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("RenderDistanceFieldLighting"), STAT_CLM_RenderDistanceFieldLighting, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("LightShaftBloom"), STAT_CLM_LightShaftBloom, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("PostProcessing"), STAT_CLM_PostProcessing, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Velocity"), STAT_CLM_Velocity, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterVelocity"), STAT_CLM_AfterVelocity, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("TranslucentVelocity"), STAT_CLM_TranslucentVelocity, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("RenderFinish"), STAT_CLM_RenderFinish, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterFrame"), STAT_CLM_AfterFrame, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Wait RayTracing Add Mesh Batch"), STAT_WaitRayTracingAddMesh, STATGROUP_SceneRendering);

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
		(Renderer->DepthPass.EarlyZPassMode != DDM_None || Renderer->DepthPass.bEarlyZPassMovable != 0);
}

bool FDeferredShadingSceneRenderer::RenderHzb(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture)
{
	RDG_GPU_STAT_SCOPE(GraphBuilder, HZB);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

		FSceneViewState* ViewState = View.ViewState;
		const FPerViewPipelineState& ViewPipelineState = *ViewPipelineStates[ViewIndex];


		if (ViewPipelineState.bClosestHZB || ViewPipelineState.bFurthestHZB)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "BuildHZB(ViewId=%d)", ViewIndex);

			FRDGTextureRef ClosestHZBTexture = nullptr;
			FRDGTextureRef FurthestHZBTexture = nullptr;

			BuildHZB(
				GraphBuilder,
				SceneDepthTexture,
				/* VisBufferTexture = */ nullptr,
				View,
				/* OutClosestHZBTexture = */ ViewPipelineState.bClosestHZB ? &ClosestHZBTexture : nullptr,
				/* OutFurthestHZBTexture = */ &FurthestHZBTexture);

			// Update the view.
			{
				View.HZBMipmap0Size = FurthestHZBTexture->Desc.Extent;
				View.HZB = FurthestHZBTexture;

				// Extract furthest HZB texture.
				if (View.ViewState)
				{
					GraphBuilder.QueueTextureExtraction(FurthestHZBTexture, &View.ViewState->PrevFrameViewInfo.HZB);
				}

				// Extract closest HZB texture.
				if (ViewPipelineState.bClosestHZB)
				{
					View.ClosestHZB = ClosestHZBTexture;
				}
			}
		}

		if (FamilyPipelineState->bHZBOcclusion && ViewState && ViewState->HZBOcclusionTests.GetNum() != 0)
		{
			check(ViewState->HZBOcclusionTests.IsValidFrame(ViewState->OcclusionFrameCounter));
			ViewState->HZBOcclusionTests.Submit(GraphBuilder, View);
		}
	}

	return FamilyPipelineState->bHZBOcclusion;
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

void AddServiceLocalQueuePass(FRDGBuilder& GraphBuilder)
{
	AddPass(GraphBuilder, [](FRHICommandListImmediate& RHICmdList)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Render_ServiceLocalQueue);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GetRenderThread_Local());

		if (IsRunningRHIInSeparateThread())
		{
			RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		}
	});
}

// @return 0/1
static int32 GetCustomDepthPassLocation()
{		
	return FMath::Clamp(CVarCustomDepthOrder.GetValueOnRenderThread(), 0, 1);
}

BEGIN_SHADER_PARAMETER_STRUCT(FRenderOpaqueFXPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
END_SHADER_PARAMETER_STRUCT()

static void RenderOpaqueFX(
	FRDGBuilder& GraphBuilder,
	TArrayView<const FViewInfo> Views,
	FFXSystemInterface* FXSystem,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer)
{
	// Notify the FX system that opaque primitives have been rendered and we now have a valid depth buffer.
	if (FXSystem && Views.Num() > 0)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, PostRenderOpsFX);
		RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderOpaqueFX);

		auto* PassParameters = GraphBuilder.AllocParameters<FRenderOpaqueFXPassParameters>();
		PassParameters->SceneTextures = SceneTexturesUniformBuffer;

		// Cascade uses pixel shaders for compute stuff in PostRenderOpaque so ERDGPassFlags::Raster is needed
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("OpaqueFX"),
			PassParameters,
			ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			[FXSystem, Views](FRHICommandListImmediate& RHICmdList)
		{
			SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_FXSystem_PostRenderOpaque);

			FXSystem->PostRenderOpaque(
				RHICmdList,
				Views[0].ViewUniformBuffer,
				&FSceneTextureUniformParameters::StaticStructMetadata,
				nullptr,
				Views[0].AllowGPUParticleUpdate()
			);

			if (FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager())
			{
				GPUSortManager->OnPostRenderOpaque(RHICmdList);
			}

			ServiceLocalQueue();
		});
	}
}

void FDeferredShadingSceneRenderer::PrepareDistanceFieldScene(FRDGBuilder& GraphBuilder, bool bSplitDispatch)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderDFAO);
	SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_DistanceFieldAO_Init);

	const bool bShouldPrepareHeightFieldScene = ShouldPrepareHeightFieldScene();
	const bool bShouldPrepareDistanceFieldScene = ShouldPrepareDistanceFieldScene();

	if (bShouldPrepareHeightFieldScene)
	{
		extern int32 GHFShadowQuality;
		if (GHFShadowQuality > 2)
		{
			GHFVisibilityTextureAtlas.UpdateAllocations(GraphBuilder, FeatureLevel);
		}
		GHeightFieldTextureAtlas.UpdateAllocations(GraphBuilder, FeatureLevel);
		UpdateGlobalHeightFieldObjectBuffers(GraphBuilder);
	}
	else if (bShouldPrepareDistanceFieldScene)
	{
		AddOrRemoveSceneHeightFieldPrimitives();
	}

	if (bShouldPrepareDistanceFieldScene)
	{
		auto DispatchToRHIThreadPass = [](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		};

		GDistanceFieldVolumeTextureAtlas.UpdateAllocations(GraphBuilder, FeatureLevel);
		UpdateGlobalDistanceFieldObjectBuffers(GraphBuilder);
		if (bSplitDispatch)
		{
			AddPass(GraphBuilder, DispatchToRHIThreadPass);
		}
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];

			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

			View.HeightfieldLightingViewInfo.SetupVisibleHeightfields(View, GraphBuilder);

			if (ShouldPrepareGlobalDistanceField())
			{
				float OcclusionMaxDistance = Scene->DefaultMaxDistanceFieldOcclusionDistance;

				// Use the skylight's max distance if there is one
				if (Scene->SkyLight && Scene->SkyLight->bCastShadows && !Scene->SkyLight->bWantsStaticShadowing)
				{
					OcclusionMaxDistance = Scene->SkyLight->OcclusionMaxDistance;
				}

				UpdateGlobalDistanceFieldVolume(GraphBuilder, Views[ViewIndex], Scene, OcclusionMaxDistance, Views[ViewIndex].GlobalDistanceFieldInfo);
			}
		}
		if (!bSplitDispatch)
		{
			AddPass(GraphBuilder, DispatchToRHIThreadPass);
		}
	}
}

#if RHI_RAYTRACING

bool FDeferredShadingSceneRenderer::GatherRayTracingWorldInstances(FRHICommandListImmediate& RHICmdList)
{
	if (!IsRayTracingEnabled() || Views.Num() == 0)
	{
		return false;
	}

	bool bAnyRayTracingPassEnabled = false;
	bool bPathTracingOrDebugViewEnabled = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		bAnyRayTracingPassEnabled |= AnyRayTracingPassEnabled(Scene, Views[ViewIndex]);
		bPathTracingOrDebugViewEnabled |= !CanOverlayRayTracingOutput(Views[ViewIndex]);
	}

	if (!bAnyRayTracingPassEnabled)
	{
		return false;
	}

	if (GetForceRayTracingEffectsCVarValue() == 0 && !bPathTracingOrDebugViewEnabled)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::GatherRayTracingWorldInstances);
	SCOPE_CYCLE_COUNTER(STAT_GatherRayTracingWorldInstances);

	RayTracingCollector.ClearViewMeshArrays();
	TArray<int> DynamicMeshBatchStartOffset;
	TArray<int> VisibleDrawCommandStartOffset;

	FGPUScenePrimitiveCollector DummyDynamicPrimitiveCollector;

	TArray<FRayTracingInstance> RayTracingInstances;

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
			&DummyDynamicPrimitiveCollector,
			ViewFamily.GetFeatureLevel(),
			&DynamicIndexBufferForInitViews,
			&DynamicVertexBufferForInitViews,
			&DynamicReadBufferForInitViews
			);

		View.DynamicRayTracingMeshCommandStorage.RayTracingMeshCommands.Reserve(Scene->Primitives.Num());
		View.VisibleRayTracingMeshCommands.Reserve(Scene->Primitives.Num());
	}

	const int8 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];

	extern TSet<IPersistentViewUniformBufferExtension*> PersistentViewUniformBufferExtensions;

	for (IPersistentViewUniformBufferExtension* Extension : PersistentViewUniformBufferExtensions)
	{
		Extension->BeginRenderView(&ReferenceView);
	}

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
		RHICmdList,
		*ReferenceView.RayTracingMeshResourceCollector
	};

	float CurrentWorldTime = ReferenceView.Family->CurrentWorldTime;

	struct FRelevantPrimitive
	{
		FRHIRayTracingGeometry* RayTracingGeometryRHI = nullptr;
		TArrayView<const int32> CachedRayTracingMeshCommandIndices;
		uint64 StateHash = 0;
		int32 PrimitiveIndex = -1;
		int8 ViewIndex = -1;
		int8 LODIndex = -1;
		uint8 RayTracedMeshElementsMask = 0;
		uint8 InstanceMask = 0;
		bool bAllSegmentsOpaque = true;
		bool bAnySegmentsCastShadow = false;
		bool bAnySegmentsDecal = false;

		uint64 InstancingKey() const
		{
			uint64 Key = StateHash;
			Key ^= uint64(InstanceMask) << 32;
			Key ^= bAllSegmentsOpaque ? 0x1ull << 40 : 0x0;
			Key ^= bAnySegmentsCastShadow ? 0x1ull << 41 : 0x0;
			Key ^= bAnySegmentsDecal ? 0x1ull << 42 : 0x0;
			return Key ^ reinterpret_cast<uint64>(RayTracingGeometryRHI);
		}
	};

	// Unified array is used for static and dynamic primitives because we don't know ahead of time how many we'll have of each.
	TArray<FRelevantPrimitive> RelevantPrimitives;
	RelevantPrimitives.Reserve(Scene->PrimitiveSceneProxies.Num());

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GatherRayTracingWorldInstances_RelevantPrimitives);

		int32 BroadIndex = 0;
		const int32 CullInRayTracing = CVarRayTracingCulling.GetValueOnRenderThread();
		const float CullingRadius = CVarRayTracingCullingRadius.GetValueOnRenderThread();
		const float CullAngleThreshold = CVarRayTracingCullingAngle.GetValueOnRenderThread();
		const float AngleThresholdRatio = FMath::Tan(CullAngleThreshold * PI / 180.0f);
		const FVector ViewOrigin = ReferenceView.ViewMatrices.GetViewOrigin();
		const FVector ViewDirection = ReferenceView.GetViewDirection();

		for (int PrimitiveIndex = 0; PrimitiveIndex < Scene->PrimitiveSceneProxies.Num(); PrimitiveIndex++)
		{
			while (PrimitiveIndex >= int(Scene->TypeOffsetTable[BroadIndex].Offset))
			{
				BroadIndex++;
			}

			const FPrimitiveSceneInfo* SceneInfo = Scene->Primitives[PrimitiveIndex];

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

			if (!(SceneInfo->bShouldRenderInMainPass && SceneInfo->bDrawInGame))
			{
				continue;
			}

			if (CullInRayTracing > 0)
			{
				FPrimitiveSceneProxy* SceneProxy = Scene->PrimitiveSceneProxies[PrimitiveIndex];

				const FBoxSphereBounds ObjectBounds = SceneProxy->GetBounds();
				const float ObjectRadius = ObjectBounds.SphereRadius;
				const FVector ObjectCenter = ObjectBounds.Origin + 0.5*ObjectBounds.BoxExtent;
				const FVector CameraToObjectCenter = FVector(ObjectCenter - ViewOrigin);

				const bool bIsBehindCamera = FVector::DotProduct(ViewDirection, CameraToObjectCenter) < -ObjectRadius;

				if (bIsBehindCamera)
				{
					const float CameraToObjectCenterLength = CameraToObjectCenter.Size();
					const bool bIsFarEnoughToCull = CameraToObjectCenterLength > (CullingRadius + ObjectRadius);

					if (bIsFarEnoughToCull) 
					{
						// Cull by solid angle: check the radius of bounding sphere against angle threshold
						const bool bAngleIsSmallEnoughToCull = ObjectRadius / CameraToObjectCenterLength < AngleThresholdRatio;

						if (bAngleIsSmallEnoughToCull)
						{
							continue;
						}					
					}
				}
			}

			bool bIsDynamic = false;

			FRelevantPrimitive Item;
			Item.PrimitiveIndex = PrimitiveIndex;

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				FViewInfo& View = Views[ViewIndex];
				if (!View.State)// || View.RayTracingRenderMode == ERayTracingRenderMode::Disabled)
				{
					continue;
				}

				if (View.bIsReflectionCapture)
				{
					continue;
				}

				if (View.HiddenPrimitives.Contains(SceneInfo->PrimitiveComponentId))
				{
					continue;
				}

				if (View.ShowOnlyPrimitives.IsSet() && !View.ShowOnlyPrimitives->Contains(SceneInfo->PrimitiveComponentId))
				{
					continue;
				}

				bool bShouldRayTraceSceneCapture = GRayTracingSceneCaptures > 0 || (GRayTracingSceneCaptures == -1 && View.bSceneCaptureUsesRayTracing);
				if (View.bIsSceneCapture && (!bShouldRayTraceSceneCapture || !SceneInfo->bIsVisibleInReflectionCaptures))
				{
					continue;
				}
				
				// Check if the primitive has been distance culled already during frustum culling
				if (View.DistanceCullingPrimitiveMap[PrimitiveIndex])
				{
					continue;
				}

				//#dxr_todo UE-68621  The Raytracing code path does not support ShowFlags since data moved to the SceneInfo. 
				//Touching the SceneProxy to determine this would simply cost too much
				static const auto RayTracingStaticMeshesCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.RayTracing.Geometry.StaticMeshes"));

				if (SceneInfo->bIsRayTracingStaticRelevant && View.Family->EngineShowFlags.StaticMeshes && RayTracingStaticMeshesCVar && RayTracingStaticMeshesCVar->GetValueOnRenderThread() > 0)
				{
					Item.ViewIndex = ViewIndex;
					RelevantPrimitives.Add(Item);
				}
				else if (View.Family->EngineShowFlags.SkeletalMeshes)
				{
					Item.RayTracedMeshElementsMask |= 1 << ViewIndex;
				}
			}

			if (Item.RayTracedMeshElementsMask)
			{
				Item.ViewIndex = ReferenceViewIndex;
				RelevantPrimitives.Add(Item);
			}
		}
	}

	FGraphEventArray LODTaskList;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GatherRayTracingWorldInstances_ComputeLOD);

		static const auto ICVarStaticMeshLODDistanceScale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.StaticMeshLODDistanceScale"));
		const float LODScaleCVarValue = ICVarStaticMeshLODDistanceScale->GetFloat();
		const int32 ForcedLODLevel = GetCVarForceLOD();

		const uint32 NumTotalItems = RelevantPrimitives.Num();
		const uint32 TargetItemsPerTask = 1024; // Granularity based on profiling Infiltrator scene
		const uint32 NumTasks = FMath::Max(1u, FMath::DivideAndRoundUp(NumTotalItems, TargetItemsPerTask));
		const uint32 ItemsPerTask = FMath::DivideAndRoundUp(NumTotalItems, NumTasks); // Evenly divide commands between tasks (avoiding potential short last task)

		LODTaskList.Reserve(NumTasks);

		for (uint32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
		{
			const uint32 FirstTaskItemIndex = TaskIndex * ItemsPerTask;

			LODTaskList.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
			[	Items = RelevantPrimitives.GetData() + FirstTaskItemIndex,
				NumItems = FMath::Min(ItemsPerTask, NumTotalItems - FirstTaskItemIndex),
				Views = Views.GetData(),
				Scene = this->Scene,
				LODScaleCVarValue,
				ForcedLODLevel
			]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GatherRayTracingWorldInstances_ComputeLOD_Task);

				for (uint32 i = 0; i < NumItems; ++i)
				{
					FRelevantPrimitive& RelevantPrimitive = Items[i];
					if (RelevantPrimitive.RayTracedMeshElementsMask)
					{
						continue; // skip dynamic primitives
					}

					const int32 PrimitiveIndex = RelevantPrimitive.PrimitiveIndex;
					const FPrimitiveSceneInfo* SceneInfo = Scene->Primitives[PrimitiveIndex];
					const int32 ViewIndex = RelevantPrimitive.ViewIndex;
					const FViewInfo& View = Views[ViewIndex];

					const FPrimitiveBounds& Bounds = Scene->PrimitiveBounds[PrimitiveIndex];
					const FPrimitiveSceneInfo* RESTRICT PrimitiveSceneInfo = Scene->Primitives[PrimitiveIndex];

					FLODMask LODToRender;

					const int8 CurFirstLODIdx = PrimitiveSceneInfo->Proxy->GetCurrentFirstLODIdx_RenderThread();
					check(CurFirstLODIdx >= 0);

					float MeshScreenSizeSquared = 0;
					float LODScale = LODScaleCVarValue * View.LODDistanceFactor;
					LODToRender = ComputeLODForMeshes(SceneInfo->StaticMeshRelevances, View, Bounds.BoxSphereBounds.Origin, Bounds.BoxSphereBounds.SphereRadius, ForcedLODLevel, MeshScreenSizeSquared, CurFirstLODIdx, LODScale, true);

					FRHIRayTracingGeometry* RayTracingGeometryInstance = SceneInfo->GetStaticRayTracingGeometryInstance(LODToRender.GetRayTracedLOD());
					if (RayTracingGeometryInstance == nullptr)
					{
						continue;
					}


					// Sometimes LODIndex is out of range because it is clamped by ClampToFirstLOD, like the requested LOD is being streamed in and hasn't been available
					// According to InitViews, we should hide the static mesh instance
					const int8 LODIndex = LODToRender.GetRayTracedLOD();
					if (SceneInfo->CachedRayTracingMeshCommandIndicesPerLOD.IsValidIndex(LODIndex))
					{
						RelevantPrimitive.LODIndex = LODIndex;
						RelevantPrimitive.RayTracingGeometryRHI = SceneInfo->GetStaticRayTracingGeometryInstance(LODIndex);

						RelevantPrimitive.CachedRayTracingMeshCommandIndices = SceneInfo->CachedRayTracingMeshCommandIndicesPerLOD[LODIndex];
						RelevantPrimitive.StateHash = SceneInfo->CachedRayTracingMeshCommandsHashPerLOD[LODIndex];

						for (int32 CommandIndex : RelevantPrimitive.CachedRayTracingMeshCommandIndices)
						{
							if (CommandIndex >= 0)
							{
								const FRayTracingMeshCommand& RayTracingMeshCommand = Scene->CachedRayTracingMeshCommands.RayTracingMeshCommands[CommandIndex];

								RelevantPrimitive.InstanceMask |= RayTracingMeshCommand.InstanceMask;
								RelevantPrimitive.bAllSegmentsOpaque &= RayTracingMeshCommand.bOpaque;
								RelevantPrimitive.bAnySegmentsCastShadow |= RayTracingMeshCommand.bCastRayTracedShadows;
								RelevantPrimitive.bAnySegmentsDecal |= RayTracingMeshCommand.bDecal;
							}
							else
							{
								// CommandIndex == -1 indicates that the mesh batch has been filtered by FRayTracingMeshProcessor (like the shadow depth pass batch)
								// Do nothing in this case
							}
						}

						RelevantPrimitive.InstanceMask |= RelevantPrimitive.bAnySegmentsCastShadow ? RAY_TRACING_MASK_SHADOW : 0;
					}
				}
			},
			TStatId(), nullptr, ENamedThreads::AnyThread));
		}
	}

	//

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GatherRayTracingWorldInstances_DynamicElements);

		Scene->GetRayTracingDynamicGeometryCollection()->BeginUpdate();

		for (const FRelevantPrimitive& RelevantPrimitive : RelevantPrimitives)
		{
			const uint8 RayTracedMeshElementsMask = RelevantPrimitive.RayTracedMeshElementsMask;

			if (RayTracedMeshElementsMask == 0)
			{
				continue;
			}

			const int32 PrimitiveIndex = RelevantPrimitive.PrimitiveIndex;
			FPrimitiveSceneInfo* SceneInfo = Scene->Primitives[PrimitiveIndex];

			FPrimitiveSceneProxy* SceneProxy = Scene->PrimitiveSceneProxies[PrimitiveIndex];
			RayTracingInstances.Reset();
			SceneProxy->GetDynamicRayTracingInstances(MaterialGatheringContext, RayTracingInstances);

			for (auto DynamicRayTracingGeometryUpdate : MaterialGatheringContext.DynamicRayTracingGeometriesToUpdate)
			{
				Scene->GetRayTracingDynamicGeometryCollection()->AddDynamicMeshBatchForGeometryUpdate(
					Scene,
					&ReferenceView,
					SceneProxy,
					DynamicRayTracingGeometryUpdate,
					PrimitiveIndex
				);
			}

			MaterialGatheringContext.DynamicRayTracingGeometriesToUpdate.Reset();

			if (RayTracingInstances.Num() > 0)
			{
				for (FRayTracingInstance& Instance : RayTracingInstances)
				{
					FRayTracingGeometryInstance RayTracingInstance = { Instance.Geometry->RayTracingGeometryRHI };
					RayTracingInstance.UserData.Add((uint32)PrimitiveIndex);
					RayTracingInstance.Mask = Instance.Mask;
					RayTracingInstance.bForceOpaque = Instance.bForceOpaque;

					// Thin geometries like hair don't have material, as they only support shadow at the moment.
					check(Instance.Materials.Num() == Instance.Geometry->Initializer.Segments.Num() ||
						(Instance.Geometry->Initializer.Segments.Num() == 0 && Instance.Materials.Num() == 1) ||
						(Instance.Materials.Num() == 0 && (Instance.Mask & RAY_TRACING_MASK_THIN_SHADOW) > 0));

					if (Instance.InstanceGPUTransformsSRV.IsValid())
					{
						RayTracingInstance.NumTransforms = Instance.NumTransforms;
						RayTracingInstance.GPUTransformsSRV = Instance.InstanceGPUTransformsSRV;
					}
					else 
					{
						RayTracingInstance.NumTransforms = Instance.InstanceTransforms.Num();
						RayTracingInstance.Transforms.SetNumUninitialized(Instance.InstanceTransforms.Num());
						FMemory::Memcpy(RayTracingInstance.Transforms.GetData(), Instance.InstanceTransforms.GetData(), Instance.InstanceTransforms.Num() * sizeof(RayTracingInstance.Transforms[0]));
						static_assert(TIsSame<decltype(RayTracingInstance.Transforms[0]), decltype(Instance.InstanceTransforms[0])>::Value, "Unexpected transform type");
					}			

					uint32 InstanceIndex = ReferenceView.RayTracingGeometryInstances.Add(RayTracingInstance);

					for (int32 ViewIndex = 1; ViewIndex < Views.Num(); ViewIndex++)
					{
						Views[ViewIndex].RayTracingGeometryInstances.Add(RayTracingInstance);
					}

#if DO_CHECK
					ReferenceView.RayTracingGeometries.Add(Instance.Geometry);
					for (int32 ViewIndex = 1; ViewIndex < Views.Num(); ViewIndex++)
					{
						Views[ViewIndex].RayTracingGeometries.Add(Instance.Geometry);
					}
#endif // DO_CHECK

					if (GRayTracingParallelMeshBatchSetup && FApp::ShouldUseThreadingForPerformance())
					{
						ReferenceView.AddRayTracingMeshBatchData.Emplace(Instance.Materials, SceneProxy, InstanceIndex);
					}
					else
					{
						for (int32 SegmentIndex = 0; SegmentIndex < Instance.Materials.Num(); SegmentIndex++)
						{
							FMeshBatch& MeshBatch = Instance.Materials[SegmentIndex];
							FDynamicRayTracingMeshCommandContext CommandContext(ReferenceView.DynamicRayTracingMeshCommandStorage, ReferenceView.VisibleRayTracingMeshCommands, SegmentIndex, InstanceIndex);
							FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
							FRayTracingMeshProcessor RayTracingMeshProcessor(&CommandContext, Scene, &ReferenceView, PassDrawRenderState);

							RayTracingMeshProcessor.AddMeshBatch(MeshBatch, 1, SceneProxy);
						}
					}
				}

				if (CVarRayTracingDynamicGeometryLastRenderTimeUpdateDistance.GetValueOnRenderThread() > 0.0f)
				{
					if (FVector::Distance(SceneProxy->GetActorPosition(), ReferenceView.ViewMatrices.GetViewOrigin()) < CVarRayTracingDynamicGeometryLastRenderTimeUpdateDistance.GetValueOnRenderThread())
					{
						// Update LastRenderTime for components so that visibility based ticking (like skeletal meshes) can get updated
						// We are only doing this for dynamic geometries now
						SceneInfo->LastRenderTime = CurrentWorldTime;
						SceneInfo->UpdateComponentLastRenderTime(CurrentWorldTime, /*bUpdateLastRenderTimeOnScreen=*/true);
						SceneInfo->ConditionalUpdateUniformBuffer(RHICmdList);
					}
				}
			}
		}
	}

	//

	if (ReferenceView.AddRayTracingMeshBatchData.Num() > 0)
	{
		const uint32 NumTotalItems = ReferenceView.AddRayTracingMeshBatchData.Num();
		const uint32 TargetItemsPerTask = GRayTracingParallelMeshBatchSize;//128; // Granularity based on profiling several scenes
		const uint32 NumTasks = FMath::Max(1u, FMath::DivideAndRoundUp(NumTotalItems, TargetItemsPerTask));
		const uint32 BatchSize = FMath::DivideAndRoundUp(NumTotalItems, NumTasks);

		ReferenceView.DynamicRayTracingMeshCommandStorageParallel.Init(FDynamicRayTracingMeshCommandStorage(), BatchSize);
		ReferenceView.VisibleRayTracingMeshCommandsParallel.Init(FRayTracingMeshCommandOneFrameArray(), BatchSize);

		for (uint32 Batch = 0; Batch < NumTasks; Batch++)
		{
			uint32 BatchStart = Batch * BatchSize;
			uint32 BatchEnd = FMath::Min(BatchStart + BatchSize, (uint32) ReferenceView.AddRayTracingMeshBatchData.Num());

			ReferenceView.DynamicRayTracingMeshCommandStorageParallel[Batch].RayTracingMeshCommands.Reserve(Scene->Primitives.Num());
			ReferenceView.VisibleRayTracingMeshCommandsParallel[Batch].Reserve(Scene->Primitives.Num());

			ReferenceView.AddRayTracingMeshBatchTaskList.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
				[&ReferenceView, Scene = this->Scene, Batch, BatchStart, BatchEnd]()
			{
				FTaskTagScope TaskTagScope(ETaskTag::EParallelRenderingThread);
				TRACE_CPUPROFILER_EVENT_SCOPE(RayTracingMeshBatchTask);

				for (uint32 Index = BatchStart; Index < BatchEnd; Index++)
				{
					FRayTracingMeshBatchWorkItem& MeshBatchJob = ReferenceView.AddRayTracingMeshBatchData[Index];
					for (uint32 SegmentIndex = 0; SegmentIndex < (uint32) MeshBatchJob.MeshBatches.Num(); SegmentIndex++)
					{
						FMeshBatch& MeshBatch = MeshBatchJob.MeshBatches[SegmentIndex];
						const FPrimitiveSceneProxy* SceneProxy = MeshBatchJob.SceneProxy;
						const uint32 InstanceIndex = MeshBatchJob.InstanceIndex;
						FDynamicRayTracingMeshCommandContext CommandContext(ReferenceView.DynamicRayTracingMeshCommandStorageParallel[Batch], ReferenceView.VisibleRayTracingMeshCommandsParallel[Batch], SegmentIndex, InstanceIndex);
						FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
						FRayTracingMeshProcessor RayTracingMeshProcessor(&CommandContext, Scene, &ReferenceView, PassDrawRenderState);
						RayTracingMeshProcessor.AddMeshBatch(MeshBatch, 1, SceneProxy);
					}
				}
			},
				TStatId(), nullptr, ENamedThreads::AnyThread));
		}
	}

	//

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GatherRayTracingWorldInstances_AddInstances);

		const bool bAutoInstance = CVarRayTracingAutoInstance.GetValueOnRenderThread() != 0;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WaitForLODTasks);
			FTaskGraphInterface::Get().WaitUntilTasksComplete(LODTaskList, ENamedThreads::GetRenderThread_Local());
		}

		Experimental::TSherwoodMap<uint64, int32> InstanceSet;
		InstanceSet.Reserve(RelevantPrimitives.Num());

		// scan relevant primitives computing hash data to look for duplicate instances
		for (const FRelevantPrimitive& RelevantPrimitive : RelevantPrimitives)
		{
			const int32 PrimitiveIndex = RelevantPrimitive.PrimitiveIndex;
			const FPrimitiveSceneInfo* SceneInfo = Scene->Primitives[PrimitiveIndex];
			const int32 ViewIndex = RelevantPrimitive.ViewIndex;
			FViewInfo& View = Views[ViewIndex];
			const int8 LODIndex = RelevantPrimitive.LODIndex;

			if (LODIndex < 0 || RelevantPrimitive.RayTracedMeshElementsMask != 0)
			{
				continue; // skip dynamic primitives and other 
			}

			if (GRayTracingExcludeDecals && RelevantPrimitive.bAnySegmentsDecal)
			{
				continue;
			}

			// location if this is a new entry
			const int32 NewInstanceIndex = View.RayTracingGeometryInstances.Num();
			const uint64 InstanceKey = RelevantPrimitive.InstancingKey();

			const int32 Index = bAutoInstance ? InstanceSet.FindOrAdd(InstanceKey, NewInstanceIndex) : NewInstanceIndex;

			if (Index != NewInstanceIndex)
			{
				// reusing a previous entry, just append to the instance list
				FRayTracingGeometryInstance& RayTracingInstance = View.RayTracingGeometryInstances[Index];
				RayTracingInstance.NumTransforms++;
				RayTracingInstance.Transforms.Add(Scene->PrimitiveTransforms[PrimitiveIndex]);
				RayTracingInstance.UserData.Add((uint32)PrimitiveIndex);

			}
			else
			{
				for (int32 CommandIndex : RelevantPrimitive.CachedRayTracingMeshCommandIndices)
				{
					if (CommandIndex >= 0)
					{
						FVisibleRayTracingMeshCommand NewVisibleMeshCommand;

						NewVisibleMeshCommand.RayTracingMeshCommand = &Scene->CachedRayTracingMeshCommands.RayTracingMeshCommands[CommandIndex];
						NewVisibleMeshCommand.InstanceIndex = NewInstanceIndex;
						View.VisibleRayTracingMeshCommands.Add(NewVisibleMeshCommand);
						VisibleDrawCommandStartOffset[ViewIndex]++;
					}
					else
					{
						// CommandIndex == -1 indicates that the mesh batch has been filtered by FRayTracingMeshProcessor (like the shadow depth pass batch)
						// Do nothing in this case
					}
				}

				FRayTracingGeometryInstance& RayTracingInstance = View.RayTracingGeometryInstances.Emplace_GetRef();
				RayTracingInstance.NumTransforms = 1;
				RayTracingInstance.Transforms.SetNumUninitialized(1);
				RayTracingInstance.UserData.SetNumUninitialized(1);

				RayTracingInstance.GeometryRHI = RelevantPrimitive.RayTracingGeometryRHI;
				RayTracingInstance.Transforms[0] = Scene->PrimitiveTransforms[PrimitiveIndex];
				RayTracingInstance.UserData[0] = (uint32)PrimitiveIndex;
				RayTracingInstance.Mask = RelevantPrimitive.InstanceMask; // When no cached command is found, InstanceMask == 0 and the instance is effectively filtered out
				RayTracingInstance.bForceOpaque = RelevantPrimitive.bAllSegmentsOpaque;
			}
		}
	}

	return true;
}

bool FDeferredShadingSceneRenderer::DispatchRayTracingWorldUpdates(FRDGBuilder& GraphBuilder)
{
	if (!IsRayTracingEnabled() || Views.Num() == 0)
	{
		return false;
	}

	bool bAnyRayTracingPassEnabled = false;
	bool bPathTracingOrDebugViewEnabled = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		bAnyRayTracingPassEnabled |= AnyRayTracingPassEnabled(Scene, Views[ViewIndex]);
		bPathTracingOrDebugViewEnabled |= !CanOverlayRayTracingOutput(Views[ViewIndex]);
	}

	if (!bAnyRayTracingPassEnabled)
	{
		return false;
	}

	if (GetForceRayTracingEffectsCVarValue() == 0 && !bPathTracingOrDebugViewEnabled)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::DispatchRayTracingWorldUpdates);

	FViewInfo& ReferenceView = Views[0];
	if (ReferenceView.AddRayTracingMeshBatchTaskList.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_WaitRayTracingAddMesh);

		FTaskGraphInterface::Get().WaitUntilTasksComplete(ReferenceView.AddRayTracingMeshBatchTaskList, ENamedThreads::GetRenderThread_Local());

		for (int32 Batch = 0; Batch < ReferenceView.AddRayTracingMeshBatchTaskList.Num(); Batch++)
		{
			ReferenceView.VisibleRayTracingMeshCommands.Append(ReferenceView.VisibleRayTracingMeshCommandsParallel[Batch]);
		}

		ReferenceView.AddRayTracingMeshBatchTaskList.Empty();
		ReferenceView.AddRayTracingMeshBatchData.Empty();
	}

	bool bAsyncUpdateGeometry = (CVarRayTracingAsyncBuild.GetValueOnRenderThread() != 0)
		&& GRHISupportsRayTracingAsyncBuildAccelerationStructure;

	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		SET_DWORD_STAT(STAT_RayTracingInstances, View.RayTracingGeometryInstances.Num());

#ifdef DO_CHECK
		// Validate all the ray tracing geometries lifetimes
		int64 SharedBufferGenerationID = (int64)Scene->GetRayTracingDynamicGeometryCollection()->GetSharedBufferGenerationID();
		for (const FRayTracingGeometry* Geometry : View.RayTracingGeometries)
		{
			check(Geometry->DynamicGeometrySharedBufferGenerationID == FRayTracingGeometry::NonSharedVertexBuffers || Geometry->DynamicGeometrySharedBufferGenerationID == SharedBufferGenerationID);
		}
#endif // DO_CHECK

		FRayTracingSceneInitializer SceneInitializer;
		SceneInitializer.Instances = View.RayTracingGeometryInstances;
		SceneInitializer.ShaderSlotsPerGeometrySegment = RAY_TRACING_NUM_SHADER_SLOTS;
		SceneInitializer.NumMissShaderSlots = RAY_TRACING_NUM_MISS_SHADER_SLOTS;

		// #dxr_todo: UE-72565: refactor ray tracing effects to not be member functions of DeferredShadingRenderer. register each effect at startup and just loop over them automatically to gather all required shaders
		TArray<FRHIRayTracingShader*> RayGenShaders;
		PrepareRayTracingReflections(View, *Scene, RayGenShaders);
		PrepareSingleLayerWaterRayTracingReflections(View, *Scene, RayGenShaders);
		PrepareRayTracingShadows(View, RayGenShaders);
		PrepareRayTracingAmbientOcclusion(View, RayGenShaders);
		PrepareRayTracingSkyLight(View, RayGenShaders);
		PrepareRayTracingGlobalIllumination(View, RayGenShaders);
		PrepareRayTracingTranslucency(View, RayGenShaders);
		PrepareRayTracingDebug(View, RayGenShaders);
		PreparePathTracing(View, RayGenShaders);
		PrepareRayTracingLumenDirectLighting(View, *Scene, RayGenShaders);
		PrepareRayTracingScreenProbeGather(View, RayGenShaders);
		PrepareLumenHardwareRayTracingReflections(View, RayGenShaders);

		View.RayTracingScene.RayTracingSceneRHI = RHICreateRayTracingScene(SceneInitializer);

		if (RayGenShaders.Num())
		{
			auto DefaultHitShader = View.ShaderMap->GetShader<FOpaqueShadowHitGroup>().GetRayTracingShader();

			View.RayTracingMaterialPipeline = BindRayTracingMaterialPipeline(GraphBuilder.RHICmdList, View,
				RayGenShaders,
				DefaultHitShader
			);
		}

		// Initialize common resources used for lighting in ray tracing effects

		View.RayTracingSubSurfaceProfileTexture = GetSubsufaceProfileTexture_RT(GraphBuilder.RHICmdList);
		if (!View.RayTracingSubSurfaceProfileTexture)
		{
			View.RayTracingSubSurfaceProfileTexture = GSystemTextures.BlackDummy;
		}

		View.RayTracingSubSurfaceProfileSRV = RHICreateShaderResourceView(View.RayTracingSubSurfaceProfileTexture->GetRenderTargetItem().ShaderResourceTexture, 0);

		View.RayTracingLightData = CreateRayTracingLightData(GraphBuilder.RHICmdList, Scene->Lights, View,
			EUniformBufferUsage::UniformBuffer_SingleFrame);
	}

	if (!bAsyncUpdateGeometry)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingAS);
		RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingDynamicGeom);

		AddPass(GraphBuilder, [this](FRHICommandListImmediate& RHICmdList)
		{
			Scene->GetRayTracingDynamicGeometryCollection()->DispatchUpdates(RHICmdList);
		});

		AddPass(GraphBuilder, RDG_EVENT_NAME("BuildRayTracingScene"), [this](FRHICommandList& RHICmdList)
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				FViewInfo& View = Views[ViewIndex];
				RHICmdList.BuildAccelerationStructure(View.RayTracingScene.RayTracingSceneRHI);
			}
		});
	}
	else
	{
		AddPass(GraphBuilder, [this](FRHICommandList& RHICmdList)
		{
			check(RayTracingDynamicGeometryUpdateEndTransition == nullptr);
			const FRHITransition* RayTracingDynamicGeometryUpdateBeginTransition = RHICreateTransition(ERHIPipeline::Graphics, ERHIPipeline::AsyncCompute, ERHICreateTransitionFlags::None, {});
			RayTracingDynamicGeometryUpdateEndTransition = RHICreateTransition(ERHIPipeline::AsyncCompute, ERHIPipeline::Graphics, ERHICreateTransitionFlags::None, {});

			FRHIAsyncComputeCommandListImmediate& RHIAsyncCmdList = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();

			// TArray<FRHIUnorderedAccessView*> VertexBuffers;
			// Scene->GetRayTracingDynamicGeometryCollection()->GetVertexBufferUAVs(VertexBuffers);

			RHICmdList.BeginTransition(RayTracingDynamicGeometryUpdateBeginTransition);
			RHIAsyncCmdList.EndTransition(RayTracingDynamicGeometryUpdateBeginTransition);

			Scene->GetRayTracingDynamicGeometryCollection()->DispatchUpdates(RHIAsyncCmdList);

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				FViewInfo& View = Views[ViewIndex];
				RHIAsyncCmdList.BuildAccelerationStructure(View.RayTracingScene.RayTracingSceneRHI);
			}

			RHIAsyncCmdList.BeginTransition(RayTracingDynamicGeometryUpdateEndTransition);
			FRHIAsyncComputeCommandListImmediate::ImmediateDispatch(RHIAsyncCmdList);
		});
	}

	AddPass(GraphBuilder, [this](FRHICommandListImmediate& RHICmdList)
	{
		Scene->GetRayTracingDynamicGeometryCollection()->EndUpdate(RHICmdList);
	});

	return true;
}


void FDeferredShadingSceneRenderer::WaitForRayTracingScene(FRDGBuilder& GraphBuilder)
{
	bool bAnyRayTracingPassEnabled = false;
	bool bPathTracingOrDebugViewEnabled = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		bAnyRayTracingPassEnabled |= AnyRayTracingPassEnabled(Scene, Views[ViewIndex]);
		bPathTracingOrDebugViewEnabled |= !CanOverlayRayTracingOutput(Views[ViewIndex]);
	}

	if (!bAnyRayTracingPassEnabled)
	{
		return;
	}

	if (GetForceRayTracingEffectsCVarValue() == 0 && !bPathTracingOrDebugViewEnabled)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::WaitForRayTracingScene);

	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

	AddPass(GraphBuilder, [this](FRHICommandListImmediate& RHICmdList)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			FViewInfo& View = Views[ViewIndex];
			if (!View.RayTracingMaterialPipeline)
			{
				check(View.RayTracingMaterialBindings.Num() == 0);
				continue;
			}

			if (View.RayTracingMaterialBindings.Num())
			{
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(View.RayTracingMaterialBindingsTask, ENamedThreads::GetRenderThread_Local());

				// Gather bindings from all chunks and submit them all as a single batch to allow RHI to bind all shader parameters in parallel.

				uint32 NumTotalBindings = 0;

				for (FRayTracingLocalShaderBindingWriter* BindingWriter : View.RayTracingMaterialBindings)
				{
					const FRayTracingLocalShaderBindingWriter::FChunk* Chunk = BindingWriter->GetFirstChunk();
					while (Chunk)
					{
						NumTotalBindings += Chunk->Num;
						Chunk = Chunk->Next;
					}
				}

				const uint32 MergedBindingsSize = sizeof(FRayTracingLocalShaderBindings) * NumTotalBindings;
				FRayTracingLocalShaderBindings* MergedBindings = (FRayTracingLocalShaderBindings*)(RHICmdList.Bypass()
					? FMemStack::Get().Alloc(MergedBindingsSize, alignof(FRayTracingLocalShaderBindings))
					: RHICmdList.Alloc(MergedBindingsSize, alignof(FRayTracingLocalShaderBindings)));

				uint32 MergedBindingIndex = 0;
				for (FRayTracingLocalShaderBindingWriter* BindingWriter : View.RayTracingMaterialBindings)
				{
					const FRayTracingLocalShaderBindingWriter::FChunk* Chunk = BindingWriter->GetFirstChunk();
					while (Chunk)
					{
						const uint32 Num = Chunk->Num;
						for (uint32_t i = 0; i < Num; ++i)
						{
							MergedBindings[MergedBindingIndex] = Chunk->Bindings[i];
							MergedBindingIndex++;
						}
						Chunk = Chunk->Next;
					}
				}

				const bool bCopyDataToInlineStorage = false; // Storage is already allocated from RHICmdList, no extra copy necessary
				RHICmdList.SetRayTracingHitGroups(
					View.RayTracingScene.RayTracingSceneRHI,
					View.RayTracingMaterialPipeline,
					NumTotalBindings, MergedBindings,
					bCopyDataToInlineStorage);

				TArray<FRHIRayTracingShader*> DeferredMaterialRayGenShaders;

				PrepareRayTracingReflectionsDeferredMaterial(View, *Scene, DeferredMaterialRayGenShaders);
				PrepareRayTracingDeferredReflectionsDeferredMaterial(View, *Scene, DeferredMaterialRayGenShaders);
				PrepareRayTracingGlobalIlluminationDeferredMaterial(View, DeferredMaterialRayGenShaders);

				if (DeferredMaterialRayGenShaders.Num())
				{
					View.RayTracingMaterialGatherPipeline = BindRayTracingDeferredMaterialGatherPipeline(RHICmdList, View, DeferredMaterialRayGenShaders);
				}

				// Move the ray tracing binding container ownership to the command list, so that memory will be
				// released on the RHI thread timeline, after the commands that reference it are processed.
				RHICmdList.EnqueueLambda([Ptrs = MoveTemp(View.RayTracingMaterialBindings)](FRHICommandListImmediate&)
				{
					for (auto Ptr : Ptrs)
					{
						delete Ptr;
					}
				});
			}

			if (CanUseRayTracingLightingMissShader(View.GetShaderPlatform()))
			{
				SetupRayTracingLightingMissShader(RHICmdList, View);
			}
		}

		if (RayTracingDynamicGeometryUpdateEndTransition)
		{
			RHICmdList.EndTransition(RayTracingDynamicGeometryUpdateEndTransition);
			RayTracingDynamicGeometryUpdateEndTransition = nullptr;
		}
	});
}

#endif // RHI_RAYTRACING

static TAutoConsoleVariable<float> CVarStallInitViews(
	TEXT("CriticalPathStall.AfterInitViews"),
	0.0f,
	TEXT("Sleep for the given time after InitViews. Time is given in ms. This is a debug option used for critical path analysis and forcing a change in the critical path."));

void FDeferredShadingSceneRenderer::CommitFinalPipelineState()
{
	ViewPipelineStates.SetNum(Views.Num());

	// Family pipeline state
	{
		FamilyPipelineState.Set(&FFamilyPipelineState::bNanite, UseNanite(ShaderPlatform)); // TODO: Should this respect ViewFamily.EngineShowFlags.NaniteMeshes?

		static const auto ICVarHZBOcc = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HZBOcclusion"));
		FamilyPipelineState.Set(&FFamilyPipelineState::bHZBOcclusion, ICVarHZBOcc->GetInt() != 0);	
	}

	CommitIndirectLightingState();

	// Views pipeline states
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		TPipelineState<FPerViewPipelineState>& ViewPipelineState = ViewPipelineStates[ViewIndex];

		// Commit HZB state
		{
			const bool bHasSSGI = ViewPipelineState[&FPerViewPipelineState::bEnableSSGI];

			// Requires FurthestHZB
			ViewPipelineState.Set(&FPerViewPipelineState::bFurthestHZB,
				FamilyPipelineState[&FFamilyPipelineState::bHZBOcclusion] ||
				FamilyPipelineState[&FFamilyPipelineState::bNanite] ||
				ViewPipelineState[&FPerViewPipelineState::bUseLumenProbeHierarchy] ||
				ViewPipelineState[&FPerViewPipelineState::AmbientOcclusionMethod] == EAmbientOcclusionMethod::SSAO ||
				ViewPipelineState[&FPerViewPipelineState::ReflectionsMethod] == EReflectionsMethod::SSR ||
				bHasSSGI);

			// SSGI requires ClosestHZB
			ViewPipelineState.Set(&FPerViewPipelineState::bClosestHZB, 
				bHasSSGI || ViewPipelineState[&FPerViewPipelineState::DiffuseIndirectMethod] == EDiffuseIndirectMethod::Lumen);
		}
	}

	// Commit all the pipeline states.
	{
		for (TPipelineState<FPerViewPipelineState>& ViewPipelineState : ViewPipelineStates)
		{
			ViewPipelineState.Commit();
		}
		FamilyPipelineState.Commit();
	} 
}

void FDeferredShadingSceneRenderer::Render(FRHICommandListImmediate& RHICmdList)
{
	const bool bNaniteEnabled = UseNanite(ShaderPlatform) && ViewFamily.EngineShowFlags.NaniteMeshes;

	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DeferredShadingRenderer_Render(ViewFamily=%s)", ViewFamily.bResolveScene ? TEXT("Primary") : TEXT("Auxiliary")));

	Scene->UpdateAllPrimitiveSceneInfos(GraphBuilder, true);

	FGPUSceneScopeBeginEndHelper GPUSceneScopeBeginEndHelper(Scene->GPUScene, GPUSceneDynamicContext, *Scene);
	if (bNaniteEnabled)
	{
		Nanite::GGlobalResources.Update(GraphBuilder); // Needed to managed scratch buffers for Nanite.
		Nanite::GStreamingManager.BeginAsyncUpdate(GraphBuilder);
	}

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderOther);

	// Setups the final FViewInfo::ViewRect.
	PrepareViewRectsForRendering();

	if (ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags))
	{
		for (int32 LightIndex = 0; LightIndex < NUM_ATMOSPHERE_LIGHTS; ++LightIndex)
		{
			if (Scene->AtmosphereLights[LightIndex])
			{
				PrepareSunLightProxy(*Scene->GetSkyAtmosphereSceneInfo(),LightIndex, *Scene->AtmosphereLights[LightIndex]);
			}
		}
	}
	else if (Scene->AtmosphereLights[0] && Scene->HasAtmosphericFog())
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		// Only one atmospheric light at one time.
		Scene->GetAtmosphericFogSceneInfo()->PrepareSunLightProxy(*Scene->AtmosphereLights[0]);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	else
	{
		Scene->ResetAtmosphereLightsProperties();
	}

	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_Render, FColor::Emerald);

#if WITH_MGPU
	const FRHIGPUMask RenderTargetGPUMask = ComputeGPUMasks(RHICmdList);
#endif // WITH_MGPU

	// By default, limit our GPU usage to only GPUs specified in the view masks.
	SCOPED_GPU_MASK(RHICmdList, AllViewsGPUMask);
	SCOPED_GPU_MASK(FRHICommandListExecutor::GetImmediateAsyncComputeCommandList(), AllViewsGPUMask);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
	
	//make sure all the targets we're going to use will be safely writable.
	GRenderTargetPool.TransitionTargetsWritable(RHICmdList);

	// this way we make sure the SceneColor format is the correct one and not the one from the end of frame before
	SceneContext.ReleaseSceneColor();

	WaitOcclusionTests(RHICmdList);

	if (!ViewFamily.EngineShowFlags.Rendering)
	{
		GraphBuilder.Execute();
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
		SceneContext.Allocate(GraphBuilder, this);
	}

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Create(GraphBuilder);

	const bool bAllowStaticLighting = IsStaticLightingAllowed();

	const bool bUseVirtualTexturing = UseVirtualTexturing(FeatureLevel);
	if (bUseVirtualTexturing)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);
		// AllocateResources needs to be called before RHIBeginScene
		FVirtualTextureSystem::Get().AllocateResources(GraphBuilder, FeatureLevel);
		FVirtualTextureSystem::Get().CallPendingCallbacks();
	}

	// Nanite materials do not currently support most debug view modes.
	const bool bShouldApplyNaniteMaterials
		 = !ViewFamily.EngineShowFlags.ShaderComplexity
		&& !ViewFamily.UseDebugViewPS()
		&& !ViewFamily.EngineShowFlags.Wireframe
		&& !ViewFamily.EngineShowFlags.LightMapDensity;

	// if DDM_AllOpaqueNoVelocity was used, then velocity should have already been rendered as well
	const bool bIsEarlyDepthComplete = (DepthPass.EarlyZPassMode == DDM_AllOpaque || DepthPass.EarlyZPassMode == DDM_AllOpaqueNoVelocity);

	// Use read-only depth in the base pass if we have a full depth prepass.
	const bool bAllowReadOnlyDepthBasePass = bIsEarlyDepthComplete
		&& !ViewFamily.EngineShowFlags.ShaderComplexity
		&& !ViewFamily.UseDebugViewPS()
		&& !ViewFamily.EngineShowFlags.Wireframe
		&& !ViewFamily.EngineShowFlags.LightMapDensity;

	const FExclusiveDepthStencil::Type BasePassDepthStencilAccess =
		bAllowReadOnlyDepthBasePass
		? FExclusiveDepthStencil::DepthRead_StencilWrite
		: FExclusiveDepthStencil::DepthWrite_StencilWrite;

	FILCUpdatePrimTaskData ILCTaskData;

	// Find the visible primitives.
	RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);


	bool bDoInitViewAftersPrepass = false;
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, VisibilityCommands);
		bDoInitViewAftersPrepass = InitViews(GraphBuilder, BasePassDepthStencilAccess, ILCTaskData);
	}

	// Compute & commit the final state of the entire dependency topology of the renderer.
	CommitFinalPipelineState();

#if !UE_BUILD_SHIPPING
	if (CVarStallInitViews.GetValueOnRenderThread() > 0.0f)
	{
		SCOPE_CYCLE_COUNTER(STAT_InitViews_Intentional_Stall);
		FPlatformProcess::Sleep(CVarStallInitViews.GetValueOnRenderThread() / 1000.0f);
	}
#endif

	extern TSet<IPersistentViewUniformBufferExtension*> PersistentViewUniformBufferExtensions;

	for (IPersistentViewUniformBufferExtension* Extension : PersistentViewUniformBufferExtensions)
	{
		Extension->BeginFrame();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			// Must happen before RHI thread flush so any tasks we dispatch here can land in the idle gap during the flush
			Extension->PrepareView(&Views[ViewIndex]);
		}
	}

#if RHI_RAYTRACING
	// Gather mesh instances, shaders, resources, parameters, etc. and build ray tracing acceleration structure
	GatherRayTracingWorldInstances(RHICmdList);

	if (Views[0].RayTracingRenderMode != ERayTracingRenderMode::PathTracing)
	{
		extern ENGINE_API float GAveragePathTracedMRays;
		GAveragePathTracedMRays = 0.0f;
	}
#endif // RHI_RAYTRACING

	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, GPUSceneUpdate);

		auto FlushResourcesPass = [this](FRHICommandListImmediate& InRHICmdList)
		{
			// we will probably stall on occlusion queries, so might as well have the RHI thread and GPU work while we wait.
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(PostInitViews_FlushDel);
			SCOPE_CYCLE_COUNTER(STAT_PostInitViews_FlushDel);
			InRHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
		};

		if (GDoPrepareDistanceFieldSceneAfterRHIFlush && (GRHINeedsExtraDeletionLatency || !GRHICommandList.Bypass()))
		{
			AddPass(GraphBuilder, FlushResourcesPass);
		}

		Scene->GPUScene.Update(GraphBuilder, *Scene);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

			ShaderPrint::BeginView(GraphBuilder, View);
			ShaderDrawDebug::BeginView(GraphBuilder, View);
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

			Scene->GPUScene.UploadDynamicPrimitiveShaderDataForView(GraphBuilder.RHICmdList, *Scene, View);
		}

		if (!bDoInitViewAftersPrepass)
		{
			bool bSplitDispatch = !GDoPrepareDistanceFieldSceneAfterRHIFlush;
			PrepareDistanceFieldScene(GraphBuilder, bSplitDispatch);
		}

		if (Views.Num() > 0)
		{
			FViewInfo& View = Views[0];
			Scene->UpdatePhysicsField(GraphBuilder, View);
		}

		if (!GDoPrepareDistanceFieldSceneAfterRHIFlush && (GRHINeedsExtraDeletionLatency || !GRHICommandList.Bypass()))
		{
			AddPass(GraphBuilder, FlushResourcesPass);
		}
	}

	// GBuffers are temporarily being allocated here until they are converted to RDG.
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_AllocGBufferTargets);
		SceneContext.AllocGBufferTargets(RHICmdList);
	}

	FSceneTextures& SceneTextures = FSceneTextures::Create(GraphBuilder);

	// Note, should happen after the GPU-Scene update to ensure rendering to runtime virtual textures is using the correctly updated scene
	if (bUseVirtualTexturing)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);

		FVirtualTextureSystem::Get().Update(GraphBuilder, FeatureLevel, Scene);

		AddPass(GraphBuilder, [this, &SceneContext](FRHICommandListImmediate& InRHICmdList)
		{
			// Clear virtual texture feedback to default value
			FUnorderedAccessViewRHIRef FeedbackUAV = SceneContext.GetVirtualTextureFeedbackUAV();
			InRHICmdList.Transition(FRHITransitionInfo(FeedbackUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			InRHICmdList.ClearUAVUint(FeedbackUAV, FUintVector4(~0u, ~0u, ~0u, ~0u));
			InRHICmdList.Transition(FRHITransitionInfo(FeedbackUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVGraphics));
			InRHICmdList.BeginUAVOverlap(FeedbackUAV);
		});
	}

	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);
	bool bCanOverlayRayTracingOutput = CanOverlayRayTracingOutput(Views[0]);// #dxr_todo: UE-72557 multi-view case
	
	const bool bRenderDeferredLighting = ViewFamily.EngineShowFlags.Lighting
		&& FeatureLevel >= ERHIFeatureLevel::SM5
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

		extern int32 GAllowLumenScene;
		bComputeLightGrid |= (
			ShouldRenderVolumetricFog() ||
			ViewFamily.ViewMode != VMI_Lit ||
			GAllowLumenScene);

		// TODO: Move decision logic to separate function for readability
		static const auto EnableVirtualSMCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.v.Enable"));

		bComputeLightGrid = bComputeLightGrid || EnableVirtualSMCVar->GetValueOnRenderThread() != 0;
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

	
	const bool bIsOcclusionTesting = DoOcclusionQueries(FeatureLevel) && (!ViewFamily.EngineShowFlags.Wireframe || bIsViewFrozen || bHasViewParent);
	const bool bNeedsPrePass = NeedsPrePass(this);

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

	if (DepthPass.IsComputeStencilDitherEnabled())
	{
		AddDitheredStencilFillPass(GraphBuilder, Views, SceneTextures.Depth.Target, DepthPass);
	}

	// Notify the FX system that the scene is about to be rendered.
	if (FXSystem && Views.IsValidIndex(0))
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_FXSystem_PreRender);
		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_FXPreRender));
		AddPass(GraphBuilder, [this](FRHICommandListImmediate& InRHICmdList)
		{
			FXSystem->PreRender(InRHICmdList, Views[0].ViewUniformBuffer, &Views[0].GlobalDistanceFieldInfo.ParameterData, Views[0].AllowGPUParticleUpdate());
			if (FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager())
			{
				GPUSortManager->OnPreRender(InRHICmdList);
			}
		});
	}

	AddPass(GraphBuilder, [this](FRHICommandList& InRHICmdList)
	{
		RunGPUSkinCacheTransition(InRHICmdList, Scene, EGPUSkinCacheTransition::Renderer);
	});

	FHairStrandsBookmarkParameters HairStrandsBookmarkParameters;

	{
		if (IsHairStrandsEnabled(EHairStrandsShaderType::All, Scene->GetShaderPlatform()))
		{
			HairStrandsBookmarkParameters = CreateHairStrandsBookmarkParameters(Views[0]);
			RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessTasks, HairStrandsBookmarkParameters);
		}

		// Interpolation needs to happen after the skin cache run as there is a dependency 
		// on the skin cache output.
		const bool bRunHairStrands = HairStrandsBookmarkParameters.bHasElements && (Views.Num() > 0);
		if (bRunHairStrands)
		{
			if (HairStrandsBookmarkParameters.bStrandsGeometryEnabled)
			{
				RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessGatherCluster, HairStrandsBookmarkParameters);

				FHairCullingParams CullingParams;
				CullingParams.bCullingProcessSkipped = false;
				CullingParams.bShadowViewMode = false;
				ComputeHairStrandsClustersCulling(GraphBuilder, *HairStrandsBookmarkParameters.ShaderMap, Views, CullingParams, HairStrandsBookmarkParameters.HairClusterData);
			}

			RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessStrandsInterpolation, HairStrandsBookmarkParameters);
		}
	}

	if (bNaniteEnabled)
	{
		Nanite::ListStatFilters(this);

		// Must happen before any Nanite rendering in the frame
		Nanite::GStreamingManager.EndAsyncUpdate(GraphBuilder);
	}

	const bool bShouldRenderVelocities = ShouldRenderVelocities();
	const bool bBasePassCanOutputVelocity = FVelocityRendering::BasePassCanOutputVelocity(FeatureLevel);
	const bool bUseSelectiveBasePassOutputs = IsUsingSelectiveBasePassOutputs(ShaderPlatform);
	const bool bIsViewCompatible = Views.Num() > 0;
	const bool bHairEnable = HairStrandsBookmarkParameters.bHasElements && bIsViewCompatible && IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Views[0].GetShaderPlatform());

	{
		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_PrePass));

		// Both compute approaches run earlier, so skip clearing stencil here, just load existing.
		const ERenderTargetLoadAction StencilLoadAction = DepthPass.IsComputeStencilDitherEnabled()
			? ERenderTargetLoadAction::ELoad
			: ERenderTargetLoadAction::EClear;

		const ERenderTargetLoadAction DepthLoadAction = ERenderTargetLoadAction::EClear;
		AddClearDepthStencilPass(GraphBuilder, SceneTextures.Depth.Target, DepthLoadAction, StencilLoadAction);

		// Draw the scene pre-pass / early z pass, populating the scene depth buffer and HiZ
		if (bNeedsPrePass)
		{
			RenderPrePass(GraphBuilder, SceneTextures.Depth.Target);
		}
		else
		{
			// We didn't do the prepass, but we still want the HMD mask if there is one
			RenderPrePassHMD(GraphBuilder, SceneTextures.Depth.Target);
		}

		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_AfterPrePass));
		AddServiceLocalQueuePass(GraphBuilder);

		// special pass for DDM_AllOpaqueNoVelocity, which uses the velocity pass to finish the early depth pass write
		if (bShouldRenderVelocities && Scene->EarlyZPassMode == DDM_AllOpaqueNoVelocity)
		{
			// Render the velocities of movable objects
			AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_Velocity));
			RenderVelocities(GraphBuilder, SceneTextures, EVelocityPass::Opaque, bHairEnable);
			AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_AfterVelocity));
			AddServiceLocalQueuePass(GraphBuilder);
		}

		if (bDoInitViewAftersPrepass)
		{
			{
				SCOPED_GPU_STAT(GraphBuilder.RHICmdList, VisibilityCommands);
				InitViewsPossiblyAfterPrepass(GraphBuilder, ILCTaskData);
			}

			{
				SCOPED_GPU_STAT(GraphBuilder.RHICmdList, GPUSceneUpdate);
				PrepareDistanceFieldScene(GraphBuilder, false);
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_FGlobalDynamicVertexBuffer_Commit);
				DynamicVertexBufferForInitShadows.Commit();
				DynamicIndexBufferForInitShadows.Commit();
				DynamicReadBufferForInitShadows.Commit();
			}

			AddServiceLocalQueuePass(GraphBuilder);
		}
	}

#if RHI_RAYTRACING
	// Must be done after FGlobalDynamicVertexBuffer::Get().Commit() for dynamic geometries to be updated
	DispatchRayTracingWorldUpdates(GraphBuilder);
#endif

	TArray<Nanite::FRasterResults, TInlineAllocator<2>> NaniteRasterResults;
	if (bNaniteEnabled && Views.Num() > 0)
	{
		LLM_SCOPE_BYTAG(Nanite);

		NaniteRasterResults.AddDefaulted(Views.Num());

		RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteRaster);
		const FIntPoint RasterTextureSize = SceneTextures.Depth.Target->Desc.Extent;

		const FViewInfo& PrimaryViewRef = Views[0];
		const FIntRect PrimaryViewRect = PrimaryViewRef.ViewRect;
		
		// Primary raster view
		{
			Nanite::FRasterState RasterState;

			Nanite::FRasterContext RasterContext = Nanite::InitRasterContext(GraphBuilder, RasterTextureSize);

			const bool bTwoPassOcclusion = true;
			const bool bUpdateStreaming = true;
			const bool bSupportsMultiplePasses = false;
			const bool bForceHWRaster = RasterContext.RasterScheduling == Nanite::ERasterScheduling::HardwareOnly;
			const bool bPrimaryContext = true;

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				const FViewInfo& View = Views[ViewIndex];
				Nanite::FRasterResults& RasterResults = NaniteRasterResults[ViewIndex];

				Nanite::FCullingContext CullingContext = Nanite::InitCullingContext(
					GraphBuilder,
					*Scene,
					!bIsEarlyDepthComplete ? View.PrevViewInfo.NaniteHZB : View.PrevViewInfo.HZB,
					View.ViewRect,
					bTwoPassOcclusion,
					bUpdateStreaming,
					bSupportsMultiplePasses,
					bForceHWRaster,
					bPrimaryContext);

				static FString EmptyFilterName = TEXT(""); // Empty filter represents primary view.
				const bool bExtractStats = Nanite::IsStatFilterActive(EmptyFilterName);

				Nanite::FPackedView PackedView = Nanite::CreatePackedViewFromViewInfo(View, RasterTextureSize, VIEW_FLAG_HZBTEST, /*StreamingPriorityCategory*/ 3);

				Nanite::CullRasterize(
					GraphBuilder,
					*Scene,
					{ PackedView },
					CullingContext,
					RasterContext,
					RasterState,
					/*OptionalInstanceDraws*/ nullptr,
					bExtractStats
				);

				Nanite::EmitDepthTargets(
					GraphBuilder,
					*Scene,
					Views[ViewIndex],
					CullingContext,
					RasterContext,
					RasterResults.MaterialDepth,
					RasterResults.NaniteMask,
					RasterResults.VelocityBuffer
				);

				if (!bIsEarlyDepthComplete && bTwoPassOcclusion && View.ViewState)
				{
					// Won't have a complete SceneDepth for post pass so can't use complete HZB for main pass or it will poke holes in the post pass HZB killing occlusion culling.
					RDG_EVENT_SCOPE(GraphBuilder, "BuildNaniteHZB");

					FRDGTextureRef SceneDepth = SystemTextures.Black;
					FRDGTextureRef GraphHZB = nullptr;

					BuildHZB(
						GraphBuilder,
						SceneDepth,
						RasterContext.VisBuffer64,
						PrimaryViewRect,
						/* OutClosestHZBTexture = */ nullptr,
						/* OutFurthestHZBTexture = */ &GraphHZB );
					
					GraphBuilder.QueueTextureExtraction( GraphHZB, &View.ViewState->PrevFrameViewInfo.NaniteHZB );
				}

				Nanite::ExtractResults(GraphBuilder, CullingContext, RasterContext, RasterResults);
			}
		}

		if (GNaniteDebugFlags != 0 && GNaniteShowStats != 0)
		{
			Nanite::PrintStats(GraphBuilder, PrimaryViewRef);
		}
	}

	SceneTextures.SetupMode = ESceneTextureSetupMode::SceneDepth;
	SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);

	AddResolveSceneDepthPass(GraphBuilder, Views, SceneTextures.Depth);

	// NOTE: The ordering of the lights is used to select sub-sets for different purposes, e.g., those that support clustered deferred.
	FSortedLightSetSceneInfo SortedLightSet;
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, SortLights);
		GatherLightsAndComputeLightGrid(GraphBuilder, bComputeLightGrid, SortedLightSet);
	}

	CSV_CUSTOM_STAT(LightCount, All,  float(SortedLightSet.SortedLights.Num()), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(LightCount, ShadowOff, float(SortedLightSet.AttenuationLightStart), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(LightCount, ShadowOn, float(SortedLightSet.SortedLights.Num()) - float(SortedLightSet.AttenuationLightStart), ECsvCustomStatOp::Set);

	// Local helper function to perform virtual shadow map allocation, which can occur early, or late.
	auto AllocateVirtualShadowMaps = [
		&GraphBuilder,
		&SortedLightSet, 
		&NaniteRasterResults,
		&VisibleLightInfos=VisibleLightInfos,
		&VirtualShadowMapArray=VirtualShadowMapArray,
		&Views= Views, 
		VirtualShadowMapArrayCacheManager = Scene->VirtualShadowMapArrayCacheManager,
		this](bool bPostBasePass)
	{
		static const auto CVarEnableVirtualSM = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.v.Enable"));

		if (CVarEnableVirtualSM->GetValueOnRenderThread() != 0)
		{
			ensureMsgf(AreLightsInLightGrid(), TEXT("Virtual shadow map setup requires local lights to be injected into the light grid (this may be caused by 'r.LightCulling.Quality=0')."));
			// ensure(ShadowMapSetupDone)
			VirtualShadowMapArray.BuildPageAllocations(GraphBuilder, Views, SortedLightSet, VisibleLightInfos, NaniteRasterResults, bPostBasePass, VirtualShadowMapArrayCacheManager);
		}
	};

	CompositionLighting::FAsyncResults CompositionLightingAsyncResults;

	const auto RenderOcclusionLambda = [&]()
	{
		RenderOcclusion(GraphBuilder, SceneTextures, bIsOcclusionTesting);

		if (CompositionLighting::CanProcessAsync(Views))
		{
			CompositionLightingAsyncResults = CompositionLighting::ProcessAsync(GraphBuilder, Views, SceneTextures);
		}
	};

	// Early occlusion queries
	const bool bOcclusionBeforeBasePass = !bNaniteEnabled && ((DepthPass.EarlyZPassMode == EDepthDrawingMode::DDM_AllOccluders) || bIsEarlyDepthComplete);

	if (bOcclusionBeforeBasePass)
	{
		RenderOcclusionLambda();
	}

	AddServiceLocalQueuePass(GraphBuilder);
	// End early occlusion queries

	// Early Shadow depth rendering
	if (bOcclusionBeforeBasePass)
	{
		if (bNaniteEnabled)
		{
			AllocateVirtualShadowMaps(false);
		}

		RenderShadowDepthMaps(GraphBuilder);
		AddServiceLocalQueuePass(GraphBuilder);
	}
	// End early Shadow depth rendering

	const bool bShouldRenderSkyAtmosphere = ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags);
	const bool bShouldRenderVolumetricCloud = ShouldRenderVolumetricCloud(Scene, ViewFamily.EngineShowFlags);
	bool bVolumetricRenderTargetRequired = bShouldRenderVolumetricCloud;

	if (bVolumetricRenderTargetRequired)
	{
		InitVolumetricRenderTargetForViews(GraphBuilder, Views);
	}

	InitVolumetricCloudsForViews(GraphBuilder, bShouldRenderVolumetricCloud);

	// Generate sky LUTs once all shadow map has been evaluated (for volumetric light shafts). Requires bOcclusionBeforeBasePass.
	// This also must happen before the BasePass for Sky material to be able to sample valid LUTs.
	if (bShouldRenderSkyAtmosphere)
	{
		// Generate the Sky/Atmosphere look up tables
		RenderSkyAtmosphereLookUpTables(GraphBuilder);
	}

	// Capture the SkyLight using the SkyAtmosphere and VolumetricCloud component if available.
	const bool bRealTimeSkyCaptureEnabled = Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled && Views.Num() > 0;
	if (bRealTimeSkyCaptureEnabled)
	{
		FViewInfo& MainView = Views[0];
		Scene->AllocateAndCaptureFrameSkyEnvMap(GraphBuilder, *this, MainView, bShouldRenderSkyAtmosphere, bShouldRenderVolumetricCloud);
	}

	// Strata initialisation
	const bool bStrataEnabled = Strata::IsStrataEnabled();
	if (bStrataEnabled)
	{		
		Strata::InitialiseStrataFrameSceneData(*this, GraphBuilder);
	}

	if (GetCustomDepthPassLocation() == 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass0);
		RenderCustomDepthPassAtLocation(GraphBuilder, 0);

		SceneTextures.SetupMode |= ESceneTextureSetupMode::CustomDepth;
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);
	}

	UpdateLumenScene(GraphBuilder);

	if (bOcclusionBeforeBasePass)
	{
		{
			LLM_SCOPE_BYTAG(Lumen);
			RenderLumenSceneLighting(GraphBuilder, Views[0]);
		}

		ComputeVolumetricFog(GraphBuilder);
	}

	FHairStrandsRenderingData* HairDatas = nullptr;
	FHairStrandsRenderingData HairDatasStorage;

	FRDGTextureRef ForwardScreenSpaceShadowMaskTexture = nullptr;
	FRDGTextureRef ForwardScreenSpaceShadowMaskHairTexture = nullptr;
	if (IsForwardShadingEnabled(ShaderPlatform))
	{
		if (bHairEnable)
		{
			RenderHairPrePass(GraphBuilder, Scene, Views, HairDatasStorage);
			RenderHairBasePass(GraphBuilder, Scene, SceneTextures, Views, HairDatasStorage);
			HairDatas = &HairDatasStorage;
		}

		RenderForwardShadowProjections(GraphBuilder, SceneTextures, ForwardScreenSpaceShadowMaskTexture, ForwardScreenSpaceShadowMaskHairTexture, HairDatas);
	}

	FDBufferTextures DBufferTextures = CreateDBufferTextures(GraphBuilder, SceneTextures.Extent, ShaderPlatform);

	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(DeferredShadingSceneRenderer_DBuffer);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_DBuffer);
		CompositionLighting::ProcessBeforeBasePass(GraphBuilder, Views, Scene->UniformBuffers, SceneTextures, DBufferTextures);
	}
	
	if (IsForwardShadingEnabled(ShaderPlatform) && bAllowStaticLighting)
	{
		RenderIndirectCapsuleShadows(GraphBuilder, SceneTextures);
	}

	FTranslucencyLightingVolumeTextures TranslucencyLightingVolumeTextures;

	if (bRenderDeferredLighting && GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute)
	{
		InitTranslucencyLightingVolumeTextures(GraphBuilder, Views, ERDGPassFlags::AsyncCompute, TranslucencyLightingVolumeTextures);
	}

	SceneContext.AllocSceneColor(RHICmdList);
	SceneTextures.Color = RegisterExternalTextureMSAA(GraphBuilder, SceneContext.GetSceneColor());

	{
		RenderBasePass(GraphBuilder, SceneTextures, DBufferTextures, BasePassDepthStencilAccess, ForwardScreenSpaceShadowMaskTexture);
		AddServiceLocalQueuePass(GraphBuilder);
		
		if (bNaniteEnabled && bShouldApplyNaniteMaterials)
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				const FViewInfo& View = Views[ViewIndex];
				const Nanite::FRasterResults& RasterResults = NaniteRasterResults[ViewIndex];

				FRDGTextureRef SceneDepth = SceneTextures.Depth.Target;

				Nanite::DrawBasePass(
					GraphBuilder,
					SceneTextures,
					DBufferTextures,
					*Scene,
					View,
					RasterResults
				);

				Nanite::DrawVisualization(
					GraphBuilder,
					SceneDepth,
					*Scene,
					View,
					RasterResults
				);
			}
		}

		if (!bAllowReadOnlyDepthBasePass)
		{
			AddResolveSceneDepthPass(GraphBuilder, Views, SceneTextures.Depth);
		}
	}

	// Rebuild scene textures to include GBuffers.
	SceneTextures.SetupMode |= ESceneTextureSetupMode::GBuffers;
	SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);

	// mark GBufferA for saving for next frame if it's needed
	ExtractNormalsForNextFrameReprojection(GraphBuilder, SceneContext, Views);

	if (ViewFamily.EngineShowFlags.VisualizeLightCulling)
	{
		AddClearRenderTargetPass(GraphBuilder, SceneTextures.Color.Target, FLinearColor::Transparent);
	}

	if (bRealTimeSkyCaptureEnabled)
	{
		Scene->ValidateSkyLightRealTimeCapture(GraphBuilder, Views[0], SceneTextures.Color.Target);
	}

	VisualizeVolumetricLightmap(GraphBuilder, SceneTextures);

	// Occlusion after base pass
	if (!bOcclusionBeforeBasePass)
	{
		RenderOcclusionLambda();
	}

	AddServiceLocalQueuePass(GraphBuilder);

	// End occlusion after base

	if (!bUseGBuffer)
	{
		AddResolveSceneColorPass(GraphBuilder, Views, SceneTextures.Color);
	}

#if RHI_RAYTRACING
	const bool bRayTracingEnabled = IsRayTracingEnabled();
	if (bRayTracingEnabled)
	{
		WaitForRayTracingScene(GraphBuilder);
	}
#endif // RHI_RAYTRACING

	// Shadow and fog after base pass
	if (!bOcclusionBeforeBasePass)
	{
		if (bNaniteEnabled)
		{
			AllocateVirtualShadowMaps(true);
		}

		RenderShadowDepthMaps(GraphBuilder);
		
		{
			LLM_SCOPE_BYTAG(Lumen);
			RenderLumenSceneLighting(GraphBuilder, Views[0]);
		}

		AddServiceLocalQueuePass(GraphBuilder);
	}
	// End shadow and fog after base pass

	if (bNaniteEnabled)
	{
		if (!bOcclusionBeforeBasePass)
		{
			ComputeVolumetricFog(GraphBuilder);
		}

		VirtualShadowMapArray.RenderDebugInfo(GraphBuilder, Scene->VirtualShadowMapArrayCacheManager);

		if (Views.Num() > 0)
		{
			VirtualShadowMapArray.PrintStats(GraphBuilder, Views[0]);
		}

		Nanite::GStreamingManager.SubmitFrameStreamingRequests(GraphBuilder);

		if (Scene->VirtualShadowMapArrayCacheManager)
		{
			Scene->VirtualShadowMapArrayCacheManager->ExtractFrameData(VirtualShadowMapArray, GraphBuilder);
		}
	}
	else if (!bOcclusionBeforeBasePass)
	{
		ComputeVolumetricFog(GraphBuilder);
	}

	if (GetCustomDepthPassLocation() == 1)
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(CustomDepthPass);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass1);
		RenderCustomDepthPassAtLocation(GraphBuilder, 1);

		SceneTextures.SetupMode |= ESceneTextureSetupMode::CustomDepth;
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);
	}

	// TODO: Keeping the velocities here for testing, but if that works, this pass will be remove and DDM_AllOpaqueNoVelocity will be the only option with
	// DBuffer decals enabled.

	// If bBasePassCanOutputVelocity is set, basepass fully writes the velocity buffer unless bUseSelectiveBasePassOutputs is enabled.
	if (bShouldRenderVelocities && (!bBasePassCanOutputVelocity || bUseSelectiveBasePassOutputs) && (Scene->EarlyZPassMode != DDM_AllOpaqueNoVelocity))
	{
		// Render the velocities of movable objects
		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_Velocity));
		RenderVelocities(GraphBuilder, SceneTextures, EVelocityPass::Opaque, bHairEnable);
		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_AfterVelocity));
		AddServiceLocalQueuePass(GraphBuilder);

		// TODO: Populate velocity buffer from Nanite visibility buffer.
	}

	// Hair base pass for deferred shading
	if (bHairEnable && !IsForwardShadingEnabled(ShaderPlatform))
	{
		RenderHairPrePass(GraphBuilder, Scene, Views, HairDatasStorage);
		HairDatas = &HairDatasStorage;
	}

	// Copy lighting channels out of stencil before deferred decals which overwrite those values
	FRDGTextureRef LightingChannelsTexture = CopyStencilToLightingChannelTexture(GraphBuilder, Views, SceneTextures.Stencil);

	// Pre-lighting composition lighting stage
	// e.g. deferred decals, SSAO
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(AfterBasePass);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_AfterBasePass);

		if (!IsForwardShadingEnabled(ShaderPlatform))
		{
			AddResolveSceneDepthPass(GraphBuilder, Views, SceneTextures.Depth);
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			TPipelineState<FPerViewPipelineState>& ViewPipelineState = ViewPipelineStates[ViewIndex];
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

			bool bEnableSSAO = ViewPipelineState->AmbientOcclusionMethod == EAmbientOcclusionMethod::SSAO;
			CompositionLighting::ProcessAfterBasePass(GraphBuilder, View, Scene->UniformBuffers, SceneTextures, CompositionLightingAsyncResults, bEnableSSAO);
		}
	}

	// Hair base pass for deferred shading
	if (bHairEnable && !IsForwardShadingEnabled(ShaderPlatform))
	{
		check(HairDatas);
		RenderHairBasePass(GraphBuilder, Scene,  SceneTextures, Views, HairDatasStorage);

		// Disable temporarly HZB update with hair data. This is not mandatory.
		#if 0
		const FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder, SceneTextures.UniformBuffer);
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			RDG_EVENT_SCOPE(GraphBuilder, "BuildHZB_HairUpdate(ViewId=%d)", ViewIndex);
			BuildHZB(
				GraphBuilder,
				SceneTextureParameters.SceneTextures.Depth, 
				/* VisBufferTexture = */ nullptr,
				View,
				/* OutClosestHZBTexture = */ nullptr,
				/* OutFurthestHZBTexture = */ nullptr);
		}
		#endif
	}

	// Rebuild scene textures to include velocity, custom depth, and SSAO.
	SceneTextures.SetupMode |= ESceneTextureSetupMode::All;
	SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);

	if (!IsForwardShadingEnabled(ShaderPlatform))
	{
		// Clear stencil to 0 now that deferred decals are done using what was setup in the base pass.
		AddClearStencilPass(GraphBuilder, SceneTextures.Depth.Target);
	}

	if (bRenderDeferredLighting)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, RenderDeferredLighting);
		RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderLighting);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Lighting);

		FRDGTextureRef DynamicBentNormalAOTexture = nullptr;
		RenderDiffuseIndirectAndAmbientOcclusion(GraphBuilder, SceneTextures, HairDatas, /* bIsVisualizePass = */ false);

		// These modulate the scenecolor output from the basepass, which is assumed to be indirect lighting
		if (bAllowStaticLighting)
		{
			RenderIndirectCapsuleShadows(GraphBuilder, SceneTextures);
		}

		// These modulate the scene color output from the base pass, which is assumed to be indirect lighting
		RenderDFAOAsIndirectShadowing(GraphBuilder, SceneTextures, DynamicBentNormalAOTexture);

		// Clear the translucent lighting volumes before we accumulate
		if ((GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute) == false)
		{
			InitTranslucencyLightingVolumeTextures(GraphBuilder, Views, ERDGPassFlags::Compute, TranslucencyLightingVolumeTextures);
		}

#if RHI_RAYTRACING
		if (bRayTracingEnabled)
		{
			RenderDitheredLODFadingOutMask(GraphBuilder, Views[0], SceneTextures.Depth.Target);
		}
#endif

		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_Lighting));
		RenderLights(GraphBuilder, SceneTextures, TranslucencyLightingVolumeTextures, LightingChannelsTexture, SortedLightSet, HairDatas);
		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_AfterLighting));
		AddServiceLocalQueuePass(GraphBuilder);

		InjectTranslucencyLightingVolumeAmbientCubemap(GraphBuilder, Views, TranslucencyLightingVolumeTextures);
		FilterTranslucencyLightingVolume(GraphBuilder, Views, TranslucencyLightingVolumeTextures);
		AddServiceLocalQueuePass(GraphBuilder);

		// Render diffuse sky lighting and reflections that only operate on opaque pixels
		RenderDeferredReflectionsAndSkyLighting(GraphBuilder, SceneTextures, DynamicBentNormalAOTexture, HairDatas);

		AddSubsurfacePass(GraphBuilder, SceneTextures, Views);

		if (HairDatas)
		{
			RenderHairStrandsSceneColorScattering(GraphBuilder, SceneTextures.Color.Target, Scene, Views, HairDatas);
		}

	#if RHI_RAYTRACING
		if (ShouldRenderRayTracingSkyLight(Scene->SkyLight))
		{
			FRDGTextureRef SkyLightTexture = nullptr;
			FRDGTextureRef SkyLightHitDistanceTexture = nullptr;
			RenderRayTracingSkyLight(GraphBuilder, SceneTextures.Color.Target, SkyLightTexture, SkyLightHitDistanceTexture, HairDatas);
			CompositeRayTracingSkyLight(GraphBuilder, SceneTextures, SkyLightTexture, SkyLightHitDistanceTexture);
		}
	#endif

		AddServiceLocalQueuePass(GraphBuilder);
	}
	else if (HairDatas)
	{
		RenderLightsForHair(GraphBuilder, SceneTextures.UniformBuffer, SortedLightSet, HairDatas, ForwardScreenSpaceShadowMaskHairTexture, LightingChannelsTexture);
		RenderDeferredReflectionsAndSkyLightingHair(GraphBuilder, HairDatas);
	}

	FRDGTextureRef HalfResolutionDepthCheckerboardMinMaxTexture = nullptr;

	if (bShouldRenderVolumetricCloud && IsVolumetricRenderTargetEnabled())
	{
		HalfResolutionDepthCheckerboardMinMaxTexture = CreateHalfResolutionDepthCheckerboardMinMax(GraphBuilder, Views, SceneTextures.Depth.Resolve);
	}

	if (bShouldRenderVolumetricCloud)
	{
		// Generate the volumetric cloud render target
		bool bSkipVolumetricRenderTarget = false;
		bool bSkipPerPixelTracing = true;
		RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, HalfResolutionDepthCheckerboardMinMaxTexture);

		// Reconstruct the volumetric cloud render target to be ready to compose it over the scene
		ReconstructVolumetricRenderTarget(GraphBuilder, Views, SceneTextures.Depth.Resolve, HalfResolutionDepthCheckerboardMinMaxTexture);
	}

	const bool bShouldRenderTranslucency = ShouldRenderTranslucency();

	// Union of all translucency view render flags.
	ETranslucencyView TranslucencyViewsToRender = bShouldRenderTranslucency ? GetTranslucencyViews(Views) : ETranslucencyView::None;

	const bool bShouldRenderSingleLayerWater = bCanOverlayRayTracingOutput && ShouldRenderSingleLayerWater(Views);
	FSceneWithoutWaterTextures SceneWithoutWaterTextures;
	if (bShouldRenderSingleLayerWater)
	{
		if (EnumHasAnyFlags(TranslucencyViewsToRender, ETranslucencyView::UnderWater))
		{
			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderTranslucency);
			SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);
			AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_Translucency));
			RenderTranslucency(GraphBuilder, SceneTextures, TranslucencyLightingVolumeTextures, nullptr, ETranslucencyView::UnderWater);
			EnumRemoveFlags(TranslucencyViewsToRender, ETranslucencyView::UnderWater);
		}

		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_WaterPass));
		RenderSingleLayerWater(GraphBuilder, SceneTextures, bShouldRenderVolumetricCloud, SceneWithoutWaterTextures);
		AddServiceLocalQueuePass(GraphBuilder);
	}

	// Rebuild scene textures to include scene color.
	SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);

	FRDGTextureRef LightShaftOcclusionTexture = nullptr;

	// Draw Lightshafts
	if (ViewFamily.EngineShowFlags.LightShafts)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLightShaftOcclusion);
		LightShaftOcclusionTexture = RenderLightShaftOcclusion(GraphBuilder, SceneTextures);
	}

	// Draw atmosphere
	if (bCanOverlayRayTracingOutput && ShouldRenderAtmosphere(ViewFamily))
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderAtmosphere);
		RenderAtmosphere(GraphBuilder, SceneTextures, LightShaftOcclusionTexture);
	}

	// Draw the sky atmosphere
	if (bShouldRenderSkyAtmosphere)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderSkyAtmosphere);
		RenderSkyAtmosphere(GraphBuilder, SceneTextures);
	}

	// Draw fog.
	if (bCanOverlayRayTracingOutput && ShouldRenderFog(ViewFamily))
	{
		RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderFog);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderFog);
		RenderFog(GraphBuilder, SceneTextures, LightShaftOcclusionTexture);
	}

	// After the height fog, Draw volumetric clouds (having fog applied on them already) when using per pixel tracing,
	if (bShouldRenderVolumetricCloud)
	{
		bool bSkipVolumetricRenderTarget = true;
		bool bSkipPerPixelTracing = false;
		RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, HalfResolutionDepthCheckerboardMinMaxTexture);
	}

	// or composite the off screen buffer over the scene.
	if (bVolumetricRenderTargetRequired)
	{
		ComposeVolumetricRenderTargetOverScene(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target, bShouldRenderSingleLayerWater, SceneWithoutWaterTextures);
	}

	FRendererModule& RendererModule = static_cast<FRendererModule&>(GetRendererModule());
	RendererModule.RenderPostOpaqueExtensions(GraphBuilder, Views, SceneTextures, SceneContext);

	RenderOpaqueFX(GraphBuilder, Views, FXSystem, SceneTextures.UniformBuffer);

	if (bShouldRenderSkyAtmosphere)
	{
		// Debug the sky atmosphere. Critically rendered before translucency to avoid emissive leaking over visualization by writing depth. 
		// Alternative: render in post process chain as VisualizeHDR.
		RenderDebugSkyAtmosphere(GraphBuilder, SceneTextures.Color.Target, SceneTextures.Depth.Target);
	}

	if (HairDatas && !IsHairStrandsComposeAfterTranslucency())
	{
		RenderHairComposition(GraphBuilder, Views, HairDatas, SceneTextures.Color.Target, SceneTextures.Depth.Target);
	}

	FSeparateTranslucencyTextures SeparateTranslucencyTextures(SeparateTranslucencyDimensions);

	// Draw translucency.
	if (bCanOverlayRayTracingOutput && TranslucencyViewsToRender != ETranslucencyView::None)
	{
		RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderTranslucency);
		SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);

		// Raytracing doesn't need the distortion effect.
		const bool bShouldRenderDistortion = TranslucencyViewsToRender != ETranslucencyView::RayTracing;

#if RHI_RAYTRACING
		if (EnumHasAnyFlags(TranslucencyViewsToRender, ETranslucencyView::RayTracing))
		{
			RenderRayTracingTranslucency(GraphBuilder, SceneTextures.Color);
			EnumRemoveFlags(TranslucencyViewsToRender, ETranslucencyView::RayTracing);
		}
#endif

		// Render all remaining translucency views.
		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_Translucency));
		RenderTranslucency(GraphBuilder, SceneTextures, TranslucencyLightingVolumeTextures, &SeparateTranslucencyTextures, TranslucencyViewsToRender);
		AddServiceLocalQueuePass(GraphBuilder);
		TranslucencyViewsToRender = ETranslucencyView::None;

		if (bShouldRenderDistortion)
		{
			AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_Distortion));
			RenderDistortion(GraphBuilder, SceneTextures.Color.Target, SceneTextures.Depth.Target);
			AddServiceLocalQueuePass(GraphBuilder);
		}

		if (bShouldRenderVelocities)
		{
			const bool bRecreateSceneTextures = !SceneTextures.Velocity;

			AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_TranslucentVelocity));
			RenderVelocities(GraphBuilder, SceneTextures, EVelocityPass::Translucent, false);
			AddServiceLocalQueuePass(GraphBuilder);

			if (bRecreateSceneTextures)
			{
				// Rebuild scene textures to include newly allocated velocity.
				SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);
			}
		}

		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_AfterTranslucency));
	}

#if !UE_BUILD_SHIPPING
	if (CVarForceBlackVelocityBuffer.GetValueOnRenderThread())
	{
		SceneTextures.Velocity = SystemTextures.Black;

		// Rebuild the scene texture uniform buffer to include black.
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);
	}
#endif

	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, HairRendering);
		if (HairDatas && IsHairStrandsComposeAfterTranslucency())
		{
			RenderHairComposition(GraphBuilder, Views, HairDatas, SceneTextures.Color.Target, SceneTextures.Depth.Target);
		}

		if (HairStrandsBookmarkParameters.bHasElements)
		{
			RenderHairStrandsDebugInfo(GraphBuilder, Views, HairDatas, HairStrandsBookmarkParameters.HairClusterData, SceneTextures.Color.Target);
		}
	}

	if (bStrataEnabled)
	{
		Strata::AddVisualizeMaterialPasses(GraphBuilder, Views, SceneTextures.Color.Target);
	}

	if (bCanOverlayRayTracingOutput && ViewFamily.EngineShowFlags.LightShafts)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLightShaftBloom);
		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_LightShaftBloom));
		RenderLightShaftBloom(GraphBuilder, SceneTextures, SeparateTranslucencyTextures);
		AddServiceLocalQueuePass(GraphBuilder);
	}

	if (bUseVirtualTexturing)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);

		AddPass(GraphBuilder, RDG_EVENT_NAME("VirtualTextureUpdate"), [this, &SceneContext](FRHICommandListImmediate& InRHICmdList)
		{
			InRHICmdList.EndUAVOverlap(SceneContext.VirtualTextureFeedbackUAV);

			// No pass after this should make VT page requests
			InRHICmdList.Transition(FRHITransitionInfo(SceneContext.VirtualTextureFeedbackUAV, ERHIAccess::UAVGraphics, ERHIAccess::CopySrc));

			TArray<FIntRect, TInlineAllocator<4>> ViewRects;
			ViewRects.AddUninitialized(Views.Num());
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				ViewRects[ViewIndex] = Views[ViewIndex].ViewRect;
			}

			FVirtualTextureFeedbackBufferDesc Desc;
			Desc.Init2D(SceneContext.GetBufferSizeXY(), ViewRects, SceneContext.GetVirtualTextureFeedbackScale());

			SubmitVirtualTextureFeedbackBuffer(InRHICmdList, SceneContext.VirtualTextureFeedback, Desc);
		});
	}

#if RHI_RAYTRACING
	if (bRayTracingEnabled)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (Views[ViewIndex].RayTracingRenderMode == ERayTracingRenderMode::PathTracing)
			{
				RenderPathTracing(GraphBuilder, Views[ViewIndex], SceneTextures.UniformBuffer, SceneTextures.Color.Target);
			}
			else if (Views[ViewIndex].RayTracingRenderMode == ERayTracingRenderMode::RayTracingDebug)
			{
				RenderRayTracingDebug(GraphBuilder, Views[ViewIndex], SceneTextures.Color.Target);
			}
		}
	}
#endif

	RendererModule.RenderOverlayExtensions(GraphBuilder, Views, SceneTextures, SceneContext);

	if (ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO && ShouldRenderDistanceFieldLighting())
	{
		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_RenderDistanceFieldLighting));

		// Use the skylight's max distance if there is one, to be consistent with DFAO shadowing on the skylight
		const float OcclusionMaxDistance = Scene->SkyLight && !Scene->SkyLight->bWantsStaticShadowing ? Scene->SkyLight->OcclusionMaxDistance : Scene->DefaultMaxDistanceFieldOcclusionDistance;
		FRDGTextureRef DummyOutput = nullptr;
		RenderDistanceFieldLighting(GraphBuilder, SceneTextures, FDistanceFieldAOParameters(OcclusionMaxDistance), DummyOutput, false, ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO);
		AddServiceLocalQueuePass(GraphBuilder);
	}

	// Draw visualizations just before use to avoid target contamination
	if (ViewFamily.EngineShowFlags.VisualizeMeshDistanceFields || ViewFamily.EngineShowFlags.VisualizeGlobalDistanceField)
	{
		RenderMeshDistanceFieldVisualization(GraphBuilder, SceneTextures, FDistanceFieldAOParameters(Scene->DefaultMaxDistanceFieldOcclusionDistance));
		AddServiceLocalQueuePass(GraphBuilder);
	}

	RenderLumenSceneVisualization(GraphBuilder);
	RenderDiffuseIndirectAndAmbientOcclusion(GraphBuilder, SceneTextures, HairDatas, /* bIsVisualizePass = */ true);

	if (ViewFamily.EngineShowFlags.StationaryLightOverlap)
	{
		RenderStationaryLightOverlap(GraphBuilder, SceneTextures, LightingChannelsTexture);
		AddServiceLocalQueuePass(GraphBuilder);
	}

	// Resolve the scene color for post processing.
	AddResolveSceneColorPass(GraphBuilder, Views, SceneTextures.Color);

	RendererModule.RenderPostResolvedSceneColorExtension(GraphBuilder, SceneContext);

	FRDGTextureRef ViewFamilyTexture = TryCreateViewFamilyTexture(GraphBuilder, ViewFamily);

	CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures.UniformBuffer, ViewFamilyTexture);

	// Finish rendering for each view.
	if (ViewFamily.bResolveScene && ViewFamilyTexture)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "PostProcessing");
		RDG_GPU_STAT_SCOPE(GraphBuilder, Postprocessing);
		SCOPE_CYCLE_COUNTER(STAT_FinishRenderViewTargetTime);

		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_PostProcessing));

		FPostProcessingInputs PostProcessingInputs;
		PostProcessingInputs.ViewFamilyTexture = ViewFamilyTexture;
		PostProcessingInputs.SeparateTranslucencyTextures = &SeparateTranslucencyTextures;
		PostProcessingInputs.SceneTextures = SceneTextures.UniformBuffer;

		if (ViewFamily.UseDebugViewPS())
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				const FViewInfo& View = Views[ViewIndex];
   				const Nanite::FRasterResults* NaniteResults = bNaniteEnabled ? &NaniteRasterResults[ViewIndex] : nullptr;
				RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
				RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);
				AddDebugViewPostProcessingPasses(GraphBuilder, View, PostProcessingInputs, NaniteResults);
			}
		}
		else
		{
			for (int32 ViewExt = 0; ViewExt < ViewFamily.ViewExtensions.Num(); ++ViewExt)
			{
				for (int32 ViewIndex = 0; ViewIndex < ViewFamily.Views.Num(); ++ViewIndex)
				{
					FViewInfo& View = Views[ViewIndex];
					RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
					ViewFamily.ViewExtensions[ViewExt]->PrePostProcessPass_RenderThread(GraphBuilder, View, PostProcessingInputs);
				}
			}
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				const FViewInfo& View = Views[ViewIndex];
				const Nanite::FRasterResults* NaniteResults = bNaniteEnabled ? &NaniteRasterResults[ViewIndex] : nullptr;
				RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
				RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);
				AddPostProcessingPasses(GraphBuilder, View, PostProcessingInputs, NaniteResults);
			}
		}

		AddPass(GraphBuilder, [&SceneContext](FRHICommandListImmediate&)
		{
			SceneContext.SetSceneColor(nullptr);
		});
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		ShaderPrint::EndView(Views[ViewIndex]);
		ShaderDrawDebug::EndView(Views[ViewIndex]);
	}

	AddPass(GraphBuilder, [this, &SceneContext](FRHICommandListImmediate& InRHICmdList)
	{
		GEngine->GetPostRenderDelegate().Broadcast();

		SceneContext.AdjustGBufferRefCount(InRHICmdList, -1);

#if RHI_RAYTRACING
		// Release resources that were bound to the ray tracing scene to allow them to be immediately recycled.
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			FViewInfo& View = Views[ViewIndex];
			if (View.RayTracingScene.RayTracingSceneRHI)
			{
				InRHICmdList.ClearRayTracingBindings(View.RayTracingScene.RayTracingSceneRHI);
				View.RayTracingScene.RayTracingSceneRHI.SafeRelease();
			}

			// Release common lighting resources
			View.RayTracingSubSurfaceProfileSRV.SafeRelease();
			View.RayTracingSubSurfaceProfileTexture.SafeRelease();

			View.RayTracingLightData.LightBufferSRV.SafeRelease();
			View.RayTracingLightData.LightBuffer.SafeRelease();
			View.RayTracingLightData.LightCullVolumeSRV.SafeRelease();
			View.RayTracingLightData.LightCullVolume.SafeRelease();
			View.RayTracingLightData.LightIndices.Release();
			View.RayTracingLightData.UniformBuffer.SafeRelease();
		}
#endif //  RHI_RAYTRACING
	});

#if WITH_MGPU
	DoCrossGPUTransfers(GraphBuilder, RenderTargetGPUMask, ViewFamilyTexture);
#endif

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (ScreenSpaceRayTracing::ShouldKeepBleedFreeSceneColor(View))
		{
			// Keep scene color and depth for next frame screen space ray tracing.
			FSceneViewState* ViewState = View.ViewState;
			GraphBuilder.QueueTextureExtraction(SceneTextures.Depth.Resolve, &ViewState->PrevFrameViewInfo.DepthBuffer);
			GraphBuilder.QueueTextureExtraction(SceneTextures.Color.Resolve, &ViewState->PrevFrameViewInfo.ScreenSpaceRayTracingInput);
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderFinish);
		RDG_GPU_STAT_SCOPE(GraphBuilder, FrameRenderFinish);

		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_RenderFinish));

		RenderFinish(GraphBuilder, ViewFamilyTexture);

		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_AfterFrame));
	}

	GraphBuilder.Execute();

	ServiceLocalQueue();
}

#if RHI_RAYTRACING

bool AnyRayTracingPassEnabled(const FScene* Scene, const FViewInfo& View)
{
	static auto CVarRayTracingSkyLight = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.SkyLight"));
	static auto CVarRayTracingShadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows"));
	static auto CVarStochasticRectLight = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.StochasticRectLight"));

	const bool bRayTracingShadows = CVarRayTracingShadows != nullptr && CVarRayTracingShadows->GetInt() > 0;
	const bool bRayTracingStochasticRectLight = CVarStochasticRectLight != nullptr && CVarStochasticRectLight->GetInt() > 0;
	const bool bLumenHardwareRayTracing = Lumen::AnyLumenHardwareRayTracingPassEnabled();

	if (
		ShouldRenderRayTracingAmbientOcclusion(View)
		|| ShouldRenderRayTracingReflections(View)
		|| ShouldRenderRayTracingGlobalIllumination(View)
		|| ShouldRenderRayTracingTranslucency(View)
		|| ShouldRenderRayTracingSkyLight(Scene? Scene->SkyLight : nullptr)
		|| bRayTracingShadows
		|| bRayTracingStochasticRectLight
		|| bLumenHardwareRayTracing
		|| View.RayTracingRenderMode == ERayTracingRenderMode::PathTracing
		|| View.RayTracingRenderMode == ERayTracingRenderMode::RayTracingDebug
		)
	{
		return true;
	}
	else
	{
		return false;
	}	
}

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
	static auto CVarWiper = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PathTracing.WiperMode"));

	return (
		(View.RayTracingRenderMode != ERayTracingRenderMode::PathTracing || (View.RayTracingRenderMode == ERayTracingRenderMode::PathTracing && CVarWiper && CVarWiper->GetInt() > 0))
		&& (View.RayTracingRenderMode != ERayTracingRenderMode::RayTracingDebug)
		);
}
#endif // RHI_RAYTRACING
