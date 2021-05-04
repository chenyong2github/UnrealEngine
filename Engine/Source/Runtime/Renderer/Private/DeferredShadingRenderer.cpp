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
#include "RayTracing/RayTracingScene.h"
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
#include "HairStrands/HairStrandsData.h"
#include "PhysicsField/PhysicsFieldComponent.h"
#include "GPUSortManager.h"
#include "NaniteVisualizationData.h"
#include "Rendering/NaniteResources.h"
#include "Rendering/NaniteStreamingManager.h"
#include "SceneTextureReductions.h"
#include "VirtualShadowMaps/VirtualShadowMapCacheManager.h"
#include "Strata/Strata.h"
#include "Lumen/Lumen.h"
#include "Experimental/Containers/SherwoodHashTable.h"
#include "RayTracingGeometryManager.h"
#include "InstanceCulling/InstanceCullingManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

extern int32 GNaniteShowStats;

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

static int32 GRayTracingParallelMeshBatchSize = 1024;
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
	TEXT(" 1: Culling by distance and solid angle enabled. Only cull objects behind camera.\n")
	TEXT(" 2: Culling by distance and solid angle enabled. Cull objects in front and behind camera.\n")
	TEXT(" 3: Culling by distance OR solid angle enabled. Cull objects in front and behind camera."),
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

static int32 GRayTracingDebugDisableTriangleCull = 0;
static FAutoConsoleVariableRef CVarRayTracingDebugDisableTriangleCull(
	TEXT("r.RayTracing.DebugDisableTriangleCull"),
	GRayTracingDebugDisableTriangleCull,
	TEXT("Forces all ray tracing geometry instances to be double-sided by disabling back-face culling. This is useful for debugging and profiling. (default = 0)")
);


static int32 GRayTracingDebugForceOpaque = 0;
static FAutoConsoleVariableRef CVarRayTracingDebugForceOpaque(
	TEXT("r.RayTracing.DebugForceOpaque"),
	GRayTracingDebugForceOpaque,
	TEXT("Forces all ray tracing geometry instances to be opaque, effectively disabling any-hit shaders. This is useful for debugging and profiling. (default = 0)")
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

DECLARE_GPU_STAT(RayTracingScene);
DECLARE_GPU_STAT(RayTracingGeometry);

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
bool FDeferredShadingSceneRenderer::ShouldRenderPrePass() const
{
	return (DepthPass.EarlyZPassMode != DDM_None || DepthPass.bEarlyZPassMovable != 0);
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
				View.ViewRect,
				View.GetFeatureLevel(),
				View.GetShaderPlatform(),
				TEXT("HZBClosest"),
				/* OutClosestHZBTexture = */ ViewPipelineState.bClosestHZB ? &ClosestHZBTexture : nullptr,
				TEXT("HZBFurthest"),
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

		const ERDGPassFlags UBPassFlags = ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::NeverCull;

		// Add a pass which extracts the RHI handle from the scene textures UB and sends it to the FX system.
		FRenderOpaqueFXPassParameters* ExtractUBPassParameters = GraphBuilder.AllocParameters<FRenderOpaqueFXPassParameters>();
		ExtractUBPassParameters->SceneTextures = SceneTexturesUniformBuffer;
		GraphBuilder.AddPass({}, ExtractUBPassParameters, UBPassFlags, [ExtractUBPassParameters, FXSystem](FRHICommandList&)
		{
			FXSystem->SetSceneTexturesUniformBuffer(ExtractUBPassParameters->SceneTextures->GetRHIRef());
		});

		FXSystem->PostRenderOpaque(GraphBuilder, Views, true /*bAllowGPUParticleUpdate*/);

		// Clear the scene textures UB pointer on the FX system. Use the same pass parameters to extend resource lifetimes.
		GraphBuilder.AddPass({}, ExtractUBPassParameters, UBPassFlags, [FXSystem](FRHICommandList&)
		{
			FXSystem->SetSceneTexturesUniformBuffer(nullptr);
		});

		if (FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager())
		{
			GPUSortManager->OnPostRenderOpaque(GraphBuilder);
		}

		ServiceLocalQueue();
	}
}

#if RHI_RAYTRACING

static void AddDebugRayTracingInstanceFlags(ERayTracingInstanceFlags& InOutFlags)
{
	if (GRayTracingDebugForceOpaque)
	{
		InOutFlags |= ERayTracingInstanceFlags::ForceOpaque;
	}
	if (GRayTracingDebugDisableTriangleCull)
	{
		InOutFlags |= ERayTracingInstanceFlags::TriangleCullDisable;
	}
}

bool FDeferredShadingSceneRenderer::GatherRayTracingWorldInstancesForView(FRHICommandListImmediate& RHICmdList, FViewInfo& View, FRayTracingScene& RayTracingScene)
{
	if (!IsRayTracingEnabled())
	{
		return false;
	}

	bool bAnyRayTracingPassEnabled = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		bAnyRayTracingPassEnabled |= AnyRayTracingPassEnabled(Scene, Views[ViewIndex]);
	}

	if (!bAnyRayTracingPassEnabled)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::GatherRayTracingWorldInstances);
	SCOPE_CYCLE_COUNTER(STAT_GatherRayTracingWorldInstances);

	RayTracingCollector.ClearViewMeshArrays();

	FGPUScenePrimitiveCollector DummyDynamicPrimitiveCollector;

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

	View.DynamicRayTracingMeshCommandStorage.Reserve(Scene->Primitives.Num());
	View.VisibleRayTracingMeshCommands.Reserve(Scene->Primitives.Num());

	extern TSet<IPersistentViewUniformBufferExtension*> PersistentViewUniformBufferExtensions;

	for (IPersistentViewUniformBufferExtension* Extension : PersistentViewUniformBufferExtensions)
	{
		Extension->BeginRenderView(&View);
	}

	View.RayTracingMeshResourceCollector = MakeUnique<FRayTracingMeshResourceCollector>(
		Scene->GetFeatureLevel(),
		&DynamicIndexBufferForInitViews,
		&DynamicVertexBufferForInitViews,
		&DynamicReadBufferForInitViews);

	FRayTracingMaterialGatheringContext MaterialGatheringContext
	{
		Scene,
		&View,
		ViewFamily,
		RHICmdList,
		*View.RayTracingMeshResourceCollector
	};

	const float CurrentWorldTime = View.Family->CurrentWorldTime;

	struct FRelevantPrimitive
	{
		FRHIRayTracingGeometry* RayTracingGeometryRHI = nullptr;
		TArrayView<const int32> CachedRayTracingMeshCommandIndices;
		uint64 StateHash = 0;
		int32 PrimitiveIndex = -1;
		int8 LODIndex = -1;
		uint8 InstanceMask = 0;
		bool bStatic = false;
		bool bAllSegmentsOpaque = true;
		bool bAnySegmentsCastShadow = false;
		bool bAnySegmentsDecal = false;
		bool bTwoSided = false;

		uint64 InstancingKey() const
		{
			uint64 Key = StateHash;
			Key ^= uint64(InstanceMask) << 32;
			Key ^= bAllSegmentsOpaque ? 0x1ull << 40 : 0x0;
			Key ^= bAnySegmentsCastShadow ? 0x1ull << 41 : 0x0;
			Key ^= bAnySegmentsDecal ? 0x1ull << 42 : 0x0;
			Key ^= bTwoSided ? 0x1ull << 43 : 0x0;
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
		const float AngleThresholdRatio = FMath::Tan(FMath::Min(89.99f, CullAngleThreshold) * PI / 180.0f);
		const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();
		const FVector ViewDirection = View.GetViewDirection();
		const bool bCullAllObjects = CullInRayTracing == 2 || CullInRayTracing == 3;
		const bool bCullByRadiusOrDistance = CullInRayTracing == 3;

		for (int PrimitiveIndex = 0; PrimitiveIndex < Scene->PrimitiveSceneProxies.Num(); PrimitiveIndex++)
		{
			while (PrimitiveIndex >= int(Scene->TypeOffsetTable[BroadIndex].Offset))
			{
				BroadIndex++;
			}

			// Skip before dereferencing SceneInfo
			if (EnumHasAnyFlags(Scene->PrimitiveRayTracingFlags[PrimitiveIndex], ERayTracingPrimitiveFlags::UnsupportedProxyType))
			{
				//skip over unsupported SceneProxies (warning don't make IsRayTracingRelevant data dependent other than the vtable)
				PrimitiveIndex = Scene->TypeOffsetTable[BroadIndex].Offset - 1;
				continue;
			}

			if (EnumHasAnyFlags(Scene->PrimitiveRayTracingFlags[PrimitiveIndex], ERayTracingPrimitiveFlags::Excluded))
			{
				continue;
			}

			const FPrimitiveSceneInfo* SceneInfo = Scene->Primitives[PrimitiveIndex];

			if (CullInRayTracing > 0)
			{
				const FBoxSphereBounds ObjectBounds = Scene->PrimitiveBounds[PrimitiveIndex].BoxSphereBounds;
				const float ObjectRadius = ObjectBounds.SphereRadius;
				const FVector ObjectCenter = ObjectBounds.Origin + 0.5*ObjectBounds.BoxExtent;
				const FVector CameraToObjectCenter = FVector(ObjectCenter - ViewOrigin);

				const bool bConsiderCulling = bCullAllObjects || FVector::DotProduct(ViewDirection, CameraToObjectCenter) < -ObjectRadius;

				if (bConsiderCulling)
				{
					const float CameraToObjectCenterLength = CameraToObjectCenter.Size();
					const bool bIsFarEnoughToCull = CameraToObjectCenterLength > (CullingRadius + ObjectRadius);

					// Cull by solid angle: check the radius of bounding sphere against angle threshold
					const bool bAngleIsSmallEnoughToCull = ObjectRadius / CameraToObjectCenterLength < AngleThresholdRatio;

					if (bCullByRadiusOrDistance && (bIsFarEnoughToCull || bAngleIsSmallEnoughToCull))
					{
						continue;
					}
					else if (bIsFarEnoughToCull && bAngleIsSmallEnoughToCull)
					{
						continue;
					}
				}
			}

			if (!View.State)
			{
				continue;
			}

			if (View.bIsReflectionCapture)
			{
				continue;
			}

			if (View.HiddenPrimitives.Contains(Scene->PrimitiveComponentIds[PrimitiveIndex]))
			{
				continue;
			}

			if (View.ShowOnlyPrimitives.IsSet() && !View.ShowOnlyPrimitives->Contains(Scene->PrimitiveComponentIds[PrimitiveIndex]))
			{
				continue;
			}

			// #dxr_todo: ray tracing in scene captures should re-use the persistent RT scene. (UE-112448)
			bool bShouldRayTraceSceneCapture = GRayTracingSceneCaptures > 0
				|| (GRayTracingSceneCaptures == -1 && View.bSceneCaptureUsesRayTracing);

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

			FRelevantPrimitive Item;
			Item.PrimitiveIndex = PrimitiveIndex;

			if (EnumHasAnyFlags(Scene->PrimitiveRayTracingFlags[PrimitiveIndex], ERayTracingPrimitiveFlags::CacheMeshCommands)
				&& View.Family->EngineShowFlags.StaticMeshes 
				&& RayTracingStaticMeshesCVar && RayTracingStaticMeshesCVar->GetValueOnRenderThread() > 0)
			{
				Item.bStatic = true;
				RelevantPrimitives.Add(Item);
			}
			else if (View.Family->EngineShowFlags.SkeletalMeshes)
			{
				Item.bStatic = false;
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
				&View,
				Scene = this->Scene,
				LODScaleCVarValue,
				ForcedLODLevel
			]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GatherRayTracingWorldInstances_ComputeLOD_Task);

				for (uint32 i = 0; i < NumItems; ++i)
				{
					FRelevantPrimitive& RelevantPrimitive = Items[i];
					if (!RelevantPrimitive.bStatic)
					{
						continue; // skip dynamic primitives
					}

					const int32 PrimitiveIndex = RelevantPrimitive.PrimitiveIndex;
					const FPrimitiveSceneInfo* SceneInfo = Scene->Primitives[PrimitiveIndex];

					int8 LODIndex = 0;

					if (EnumHasAnyFlags(Scene->PrimitiveRayTracingFlags[PrimitiveIndex], ERayTracingPrimitiveFlags::ComputeLOD))
					{
						const FPrimitiveBounds& Bounds = Scene->PrimitiveBounds[PrimitiveIndex];
						const FPrimitiveSceneInfo* RESTRICT PrimitiveSceneInfo = Scene->Primitives[PrimitiveIndex];

						FLODMask LODToRender;

						const int8 CurFirstLODIdx = PrimitiveSceneInfo->Proxy->GetCurrentFirstLODIdx_RenderThread();
						check(CurFirstLODIdx >= 0);

						float MeshScreenSizeSquared = 0;
						float LODScale = LODScaleCVarValue * View.LODDistanceFactor;
						LODToRender = ComputeLODForMeshes(SceneInfo->StaticMeshRelevances, View, Bounds.BoxSphereBounds.Origin, Bounds.BoxSphereBounds.SphereRadius, ForcedLODLevel, MeshScreenSizeSquared, CurFirstLODIdx, LODScale, true);

						LODIndex = LODToRender.GetRayTracedLOD();
					}

					if (!EnumHasAllFlags(Scene->PrimitiveRayTracingFlags[PrimitiveIndex], ERayTracingPrimitiveFlags::CacheInstances))
					{
						FRHIRayTracingGeometry* RayTracingGeometryInstance = SceneInfo->GetStaticRayTracingGeometryInstance(LODIndex);
						if (RayTracingGeometryInstance == nullptr)
						{
							continue;
						}

						// Sometimes LODIndex is out of range because it is clamped by ClampToFirstLOD, like the requested LOD is being streamed in and hasn't been available
						// According to InitViews, we should hide the static mesh instance
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
									const FRayTracingMeshCommand& RayTracingMeshCommand = Scene->CachedRayTracingMeshCommands[CommandIndex];

									RelevantPrimitive.InstanceMask |= RayTracingMeshCommand.InstanceMask;
									RelevantPrimitive.bAllSegmentsOpaque &= RayTracingMeshCommand.bOpaque;
									RelevantPrimitive.bAnySegmentsCastShadow |= RayTracingMeshCommand.bCastRayTracedShadows;
									RelevantPrimitive.bAnySegmentsDecal |= RayTracingMeshCommand.bDecal;
									RelevantPrimitive.bTwoSided |= RayTracingMeshCommand.bTwoSided;
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
				}
			},
			TStatId(), nullptr, ENamedThreads::AnyThread));
		}
	}

	//

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GatherRayTracingWorldInstances_DynamicElements);

		const bool bParallelMeshBatchSetup = GRayTracingParallelMeshBatchSetup && FApp::ShouldUseThreadingForPerformance();

		const int64 SharedBufferGenerationID = Scene->GetRayTracingDynamicGeometryCollection()->BeginUpdate();

		struct FRayTracingMeshBatchWorkItem
		{
			const FPrimitiveSceneProxy* SceneProxy = nullptr;
			TArray<FMeshBatch> MeshBatchesOwned;
			TArrayView<const FMeshBatch> MeshBatchesView;
			uint32 InstanceIndex = 0;

			TArrayView<const FMeshBatch> GetMeshBatches() const
			{
				if (MeshBatchesOwned.Num())
				{
					check(MeshBatchesView.Num() == 0);
					return TArrayView<const FMeshBatch>(MeshBatchesOwned);
				}
				else
				{
					check(MeshBatchesOwned.Num() == 0);
					return MeshBatchesView;
				}
			}
		};

		static constexpr uint32 MaxWorkItemsPerPage = 128; // Try to keep individual pages small to avoid slow-path memory allocations
		struct FRayTracingMeshBatchTaskPage
		{
			FRayTracingMeshBatchWorkItem WorkItems[MaxWorkItemsPerPage];
			uint32 NumWorkItems = 0;
			FRayTracingMeshBatchTaskPage* Next = nullptr;
		};

		FRayTracingMeshBatchTaskPage* MeshBatchTaskHead = nullptr;
		FRayTracingMeshBatchTaskPage* MeshBatchTaskPage = nullptr;
		uint32 NumPendingMeshBatches = 0;
		const uint32 RayTracingParallelMeshBatchSize = GRayTracingParallelMeshBatchSize;

		auto KickRayTracingMeshBatchTask = [&View, &MeshBatchTaskHead, &MeshBatchTaskPage, &NumPendingMeshBatches, Scene = this->Scene]()
		{
			if (MeshBatchTaskHead)
			{
				FDynamicRayTracingMeshCommandStorage* TaskDynamicCommandStorage = new(FMemStack::Get()) FDynamicRayTracingMeshCommandStorage;
				View.DynamicRayTracingMeshCommandStoragePerTask.Add(TaskDynamicCommandStorage);

				FRayTracingMeshCommandOneFrameArray* TaskVisibleCommands = new(FMemStack::Get()) FRayTracingMeshCommandOneFrameArray;
				TaskVisibleCommands->Reserve(NumPendingMeshBatches);
				View.VisibleRayTracingMeshCommandsPerTask.Add(TaskVisibleCommands);

				View.AddRayTracingMeshBatchTaskList.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
					[TaskDataHead = MeshBatchTaskHead, &View, Scene, TaskDynamicCommandStorage, TaskVisibleCommands]()
				{
					FTaskTagScope TaskTagScope(ETaskTag::EParallelRenderingThread);
					TRACE_CPUPROFILER_EVENT_SCOPE(RayTracingMeshBatchTask);
					FRayTracingMeshBatchTaskPage* Page = TaskDataHead;
					const int32 ExpectedMaxVisibieCommands = TaskVisibleCommands->Max();
					while (Page)
					{
						for (uint32 ItemIndex = 0; ItemIndex < Page->NumWorkItems; ++ItemIndex)
						{
							const FRayTracingMeshBatchWorkItem& WorkItem = Page->WorkItems[ItemIndex];
							TArrayView<const FMeshBatch> MeshBatches = WorkItem.GetMeshBatches();
							for (int32 SegmentIndex = 0; SegmentIndex < MeshBatches.Num(); SegmentIndex++)
							{
								const FMeshBatch& MeshBatch = MeshBatches[SegmentIndex];
								FDynamicRayTracingMeshCommandContext CommandContext(
									*TaskDynamicCommandStorage, *TaskVisibleCommands,
									SegmentIndex, WorkItem.InstanceIndex);
								FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
								FRayTracingMeshProcessor RayTracingMeshProcessor(&CommandContext, Scene, &View, PassDrawRenderState);
								RayTracingMeshProcessor.AddMeshBatch(MeshBatch, 1, WorkItem.SceneProxy);
							}
						}
						FRayTracingMeshBatchTaskPage* NextPage = Page->Next;
						Page->~FRayTracingMeshBatchTaskPage();
						Page = NextPage;
					}
					check(ExpectedMaxVisibieCommands <= TaskVisibleCommands->Max());
				}, TStatId(), nullptr, ENamedThreads::AnyThread));
			}

			MeshBatchTaskHead = nullptr;
			MeshBatchTaskPage = nullptr;
			NumPendingMeshBatches = 0;
		};

		// Local temporary array of instances used for GetDynamicRayTracingInstances()
		TArray<FRayTracingInstance> TempRayTracingInstances;

		for (const FRelevantPrimitive& RelevantPrimitive : RelevantPrimitives)
		{
			if (RelevantPrimitive.bStatic)
			{
				continue;
			}

			const int32 PrimitiveIndex = RelevantPrimitive.PrimitiveIndex;
			FPrimitiveSceneInfo* SceneInfo = Scene->Primitives[PrimitiveIndex];

			FPrimitiveSceneProxy* SceneProxy = Scene->PrimitiveSceneProxies[PrimitiveIndex];
			TempRayTracingInstances.Reset();
			MaterialGatheringContext.DynamicRayTracingGeometriesToUpdate.Reset();

			SceneProxy->GetDynamicRayTracingInstances(MaterialGatheringContext, TempRayTracingInstances);

			for (auto DynamicRayTracingGeometryUpdate : MaterialGatheringContext.DynamicRayTracingGeometriesToUpdate)
			{
				Scene->GetRayTracingDynamicGeometryCollection()->AddDynamicMeshBatchForGeometryUpdate(
					Scene,
					&View,
					SceneProxy,
					DynamicRayTracingGeometryUpdate,
					PrimitiveIndex
				);
			}

			if (TempRayTracingInstances.Num() > 0)
			{
				for (FRayTracingInstance& Instance : TempRayTracingInstances)
				{
					const FRayTracingGeometry* Geometry = Instance.Geometry;

					if (!ensureMsgf(Geometry->DynamicGeometrySharedBufferGenerationID == FRayTracingGeometry::NonSharedVertexBuffers
						|| Geometry->DynamicGeometrySharedBufferGenerationID == SharedBufferGenerationID,
						TEXT("GenerationID %lld, but expected to be %lld or %lld. Geometry debug name: '%s'. ")
						TEXT("When shared vertex buffers are used, the contents is expected to be written every frame. ")
						TEXT("Possibly AddDynamicMeshBatchForGeometryUpdate() was not called for this geometry."),
						Geometry->DynamicGeometrySharedBufferGenerationID, SharedBufferGenerationID, FRayTracingGeometry::NonSharedVertexBuffers,
						*Geometry->Initializer.DebugName.ToString()))
					{
						continue;
					}

					// If geometry still has pending build request then add to list which requires a force build
					if (Geometry->HasPendingBuildRequest())
					{
						RayTracingScene.GeometriesToBuild.Add(Geometry);
					}

					// Thin geometries like hair don't have material, as they only support shadow at the moment.
					if (!ensureMsgf(Instance.GetMaterials().Num() == Geometry->Initializer.Segments.Num() ||
						(Geometry->Initializer.Segments.Num() == 0 && Instance.GetMaterials().Num() == 1) ||
						(Instance.GetMaterials().Num() == 0 && (Instance.Mask & RAY_TRACING_MASK_THIN_SHADOW) > 0),
						TEXT("Ray tracing material assignment validation failed for geometry '%s'. "
							"Instance.GetMaterials().Num() = %d, Geometry->Initializer.Segments.Num() = %d, Instance.Mask = 0x%X."),
						*Geometry->Initializer.DebugName.ToString(), Instance.GetMaterials().Num(),
						Geometry->Initializer.Segments.Num(), Instance.Mask))
					{
						continue;
					}

					const uint32 InstanceIndex = RayTracingScene.Instances.Num();

					FRayTracingGeometryInstance& RayTracingInstance = RayTracingScene.Instances.AddDefaulted_GetRef();
					RayTracingInstance.GeometryRHI = Geometry->RayTracingGeometryRHI;
					RayTracingInstance.DefaultUserData = PrimitiveIndex;
					RayTracingInstance.Mask = Instance.Mask;
					if (Instance.bForceOpaque)
					{
						RayTracingInstance.Flags |= ERayTracingInstanceFlags::ForceOpaque;
					}
					if (Instance.bDoubleSided)
					{
						RayTracingInstance.Flags |= ERayTracingInstanceFlags::TriangleCullDisable;
					}
					AddDebugRayTracingInstanceFlags(RayTracingInstance.Flags);

					if (Instance.InstanceGPUTransformsSRV.IsValid())
					{
						RayTracingInstance.NumTransforms = Instance.NumTransforms;
						RayTracingInstance.GPUTransformsSRV = Instance.InstanceGPUTransformsSRV;
					}
					else 
					{
						if (Instance.OwnsTransforms())
						{
							// Slow path: copy transforms to the owned storage
							checkf(Instance.InstanceTransformsView.Num() == 0, TEXT("InstanceTransformsView is expected to be empty if using InstanceTransforms"));
							TArrayView<FMatrix> SceneOwnedTransforms = RayTracingScene.Allocate<FMatrix>(Instance.InstanceTransforms.Num());
							FMemory::Memcpy(SceneOwnedTransforms.GetData(), Instance.InstanceTransforms.GetData(), Instance.InstanceTransforms.Num() * sizeof(RayTracingInstance.Transforms[0]));
							static_assert(TIsSame<decltype(SceneOwnedTransforms[0]), decltype(Instance.InstanceTransforms[0])>::Value, "Unexpected transform type");

							RayTracingInstance.NumTransforms = SceneOwnedTransforms.Num();
							RayTracingInstance.Transforms = SceneOwnedTransforms;
						}
						else
						{
							// Fast path: just reference persistently-allocated transforms and avoid a copy
							checkf(Instance.InstanceTransforms.Num() == 0, TEXT("InstanceTransforms is expected to be empty if using InstanceTransformsView"));
							RayTracingInstance.NumTransforms = Instance.InstanceTransformsView.Num();
							RayTracingInstance.Transforms = Instance.InstanceTransformsView;
						}
					}

					if (bParallelMeshBatchSetup)
					{
						if (NumPendingMeshBatches >= RayTracingParallelMeshBatchSize)
						{
							KickRayTracingMeshBatchTask();
						}

						if (MeshBatchTaskPage == nullptr || MeshBatchTaskPage->NumWorkItems == MaxWorkItemsPerPage)
						{
							FRayTracingMeshBatchTaskPage* NextPage = new(FMemStack::Get()) FRayTracingMeshBatchTaskPage;
							if (MeshBatchTaskHead == nullptr)
							{
								MeshBatchTaskHead = NextPage;
							}
							if (MeshBatchTaskPage)
							{
								MeshBatchTaskPage->Next = NextPage;
							}
							MeshBatchTaskPage = NextPage;
						}

						FRayTracingMeshBatchWorkItem& WorkItem = MeshBatchTaskPage->WorkItems[MeshBatchTaskPage->NumWorkItems];
						MeshBatchTaskPage->NumWorkItems++;

						NumPendingMeshBatches += Instance.GetMaterials().Num();

						if (Instance.OwnsMaterials())
						{
							Swap(WorkItem.MeshBatchesOwned, Instance.Materials);
						}
						else
						{
							WorkItem.MeshBatchesView = Instance.MaterialsView;
						}

						WorkItem.SceneProxy = SceneProxy;
						WorkItem.InstanceIndex = InstanceIndex;
					}
					else
					{
						TArrayView<const FMeshBatch> InstanceMaterials = Instance.GetMaterials();
						for (int32 SegmentIndex = 0; SegmentIndex < InstanceMaterials.Num(); SegmentIndex++)
						{
							const FMeshBatch& MeshBatch = InstanceMaterials[SegmentIndex];
							FDynamicRayTracingMeshCommandContext CommandContext(View.DynamicRayTracingMeshCommandStorage, View.VisibleRayTracingMeshCommands, SegmentIndex, InstanceIndex);
							FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
							FRayTracingMeshProcessor RayTracingMeshProcessor(&CommandContext, Scene, &View, PassDrawRenderState);
							RayTracingMeshProcessor.AddMeshBatch(MeshBatch, 1, SceneProxy);
						}
					}
				}

				if (CVarRayTracingDynamicGeometryLastRenderTimeUpdateDistance.GetValueOnRenderThread() > 0.0f)
				{
					if (FVector::Distance(SceneProxy->GetActorPosition(), View.ViewMatrices.GetViewOrigin()) < CVarRayTracingDynamicGeometryLastRenderTimeUpdateDistance.GetValueOnRenderThread())
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

		KickRayTracingMeshBatchTask();
	}

	//

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GatherRayTracingWorldInstances_AddInstances);

		const bool bAutoInstance = CVarRayTracingAutoInstance.GetValueOnRenderThread() != 0;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WaitForLODTasks);
			FTaskGraphInterface::Get().WaitUntilTasksComplete(LODTaskList, ENamedThreads::GetRenderThread_Local());
		}

		struct FAutoInstanceBatch
		{
			int32 Index = INDEX_NONE;

			// Copies the next transform and user data into the current batch, returns true if arrays were re-allocated.
			bool Add(FRayTracingScene& RayTracingScene, const FMatrix& InTransform, uint32 InUserData)
			{
				// Adhoc TArray-like resize behavior, in lieu of support for using a custom FMemStackBase in TArray.
				// Idea for future: if batch becomes large enough, we could actually split it into multiple instances to avoid memory waste.

				const bool bNeedReallocation = Cursor == Transforms.Num();

				if (bNeedReallocation)
				{
					int32 PrevCount = Transforms.Num();
					int32 NextCount = FMath::Max(PrevCount * 2, 1);

					TArrayView<FMatrix> NewTransforms = RayTracingScene.Allocate<FMatrix>(NextCount);
					if (PrevCount)
					{
						FMemory::Memcpy(NewTransforms.GetData(), Transforms.GetData(), Transforms.GetTypeSize() * Transforms.Num());
					}
					Transforms = NewTransforms;

					TArrayView<uint32> NewUserData = RayTracingScene.Allocate<uint32>(NextCount);
					if (PrevCount)
					{
						FMemory::Memcpy(NewUserData.GetData(), UserData.GetData(), UserData.GetTypeSize() * UserData.Num());
					}
					UserData = NewUserData;
				}

				Transforms[Cursor] = InTransform;
				UserData[Cursor] = InUserData;

				++Cursor;

				return bNeedReallocation;
			}

			bool IsValid() const
			{
				return Transforms.Num() != 0;
			}

			TArrayView<FMatrix> Transforms;
			TArrayView<uint32> UserData;
			uint32 Cursor = 0;
		};

		Experimental::TSherwoodMap<uint64, FAutoInstanceBatch> InstanceBatches;

		InstanceBatches.Reserve(RelevantPrimitives.Num());

		// scan relevant primitives computing hash data to look for duplicate instances
		for (const FRelevantPrimitive& RelevantPrimitive : RelevantPrimitives)
		{
			const int32 PrimitiveIndex = RelevantPrimitive.PrimitiveIndex;
			const FPrimitiveSceneInfo* SceneInfo = Scene->Primitives[PrimitiveIndex];
			ERayTracingPrimitiveFlags Flags = Scene->PrimitiveRayTracingFlags[PrimitiveIndex];

			if (EnumHasAnyFlags(Flags, ERayTracingPrimitiveFlags::CacheInstances))
			{
				// TODO: support GRayTracingExcludeDecals, but not in the form of RayTracingMeshCommand.bDecal as that requires looping over all cached MDCs
				// Instead, either make r.RayTracing.ExcludeDecals read only or request a recache of all ray tracing commands during which decals are excluded

				const int32 NewInstanceIndex = RayTracingScene.Instances.Num();

				// At the moment we only support SM & ISMs on this path
				check(EnumHasAnyFlags(Flags, ERayTracingPrimitiveFlags::CacheMeshCommands));
				for (int32 CommandIndex : SceneInfo->CachedRayTracingMeshCommandIndicesPerLOD[0])
				{
					FVisibleRayTracingMeshCommand NewVisibleMeshCommand;

					NewVisibleMeshCommand.RayTracingMeshCommand = &Scene->CachedRayTracingMeshCommands[CommandIndex];
					NewVisibleMeshCommand.InstanceIndex = NewInstanceIndex;
					View.VisibleRayTracingMeshCommands.Add(NewVisibleMeshCommand);
				}

				RayTracingScene.Instances.Add(SceneInfo->CachedRayTracingInstance);
				AddDebugRayTracingInstanceFlags(RayTracingScene.Instances.Last().Flags);
			}
			else
			{
				const int8 LODIndex = RelevantPrimitive.LODIndex;

				if (LODIndex < 0 || !RelevantPrimitive.bStatic)
				{
					continue; // skip dynamic primitives and other 
				}

				if (GRayTracingExcludeDecals && RelevantPrimitive.bAnySegmentsDecal)
				{
					continue;
				}

				// location if this is a new entry
				const int32 NewInstanceIndex = RayTracingScene.Instances.Num();
				const uint64 InstanceKey = RelevantPrimitive.InstancingKey();

				FAutoInstanceBatch DummyInstanceBatch = { NewInstanceIndex };
				FAutoInstanceBatch& InstanceBatch = bAutoInstance ? InstanceBatches.FindOrAdd(InstanceKey, DummyInstanceBatch) : DummyInstanceBatch;

				if (InstanceBatch.Index != NewInstanceIndex)
				{
					// Reusing a previous entry, just append to the instance list.

					FRayTracingGeometryInstance& RayTracingInstance = RayTracingScene.Instances[InstanceBatch.Index];
					bool bReallocated = InstanceBatch.Add(RayTracingScene, Scene->PrimitiveTransforms[PrimitiveIndex], (uint32)PrimitiveIndex);

					++RayTracingInstance.NumTransforms;
					check(RayTracingInstance.NumTransforms == InstanceBatch.Cursor); // sanity check

					if (bReallocated)
					{
						RayTracingInstance.Transforms = InstanceBatch.Transforms;
						RayTracingInstance.UserData = InstanceBatch.UserData;
					}
				}
				else
				{
					// Starting new instance batch

					for (int32 CommandIndex : RelevantPrimitive.CachedRayTracingMeshCommandIndices)
					{
						if (CommandIndex >= 0)
						{
							FVisibleRayTracingMeshCommand NewVisibleMeshCommand;

							NewVisibleMeshCommand.RayTracingMeshCommand = &Scene->CachedRayTracingMeshCommands[CommandIndex];
							NewVisibleMeshCommand.InstanceIndex = NewInstanceIndex;
							View.VisibleRayTracingMeshCommands.Add(NewVisibleMeshCommand);
						}
						else
						{
							// CommandIndex == -1 indicates that the mesh batch has been filtered by FRayTracingMeshProcessor (like the shadow depth pass batch)
							// Do nothing in this case
						}
					}

					FRayTracingGeometryInstance& RayTracingInstance = RayTracingScene.Instances.AddDefaulted_GetRef();

					RayTracingInstance.GeometryRHI = RelevantPrimitive.RayTracingGeometryRHI;

					InstanceBatch.Add(RayTracingScene, Scene->PrimitiveTransforms[PrimitiveIndex], (uint32)PrimitiveIndex);
					RayTracingInstance.Transforms = InstanceBatch.Transforms;
					RayTracingInstance.UserData = InstanceBatch.UserData;
					RayTracingInstance.NumTransforms = 1;

					RayTracingInstance.Mask = RelevantPrimitive.InstanceMask; // When no cached command is found, InstanceMask == 0 and the instance is effectively filtered out

					if (RelevantPrimitive.bAllSegmentsOpaque)
					{
						RayTracingInstance.Flags |= ERayTracingInstanceFlags::ForceOpaque;
					}
					if (RelevantPrimitive.bTwoSided)
					{
						RayTracingInstance.Flags |= ERayTracingInstanceFlags::TriangleCullDisable;
					}
					AddDebugRayTracingInstanceFlags(RayTracingInstance.Flags);
				}
			}
		}
	}

	return true;
}

static void DeduplicateRayGenerationShaders(TArray< FRHIRayTracingShader*>& RayGenShaders)
{
	TSet<FRHIRayTracingShader*> UniqueRayGenShaders;
	for (FRHIRayTracingShader* Shader : RayGenShaders)
	{
		UniqueRayGenShaders.Add(Shader);
	}
	RayGenShaders = UniqueRayGenShaders.Array();
}

BEGIN_SHADER_PARAMETER_STRUCT(FBuildAccelerationStructurePassParams, )
RDG_BUFFER_ACCESS(RayTracingSceneScratchBuffer, ERHIAccess::UAVCompute)
END_SHADER_PARAMETER_STRUCT()

bool FDeferredShadingSceneRenderer::SetupRayTracingPipelineStates(FRHICommandListImmediate& RHICmdList)
{
	if (!IsRayTracingEnabled() || Views.Num() == 0)
	{
		return false;
	}

	bool bAnyRayTracingPassEnabled = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		bAnyRayTracingPassEnabled |= AnyRayTracingPassEnabled(Scene, Views[ViewIndex]);
	}

	if (!bAnyRayTracingPassEnabled)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::SetupRayTracingPipelineStates);

	const int32 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];

	if (ReferenceView.AddRayTracingMeshBatchTaskList.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_WaitRayTracingAddMesh);

		FTaskGraphInterface::Get().WaitUntilTasksComplete(ReferenceView.AddRayTracingMeshBatchTaskList, ENamedThreads::GetRenderThread_Local());

		for (int32 TaskIndex = 0; TaskIndex < ReferenceView.AddRayTracingMeshBatchTaskList.Num(); TaskIndex++)
		{
			ReferenceView.VisibleRayTracingMeshCommands.Append(*ReferenceView.VisibleRayTracingMeshCommandsPerTask[TaskIndex]);
		}

		ReferenceView.AddRayTracingMeshBatchTaskList.Empty();
	}

	// #dxr_todo: UE-72565: refactor ray tracing effects to not be member functions of DeferredShadingRenderer. register each effect at startup and just loop over them automatically to gather all required shaders
	TArray<FRHIRayTracingShader*> RayGenShaders; // TODO: inline allocator here?

	if (ReferenceView.RayTracingRenderMode == ERayTracingRenderMode::PathTracing)
	{
		// this view only needs the path tracing raygen shaders as all other
		// passes should be disabled
		PreparePathTracing(ReferenceView, RayGenShaders);
	}
	else
	{
		// path tracing is disabled, get all other possible raygen shaders
		PrepareRayTracingReflections(ReferenceView, *Scene, RayGenShaders);
		PrepareSingleLayerWaterRayTracingReflections(ReferenceView, *Scene, RayGenShaders);
		PrepareRayTracingShadows(ReferenceView, RayGenShaders);
		PrepareRayTracingAmbientOcclusion(ReferenceView, RayGenShaders);
		PrepareRayTracingSkyLight(ReferenceView, RayGenShaders);
		PrepareRayTracingGlobalIllumination(ReferenceView, RayGenShaders);
		PrepareRayTracingTranslucency(ReferenceView, RayGenShaders);
		PrepareRayTracingDebug(ReferenceView, RayGenShaders);

		PrepareRayTracingLumenDirectLighting(ReferenceView, *Scene, RayGenShaders);
		PrepareLumenHardwareRayTracingScreenProbeGather(ReferenceView, RayGenShaders);
		PrepareLumenHardwareRayTracingRadianceCache(ReferenceView, RayGenShaders);
		PrepareLumenHardwareRayTracingReflections(ReferenceView, RayGenShaders);
		PrepareLumenHardwareRayTracingVisualize(ReferenceView, RayGenShaders);
	}

	if (RayGenShaders.Num())
	{
		ReferenceView.RayTracingMaterialPipeline = BindRayTracingMaterialPipeline(RHICmdList, ReferenceView, RayGenShaders);
	}

	// Initialize common resources used for lighting in ray tracing effects

	ReferenceView.RayTracingSubSurfaceProfileTexture = GetSubsufaceProfileTexture_RT(RHICmdList);
	if (!ReferenceView.RayTracingSubSurfaceProfileTexture)
	{
		ReferenceView.RayTracingSubSurfaceProfileTexture = GSystemTextures.BlackDummy;
	}

	ReferenceView.RayTracingSubSurfaceProfileSRV = RHICreateShaderResourceView(ReferenceView.RayTracingSubSurfaceProfileTexture->GetRenderTargetItem().ShaderResourceTexture, 0);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];

		View.RayTracingLightData = CreateRayTracingLightData(RHICmdList,
			Scene->Lights, View, EUniformBufferUsage::UniformBuffer_SingleFrame);

		// Send common ray tracing resources from reference view to all others.
		if (ViewIndex != ReferenceViewIndex)
		{
			View.RayTracingSubSurfaceProfileTexture = ReferenceView.RayTracingSubSurfaceProfileTexture;
			View.RayTracingSubSurfaceProfileSRV = ReferenceView.RayTracingSubSurfaceProfileSRV;
			View.RayTracingLightData = ReferenceView.RayTracingLightData;
			View.RayTracingMaterialPipeline = ReferenceView.RayTracingMaterialPipeline;
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
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		bAnyRayTracingPassEnabled |= AnyRayTracingPassEnabled(Scene, Views[ViewIndex]);
	}

	if (!bAnyRayTracingPassEnabled)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::DispatchRayTracingWorldUpdates);

	// Make sure there are no pending skin cache builds and updates anymore:
	// FSkeletalMeshObjectGPUSkin::UpdateDynamicData_RenderThread could have enqueued build operations which might not have
	// been processed by CommitRayTracingGeometryUpdates. All pending builds should be done before adding them to the 
	// top level BVH
	if (FGPUSkinCache* GPUSkinCache = Scene->GetGPUSkinCache())
	{
		AddPass(GraphBuilder, [this](FRHICommandListImmediate& RHICmdList)
			{
				Scene->GetGPUSkinCache()->CommitRayTracingGeometryUpdates(RHICmdList);
			});
	}

	GRayTracingGeometryManager.ProcessBuildRequests(GraphBuilder.RHICmdList);

	const int32 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];
	FRayTracingScene& RayTracingScene = Scene->RayTracingScene;

	if (RayTracingScene.GeometriesToBuild.Num() > 0)
	{
		// Force update all the collected geometries (use stack allocator?)
		GRayTracingGeometryManager.ForceBuildIfPending(GraphBuilder.RHICmdList, RayTracingScene.GeometriesToBuild);
	}

	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

	RayTracingScene.BeginCreate(GraphBuilder);

	const bool bRayTracingAsyncBuild = CVarRayTracingAsyncBuild.GetValueOnRenderThread() != 0;

	if (bRayTracingAsyncBuild && GRHISupportsRayTracingAsyncBuildAccelerationStructure)
	{
		AddPass(GraphBuilder, [this](FRHICommandList& RHICmdList)
		{
			check(RayTracingDynamicGeometryUpdateEndTransition == nullptr);
			const FRHITransition* RayTracingDynamicGeometryUpdateBeginTransition = RHICreateTransition(FRHITransitionCreateInfo(ERHIPipeline::Graphics, ERHIPipeline::AsyncCompute));
			RayTracingDynamicGeometryUpdateEndTransition = RHICreateTransition(FRHITransitionCreateInfo(ERHIPipeline::AsyncCompute, ERHIPipeline::Graphics));

			FRHIAsyncComputeCommandListImmediate& RHIAsyncCmdList = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();

			RHICmdList.BeginTransition(RayTracingDynamicGeometryUpdateBeginTransition);
			RHIAsyncCmdList.EndTransition(RayTracingDynamicGeometryUpdateBeginTransition);

			Scene->GetRayTracingDynamicGeometryCollection()->DispatchUpdates(RHIAsyncCmdList);

			FRHIRayTracingScene* RayTracingSceneRHI = Scene->RayTracingScene.GetRHIRayTracingSceneChecked();

			RHIAsyncCmdList.BindAccelerationStructureMemory(RayTracingSceneRHI, Scene->RayTracingScene.GetBufferChecked(), 0);

			{
				SCOPED_DRAW_EVENT(RHIAsyncCmdList, RayTracingScene);
				RHIAsyncCmdList.BuildAccelerationStructure(RayTracingSceneRHI);				
			}

			RHIAsyncCmdList.BeginTransition(RayTracingDynamicGeometryUpdateEndTransition);
			FRHIAsyncComputeCommandListImmediate::ImmediateDispatch(RHIAsyncCmdList);
		});
	}
	else
	{
		{
			RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingGeometry);
			AddPass(GraphBuilder, RDG_EVENT_NAME("RayTracingGeometry"), [this](FRHICommandListImmediate& RHICmdList)
			{
				Scene->GetRayTracingDynamicGeometryCollection()->DispatchUpdates(RHICmdList);
			});
		}

		{
			RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingScene);

			FBuildAccelerationStructurePassParams* PassParams = GraphBuilder.AllocParameters<FBuildAccelerationStructurePassParams>();
			PassParams->RayTracingSceneScratchBuffer = Scene->RayTracingScene.BuildScratchBuffer;

			GraphBuilder.AddPass(RDG_EVENT_NAME("RayTracingScene"), PassParams, ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				[this, PassParams](FRHICommandList& RHICmdList)
			{
				FRHIRayTracingScene* RayTracingSceneRHI = Scene->RayTracingScene.GetRHIRayTracingSceneChecked();

				RHICmdList.BindAccelerationStructureMemory(RayTracingSceneRHI, Scene->RayTracingScene.GetBufferChecked(), 0);

				FRayTracingSceneBuildParams BuildParams;
				BuildParams.Scene = RayTracingSceneRHI;
				BuildParams.ScratchBuffer = PassParams->RayTracingSceneScratchBuffer->GetRHI();
				BuildParams.ScratchBufferOffset = 0;
				RHICmdList.BuildAccelerationStructure(BuildParams);

				// Submit potentially expensive BVH build commands to the GPU as soon as possible.
				// Avoids a GPU bubble in some CPU-limited cases.
				RHICmdList.SubmitCommandsHint();
			});
		}
	}

	AddPass(GraphBuilder, [this](FRHICommandListImmediate& RHICmdList)
	{
		Scene->GetRayTracingDynamicGeometryCollection()->EndUpdate(RHICmdList);
	});

	return true;
}

static void ReleaseRaytracingResources(FRDGBuilder& GraphBuilder, TArrayView<FViewInfo> Views, FRayTracingScene &RayTracingScene)
{
	AddPass(GraphBuilder, [Views, &RayTracingScene](FRHICommandListImmediate& RHICmdList)
	{
		if (RayTracingScene.IsCreated())
		{
			RHICmdList.ClearRayTracingBindings(RayTracingScene.GetRHIRayTracingScene());

			// If we did not end up rendering anything this frame, then release all ray tracing scene resources.
			if (RayTracingScene.Instances.Num() == 0)
			{
				RayTracingScene.ResetAndReleaseResources();
			}
		}

		// Release resources that were bound to the ray tracing scene to allow them to be immediately recycled.
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			FViewInfo& View = Views[ViewIndex];

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
	});
}

void FDeferredShadingSceneRenderer::WaitForRayTracingScene(FRDGBuilder& GraphBuilder)
{
	bool bAnyRayTracingPassEnabled = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		bAnyRayTracingPassEnabled |= AnyRayTracingPassEnabled(Scene, Views[ViewIndex]);
	}

	if (!bAnyRayTracingPassEnabled)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::WaitForRayTracingScene);

	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

	// Scratch buffer must be referenced in this pass, as it must live until the BVH build is complete.
	FBuildAccelerationStructurePassParams* PassParams = GraphBuilder.AllocParameters<FBuildAccelerationStructurePassParams>();
	PassParams->RayTracingSceneScratchBuffer = Scene->RayTracingScene.BuildScratchBuffer;

	SetupRayTracingPipelineStates(GraphBuilder.RHICmdList);

	GraphBuilder.AddPass(RDG_EVENT_NAME("WaitForRayTracingScene"), PassParams, ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		[this, PassParams](FRHICommandListImmediate& RHICmdList)
	{
		const int32 ReferenceViewIndex = 0;
		FViewInfo& ReferenceView = Views[ReferenceViewIndex];

		const bool bIsPathTracing = ReferenceView.RayTracingRenderMode == ERayTracingRenderMode::PathTracing;

		check(ReferenceView.RayTracingMaterialPipeline || ReferenceView.RayTracingMaterialBindings.Num() == 0);

		if (ReferenceView.RayTracingMaterialPipeline && ReferenceView.RayTracingMaterialBindings.Num())
		{
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(ReferenceView.RayTracingMaterialBindingsTask, ENamedThreads::GetRenderThread_Local());

			// Gather bindings from all chunks and submit them all as a single batch to allow RHI to bind all shader parameters in parallel.

			uint32 NumTotalBindings = 0;

			for (FRayTracingLocalShaderBindingWriter* BindingWriter : ReferenceView.RayTracingMaterialBindings)
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
			for (FRayTracingLocalShaderBindingWriter* BindingWriter : ReferenceView.RayTracingMaterialBindings)
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

			Scene->RayTracingScene.WaitForTasks();

			const bool bCopyDataToInlineStorage = false; // Storage is already allocated from RHICmdList, no extra copy necessary
			RHICmdList.SetRayTracingHitGroups(
				ReferenceView.GetRayTracingSceneChecked(),
				ReferenceView.RayTracingMaterialPipeline,
				NumTotalBindings, MergedBindings,
				bCopyDataToInlineStorage);

			if (!bIsPathTracing)
			{
				TArray<FRHIRayTracingShader*> DeferredMaterialRayGenShaders;
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
				{
					FViewInfo& View = Views[ViewIndex];
					PrepareRayTracingReflectionsDeferredMaterial(View, *Scene, DeferredMaterialRayGenShaders);
					PrepareRayTracingDeferredReflectionsDeferredMaterial(View, *Scene, DeferredMaterialRayGenShaders);
					PrepareRayTracingGlobalIlluminationDeferredMaterial(View, DeferredMaterialRayGenShaders);
					PrepareLumenHardwareRayTracingReflectionsDeferredMaterial(View, DeferredMaterialRayGenShaders);
					PrepareLumenHardwareRayTracingRadianceCacheDeferredMaterial(View, DeferredMaterialRayGenShaders);
					PrepareLumenHardwareRayTracingScreenProbeGatherDeferredMaterial(View, DeferredMaterialRayGenShaders);
					PrepareLumenHardwareRayTracingVisualizeDeferredMaterial(View, DeferredMaterialRayGenShaders);
				}
				DeduplicateRayGenerationShaders(DeferredMaterialRayGenShaders);

				if (DeferredMaterialRayGenShaders.Num())
				{
					ReferenceView.RayTracingMaterialGatherPipeline = BindRayTracingDeferredMaterialGatherPipeline(RHICmdList, ReferenceView, DeferredMaterialRayGenShaders);
				}

				// Add Lumen hardware ray tracing materials
				TArray<FRHIRayTracingShader*> LumenHardwareRayTracingRayGenShaders;
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
				{
					FViewInfo& View = Views[ViewIndex];
					PrepareLumenHardwareRayTracingVisualizeLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
					PrepareLumenHardwareRayTracingRadianceCacheLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
					PrepareLumenHardwareRayTracingReflectionsLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
					PrepareLumenHardwareRayTracingScreenProbeGatherLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
				}
				DeduplicateRayGenerationShaders(DeferredMaterialRayGenShaders);

				if (LumenHardwareRayTracingRayGenShaders.Num())
				{
					ReferenceView.LumenHardwareRayTracingMaterialPipeline = BindLumenHardwareRayTracingMaterialPipeline(RHICmdList, ReferenceView, LumenHardwareRayTracingRayGenShaders);
				}
			}

			// Move the ray tracing binding container ownership to the command list, so that memory will be
			// released on the RHI thread timeline, after the commands that reference it are processed.
			RHICmdList.EnqueueLambda([Ptrs = MoveTemp(ReferenceView.RayTracingMaterialBindings)](FRHICommandListImmediate&)
			{
				for (auto Ptr : Ptrs)
				{
					delete Ptr;
				}
			});

			// Send ray tracing resources from reference view to all others.
			for (int32 ViewIndex = 1; ViewIndex < Views.Num(); ++ViewIndex)
			{
				FViewInfo& View = Views[ViewIndex];
				View.RayTracingMaterialGatherPipeline = ReferenceView.RayTracingMaterialGatherPipeline;
				View.LumenHardwareRayTracingMaterialPipeline = ReferenceView.LumenHardwareRayTracingMaterialPipeline;
			}

			if (!bIsPathTracing)
			{
				SetupRayTracingLightingMissShader(RHICmdList, ReferenceView);
			}
		}

		if (RayTracingDynamicGeometryUpdateEndTransition)
		{
			RHICmdList.EndTransition(RayTracingDynamicGeometryUpdateEndTransition);
			RayTracingDynamicGeometryUpdateEndTransition = nullptr;
		}

		FRHIRayTracingScene* RayTracingScene = ReferenceView.GetRayTracingSceneChecked();
		RHICmdList.Transition(FRHITransitionInfo(RayTracingScene, ERHIAccess::BVHWrite, ERHIAccess::BVHRead));
	});
}

enum class ERayTracingWorldUpdatesDispatchPoint
{
	BeforeLumenSceneLighting,
	OverlapWithBasePass
};

ERayTracingWorldUpdatesDispatchPoint GetRayTracingWorldUpdatesDispatchPoint(bool bOcclusionBeforeBasePass, bool bLumenUseHardwareRayTracedShadows)
{
	if (bOcclusionBeforeBasePass && bLumenUseHardwareRayTracedShadows)
	{
		return ERayTracingWorldUpdatesDispatchPoint::BeforeLumenSceneLighting;
	}

	return ERayTracingWorldUpdatesDispatchPoint::OverlapWithBasePass;
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
			const bool bHasSSGI = ViewPipelineState[&FPerViewPipelineState::DiffuseIndirectMethod] == EDiffuseIndirectMethod::SSGI;
			const bool bUseLumen = ViewPipelineState[&FPerViewPipelineState::DiffuseIndirectMethod] == EDiffuseIndirectMethod::Lumen 
				|| ViewPipelineState[&FPerViewPipelineState::ReflectionsMethod] == EReflectionsMethod::Lumen;

			// Requires FurthestHZB
			ViewPipelineState.Set(&FPerViewPipelineState::bFurthestHZB,
				FamilyPipelineState[&FFamilyPipelineState::bHZBOcclusion] ||
				FamilyPipelineState[&FFamilyPipelineState::bNanite] ||
				ViewPipelineState[&FPerViewPipelineState::bUseLumenProbeHierarchy] ||
				ViewPipelineState[&FPerViewPipelineState::AmbientOcclusionMethod] == EAmbientOcclusionMethod::SSAO ||
				ViewPipelineState[&FPerViewPipelineState::ReflectionsMethod] == EReflectionsMethod::SSR ||
				bHasSSGI || bUseLumen);

			ViewPipelineState.Set(&FPerViewPipelineState::bClosestHZB, 
				bHasSSGI || bUseLumen);
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

void FDeferredShadingSceneRenderer::Render(FRDGBuilder& GraphBuilder)
{
	const bool bNaniteEnabled = UseNanite(ShaderPlatform) && ViewFamily.EngineShowFlags.NaniteMeshes;

	Scene->UpdateAllPrimitiveSceneInfos(GraphBuilder, true);

	FGPUSceneScopeBeginEndHelper GPUSceneScopeBeginEndHelper(Scene->GPUScene, GPUSceneDynamicContext, Scene);

	bool bVisualizeNanite = false;
	if (bNaniteEnabled)
	{
		Nanite::GGlobalResources.Update(GraphBuilder); // Needed to managed scratch buffers for Nanite.
		Nanite::GStreamingManager.BeginAsyncUpdate(GraphBuilder);

		FNaniteVisualizationData& NaniteVisualization = GetNaniteVisualizationData();
		if (Views.Num() > 0)
		{
			const FName& NaniteViewMode = Views[0].CurrentNaniteVisualizationMode;
			if (NaniteVisualization.Update(NaniteViewMode))
			{
				// When activating the view modes from the command line, automatically enable the VisualizeNanite show flag for convenience.
				ViewFamily.EngineShowFlags.SetVisualizeNanite(true);
			}
			bVisualizeNanite = NaniteVisualization.IsActive() && ViewFamily.EngineShowFlags.VisualizeNanite;
		}
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
	const FRHIGPUMask RenderTargetGPUMask = ComputeGPUMasks(GraphBuilder.RHICmdList);
#endif // WITH_MGPU

	// By default, limit our GPU usage to only GPUs specified in the view masks.
	RDG_GPU_MASK_SCOPE(GraphBuilder, AllViewsGPUMask);

	WaitOcclusionTests(GraphBuilder.RHICmdList);

	if (!ViewFamily.EngineShowFlags.Rendering)
	{
		return;
	}

	RDG_RHI_EVENT_SCOPE(GraphBuilder, Scene);
	RDG_RHI_GPU_STAT_SCOPE(GraphBuilder, Unaccounted);
	
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Render_Init);
		RDG_RHI_GPU_STAT_SCOPE(GraphBuilder, AllocateRendertargets);

		// Initialize global system textures (pass-through if already initialized).
		GSystemTextures.InitializeTextures(GraphBuilder.RHICmdList, FeatureLevel);
	}

	const FSceneTexturesConfig SceneTexturesConfig = FSceneTexturesConfig::Create(ViewFamily);
	FSceneTexturesConfig::Set(SceneTexturesConfig);

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Create(GraphBuilder);

	const bool bAllowStaticLighting = IsStaticLightingAllowed();

	const bool bUseVirtualTexturing = UseVirtualTexturing(FeatureLevel);
	if (bUseVirtualTexturing)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);
		// AllocateResources needs to be called before RHIBeginScene
		FVirtualTextureSystem::Get().AllocateResources(GraphBuilder, FeatureLevel);
		FVirtualTextureSystem::Get().CallPendingCallbacks();
		VirtualTextureFeedbackBegin(GraphBuilder, Views, SceneTexturesConfig.Extent);
	}

	// Important that this uses consistent logic throughout the frame, so evaluate once and pass in the flag from here
	// NOTE: Must be done after  system texture initialization
	VirtualShadowMapArray.Initialize(GraphBuilder, Scene->VirtualShadowMapArrayCacheManager, UseVirtualShadowMaps(ShaderPlatform, FeatureLevel));

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
	GraphBuilder.RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

	FInstanceCullingManager InstanceCullingManager(GInstanceCullingManagerResources, Scene->GPUScene.IsEnabled());

	bool bDoInitViewAftersPrepass = false;
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, VisibilityCommands);
		bDoInitViewAftersPrepass = InitViews(GraphBuilder, SceneTexturesConfig, BasePassDepthStencilAccess, ILCTaskData, InstanceCullingManager);
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

	FRayTracingScene& RayTracingScene = Scene->RayTracingScene;
	RayTracingScene.Reset(); // Resets the internal arrays, but does not release any resources.

	const int32 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];

	// Prepare the scene for rendering this frame.
	GatherRayTracingWorldInstancesForView(GraphBuilder.RHICmdList, ReferenceView, RayTracingScene);

	if (ReferenceView.RayTracingRenderMode != ERayTracingRenderMode::PathTracing)
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

		if (!ViewFamily.bIsRenderedImmediatelyAfterAnotherViewFamily && GDoPrepareDistanceFieldSceneAfterRHIFlush && (GRHINeedsExtraDeletionLatency || !GRHICommandList.Bypass()))
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

			Scene->GPUScene.UploadDynamicPrimitiveShaderDataForView(GraphBuilder, Scene, View);
		}

		{
			// GPUCULL_TODO: Possibly fold into unpack step
			InstanceCullingManager.CullInstances(GraphBuilder, Scene->GPUScene);
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

	FSceneTextures& SceneTextures = FSceneTextures::Create(GraphBuilder, SceneTexturesConfig); 

	// Note, should happen after the GPU-Scene update to ensure rendering to runtime virtual textures is using the correctly updated scene
	if (bUseVirtualTexturing)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);
		FVirtualTextureSystem::Get().Update(GraphBuilder, FeatureLevel, Scene);
	}

	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);
	bool bCanOverlayRayTracingOutput = CanOverlayRayTracingOutput(Views[0]);// #dxr_todo: UE-72557 multi-view case
	
	const bool bRenderDeferredLighting = ViewFamily.EngineShowFlags.Lighting
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& ViewFamily.EngineShowFlags.DeferredLighting
		&& bUseGBuffer
		&& bCanOverlayRayTracingOutput;

	bool bComputeLightGrid = false;
	bool bAnyLumenEnabled = false;
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

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			bAnyLumenEnabled = bAnyLumenEnabled 
				|| GetViewPipelineState(View).DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen
				|| GetViewPipelineState(View).ReflectionsMethod == EReflectionsMethod::Lumen;
		}

		bComputeLightGrid |= (
			ShouldRenderVolumetricFog() ||
			ViewFamily.ViewMode != VMI_Lit ||
			bAnyLumenEnabled ||
			VirtualShadowMapArray.IsEnabled());
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

	
	const bool bIsOcclusionTesting = DoOcclusionQueries() && (!ViewFamily.EngineShowFlags.Wireframe || bIsViewFrozen || bHasViewParent);
	const bool bNeedsPrePass = ShouldRenderPrePass();

	GEngine->GetPreRenderDelegateEx().Broadcast(GraphBuilder);

	// Dynamic vertex and index buffers need to be committed before rendering.
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
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_FXPreRender));
		FXSystem->PreRender(GraphBuilder, Views, true /*bAllowGPUParticleUpdate*/);
		if (FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager())
		{
			GPUSortManager->OnPreRender(GraphBuilder);
		}
	}

	AddPass(GraphBuilder, [this](FRHICommandList& InRHICmdList)
	{
		RunGPUSkinCacheTransition(InRHICmdList, Scene, EGPUSkinCacheTransition::Renderer);
	});

	FHairStrandsBookmarkParameters& HairStrandsBookmarkParameters = *GraphBuilder.AllocObject<FHairStrandsBookmarkParameters>();
	if (IsHairStrandsEnabled(EHairStrandsShaderType::All, Scene->GetShaderPlatform()))
	{
		HairStrandsBookmarkParameters = CreateHairStrandsBookmarkParameters(Scene, Views[0]);
		RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessTasks, HairStrandsBookmarkParameters);

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
				ComputeHairStrandsClustersCulling(GraphBuilder, *HairStrandsBookmarkParameters.ShaderMap, Views, CullingParams, HairStrandsBookmarkParameters.HairClusterData);
			}

			RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessStrandsInterpolation, HairStrandsBookmarkParameters);
		}
		else
		{
			for (FViewInfo& View : Views)
			{
				View.HairStrandsViewData.UniformBuffer = HairStrands::CreateDefaultHairStrandsViewUniformBuffer(GraphBuilder, View);
			}
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
	const bool bHairEnable = HairStrandsBookmarkParameters.bHasElements && Views.Num() > 0 && IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Views[0].GetShaderPlatform());

	{
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_PrePass));

		// Both compute approaches run earlier, so skip clearing stencil here, just load existing.
		const ERenderTargetLoadAction StencilLoadAction = DepthPass.IsComputeStencilDitherEnabled()
			? ERenderTargetLoadAction::ELoad
			: ERenderTargetLoadAction::EClear;

		const ERenderTargetLoadAction DepthLoadAction = ERenderTargetLoadAction::EClear;
		AddClearDepthStencilPass(GraphBuilder, SceneTextures.Depth.Target, DepthLoadAction, StencilLoadAction);

		// Draw the scene pre-pass / early z pass, populating the scene depth buffer and HiZ
		if (bNeedsPrePass)
		{
			RenderPrePass(GraphBuilder, SceneTextures.Depth.Target, InstanceCullingManager);
		}
		else
		{
			// We didn't do the prepass, but we still want the HMD mask if there is one
			RenderPrePassHMD(GraphBuilder, SceneTextures.Depth.Target);
		}

		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_AfterPrePass));
		AddServiceLocalQueuePass(GraphBuilder);

		// special pass for DDM_AllOpaqueNoVelocity, which uses the velocity pass to finish the early depth pass write
		if (bShouldRenderVelocities && Scene->EarlyZPassMode == DDM_AllOpaqueNoVelocity)
		{
			// Render the velocities of movable objects
			GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_Velocity));
			RenderVelocities(GraphBuilder, SceneTextures, EVelocityPass::Opaque, bHairEnable);
			GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_AfterVelocity));
			AddServiceLocalQueuePass(GraphBuilder);
		}

		if (bDoInitViewAftersPrepass)
		{
			{
				RDG_RHI_GPU_STAT_SCOPE(GraphBuilder, VisibilityCommands);
				InitViewsPossiblyAfterPrepass(GraphBuilder, ILCTaskData, InstanceCullingManager);
			}

			{
				RDG_RHI_GPU_STAT_SCOPE(GraphBuilder, GPUSceneUpdate);
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

	TArray<Nanite::FRasterResults, TInlineAllocator<2>> NaniteRasterResults;
	if (bNaniteEnabled && Views.Num() > 0)
	{
		LLM_SCOPE_BYTAG(Nanite);
		TRACE_CPUPROFILER_EVENT_SCOPE(InitNaniteRaster);

		NaniteRasterResults.AddDefaulted(Views.Num());

		RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteRaster);
		const FIntPoint RasterTextureSize = SceneTextures.Depth.Target->Desc.Extent;

		const FViewInfo& PrimaryViewRef = Views[0];
		const FIntRect PrimaryViewRect = PrimaryViewRef.ViewRect;
		
		// Primary raster view
		{
			Nanite::FRasterState RasterState;

			Nanite::FRasterContext RasterContext = Nanite::InitRasterContext(GraphBuilder, FeatureLevel, RasterTextureSize);

			const bool bTwoPassOcclusion = true;
			const bool bUpdateStreaming = true;
			const bool bSupportsMultiplePasses = false;
			const bool bForceHWRaster = RasterContext.RasterScheduling == Nanite::ERasterScheduling::HardwareOnly;
			const bool bPrimaryContext = true;
			const bool bDiscardNonMoving = ViewFamily.EngineShowFlags.DrawOnlyVSMInvalidatingGeo != 0;

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				const FViewInfo& View = Views[ViewIndex];

				Nanite::FCullingContext CullingContext = Nanite::InitCullingContext(
					GraphBuilder,
					*Scene,
					!bIsEarlyDepthComplete ? View.PrevViewInfo.NaniteHZB : View.PrevViewInfo.HZB,
					View.ViewRect,
					bTwoPassOcclusion,
					bUpdateStreaming,
					bSupportsMultiplePasses,
					bForceHWRaster,
					bPrimaryContext,
					bDiscardNonMoving
				);

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

				Nanite::FRasterResults& RasterResults = NaniteRasterResults[ViewIndex];

				if (bNeedsPrePass)
				{
					Nanite::EmitDepthTargets(
						GraphBuilder,
						*Scene,
						Views[ViewIndex],
						CullingContext.SOAStrides,
						CullingContext.VisibleClustersSWHW,
						CullingContext.ViewsBuffer,
						SceneTextures.Depth.Target,
						RasterContext.VisBuffer64,
						RasterResults.MaterialDepth,
						RasterResults.NaniteMask,
						RasterResults.VelocityBuffer,
						bNeedsPrePass
					);
				}

				if (!bIsEarlyDepthComplete && bTwoPassOcclusion && View.ViewState)
				{
					// Won't have a complete SceneDepth for post pass so can't use complete HZB for main pass or it will poke holes in the post pass HZB killing occlusion culling.
					RDG_EVENT_SCOPE(GraphBuilder, "Nanite::BuildHZB");

					FRDGTextureRef SceneDepth = SystemTextures.Black;
					FRDGTextureRef GraphHZB = nullptr;

					BuildHZBFurthest(
						GraphBuilder,
						SceneDepth,
						RasterContext.VisBuffer64,
						PrimaryViewRect,
						FeatureLevel,
						ShaderPlatform,
						TEXT("Nanite.HZB"),
						/* OutFurthestHZBTexture = */ &GraphHZB );
					
					GraphBuilder.QueueTextureExtraction( GraphHZB, &View.ViewState->PrevFrameViewInfo.NaniteHZB );
				}

				Nanite::ExtractResults(GraphBuilder, CullingContext, RasterContext, RasterResults);
			}
		}

		if (GNaniteShowStats != 0)
		{
			Nanite::PrintStats(GraphBuilder, PrimaryViewRef);
		}
	}

	SceneTextures.SetupMode = ESceneTextureSetupMode::SceneDepth;
	SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);

	AddResolveSceneDepthPass(GraphBuilder, Views, SceneTextures.Depth);

	// NOTE: The ordering of the lights is used to select sub-sets for different purposes, e.g., those that support clustered deferred.
	FSortedLightSetSceneInfo& SortedLightSet = *GraphBuilder.AllocObject<FSortedLightSetSceneInfo>();
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, SortLights);
		GatherLightsAndComputeLightGrid(GraphBuilder, bComputeLightGrid, SortedLightSet);
	}

	CSV_CUSTOM_STAT(LightCount, All,  float(SortedLightSet.SortedLights.Num()), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(LightCount, ShadowOff, float(SortedLightSet.AttenuationLightStart), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(LightCount, ShadowOn, float(SortedLightSet.SortedLights.Num()) - float(SortedLightSet.AttenuationLightStart), ECsvCustomStatOp::Set);

	// Local helper function to perform virtual shadow map allocation, which can occur early, or late.
	const auto AllocateVirtualShadowMaps = [&](bool bPostBasePass)
	{
		if (VirtualShadowMapArray.IsEnabled())
		{
			ensureMsgf(AreLightsInLightGrid(), TEXT("Virtual shadow map setup requires local lights to be injected into the light grid (this may be caused by 'r.LightCulling.Quality=0')."));
			// ensure(ShadowMapSetupDone)
			VirtualShadowMapArray.BuildPageAllocations(GraphBuilder, SceneTextures, Views, SortedLightSet, VisibleLightInfos, NaniteRasterResults, bPostBasePass);
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
	const bool bOcclusionBeforeBasePass = !bNaniteEnabled && !bAnyLumenEnabled && !bHairEnable && ((DepthPass.EarlyZPassMode == EDepthDrawingMode::DDM_AllOccluders) || bIsEarlyDepthComplete);

#if RHI_RAYTRACING
	ERayTracingWorldUpdatesDispatchPoint RayTracingWorldUpdatesDispatchPoint = GetRayTracingWorldUpdatesDispatchPoint(bOcclusionBeforeBasePass, Lumen::UseHardwareRayTracedShadows(Views[0]));
#endif

	if (bOcclusionBeforeBasePass)
	{
		RenderOcclusionLambda();
	}

	AddServiceLocalQueuePass(GraphBuilder);
	// End early occlusion queries

	// Early Shadow depth rendering
	if (bCanOverlayRayTracingOutput && bOcclusionBeforeBasePass)
	{
		const bool bAfterBasePass = false;
		AllocateVirtualShadowMaps(bAfterBasePass);

		RenderShadowDepthMaps(GraphBuilder, InstanceCullingManager);
		AddServiceLocalQueuePass(GraphBuilder);
	}
	// End early Shadow depth rendering

	const bool bShouldRenderSkyAtmosphere = ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags);
	const bool bShouldRenderVolumetricCloudBase = ShouldRenderVolumetricCloud(Scene, ViewFamily.EngineShowFlags);
	const bool bShouldRenderVolumetricCloud = bShouldRenderVolumetricCloudBase && !ViewFamily.EngineShowFlags.VisualizeVolumetricCloudConservativeDensity;
	const bool bShouldVisualizeVolumetricCloud = bShouldRenderVolumetricCloudBase && !!ViewFamily.EngineShowFlags.VisualizeVolumetricCloudConservativeDensity;
	bool bAsyncComputeVolumetricCloud = IsVolumetricRenderTargetEnabled() && IsVolumetricRenderTargetAsyncCompute();
	bool bHasHalfResCheckerboardMinMaxDepth = false;
	bool bVolumetricRenderTargetRequired = bShouldRenderVolumetricCloud && bCanOverlayRayTracingOutput;

	if (bShouldRenderVolumetricCloudBase)
	{
		InitVolumetricRenderTargetForViews(GraphBuilder, Views);
	}

	InitVolumetricCloudsForViews(GraphBuilder, bShouldRenderVolumetricCloudBase, InstanceCullingManager);

	// Generate sky LUTs once all shadow map has been evaluated (for volumetric light shafts). Requires bOcclusionBeforeBasePass.
	// This also must happen before the BasePass for Sky material to be able to sample valid LUTs.
	if (bShouldRenderSkyAtmosphere)
	{
		// Generate the Sky/Atmosphere look up tables
		RenderSkyAtmosphereLookUpTables(GraphBuilder);
	}

	// Capture the SkyLight using the SkyAtmosphere and VolumetricCloud component if available.
	const bool bRealTimeSkyCaptureEnabled = Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled && Views.Num() > 0 && ViewFamily.EngineShowFlags.SkyLighting;
	if (bRealTimeSkyCaptureEnabled)
	{
		FViewInfo& MainView = Views[0];
		Scene->AllocateAndCaptureFrameSkyEnvMap(GraphBuilder, *this, MainView, bShouldRenderSkyAtmosphere, bShouldRenderVolumetricCloud, InstanceCullingManager);
	}

	// Strata initialisation is always run even when not enabled.
	const bool bStrataEnabled = Strata::IsStrataEnabled();
	Strata::InitialiseStrataFrameSceneData(*this, GraphBuilder);

	if (GetCustomDepthPassLocation() == ECustomDepthPassLocation::BeforeBasePass)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass_BeforeBasePass);
		if (RenderCustomDepthPass(GraphBuilder, SceneTextures.CustomDepth, SceneTextures.GetSceneTextureShaderParameters(FeatureLevel)))
		{
			SceneTextures.SetupMode |= ESceneTextureSetupMode::CustomDepth;
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);
			AddServiceLocalQueuePass(GraphBuilder);
		}
	}

	UpdateLumenScene(GraphBuilder);

	if (bOcclusionBeforeBasePass)
	{
#if RHI_RAYTRACING
		if (RayTracingWorldUpdatesDispatchPoint == ERayTracingWorldUpdatesDispatchPoint::BeforeLumenSceneLighting)
		{
			DispatchRayTracingWorldUpdates(GraphBuilder);
		}

		// Lumen scene lighting requires ray tracing scene to be ready if HWRT shadows are desired
		if (Lumen::UseHardwareRayTracedShadows(Views[0]))
		{
			WaitForRayTracingScene(GraphBuilder);
		}
#endif // RHI_RAYTRACING

		{
			LLM_SCOPE_BYTAG(Lumen);
			RenderLumenSceneLighting(GraphBuilder, Views[0]);
		}

		ComputeVolumetricFog(GraphBuilder, SceneTextures);
	}

	FRDGTextureRef HalfResolutionDepthCheckerboardMinMaxTexture = nullptr;

	// Kick off async compute cloud eraly if all depth has been written in the prepass
	if (bShouldRenderVolumetricCloud && bAsyncComputeVolumetricCloud && DepthPass.EarlyZPassMode == DDM_AllOpaque && bCanOverlayRayTracingOutput)
	{
		HalfResolutionDepthCheckerboardMinMaxTexture = CreateHalfResolutionDepthCheckerboardMinMax(GraphBuilder, Views, SceneTextures.Depth.Resolve);
		bHasHalfResCheckerboardMinMaxDepth = true;

		bool bSkipVolumetricRenderTarget = false;
		bool bSkipPerPixelTracing = true;
		bAsyncComputeVolumetricCloud = RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, HalfResolutionDepthCheckerboardMinMaxTexture, true, InstanceCullingManager);
	}
	
	FRDGTextureRef ForwardScreenSpaceShadowMaskTexture = nullptr;
	FRDGTextureRef ForwardScreenSpaceShadowMaskHairTexture = nullptr;
	if (IsForwardShadingEnabled(ShaderPlatform))
	{
		if (bHairEnable)
		{
			RenderHairPrePass(GraphBuilder, Scene, Views, InstanceCullingManager);
			RenderHairBasePass(GraphBuilder, Scene, SceneTextures, Views, InstanceCullingManager);
		}

		RenderForwardShadowProjections(GraphBuilder, SceneTextures, ForwardScreenSpaceShadowMaskTexture, ForwardScreenSpaceShadowMaskHairTexture);
	}

	FDBufferTextures DBufferTextures = CreateDBufferTextures(GraphBuilder, SceneTextures.Config.Extent, ShaderPlatform);

	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(DeferredShadingSceneRenderer_DBuffer);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_DBuffer);
		CompositionLighting::ProcessBeforeBasePass(GraphBuilder, Views, SceneTextures, DBufferTextures);
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

#if RHI_RAYTRACING
	if (RayTracingWorldUpdatesDispatchPoint == ERayTracingWorldUpdatesDispatchPoint::OverlapWithBasePass)
	{
		// Async AS builds can potentially overlap with BasePass
		DispatchRayTracingWorldUpdates(GraphBuilder);
	}
#endif

	{
		RenderBasePass(GraphBuilder, SceneTextures, DBufferTextures, BasePassDepthStencilAccess, ForwardScreenSpaceShadowMaskTexture, InstanceCullingManager);
		AddServiceLocalQueuePass(GraphBuilder);
		
		if (bNaniteEnabled && bShouldApplyNaniteMaterials)
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				const FViewInfo& View = Views[ViewIndex];
				Nanite::FRasterResults& RasterResults = NaniteRasterResults[ViewIndex];

				if (!bNeedsPrePass)
				{
					Nanite::EmitDepthTargets(
						GraphBuilder,
						*Scene,
						Views[ViewIndex],
						RasterResults.SOAStrides,
						RasterResults.VisibleClustersSWHW,
						RasterResults.ViewsBuffer,
						SceneTextures.Depth.Target,
						RasterResults.VisBuffer64,
						RasterResults.MaterialDepth,
						RasterResults.NaniteMask,
						RasterResults.VelocityBuffer,
						bNeedsPrePass
					);
				}

				Nanite::DrawBasePass(
					GraphBuilder,
					SceneTextures,
					DBufferTextures,
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

		if (bVisualizeNanite)
		{
			Nanite::AddVisualizationPasses(
				GraphBuilder,
				Scene,
				SceneTextures,
				ViewFamily.EngineShowFlags,
				Views,
				NaniteRasterResults
			);
		}
	}

	if (ViewFamily.EngineShowFlags.VisualizeLightCulling)
	{
		FRDGTextureRef VisualizeLightCullingTexture = GraphBuilder.CreateTexture(SceneTextures.Color.Target->Desc, TEXT("SceneColorVisualizeLightCulling"));
		AddClearRenderTargetPass(GraphBuilder, VisualizeLightCullingTexture, FLinearColor::Transparent);
		SceneTextures.Color.Target = VisualizeLightCullingTexture;

		// When not in MSAA, assign to both targets.
		if (SceneTexturesConfig.NumSamples == 1)
		{
			SceneTextures.Color.Resolve = SceneTextures.Color.Target;
		}
	}

	// mark GBufferA for saving for next frame if it's needed
	ExtractNormalsForNextFrameReprojection(GraphBuilder, SceneTextures, Views);

	// Rebuild scene textures to include GBuffers.
	SceneTextures.SetupMode |= ESceneTextureSetupMode::GBuffers;
	SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);

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

	// Render hair
	if (bHairEnable && !IsForwardShadingEnabled(ShaderPlatform))
	{
		RenderHairPrePass(GraphBuilder, Scene, Views, InstanceCullingManager);
		RenderHairBasePass(GraphBuilder, Scene, SceneTextures, Views, InstanceCullingManager);
	}

	// Shadow and fog after base pass
	if (bCanOverlayRayTracingOutput && !bOcclusionBeforeBasePass)
	{
		const bool bAfterBasePass = true;
		AllocateVirtualShadowMaps(bAfterBasePass);

		RenderShadowDepthMaps(GraphBuilder, InstanceCullingManager);

#if RHI_RAYTRACING
		// Lumen scene lighting requires ray tracing scene to be ready if HWRT shadows are desired
		if (Lumen::UseHardwareRayTracedShadows(Views[0]))
		{
			WaitForRayTracingScene(GraphBuilder);
		}
#endif // RHI_RAYTRACING

		{
			LLM_SCOPE_BYTAG(Lumen);
			RenderLumenSceneLighting(GraphBuilder, Views[0]);
		}

		ComputeVolumetricFog(GraphBuilder, SceneTextures);
		AddServiceLocalQueuePass(GraphBuilder);
	}
	// End shadow and fog after base pass

	if (bNaniteEnabled)
	{
		Nanite::GStreamingManager.SubmitFrameStreamingRequests(GraphBuilder);
	}

	if (VirtualShadowMapArray.IsEnabled())
	{
		VirtualShadowMapArray.RenderDebugInfo(GraphBuilder);

		if (Views.Num() > 0)
		{
			VirtualShadowMapArray.PrintStats(GraphBuilder, Views[0]);
		}

		if (Scene->VirtualShadowMapArrayCacheManager)
		{
			Scene->VirtualShadowMapArrayCacheManager->ExtractFrameData(ViewFamily.EngineShowFlags.VirtualShadowMapCaching, VirtualShadowMapArray, GraphBuilder);
		}
	}

	// If not all depth is written during the prepass, kick off async compute cloud after basepass
	if (bShouldRenderVolumetricCloud && bAsyncComputeVolumetricCloud && DepthPass.EarlyZPassMode != DDM_AllOpaque && bCanOverlayRayTracingOutput)
	{
		HalfResolutionDepthCheckerboardMinMaxTexture = CreateHalfResolutionDepthCheckerboardMinMax(GraphBuilder, Views, SceneTextures.Depth.Resolve);
		bHasHalfResCheckerboardMinMaxDepth = true;

		bool bSkipVolumetricRenderTarget = false;
		bool bSkipPerPixelTracing = true;
		bAsyncComputeVolumetricCloud = RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, HalfResolutionDepthCheckerboardMinMaxTexture, true, InstanceCullingManager);
	}

	if (GetCustomDepthPassLocation() == ECustomDepthPassLocation::AfterBasePass)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass_AfterBasePass);
		if (RenderCustomDepthPass(GraphBuilder, SceneTextures.CustomDepth, SceneTextures.GetSceneTextureShaderParameters(FeatureLevel)))
		{
			SceneTextures.SetupMode |= ESceneTextureSetupMode::CustomDepth;
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);
			AddServiceLocalQueuePass(GraphBuilder);
		}
	}

	// TODO: Keeping the velocities here for testing, but if that works, this pass will be remove and DDM_AllOpaqueNoVelocity will be the only option with
	// DBuffer decals enabled.

	// If bBasePassCanOutputVelocity is set, basepass fully writes the velocity buffer unless bUseSelectiveBasePassOutputs is enabled.
	if (bShouldRenderVelocities && (!bBasePassCanOutputVelocity || bUseSelectiveBasePassOutputs) && (Scene->EarlyZPassMode != DDM_AllOpaqueNoVelocity))
	{
		// Render the velocities of movable objects
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_Velocity));
		RenderVelocities(GraphBuilder, SceneTextures, EVelocityPass::Opaque, bHairEnable);
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_AfterVelocity));
		AddServiceLocalQueuePass(GraphBuilder);

		// TODO: Populate velocity buffer from Nanite visibility buffer.
	}

	// Copy lighting channels out of stencil before deferred decals which overwrite those values
	FRDGTextureRef LightingChannelsTexture = CopyStencilToLightingChannelTexture(GraphBuilder, SceneTextures.Stencil);

	// Post base pass for material classification
	if (Strata::IsStrataEnabled())
	{
		Strata::AddStrataMaterialClassificationPass(GraphBuilder, SceneTextures, Views);
	}

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
			CompositionLighting::ProcessAfterBasePass(GraphBuilder, View, SceneTextures, CompositionLightingAsyncResults, bEnableSSAO);
		}
	}

	// Rebuild scene textures to include velocity, custom depth, and SSAO.
	SceneTextures.SetupMode |= ESceneTextureSetupMode::All;
	SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);

	if (!IsForwardShadingEnabled(ShaderPlatform))
	{
		// Clear stencil to 0 now that deferred decals are done using what was setup in the base pass.
		AddClearStencilPass(GraphBuilder, SceneTextures.Depth.Target);
	}

#if RHI_RAYTRACING
	// If Lumen is not using HWRT shadows, we can wait until here: before Lumen diffuse indirect
	// Also catch the case of path tracer or RT debug output
	if (!Lumen::UseHardwareRayTracedShadows(Views[0]) || !bCanOverlayRayTracingOutput)
	{
		WaitForRayTracingScene(GraphBuilder);
	}
#endif // RHI_RAYTRACING

	if (bRenderDeferredLighting)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, RenderDeferredLighting);
		RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderLighting);
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Lighting);

		BeginGatheringLumenSurfaceCacheFeedback(GraphBuilder, Views[0]);

		FRDGTextureRef DynamicBentNormalAOTexture = nullptr;
		RenderDiffuseIndirectAndAmbientOcclusion(GraphBuilder, SceneTextures, LightingChannelsTexture, /* bIsVisualizePass = */ false);

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
		if (IsRayTracingEnabled())
		{
			RenderDitheredLODFadingOutMask(GraphBuilder, Views[0], SceneTextures.Depth.Target);
		}
#endif

		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_Lighting));
		RenderLights(GraphBuilder, SceneTextures, TranslucencyLightingVolumeTextures, LightingChannelsTexture, SortedLightSet);
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_AfterLighting));
		AddServiceLocalQueuePass(GraphBuilder);

		InjectTranslucencyLightingVolumeAmbientCubemap(GraphBuilder, Views, TranslucencyLightingVolumeTextures);
		FilterTranslucencyLightingVolume(GraphBuilder, Views, TranslucencyLightingVolumeTextures);
		AddServiceLocalQueuePass(GraphBuilder);

		// Render diffuse sky lighting and reflections that only operate on opaque pixels
		RenderDeferredReflectionsAndSkyLighting(GraphBuilder, SceneTextures, DynamicBentNormalAOTexture);

		AddSubsurfacePass(GraphBuilder, SceneTextures, Views);

		{
			RenderHairStrandsSceneColorScattering(GraphBuilder, SceneTextures.Color.Target, Scene, Views);
		}

	#if RHI_RAYTRACING
		if (ShouldRenderRayTracingSkyLight(Scene->SkyLight))
		{
			FRDGTextureRef SkyLightTexture = nullptr;
			FRDGTextureRef SkyLightHitDistanceTexture = nullptr;
			RenderRayTracingSkyLight(GraphBuilder, SceneTextures.Color.Target, SkyLightTexture, SkyLightHitDistanceTexture);
			CompositeRayTracingSkyLight(GraphBuilder, SceneTextures, SkyLightTexture, SkyLightHitDistanceTexture);
		}
	#endif

		AddServiceLocalQueuePass(GraphBuilder);
	}
	else if (HairStrands::HasViewHairStrandsData(Views) && ViewFamily.EngineShowFlags.Lighting)
	{
		RenderLightsForHair(GraphBuilder, SceneTextures.UniformBuffer, SortedLightSet, ForwardScreenSpaceShadowMaskHairTexture, LightingChannelsTexture);
		RenderDeferredReflectionsAndSkyLightingHair(GraphBuilder);
	}

	if (bShouldRenderVolumetricCloud && IsVolumetricRenderTargetEnabled() && !bHasHalfResCheckerboardMinMaxDepth && bCanOverlayRayTracingOutput)
	{
		HalfResolutionDepthCheckerboardMinMaxTexture = CreateHalfResolutionDepthCheckerboardMinMax(GraphBuilder, Views, SceneTextures.Depth.Resolve);
	}

	if (bShouldRenderVolumetricCloud && bCanOverlayRayTracingOutput)
	{
		if (!bAsyncComputeVolumetricCloud)
		{
			// Generate the volumetric cloud render target
			bool bSkipVolumetricRenderTarget = false;
			bool bSkipPerPixelTracing = true;
			RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, HalfResolutionDepthCheckerboardMinMaxTexture, false, InstanceCullingManager);
		}
		// Reconstruct the volumetric cloud render target to be ready to compose it over the scene
		ReconstructVolumetricRenderTarget(GraphBuilder, Views, SceneTextures.Depth.Resolve, HalfResolutionDepthCheckerboardMinMaxTexture, bAsyncComputeVolumetricCloud);
	}

	const bool bShouldRenderTranslucency = bCanOverlayRayTracingOutput && ShouldRenderTranslucency();

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
			GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_Translucency));
			RenderTranslucency(GraphBuilder, SceneTextures, TranslucencyLightingVolumeTextures, nullptr, ETranslucencyView::UnderWater, InstanceCullingManager);
			EnumRemoveFlags(TranslucencyViewsToRender, ETranslucencyView::UnderWater);
		}

		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_WaterPass));
		RenderSingleLayerWater(GraphBuilder, SceneTextures, bShouldRenderVolumetricCloud, SceneWithoutWaterTextures);
		AddServiceLocalQueuePass(GraphBuilder);
	}

	// Rebuild scene textures to include scene color.
	SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);

	FRDGTextureRef LightShaftOcclusionTexture = nullptr;

	// Draw Lightshafts
	if (bCanOverlayRayTracingOutput && ViewFamily.EngineShowFlags.LightShafts)
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
	if (bCanOverlayRayTracingOutput && bShouldRenderSkyAtmosphere)
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
	if (bCanOverlayRayTracingOutput && bShouldRenderVolumetricCloud)
	{
		bool bSkipVolumetricRenderTarget = true;
		bool bSkipPerPixelTracing = false;
		RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, HalfResolutionDepthCheckerboardMinMaxTexture, false, InstanceCullingManager);
	}

	// or composite the off screen buffer over the scene.
	if (bVolumetricRenderTargetRequired)
	{
		ComposeVolumetricRenderTargetOverScene(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target, bShouldRenderSingleLayerWater, SceneWithoutWaterTextures, SceneTextures);
	}

	FRendererModule& RendererModule = static_cast<FRendererModule&>(GetRendererModule());
	RendererModule.RenderPostOpaqueExtensions(GraphBuilder, Views, SceneTextures);

	RenderOpaqueFX(GraphBuilder, Views, FXSystem, SceneTextures.UniformBuffer);

	if (bCanOverlayRayTracingOutput && bShouldRenderSkyAtmosphere)
	{
		// Debug the sky atmosphere. Critically rendered before translucency to avoid emissive leaking over visualization by writing depth. 
		// Alternative: render in post process chain as VisualizeHDR.
		RenderDebugSkyAtmosphere(GraphBuilder, SceneTextures.Color.Target, SceneTextures.Depth.Target);
	}

	if (GetHairStrandsComposition() == EHairStrandsCompositionType::BeforeTranslucent)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, HairRendering);
		RenderHairComposition(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target);
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
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_Translucency));
		RenderTranslucency(GraphBuilder, SceneTextures, TranslucencyLightingVolumeTextures, &SeparateTranslucencyTextures, TranslucencyViewsToRender, InstanceCullingManager);
		AddServiceLocalQueuePass(GraphBuilder);
		TranslucencyViewsToRender = ETranslucencyView::None;

		// Compose hair before velocity/distortion pass since these pass write depth value, 
		// and this would make the hair composition fails in this cases.
		if (GetHairStrandsComposition() == EHairStrandsCompositionType::AfterTranslucent)
		{
			RDG_GPU_STAT_SCOPE(GraphBuilder, HairRendering);
			RenderHairComposition(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target);
		}

		if (bShouldRenderDistortion)
		{
			GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_Distortion));
			RenderDistortion(GraphBuilder, SceneTextures.Color.Target, SceneTextures.Depth.Target);
			AddServiceLocalQueuePass(GraphBuilder);
		}

		if (bShouldRenderVelocities)
		{
			const bool bRecreateSceneTextures = !SceneTextures.Velocity;

			GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_TranslucentVelocity));
			RenderVelocities(GraphBuilder, SceneTextures, EVelocityPass::Translucent, false);
			AddServiceLocalQueuePass(GraphBuilder);

			if (bRecreateSceneTextures)
			{
				// Rebuild scene textures to include newly allocated velocity.
				SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, SceneTextures.SetupMode);
			}
		}

		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_AfterTranslucency));
	}
	else if (GetHairStrandsComposition() == EHairStrandsCompositionType::AfterTranslucent)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, HairRendering);
		RenderHairComposition(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target);
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
		if (HairStrandsBookmarkParameters.bHasElements)
		{
			RenderHairStrandsDebugInfo(GraphBuilder, Scene, Views, HairStrandsBookmarkParameters.HairClusterData, SceneTextures.Color.Target);
		}
	}

	if (bStrataEnabled)
	{
		Strata::AddStrataDebugPasses(GraphBuilder, Views, SceneTextures.Color.Target, Scene->GetShaderPlatform());
	}

	if (bCanOverlayRayTracingOutput && ViewFamily.EngineShowFlags.LightShafts)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLightShaftBloom);
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_LightShaftBloom));
		RenderLightShaftBloom(GraphBuilder, SceneTextures, SeparateTranslucencyTextures);
		AddServiceLocalQueuePass(GraphBuilder);
	}

	if (bUseVirtualTexturing)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);
		VirtualTextureFeedbackEnd(GraphBuilder);
	}

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (Views[ViewIndex].RayTracingRenderMode == ERayTracingRenderMode::PathTracing
				&& FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(Views[ViewIndex].GetShaderPlatform()))
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

	RendererModule.RenderOverlayExtensions(GraphBuilder, Views, SceneTextures);

	if (ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO && ShouldRenderDistanceFieldLighting())
	{
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_RenderDistanceFieldLighting));

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

	RenderLumenSceneVisualization(GraphBuilder, SceneTextures);
	FinishGatheringLumenSurfaceCacheFeedback(GraphBuilder);
	RenderDiffuseIndirectAndAmbientOcclusion(GraphBuilder, SceneTextures, LightingChannelsTexture, /* bIsVisualizePass = */ true);

	if (ViewFamily.EngineShowFlags.StationaryLightOverlap)
	{
		RenderStationaryLightOverlap(GraphBuilder, SceneTextures, LightingChannelsTexture);
		AddServiceLocalQueuePass(GraphBuilder);
	}

	if (bShouldVisualizeVolumetricCloud && bCanOverlayRayTracingOutput)
	{
		RenderVolumetricCloud(GraphBuilder, SceneTextures, false, true, HalfResolutionDepthCheckerboardMinMaxTexture, false, InstanceCullingManager);
		ReconstructVolumetricRenderTarget(GraphBuilder, Views, SceneTextures.Depth.Resolve, HalfResolutionDepthCheckerboardMinMaxTexture, false);
		ComposeVolumetricRenderTargetOverSceneForVisualization(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures);
		RenderVolumetricCloud(GraphBuilder, SceneTextures, true, false, HalfResolutionDepthCheckerboardMinMaxTexture, false, InstanceCullingManager);
		AddServiceLocalQueuePass(GraphBuilder);
	}

	// Resolve the scene color for post processing.
	AddResolveSceneColorPass(GraphBuilder, Views, SceneTextures.Color);

	RendererModule.RenderPostResolvedSceneColorExtension(GraphBuilder, SceneTextures);

	FRDGTextureRef ViewFamilyTexture = TryCreateViewFamilyTexture(GraphBuilder, ViewFamily);

	CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures.UniformBuffer, ViewFamilyTexture);

	// Finish rendering for each view.
	if (ViewFamily.bResolveScene && ViewFamilyTexture)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "PostProcessing");
		RDG_GPU_STAT_SCOPE(GraphBuilder, Postprocessing);
		SCOPE_CYCLE_COUNTER(STAT_FinishRenderViewTargetTime);

		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_PostProcessing));

		FPostProcessingInputs PostProcessingInputs;
		PostProcessingInputs.ViewFamilyTexture = ViewFamilyTexture;
		PostProcessingInputs.CustomDepthTexture = SceneTextures.CustomDepth.Depth;
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

#if !(UE_BUILD_SHIPPING)
				if (IsPostProcessVisualizeCalibrationMaterialEnabled(View))
				{
					const UMaterialInterface* DebugMaterialInterface = GetPostProcessVisualizeCalibrationMaterialInterface(View);
					check(DebugMaterialInterface);

					AddVisualizeCalibrationMaterialPostProcessingPasses(GraphBuilder, View, PostProcessingInputs, DebugMaterialInterface);
				}
				else
#endif
				{
					AddPostProcessingPasses(GraphBuilder, View, PostProcessingInputs, NaniteResults, InstanceCullingManager);
				}
			}
		}
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		ShaderPrint::EndView(Views[ViewIndex]);
		ShaderDrawDebug::EndView(Views[ViewIndex]);
	}

	GEngine->GetPostRenderDelegateEx().Broadcast(GraphBuilder);

#if RHI_RAYTRACING
	ReleaseRaytracingResources(GraphBuilder, Views, Scene->RayTracingScene);
#endif //  RHI_RAYTRACING

#if WITH_MGPU
	DoCrossGPUTransfers(GraphBuilder, RenderTargetGPUMask, ViewFamilyTexture);
#endif

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (((View.FinalPostProcessSettings.DynamicGlobalIlluminationMethod == EDynamicGlobalIlluminationMethod::ScreenSpace && ScreenSpaceRayTracing::ShouldKeepBleedFreeSceneColor(View))
				|| GetViewPipelineState(View).DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen)
			&& !View.bStatePrevViewInfoIsReadOnly)
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
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_RenderFinish));
		RenderFinish(GraphBuilder, ViewFamilyTexture);
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_AfterFrame));
		AddServiceLocalQueuePass(GraphBuilder);
	}

	QueueSceneTextureExtractions(GraphBuilder, SceneTextures);
}

#if RHI_RAYTRACING

bool AnyRayTracingPassEnabled(const FScene* Scene, const FViewInfo& View)
{
	if (ShouldRenderRayTracingAmbientOcclusion(View)
		|| ShouldRenderRayTracingReflections(View)
		|| ShouldRenderRayTracingGlobalIllumination(View)
		|| ShouldRenderRayTracingTranslucency(View)
		|| ShouldRenderRayTracingSkyLight(Scene ? Scene->SkyLight : nullptr)
		|| ShouldRenderRayTracingShadows()
        || Lumen::AnyLumenHardwareRayTracingPassEnabled(Scene, View)
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

bool ShouldRenderRayTracingEffect(bool bEffectEnabled)
{
	if (!IsRayTracingEnabled())
	{
		return false;
	}

	static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.ForceAllRayTracingEffects"));
	const int32 OverrideMode = CVar != nullptr ? CVar->GetInt() : -1;

	if (OverrideMode >= 0)
	{
		return OverrideMode > 0;
	}
	else
	{
		return bEffectEnabled;
	}
}

bool CanOverlayRayTracingOutput(const FViewInfo& View)
{
	// Return false if a full screen ray tracing pass will be displayed on top of the raster pass
	// This can be used to skip certain calculations
	return (View.RayTracingRenderMode != ERayTracingRenderMode::PathTracing &&
			View.RayTracingRenderMode != ERayTracingRenderMode::RayTracingDebug);
}
#endif // RHI_RAYTRACING
