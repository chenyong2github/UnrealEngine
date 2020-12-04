// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MobileShadingRenderer.cpp: Scene rendering code for ES3/3.1 feature level.
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Misc/MemStack.h"
#include "HAL/IConsoleManager.h"
#include "EngineGlobals.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "SceneUtils.h"
#include "UniformBuffer.h"
#include "Engine/BlendableInterface.h"
#include "ShaderParameters.h"
#include "RHIStaticStates.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "FXSystem.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/PostProcessMobile.h"
#include "PostProcess/PostProcessUpscale.h"
#include "PostProcess/PostProcessCompositeEditorPrimitives.h"
#include "PostProcess/PostProcessHMD.h"
#include "PostProcess/PostProcessPixelProjectedReflectionMobile.h"
#include "PostProcess/PostProcessAmbientOcclusionMobile.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "SceneViewExtension.h"
#include "ScreenRendering.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "MobileSeparateTranslucencyPass.h"
#include "MobileDistortionPass.h"
#include "VisualizeTexturePresent.h"
#include "RendererModule.h"
#include "EngineModule.h"
#include "GPUScene.h"
#include "MaterialSceneTextureId.h"
#include "DebugViewModeRendering.h"
#include "SkyAtmosphereRendering.h"
#include "VisualizeTexture.h"
#include "VT/VirtualTextureFeedback.h"
#include "VT/VirtualTextureSystem.h"
#include "GPUSortManager.h"
#include "MobileDeferredShadingPass.h"
#include "PlanarReflectionSceneProxy.h"

uint32 GetShadowQuality();

static TAutoConsoleVariable<int32> CVarMobileAlwaysResolveDepth(
	TEXT("r.Mobile.AlwaysResolveDepth"),
	0,
	TEXT("0: Depth buffer is resolved after opaque pass only when decals or modulated shadows are in use. (Default)\n")
	TEXT("1: Depth buffer is always resolved after opaque pass.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileForceDepthResolve(
	TEXT("r.Mobile.ForceDepthResolve"),
	0,
	TEXT("0: Depth buffer is resolved by switching out render targets. (Default)\n")
	TEXT("1: Depth buffer is resolved by switching out render targets and drawing with the depth texture.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileAdrenoOcclusionMode(
	TEXT("r.Mobile.AdrenoOcclusionMode"),
	0,
	TEXT("0: Render occlusion queries after the base pass (default).\n")
	TEXT("1: Render occlusion queries after translucency and a flush, which can help Adreno devices in GL mode."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileFlushSceneColorRendering(
	TEXT("r.Mobile.FlushSceneColorRendering"),
	1,
	TEXT("0: Submmit command buffer after all rendering is finished.\n")
	TEXT("1: Submmit command buffer (flush) before starting post-processing (default)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileCustomDepthForTranslucency(
	TEXT("r.Mobile.CustomDepthForTranslucency"),
	1,
	TEXT(" Whether to render custom depth/stencil if any tranclucency in the scene uses it. \n")
	TEXT(" 0 = Off \n")
	TEXT(" 1 = On [default]"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

DECLARE_GPU_STAT_NAMED(MobileSceneRender, TEXT("Mobile Scene Render"));

DECLARE_CYCLE_STAT(TEXT("SceneStart"), STAT_CLMM_SceneStart, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("SceneEnd"), STAT_CLMM_SceneEnd, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("InitViews"), STAT_CLMM_InitViews, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Opaque"), STAT_CLMM_Opaque, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Occlusion"), STAT_CLMM_Occlusion, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Post"), STAT_CLMM_Post, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Translucency"), STAT_CLMM_Translucency, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Shadows"), STAT_CLMM_Shadows, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("SceneSimulation"), STAT_CLMM_SceneSim, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("PrePass"), STAT_CLM_MobilePrePass, STATGROUP_CommandListMarkers);

FGlobalDynamicIndexBuffer FMobileSceneRenderer::DynamicIndexBuffer;
FGlobalDynamicVertexBuffer FMobileSceneRenderer::DynamicVertexBuffer;
TGlobalResource<FGlobalDynamicReadBuffer> FMobileSceneRenderer::DynamicReadBuffer;

extern bool IsMobileEyeAdaptationEnabled(const FViewInfo& View);

static bool UsesCustomDepthStencilLookup(const FViewInfo& View)
{
	bool bUsesCustomDepthStencil = false;

	// Find out whether CustomDepth/Stencil used in translucent materials
	if (View.bUsesCustomDepthStencilInTranslucentMaterials && CVarMobileCustomDepthForTranslucency.GetValueOnAnyThread() != 0)
	{
		bUsesCustomDepthStencil = true;
	}
	else
	{
		// Find out whether post-process materials use CustomDepth/Stencil lookups
		const FBlendableManager& BlendableManager = View.FinalPostProcessSettings.BlendableManager;
		FBlendableEntry* BlendableIt = nullptr;

		while (FPostProcessMaterialNode* DataPtr = BlendableManager.IterateBlendables<FPostProcessMaterialNode>(BlendableIt))
		{
			if (DataPtr->IsValid())
			{
				FMaterialRenderProxy* Proxy = DataPtr->GetMaterialInterface()->GetRenderProxy();
				check(Proxy);

				const FMaterial& Material = Proxy->GetIncompleteMaterialWithFallback(View.GetFeatureLevel());
				if (Material.IsStencilTestEnabled())
				{
					bUsesCustomDepthStencil = true;
					break;
				}

				const FMaterialShaderMap* MaterialShaderMap = Material.GetRenderingThreadShaderMap();
				if (MaterialShaderMap->UsesSceneTexture(PPI_CustomDepth) || MaterialShaderMap->UsesSceneTexture(PPI_CustomStencil))
				{
					bUsesCustomDepthStencil = true;
					break;
				}
			}
		}
	}

	// Find out whether there are primitives will render in custom depth pass or just always render custom depth 
	static const auto CVarCustomDepth = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.CustomDepth"));

	bUsesCustomDepthStencil &= (View.bHasCustomDepthPrimitives || (CVarCustomDepth && CVarCustomDepth->GetValueOnRenderThread() > 1));
	
	return bUsesCustomDepthStencil;
}

BEGIN_SHADER_PARAMETER_STRUCT(FRenderOpaqueFXPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMobileSceneTextureUniformParameters, SceneTextures)
END_SHADER_PARAMETER_STRUCT()

FMobileSceneRenderer::FMobileSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer)
	: FSceneRenderer(InViewFamily, HitProxyConsumer)
	, bGammaSpace(!IsMobileHDR())
	, bDeferredShading(IsMobileDeferredShadingEnabled(ShaderPlatform))
	, bUseVirtualTexturing(UseVirtualTexturing(FeatureLevel))
{
	bRenderToSceneColor = false;
	bRequiresMultiPass = false;
	bKeepDepthContent = false;
	bSubmitOffscreenRendering = false;
	bModulatedShadowsInUse = false;
	bShouldRenderCustomDepth = false;
	bRequiresPixelProjectedPlanarRelfectionPass = false;
	bRequriesAmbientOcclusionPass = false;

	// Don't do occlusion queries when doing scene captures
	for (FViewInfo& View : Views)
	{
		if (View.bIsSceneCapture)
		{
			View.bDisableQuerySubmissions = true;
			View.bIgnoreExistingQueries = true;
		}
	}

	static const auto CVarMobileMSAA = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileMSAA"));
	NumMSAASamples = (CVarMobileMSAA ? CVarMobileMSAA->GetValueOnAnyThread() : 1);
}

class FMobileDirLightShaderParamsRenderResource : public FRenderResource
{
public:
	using MobileDirLightUniformBufferRef = TUniformBufferRef<FMobileDirectionalLightShaderParameters>;

	virtual void InitRHI() override
	{
		UniformBufferRHI =
			MobileDirLightUniformBufferRef::CreateUniformBufferImmediate(
				FMobileDirectionalLightShaderParameters(),
				UniformBuffer_MultiFrame);
	}

	virtual void ReleaseRHI() override
	{
		UniformBufferRHI.SafeRelease();
	}

	MobileDirLightUniformBufferRef UniformBufferRHI;
};

TUniformBufferRef<FMobileDirectionalLightShaderParameters>& GetNullMobileDirectionalLightShaderParameters()
{
	static TGlobalResource<FMobileDirLightShaderParamsRenderResource>* NullLightParams;
	if (!NullLightParams)
	{
		NullLightParams = new TGlobalResource<FMobileDirLightShaderParamsRenderResource>();
	}
	check(!!NullLightParams->UniformBufferRHI);
	return NullLightParams->UniformBufferRHI;
}

void FMobileSceneRenderer::PrepareViewVisibilityLists()
{
	// Prepare view's visibility lists.
	// TODO: only do this when CSM + static is required.
	for (auto& View : Views)
	{
		FMobileCSMVisibilityInfo& MobileCSMVisibilityInfo = View.MobileCSMVisibilityInfo;
		// Init list of primitives that can receive Dynamic CSM.
		MobileCSMVisibilityInfo.MobilePrimitiveCSMReceiverVisibilityMap.Init(false, View.PrimitiveVisibilityMap.Num());

		// Init static mesh visibility info for CSM drawlist
		MobileCSMVisibilityInfo.MobileCSMStaticMeshVisibilityMap.Init(false, View.StaticMeshVisibilityMap.Num());

		// Init static mesh visibility info for default drawlist that excludes meshes in CSM only drawlist.
		MobileCSMVisibilityInfo.MobileNonCSMStaticMeshVisibilityMap = View.StaticMeshVisibilityMap;
	}
}

void FMobileSceneRenderer::SetupMobileBasePassAfterShadowInit(FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewVisibleCommandsPerView& ViewCommandsPerView)
{
	// Sort front to back on all platforms, even HSR benefits from it
	//const bool bWantsFrontToBackSorting = (GHardwareHiddenSurfaceRemoval == false);

	// compute keys for front to back sorting and dispatch pass setup.
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		FViewCommands& ViewCommands = ViewCommandsPerView[ViewIndex];

		PassProcessorCreateFunction CreateFunction = FPassProcessorManager::GetCreateFunction(EShadingPath::Mobile, EMeshPass::BasePass);
		FMeshPassProcessor* MeshPassProcessor = CreateFunction(Scene, &View, nullptr);

		PassProcessorCreateFunction BasePassCSMCreateFunction = FPassProcessorManager::GetCreateFunction(EShadingPath::Mobile, EMeshPass::MobileBasePassCSM);
		FMeshPassProcessor* BasePassCSMMeshPassProcessor = BasePassCSMCreateFunction(Scene, &View, nullptr);

		// Run sorting on BasePass, as it's ignored inside FSceneRenderer::SetupMeshPass, so it can be done after shadow init on mobile.
		FParallelMeshDrawCommandPass& Pass = View.ParallelMeshDrawCommandPasses[EMeshPass::BasePass];
		Pass.DispatchPassSetup(
			Scene,
			View,
			EMeshPass::BasePass,
			BasePassDepthStencilAccess,
			MeshPassProcessor,
			View.DynamicMeshElements,
			&View.DynamicMeshElementsPassRelevance,
			View.NumVisibleDynamicMeshElements[EMeshPass::BasePass],
			ViewCommands.DynamicMeshCommandBuildRequests[EMeshPass::BasePass],
			ViewCommands.NumDynamicMeshCommandBuildRequestElements[EMeshPass::BasePass],
			ViewCommands.MeshCommands[EMeshPass::BasePass],
			BasePassCSMMeshPassProcessor,
			&ViewCommands.MeshCommands[EMeshPass::MobileBasePassCSM]);
	}
}

/**
 * Initialize scene's views.
 * Check visibility, sort translucent items, etc.
 */
void FMobileSceneRenderer::InitViews(FRDGBuilder& GraphBuilder)
{
	FRHICommandListImmediate& RHICmdList = GraphBuilder.RHICmdList;
	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_InitViews));

	SCOPED_DRAW_EVENT(RHICmdList, InitViews);

	SCOPE_CYCLE_COUNTER(STAT_InitViewsTime);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(InitViews_Scene);

	check(Scene);

	if (bUseVirtualTexturing)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);
		// AllocateResources needs to be called before RHIBeginScene
		FVirtualTextureSystem::Get().AllocateResources(GraphBuilder, FeatureLevel);
		FVirtualTextureSystem::Get().CallPendingCallbacks();
	}

	FILCUpdatePrimTaskData ILCTaskData;
	FViewVisibleCommandsPerView ViewCommandsPerView;
	ViewCommandsPerView.SetNum(Views.Num());

	const FExclusiveDepthStencil::Type BasePassDepthStencilAccess = FExclusiveDepthStencil::DepthWrite_StencilWrite;

	PreVisibilityFrameSetup(GraphBuilder);
	ComputeViewVisibility(RHICmdList, BasePassDepthStencilAccess, ViewCommandsPerView, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer);
	PostVisibilityFrameSetup(ILCTaskData);

	const FIntPoint RenderTargetSize = (ViewFamily.RenderTarget->GetRenderTargetTexture().IsValid()) ? ViewFamily.RenderTarget->GetRenderTargetTexture()->GetSizeXY() : ViewFamily.RenderTarget->GetSizeXY();
	const bool bRequiresUpscale = ((int32)RenderTargetSize.X > FamilySize.X || (int32)RenderTargetSize.Y > FamilySize.Y);
	// ES requires that the back buffer and depth match dimensions.
	// For the most part this is not the case when using scene captures. Thus scene captures always render to scene color target.
	const bool bStereoRenderingAndHMD = ViewFamily.EngineShowFlags.StereoRendering && ViewFamily.EngineShowFlags.HMDDistortion;
	bRenderToSceneColor = !bGammaSpace || bStereoRenderingAndHMD || bRequiresUpscale || FSceneRenderer::ShouldCompositeEditorPrimitives(Views[0]) || Views[0].bIsSceneCapture || Views[0].bIsReflectionCapture;
	const FPlanarReflectionSceneProxy* PlanarReflectionSceneProxy = Scene ? Scene->GetForwardPassGlobalPlanarReflection() : nullptr;

	bRequiresPixelProjectedPlanarRelfectionPass = IsUsingMobilePixelProjectedReflection(ShaderPlatform)
		&& PlanarReflectionSceneProxy != nullptr
		&& PlanarReflectionSceneProxy->RenderTarget != nullptr
		&& !Views[0].bIsReflectionCapture
		&& !ViewFamily.EngineShowFlags.HitProxies
		&& ViewFamily.EngineShowFlags.Lighting
		&& !ViewFamily.EngineShowFlags.VisualizeLightCulling
		&& !ViewFamily.UseDebugViewPS()
		// Only support forward shading, we don't want to break tiled deferred shading.
		&& !bDeferredShading;


	bRequriesAmbientOcclusionPass = IsUsingMobileAmbientOcclusion(ShaderPlatform)
		&& Views[0].FinalPostProcessSettings.AmbientOcclusionIntensity > 0
		&& Views[0].FinalPostProcessSettings.AmbientOcclusionStaticFraction >= 1 / 100.0f
		&& ViewFamily.EngineShowFlags.Lighting
		&& !Views[0].bIsReflectionCapture
		&& !Views[0].bIsPlanarReflection
		&& !ViewFamily.EngineShowFlags.HitProxies
		&& !ViewFamily.EngineShowFlags.VisualizeLightCulling
		&& !ViewFamily.UseDebugViewPS()
		// Only support forward shading, we don't want to break tiled deferred shading.
		&& !bDeferredShading;

	// Whether we need to store depth for post-processing
	// On PowerVR we see flickering of shadows and depths not updating correctly if targets are discarded.
	// See CVarMobileForceDepthResolve use in ConditionalResolveSceneDepth.
	const bool bForceDepthResolve = (CVarMobileForceDepthResolve.GetValueOnRenderThread() == 1);
	const bool bSeparateTranslucencyActive = IsMobileSeparateTranslucencyActive(Views.GetData(), Views.Num()); 
	bRequiresMultiPass = RequiresMultiPass(RHICmdList, Views[0]);
	bKeepDepthContent = 
		bRequiresMultiPass || 
		bForceDepthResolve ||
		bRequriesAmbientOcclusionPass ||
		bRequiresPixelProjectedPlanarRelfectionPass ||
		bSeparateTranslucencyActive ||
		Views[0].bIsReflectionCapture;
	// never keep MSAA depth
	bKeepDepthContent = (NumMSAASamples > 1 ? false : bKeepDepthContent);

	// Initialize global system textures (pass-through if already initialized).
	GSystemTextures.InitializeTextures(RHICmdList, FeatureLevel);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();

	// Allocate the maximum scene render target space for the current view family.
	SceneContext.SetKeepDepthContent(bKeepDepthContent);
	SceneContext.Allocate(GraphBuilder, this);

	if (bDeferredShading)
	{
		ETextureCreateFlags AddFlags = bRequiresMultiPass ? TexCreate_InputAttachmentRead : (TexCreate_InputAttachmentRead | TexCreate_Memoryless);
		SceneContext.AllocGBufferTargets(RHICmdList, AddFlags);
	}

	// Initialise Sky/View resources before the view global uniform buffer is built.
	if (ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags))
	{
		InitSkyAtmosphereForViews(RHICmdList);
	}

	if (bRequiresPixelProjectedPlanarRelfectionPass)
	{
		InitPixelProjectedReflectionOutputs(RHICmdList, PlanarReflectionSceneProxy->RenderTarget->GetSizeXY());
	}
	else
	{
		ReleasePixelProjectedReflectionOutputs();
	}

	if (bRequriesAmbientOcclusionPass)
	{
		InitAmbientOcclusionOutputs(RHICmdList, SceneContext.SceneDepthZ);
	}
	else
	{
		ReleaseAmbientOcclusionOutputs();
	}

	//make sure all the targets we're going to use will be safely writable.
	GRenderTargetPool.TransitionTargetsWritable(RHICmdList);

	// Find out whether custom depth pass should be rendered.
	{
		bool bCouldUseCustomDepthStencil = !bGammaSpace && (!Scene->World || (Scene->World->WorldType != EWorldType::EditorPreview && Scene->World->WorldType != EWorldType::Inactive));
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			Views[ViewIndex].bCustomDepthStencilValid = bCouldUseCustomDepthStencil && UsesCustomDepthStencilLookup(Views[ViewIndex]);
			bShouldRenderCustomDepth |= Views[ViewIndex].bCustomDepthStencilValid;
		}
	}
	
	const bool bDynamicShadows = ViewFamily.EngineShowFlags.DynamicShadows;
	
	if (bDynamicShadows && !IsSimpleForwardShadingEnabled(ShaderPlatform))
	{
		// Setup dynamic shadows.
		InitDynamicShadows(RHICmdList);		
	}
	else
	{
		// TODO: only do this when CSM + static is required.
		PrepareViewVisibilityLists();
	}

	/** Before SetupMobileBasePassAfterShadowInit, we need to update the uniform buffer and shadow info for all movable point lights.*/
	UpdateMovablePointLightUniformBufferAndShadowInfo();

	SetupMobileBasePassAfterShadowInit(BasePassDepthStencilAccess, ViewCommandsPerView);

	// if we kicked off ILC update via task, wait and finalize.
	if (ILCTaskData.TaskRef.IsValid())
	{
		Scene->IndirectLightingCache.FinalizeCacheUpdates(Scene, *this, ILCTaskData);
	}

	// initialize per-view uniform buffer.  Pass in shadow info as necessary.
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		
		if (bDeferredShading)
		{
			if (View.ViewState)
			{
				if (!View.ViewState->ForwardLightingResources)
				{
					View.ViewState->ForwardLightingResources.Reset(new FForwardLightingViewResources());
				}
				View.ForwardLightingResources = View.ViewState->ForwardLightingResources.Get();
			}
			else
			{
				View.ForwardLightingResourcesStorage.Reset(new FForwardLightingViewResources());
				View.ForwardLightingResources = View.ForwardLightingResourcesStorage.Get();
			}
		}
	
		if (View.ViewState)
		{
			View.ViewState->UpdatePreExposure(View);
		}

		// Initialize the view's RHI resources.
		View.InitRHIResources();

		// Get the custom 1x1 target used to store exposure value and Toggle the two render targets used to store new and old.
		if (IsMobileEyeAdaptationEnabled(View))
		{
			View.SwapEyeAdaptationBuffers();
		}
	}

	Scene->GPUScene.Update(GraphBuilder, *Scene);
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		Scene->GPUScene.UploadDynamicPrimitiveShaderDataForView(RHICmdList, *Scene, Views[ViewIndex]);
	}

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

	// update buffers used in cached mesh path
	// in case there are multiple views, these buffers will be updated before rendering each view
	if (Views.Num() > 0)
	{
		const FViewInfo& View = Views[0];
		// We want to wait for the extension jobs only when the view is being actually rendered for the first time
		Scene->UniformBuffers.UpdateViewUniformBuffer(View, false);
		UpdateOpaqueBasePassUniformBuffer(RHICmdList, View);
		UpdateTranslucentBasePassUniformBuffer(RHICmdList, View);
		UpdateDirectionalLightUniformBuffers(RHICmdList, View);
	}
	if (bDeferredShading)
	{
		SetupSceneReflectionCaptureBuffer(RHICmdList);
	}
	UpdateSkyReflectionUniformBuffer();

	// Now that the indirect lighting cache is updated, we can update the uniform buffers.
	UpdatePrimitiveIndirectLightingCacheBuffers();
	
	OnStartRender(RHICmdList);

	// Whether to submit cmdbuffer with offscreen rendering before doing post-processing
	bSubmitOffscreenRendering = (!bGammaSpace || bRenderToSceneColor) && CVarMobileFlushSceneColorRendering.GetValueOnAnyThread() != 0;
}

/** 
* Renders the view family. 
*/
void FMobileSceneRenderer::Render(FRHICommandListImmediate& RHICmdList)
{
	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("MobileShadingRenderer_Render(ViewFamily=%s)", ViewFamily.bResolveScene ? TEXT("Primary") : TEXT("Auxiliary")));

	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_SceneStart));

	SCOPED_DRAW_EVENT(RHICmdList, MobileSceneRender);
	SCOPED_GPU_STAT(RHICmdList, MobileSceneRender);

	Scene->UpdateAllPrimitiveSceneInfos(GraphBuilder);

	// Establish scene primitive count (must be done after UpdateAllPrimitiveSceneInfos)
	FGPUSceneScopeBeginEndHelper GPUSceneScopeBeginEndHelper(Scene->GPUScene, GPUSceneDynamicContext, *Scene);

	PrepareViewRectsForRendering();

	if (ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags))
	{
		for (int32 LightIndex = 0; LightIndex < NUM_ATMOSPHERE_LIGHTS; ++LightIndex)
		{
			if (Scene->AtmosphereLights[LightIndex])
			{
				PrepareSunLightProxy(*Scene->GetSkyAtmosphereSceneInfo(), LightIndex, *Scene->AtmosphereLights[LightIndex]);
			}
		}
	}
	else
	{
		Scene->ResetAtmosphereLightsProperties();
	}

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderOther);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMobileSceneRenderer_Render);
	//FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

	if (!ViewFamily.EngineShowFlags.Rendering)
	{
		GraphBuilder.Execute();
		return;
	}

	SCOPED_GPU_STAT(RHICmdList, MobileSceneRender);

	WaitOcclusionTests(RHICmdList);
	FRHICommandListExecutor::GetImmediateCommandList().PollOcclusionQueries();
	RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

	// Find the visible primitives and prepare targets and buffers for rendering
	InitViews(GraphBuilder);

	
	if (GRHINeedsExtraDeletionLatency || !GRHICommandList.Bypass())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FMobileSceneRenderer_PostInitViewsFlushDel);
		// we will probably stall on occlusion queries, so might as well have the RHI thread and GPU work while we wait.
		// Also when doing RHI thread this is the only spot that will process pending deletes
		FRHICommandListExecutor::GetImmediateCommandList().PollOcclusionQueries();
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
	}

	GEngine->GetPreRenderDelegate().Broadcast();

	// Global dynamic buffers need to be committed before rendering.
	DynamicIndexBuffer.Commit();
	DynamicVertexBuffer.Commit();
	DynamicReadBuffer.Commit();
	RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

	GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLMM_SceneSim));

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();

	if (bUseVirtualTexturing)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);
		FVirtualTextureSystem::Get().Update(GraphBuilder, FeatureLevel, Scene);

		AddPass(GraphBuilder, [&SceneContext](FRHICommandList& InRHICmdList)
		{
			// Clear virtual texture feedback to default value
			FUnorderedAccessViewRHIRef FeedbackUAV = SceneContext.GetVirtualTextureFeedbackUAV();
			InRHICmdList.Transition(FRHITransitionInfo(FeedbackUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			InRHICmdList.ClearUAVUint(FeedbackUAV, FUintVector4(~0u, ~0u, ~0u, ~0u));
			InRHICmdList.Transition(FRHITransitionInfo(FeedbackUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
		});
	}

	FSortedLightSetSceneInfo SortedLightSet;
	if (bDeferredShading)
	{
		GatherAndSortLights(SortedLightSet);
		int32 NumReflectionCaptures = Views[0].NumBoxReflectionCaptures + Views[0].NumSphereReflectionCaptures;
		bool bCullLightsToGrid = (NumReflectionCaptures > 0 || GMobileUseClusteredDeferredShading != 0);
		ComputeLightGrid(GraphBuilder, bCullLightsToGrid, SortedLightSet);
	}

	// Generate the Sky/Atmosphere look up tables
	const bool bShouldRenderSkyAtmosphere = ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags);
	if (bShouldRenderSkyAtmosphere)
	{
		RenderSkyAtmosphereLookUpTables(GraphBuilder);
	}

	// Notify the FX system that the scene is about to be rendered.
	if (FXSystem && ViewFamily.EngineShowFlags.Particles)
	{
		AddPass(GraphBuilder, [this](FRHICommandListImmediate& InRHICmdList)
		{
			FXSystem->PreRender(InRHICmdList, Views[0].ViewUniformBuffer, NULL, !Views[0].bIsPlanarReflection);
			if (FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager())
			{
				GPUSortManager->OnPreRender(InRHICmdList);
			}
		});
	}

	auto PollOcclusionQueriesAndDispatchToRHIThreadPass = [](FRHICommandListImmediate& InRHICmdList)
	{
		FRHICommandListExecutor::GetImmediateCommandList().PollOcclusionQueries();
		InRHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	};

	AddPass(GraphBuilder, PollOcclusionQueriesAndDispatchToRHIThreadPass);

	GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLMM_Shadows));

	RenderShadowDepthMaps(GraphBuilder);

	AddPass(GraphBuilder, PollOcclusionQueriesAndDispatchToRHIThreadPass);

	// Default view list
	TArray<const FViewInfo*> ViewList;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++) 
	{
		ViewList.Add(&Views[ViewIndex]);
	}

	// Custom depth
	// bShouldRenderCustomDepth has been initialized in InitViews on mobile platform
	if (bShouldRenderCustomDepth)
	{
		RenderCustomDepthPass(GraphBuilder);
	}

	// Include the CustomDepth flag in any case, so we don't have to create the MobileSceneTextures uniform buffer for each view.
	EMobileSceneTextureSetupMode SetupMode = EMobileSceneTextureSetupMode::SceneColor | EMobileSceneTextureSetupMode::CustomDepth;

	TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> MobileSceneTextures = CreateMobileSceneTextureUniformBuffer(GraphBuilder, SetupMode);

	FRDGTextureRef ViewFamilyTexture = TryCreateViewFamilyTexture(GraphBuilder, ViewFamily);

	FRDGTextureMSAA SceneColorMSAA = RegisterExternalTextureMSAA(GraphBuilder, SceneContext.GetSceneColor());
	FRDGTextureMSAA SceneDepthMSAA = RegisterExternalTextureMSAA(GraphBuilder, SceneContext.SceneDepthZ);
	
	if (bDeferredShading)
	{
		RenderDeferred(GraphBuilder, ViewList, SortedLightSet, ViewFamilyTexture, SceneColorMSAA, SceneDepthMSAA, MobileSceneTextures);
	}
	else
	{
		RenderForward(GraphBuilder, ViewList, ViewFamilyTexture, SceneColorMSAA, SceneDepthMSAA, MobileSceneTextures);
	}
		
	if (FXSystem && Views.IsValidIndex(0))
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FRenderOpaqueFXPassParameters>();
		PassParameters->SceneTextures = MobileSceneTextures;

		// Cascade uses pixel shaders for compute stuff in PostRenderOpaque so ERDGPassFlags::Raster is needed
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("OpaqueFX"),
			PassParameters,
			ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			[this](FRHICommandListImmediate& InRHICmdList)
		{
			check(InRHICmdList.IsOutsideRenderPass());

			FXSystem->PostRenderOpaque(
				InRHICmdList,
				Views[0].ViewUniformBuffer,
				nullptr,
				nullptr,
				Views[0].AllowGPUParticleUpdate()
			);
			if (FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager())
			{
				GPUSortManager->OnPostRenderOpaque(InRHICmdList);
			}
			InRHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		});
	}

	// Flush / submit cmdbuffer
	if (bSubmitOffscreenRendering)
	{
		AddPass(GraphBuilder, [this](FRHICommandListImmediate& InRHICmdList)
		{
			InRHICmdList.SubmitCommandsHint();
			InRHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		});
	}

	if (bRequriesAmbientOcclusionPass)
	{
		FRDGTextureRef AmbientOcclusionTexture = GraphBuilder.RegisterExternalTexture(GAmbientOcclusionMobileOutputs.AmbientOcclusionTexture, ERenderTargetTexture::Targetable);
		RenderAmbientOcclusion(GraphBuilder, (*MobileSceneTextures)->SceneDepthTexture, AmbientOcclusionTexture);
	}

	if (bDeferredShading)
	{
		AddPass(GraphBuilder, [this, &SceneContext](FRHICommandListImmediate& InRHICmdList)
		{
			// Release the original reference on the scene render targets
			SceneContext.AdjustGBufferRefCount(InRHICmdList, -1);
		});
	}

	GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLMM_Post));

	if (bUseVirtualTexturing)
	{	
		RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);

		AddPass(GraphBuilder, RDG_EVENT_NAME("VirtualTextureUpdate"), [this, &SceneContext](FRHICommandListImmediate& InRHICmdList)
		{
			// No pass after this should make VT page requests
			InRHICmdList.Transition(FRHITransitionInfo(SceneContext.VirtualTextureFeedbackUAV, ERHIAccess::UAVMask, ERHIAccess::SRVMask));

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
	
	if (ViewFamily.bResolveScene)
	{
		if (!bGammaSpace || bRenderToSceneColor)
		{
			// Finish rendering for each view, or the full stereo buffer if enabled
			{
				RDG_EVENT_SCOPE(GraphBuilder, "PostProcessing");
				SCOPE_CYCLE_COUNTER(STAT_FinishRenderViewTargetTime);

				FMobilePostProcessingInputs PostProcessingInputs;
				PostProcessingInputs.ViewFamilyTexture = ViewFamilyTexture;

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);
					PostProcessingInputs.SceneTextures = MobileSceneTextures;
					AddMobilePostProcessingPasses(GraphBuilder, Views[ViewIndex], PostProcessingInputs);
				}
			}
		}
	}

	AddPass(GraphBuilder, [this](FRHICommandListImmediate& InRHICmdList)
	{
		GEngine->GetPostRenderDelegate().Broadcast();

		InRHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_SceneEnd));
	});
	
	RenderFinish(GraphBuilder, ViewFamilyTexture);
	
	AddPass(GraphBuilder, PollOcclusionQueriesAndDispatchToRHIThreadPass);

	GraphBuilder.Execute();
}

BEGIN_SHADER_PARAMETER_STRUCT(FMobilePostBasePassViewExtensionParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMobileSceneTextureUniformParameters, SceneTextures)
END_SHADER_PARAMETER_STRUCT()

void FMobileSceneRenderer::RenderForward(FRDGBuilder& GraphBuilder, const TArrayView<const FViewInfo*> ViewList, FRDGTextureRef ViewFamilyTexture, FRDGTextureMSAA& SceneColorMSAA, FRDGTextureMSAA& SceneDepthMSAA, TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> MobileSceneTextures)
{
	const FViewInfo& View = *ViewList[0];
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
				
	FRDGTextureRef SceneColor = nullptr;
	FRDGTextureRef SceneColorResolve = nullptr;
	FRDGTextureRef SceneDepth = nullptr;

	// Verify using both MSAA sample count AND the scene color surface sample count, since on GLES you can't have MSAA color targets,
	// so the color target would be created without MSAA, and MSAA is achieved through magical means (the framebuffer, being MSAA,
	// tells the GPU "execute this renderpass as MSAA, and when you're done, automatically resolve and copy into this non-MSAA texture").
	bool bMobileMSAA = NumMSAASamples > 1 && SceneContext.GetSceneColorSurface()->GetNumSamples() > 1;

	static const auto CVarMobileMultiView = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.MobileMultiView"));
	const bool bIsMultiViewApplication = (CVarMobileMultiView && CVarMobileMultiView->GetValueOnAnyThread() != 0);
	
	if (bGammaSpace && !bRenderToSceneColor)
	{
		if (bMobileMSAA)
		{
			SceneColor = SceneColorMSAA.Target;
			SceneColorResolve = ViewFamilyTexture;
		}
		else
		{
			SceneColor = ViewFamilyTexture;
		}
		SceneDepth = SceneDepthMSAA.Target;
	}
	else
	{
		SceneColor = SceneColorMSAA.Target;
		SceneColorResolve = bMobileMSAA ? SceneColorMSAA.Resolve : nullptr;
		SceneDepth = SceneDepthMSAA.Target;
	}

	FRDGTextureRef FoveationTexture = nullptr;
	
	if (SceneContext.IsFoveationTextureAllocated()	&& !View.bIsSceneCapture && !View.bIsReflectionCapture)
	{
		FoveationTexture = GraphBuilder.RegisterExternalTexture(SceneContext.FoveationTexture, ERenderTargetTexture::Targetable);
	}

	FRenderTargetBindingSlots BasePassRenderTargets;
	BasePassRenderTargets[0] = FRenderTargetBinding(SceneColor, SceneColorResolve, ERenderTargetLoadAction::EClear);
	BasePassRenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);
	BasePassRenderTargets.FoveationTexture = FoveationTexture;
	BasePassRenderTargets.SubpassHint = ESubpassHint::DepthReadSubpass;
	BasePassRenderTargets.NumOcclusionQueries = ComputeNumOcclusionQueriesToBatch();

	//if the scenecolor isn't multiview but the app is, need to render as a single-view multiview due to shaders
	BasePassRenderTargets.MultiViewCount = View.bIsMobileMultiViewEnabled ? 2 : (bIsMultiViewApplication ? 1 : 0);

	// Update the render targets load action to Eload after executing the first pass that is EClear flag to let the RDG merges as many passes as possible.
	auto UpdateRenderTargetsLoadAction = [](FRenderTargetBindingSlots& BasePassRenderTargets)
	{
		if (BasePassRenderTargets[0].GetLoadAction() == ERenderTargetLoadAction::EClear)
		{
			BasePassRenderTargets[0].SetLoadAction(ERenderTargetLoadAction::ELoad);
			BasePassRenderTargets.DepthStencil.SetDepthLoadAction(ERenderTargetLoadAction::ELoad);
			BasePassRenderTargets.DepthStencil.SetStencilLoadAction(ERenderTargetLoadAction::ELoad);
		}
	};

	RDG_EVENT_SCOPE(GraphBuilder, "SceneColorRendering");

	if (GIsEditor && !View.bIsSceneCapture)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets = BasePassRenderTargets;

		GraphBuilder.AddPass(RDG_EVENT_NAME("ClearSceneColorPass"), PassParameters, ERDGPassFlags::Raster,
			[this](FRHICommandListImmediate& RHICmdList)
		{
			DrawClearQuad(RHICmdList, Views[0].BackgroundColor);
		});

		UpdateRenderTargetsLoadAction(BasePassRenderTargets);
	}

	// Depth pre-pass
	GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_MobilePrePass));
	RenderPrePass(GraphBuilder, BasePassRenderTargets, UpdateRenderTargetsLoadAction);
	
	// Opaque and masked
	GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLMM_Opaque));
	RenderMobileBasePass(GraphBuilder, BasePassRenderTargets, UpdateRenderTargetsLoadAction, ViewList);
	AddDispatchToRHIThreadPass(GraphBuilder);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (ViewFamily.UseDebugViewPS())
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets = BasePassRenderTargets;

		// Here we use the base pass depth result to get z culling for opaque and masque.
		// The color needs to be cleared at this point since shader complexity renders in additive.
		GraphBuilder.AddPass(RDG_EVENT_NAME("ClearDebugViewPass"), PassParameters, ERDGPassFlags::Raster,
			[this](FRHICommandListImmediate& RHICmdList)
		{
			DrawClearQuad(RHICmdList, FLinearColor::Black);
		});

		RenderMobileDebugView(GraphBuilder, ViewList);
		AddDispatchToRHIThreadPass(GraphBuilder);
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	const bool bAdrenoOcclusionMode = CVarMobileAdrenoOcclusionMode.GetValueOnRenderThread() != 0;
	if (!bAdrenoOcclusionMode)
	{
		// Issue occlusion queries
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLMM_Occlusion));
		RenderOcclusion(GraphBuilder, BasePassRenderTargets);
		AddDispatchToRHIThreadPass(GraphBuilder);
	}

	if (ViewFamily.ViewExtensions.Num() > 1)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FMobilePostBasePassViewExtensionParameters>();
		PassParameters->SceneTextures = MobileSceneTextures;

		GraphBuilder.AddPass(
			{},
			PassParameters,
			ERDGPassFlags::Raster | ERDGPassFlags::NeverCull,
			[this](FRHICommandListImmediate& RHICmdList)
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(ViewExtensionPostRenderBasePass);
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FMobileSceneRenderer_ViewExtensionPostRenderBasePass);
			for (int32 ViewExt = 0; ViewExt < ViewFamily.ViewExtensions.Num(); ++ViewExt)
			{
				for (int32 ViewIndex = 0; ViewIndex < ViewFamily.Views.Num(); ++ViewIndex)
				{
					ViewFamily.ViewExtensions[ViewExt]->PostRenderBasePass_RenderThread(RHICmdList, Views[ViewIndex]);
				}
			}
		});
	}
	   
	GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLMM_Translucency));

	// Restart translucency render pass if needed
	if (bRequiresMultiPass || bRequiresPixelProjectedPlanarRelfectionPass)
	{
		// Make a copy of the scene depth if the current hardware doesn't support reading and writing to the same depth buffer
		ConditionalResolveSceneDepth(GraphBuilder, View, SceneDepthMSAA);
	}

	if (bRequiresPixelProjectedPlanarRelfectionPass)
	{
		const FPlanarReflectionSceneProxy* PlanarReflectionSceneProxy = Scene ? Scene->GetForwardPassGlobalPlanarReflection() : nullptr;

		FRDGTextureRef PixelProjectedReflectionTexture = GraphBuilder.RegisterExternalTexture(GPixelProjectedReflectionMobileOutputs.PixelProjectedReflectionTexture);

		RenderPixelProjectedReflection(GraphBuilder, SceneColorMSAA.Resolve, SceneDepthMSAA.Resolve, PixelProjectedReflectionTexture, PlanarReflectionSceneProxy);
	}


	if (bRequiresMultiPass || bRequiresPixelProjectedPlanarRelfectionPass)
	{
		FExclusiveDepthStencil::Type ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilRead;
		if (bModulatedShadowsInUse)
		{
			// FIXME: modulated shadows write to stencil
			ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;
		}

		// The opaque meshes used for mobile pixel projected reflection have to write depth to depth RT, since we only render the meshes once if the quality level is less or equal to BestPerformance 
		if (IsMobilePixelProjectedReflectionEnabled(View.GetShaderPlatform())
			&& GetMobilePixelProjectedReflectionQuality() == EMobilePixelProjectedReflectionQuality::BestPerformance)
		{
			ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
		}

		BasePassRenderTargets.DepthStencil.SetDepthStencilAccess(ExclusiveDepthStencil);

		BasePassRenderTargets.NumOcclusionQueries = 0;
		BasePassRenderTargets.SubpassHint = ESubpassHint::DepthReadSubpass;
	}

	{
		auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets = BasePassRenderTargets;

		GraphBuilder.AddPass(RDG_EVENT_NAME("Nextsubpass"), PassParameters, ERDGPassFlags::Raster,
			[this](FRHICommandListImmediate& RHICmdList)
		{
			// scene depth is read only and can be fetched
			RHICmdList.NextSubpass();
		});
	}

	if (!View.bIsPlanarReflection)
	{
		if (ViewFamily.EngineShowFlags.Decals)
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderDecals);
			RenderDecals(GraphBuilder, BasePassRenderTargets, MobileSceneTextures);
		}

		if (ViewFamily.EngineShowFlags.DynamicShadows)
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderShadowProjections);
			RenderModulatedShadowProjections(GraphBuilder, BasePassRenderTargets, MobileSceneTextures);
		}
	}
	
	// Draw translucency.
	if (ViewFamily.EngineShowFlags.Translucency)
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderTranslucency);
		SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);
		RenderTranslucency(GraphBuilder, BasePassRenderTargets, ViewList);
		AddPass(GraphBuilder, [](FRHICommandListImmediate& RHICmdList)
		{
			FRHICommandListExecutor::GetImmediateCommandList().PollOcclusionQueries();
			RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		});
	}

	if (bAdrenoOcclusionMode)
	{
		GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLMM_Occlusion));
		AddPass(GraphBuilder, [this](FRHICommandListImmediate& RHICmdList)
		{
			// flush
			RHICmdList.SubmitCommandsHint();
		});
		bSubmitOffscreenRendering = false; // submit once
		// Issue occlusion queries
		RenderOcclusion(GraphBuilder, BasePassRenderTargets);
		AddDispatchToRHIThreadPass(GraphBuilder);
	}

	// Pre-tonemap before MSAA resolve (iOS only)
	if (!bGammaSpace)
	{
		PreTonemapMSAA(GraphBuilder, BasePassRenderTargets);
	}
}

void FMobileSceneRenderer::RenderDeferred(FRDGBuilder& GraphBuilder, const TArrayView<const FViewInfo*> ViewList, const FSortedLightSetSceneInfo& SortedLightSet, FRDGTextureRef ViewFamilyTexture, FRDGTextureMSAA& SceneColorMSAA, FRDGTextureMSAA& SceneDepthMSAA, TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> MobileSceneTextures)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();

	FRDGTextureRef ColorTargets[5] = {
		SceneColorMSAA.Target,
		GraphBuilder.RegisterExternalTexture(SceneContext.GBufferA),
		GraphBuilder.RegisterExternalTexture(SceneContext.GBufferB),
		GraphBuilder.RegisterExternalTexture(SceneContext.GBufferC),
		GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthAux)
	};

	TArrayView<FRDGTextureRef> BasePassTexturesView = MakeArrayView(ColorTargets, 5);

	FRenderTargetBindingSlots BasePassRenderTargets = GetRenderTargetBindings(ERenderTargetLoadAction::EClear, BasePassTexturesView);
	BasePassRenderTargets.DepthStencil = FDepthStencilBinding(SceneDepthMSAA.Target, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);
	BasePassRenderTargets.SubpassHint = ESubpassHint::DeferredShadingSubpass;
	BasePassRenderTargets.NumOcclusionQueries = ComputeNumOcclusionQueriesToBatch();
	BasePassRenderTargets.FoveationTexture = nullptr;
	BasePassRenderTargets.MultiViewCount = 0;

	// Update the render targets load action to Eload after adding the first pass with EClear flag to let the RDG merge all passes together.
	auto UpdateRenderTargetsLoadAction = [](FRenderTargetBindingSlots& BasePassRenderTargets)
	{
		if (BasePassRenderTargets[0].GetLoadAction() == ERenderTargetLoadAction::EClear)
		{
			for (uint32 i = 0; i < 5; ++i)
			{
				BasePassRenderTargets[i].SetLoadAction(ERenderTargetLoadAction::ELoad);
			}
			
			BasePassRenderTargets.DepthStencil.SetDepthLoadAction(ERenderTargetLoadAction::ELoad);
			BasePassRenderTargets.DepthStencil.SetStencilLoadAction(ERenderTargetLoadAction::ELoad);
		}
	};

	RDG_EVENT_SCOPE(GraphBuilder, "SceneColorRendering");
	
	if (GIsEditor && !Views[0].bIsSceneCapture)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets = BasePassRenderTargets;

		GraphBuilder.AddPass(RDG_EVENT_NAME("ClearSceneColorPass"), PassParameters, ERDGPassFlags::Raster,
			[this](FRHICommandListImmediate& RHICmdList)
		{
			DrawClearQuad(RHICmdList, Views[0].BackgroundColor);
		});

		UpdateRenderTargetsLoadAction(BasePassRenderTargets);
	}

	GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLM_MobilePrePass));
	// Depth pre-pass
	RenderPrePass(GraphBuilder, BasePassRenderTargets, UpdateRenderTargetsLoadAction);
	
	// Opaque and masked
	GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLMM_Opaque));
	RenderMobileBasePass(GraphBuilder, BasePassRenderTargets, UpdateRenderTargetsLoadAction, ViewList);
	AddDispatchToRHIThreadPass(GraphBuilder);

	// Issue occlusion queries
	GraphBuilder.SetCommandListStat(GET_STATID(STAT_CLMM_Occlusion));
	RenderOcclusion(GraphBuilder, BasePassRenderTargets);
	AddDispatchToRHIThreadPass(GraphBuilder);

	auto PollOcclusionQueriesAndDispatchToRHIThreadPass = [](FRHICommandListImmediate& InRHICmdList)
	{
		FRHICommandListExecutor::GetImmediateCommandList().PollOcclusionQueries();
		InRHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	};

	if (!bRequiresMultiPass)
	{
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
			PassParameters->RenderTargets = BasePassRenderTargets;

			GraphBuilder.AddPass(RDG_EVENT_NAME("Nextsubpass"), PassParameters, ERDGPassFlags::Raster,
				[this](FRHICommandListImmediate& RHICmdList)
			{
				// SceneColor + GBuffer write, SceneDepth is read only
				RHICmdList.NextSubpass();
			});
		}
		
		if (ViewFamily.EngineShowFlags.Decals)
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderDecals);
			RenderDecals(GraphBuilder, BasePassRenderTargets, MobileSceneTextures);
		}

		{
			auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
			PassParameters->RenderTargets = BasePassRenderTargets;

			GraphBuilder.AddPass(RDG_EVENT_NAME("Nextsubpass"), PassParameters, ERDGPassFlags::Raster,
				[this](FRHICommandListImmediate& RHICmdList)
			{
				// SceneColor write, SceneDepth is read only
				RHICmdList.NextSubpass();
			});
		}
		
		MobileDeferredShadingPass(GraphBuilder, BasePassRenderTargets, MobileSceneTextures, *Scene, *ViewList[0], SortedLightSet);
		// Draw translucency.
		if (ViewFamily.EngineShowFlags.Translucency)
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderTranslucency);
			SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);
			RenderTranslucency(GraphBuilder, BasePassRenderTargets, ViewList);
			AddPass(GraphBuilder, PollOcclusionQueriesAndDispatchToRHIThreadPass);
		}
	}
	else
	{
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
			PassParameters->RenderTargets = BasePassRenderTargets;

			GraphBuilder.AddPass(RDG_EVENT_NAME("Nextsubpass"), PassParameters, ERDGPassFlags::Raster,
				[this](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.NextSubpass();
				RHICmdList.NextSubpass();
			});
		}
		
		// SceneColor + GBuffer write, SceneDepth is read only
		{
			BasePassRenderTargets.DepthStencil.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilRead);
			BasePassRenderTargets.SubpassHint = ESubpassHint::None;
			BasePassRenderTargets.NumOcclusionQueries = 0;
			
			RDG_EVENT_SCOPE(GraphBuilder, "AfterBasePass");

			if (ViewFamily.EngineShowFlags.Decals)
			{
				CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderDecals);
				RenderDecals(GraphBuilder, BasePassRenderTargets, MobileSceneTextures);
			}
		}

		// SceneColor write, SceneDepth is read only
		{
			for (uint32 i = 1; i < UE_ARRAY_COUNT(ColorTargets); ++i)
			{
				BasePassRenderTargets[i] = FRenderTargetBinding();
			}
			BasePassRenderTargets.DepthStencil.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilWrite);

			RDG_EVENT_SCOPE(GraphBuilder, "MobileShadingPass");
			
			MobileDeferredShadingPass(GraphBuilder, BasePassRenderTargets, MobileSceneTextures, *Scene, *ViewList[0], SortedLightSet);
			// Draw translucency.
			if (ViewFamily.EngineShowFlags.Translucency)
			{
				CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderTranslucency);
				SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);
				RenderTranslucency(GraphBuilder, BasePassRenderTargets, ViewList);
				AddPass(GraphBuilder, PollOcclusionQueriesAndDispatchToRHIThreadPass);
			}
		}
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FMobileDebugViewPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FDebugViewModePassUniformParameters, DebugViewMode)
END_SHADER_PARAMETER_STRUCT()

void FMobileSceneRenderer::RenderMobileDebugView(FRDGBuilder& GraphBuilder, const TArrayView<const FViewInfo*> PassViews)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderDebugView);
	RDG_EVENT_SCOPE(GraphBuilder, "MobileDebugView");
	SCOPE_CYCLE_COUNTER(STAT_BasePassDrawTime);

	for (int32 ViewIndex = 0; ViewIndex < PassViews.Num(); ViewIndex++)
	{
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);
		const FViewInfo& View = *PassViews[ViewIndex];
		if (!View.ShouldRenderView())
		{
			continue;
		}

		auto* PassParameters = GraphBuilder.AllocParameters<FMobileDebugViewPassParameters>();
		PassParameters->DebugViewMode = CreateDebugViewModePassUniformBuffer(GraphBuilder, View);

		GraphBuilder.AddPass(
			{},
			PassParameters,
			ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::NeverCull,
			[&View](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
			View.ParallelMeshDrawCommandPasses[EMeshPass::DebugViewMode].DispatchDraw(nullptr, RHICmdList);
		});
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

int32 FMobileSceneRenderer::ComputeNumOcclusionQueriesToBatch() const
{
	int32 NumQueriesForBatch = 0;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		const FSceneViewState* ViewState = (FSceneViewState*)View.State;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (!ViewState || (!ViewState->HasViewParent() && !ViewState->bIsFrozen))
#endif
		{
			NumQueriesForBatch += View.IndividualOcclusionQueries.GetNumBatchOcclusionQueries();
			NumQueriesForBatch += View.GroupedOcclusionQueries.GetNumBatchOcclusionQueries();
		}
	}
	
	return NumQueriesForBatch;
}

// Whether we need a separate render-passes for translucency, decals etc
bool FMobileSceneRenderer::RequiresMultiPass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View) const
{
	// Vulkan uses subpasses
	if (IsVulkanPlatform(ShaderPlatform))
	{
		return false;
	}

	// All iOS support frame_buffer_fetch
	if (IsMetalMobilePlatform(ShaderPlatform))
	{
		return false;
	}

	if (IsMobileDeferredShadingEnabled(ShaderPlatform))
	{
		// TODO: add GL support
		return true;
	}
	
	// Some Androids support frame_buffer_fetch
	if (IsAndroidOpenGLESPlatform(ShaderPlatform) && (GSupportsShaderFramebufferFetch || GSupportsShaderDepthStencilFetch))
	{
		return false;
	}
		
	// Always render reflection capture in single pass
	if (View.bIsPlanarReflection || View.bIsSceneCapture)
	{
		return false;
	}

	// Always render LDR in single pass
	if (!IsMobileHDR())
	{
		return false;
	}

	// MSAA depth can't be sampled or resolved, unless we are on PC (no vulkan)
	if (NumMSAASamples > 1 && !IsSimulatedPlatform(ShaderPlatform))
	{
		return false;
	}

	return true;
}


void FMobileSceneRenderer::ConditionalResolveSceneDepth(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureMSAA& SceneDepth)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
	
	if (IsSimulatedPlatform(ShaderPlatform)) // mobile emulation on PC
	{
		// resolve MSAA depth for translucency
		AddResolveSceneDepthPass(GraphBuilder, View, SceneDepth);
	}
	else if (IsAndroidOpenGLESPlatform(ShaderPlatform))
	{
		const bool bSceneDepthInAlpha = (SceneContext.GetSceneColor()->GetDesc().Format == PF_FloatRGBA);
		const bool bAlwaysResolveDepth = CVarMobileAlwaysResolveDepth.GetValueOnRenderThread() == 1;
		// Only these features require depth texture
		bool bDecals = ViewFamily.EngineShowFlags.Decals && Scene->Decals.Num();
		bool bModulatedShadows = ViewFamily.EngineShowFlags.DynamicShadows && bModulatedShadowsInUse;

		if (bDecals || bModulatedShadows || bAlwaysResolveDepth || View.bUsesSceneDepth || bRequiresPixelProjectedPlanarRelfectionPass)
		{
			// WEBGL copies depth from SceneColor alpha to a separate texture
			// Switch target to force hardware flush current depth to texture
			FRDGTextureRef DummySceneColor = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy, ERenderTargetTexture::Targetable);
			FRDGTextureRef DummyDepthTarget = GraphBuilder.RegisterExternalTexture(GSystemTextures.DepthDummy, ERenderTargetTexture::Targetable);

			auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();

			PassParameters->RenderTargets[0] = FRenderTargetBinding(DummySceneColor, ERenderTargetLoadAction::ENoAction);
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(DummyDepthTarget, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ResolveDepthPass"), PassParameters, ERDGPassFlags::Raster,
				[&View, &SceneContext](FRHICommandListImmediate& RHICmdList)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				// for devices that do not support framebuffer fetch we rely on undocumented behavior:
				// Depth reading features will have the depth bound as an attachment AND as a sampler this means
				// some driver implementations will ignore our attempts to resolve, here we draw with the depth texture to force a resolve.
				// See UE-37809 for a description of the desired fix.
				// The results of this draw are irrelevant.
				TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);
				TShaderMapRef<FScreenPS> PixelShader(View.ShaderMap);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ScreenVertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				ScreenVertexShader->SetParameters(RHICmdList, View.ViewUniformBuffer);
				PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SceneContext.GetSceneDepthTexture());
				DrawRectangle(
					RHICmdList,
					0, 0,
					0, 0,
					0, 0,
					1, 1,
					FIntPoint(1, 1),
					FIntPoint(1, 1),
					ScreenVertexShader,
					EDRF_UseTriangleOptimization);
			}); // force depth resolve
		}
	}
}

void FMobileSceneRenderer::UpdateOpaqueBasePassUniformBuffer(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	FMobileBasePassUniformParameters Parameters;
	SetupMobileBasePassUniformParameters(RHICmdList, View, false, false, Parameters);
	Scene->UniformBuffers.MobileOpaqueBasePassUniformBuffer.UpdateUniformBufferImmediate(Parameters);
	SetupMobileBasePassUniformParameters(RHICmdList, View, false, true, Parameters);
	Scene->UniformBuffers.MobileCSMOpaqueBasePassUniformBuffer.UpdateUniformBufferImmediate(Parameters);	
}

void FMobileSceneRenderer::UpdateTranslucentBasePassUniformBuffer(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	FMobileBasePassUniformParameters Parameters;
	SetupMobileBasePassUniformParameters(RHICmdList, View, true, false, Parameters);
	Scene->UniformBuffers.MobileTranslucentBasePassUniformBuffer.UpdateUniformBufferImmediate(Parameters);
}

void FMobileSceneRenderer::UpdateDirectionalLightUniformBuffers(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	bool bDynamicShadows = ViewFamily.EngineShowFlags.DynamicShadows;
	// Fill in the other entries based on the lights
	for (int32 ChannelIdx = 0; ChannelIdx < UE_ARRAY_COUNT(Scene->MobileDirectionalLights); ChannelIdx++)
	{
		FMobileDirectionalLightShaderParameters Params;
		SetupMobileDirectionalLightUniformParameters(*Scene, View, VisibleLightInfos, ChannelIdx, bDynamicShadows, Params);
		Scene->UniformBuffers.MobileDirectionalLightUniformBuffers[ChannelIdx + 1].UpdateUniformBufferImmediate(Params);
	}
}

void FMobileSceneRenderer::UpdateSkyReflectionUniformBuffer()
{
	FSkyLightSceneProxy* SkyLight = nullptr;
	if (Scene->ReflectionSceneData.RegisteredReflectionCapturePositions.Num() == 0
		&& Scene->SkyLight
		&& Scene->SkyLight->ProcessedTexture
		&& Scene->SkyLight->ProcessedTexture->TextureRHI
		// Don't use skylight reflection if it is a static sky light for keeping coherence with PC.
		&& !Scene->SkyLight->bHasStaticLighting)
	{
		SkyLight = Scene->SkyLight;
	}

	FMobileReflectionCaptureShaderParameters Parameters;
	SetupMobileSkyReflectionUniformParameters(SkyLight, Parameters);
	Scene->UniformBuffers.MobileSkyReflectionUniformBuffer.UpdateUniformBufferImmediate(Parameters);
}

class FPreTonemapMSAA_Mobile : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPreTonemapMSAA_Mobile, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMetalMobilePlatform(Parameters.Platform);
	}	

	FPreTonemapMSAA_Mobile() {}

public:
	FPreTonemapMSAA_Mobile(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

IMPLEMENT_SHADER_TYPE(, FPreTonemapMSAA_Mobile,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("PreTonemapMSAA_Mobile"),SF_Pixel);

void FMobileSceneRenderer::PreTonemapMSAA(FRDGBuilder& GraphBuilder, FRenderTargetBindingSlots& BasePassRenderTargets)
{
	// iOS only
	bool bOnChipPP = GSupportsRenderTargetFormat_PF_FloatRGBA && GSupportsShaderFramebufferFetch &&	ViewFamily.EngineShowFlags.PostProcessing;
	bool bOnChipPreTonemapMSAA = bOnChipPP && IsMetalMobilePlatform(ViewFamily.GetShaderPlatform()) && (NumMSAASamples > 1);
	if (!bOnChipPreTonemapMSAA)
	{
		return;
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
	const FIntPoint TargetSize = SceneContext.GetBufferSizeXY();

	auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	PassParameters->RenderTargets = BasePassRenderTargets;

	const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FPreTonemapMSAA_Mobile> PixelShader(ShaderMap);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("PreTonemapMSAAPass"),
		PassParameters,
		ERDGPassFlags::Raster,
		[this, TargetSize, VertexShader, PixelShader](FRHICommandListImmediate& RHICmdList)
	{

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		RHICmdList.SetViewport(0, 0, 0.0f, TargetSize.X, TargetSize.Y, 1.0f);

		DrawRectangle(
			RHICmdList,
			0, 0,
			TargetSize.X, TargetSize.Y,
			0, 0,
			TargetSize.X, TargetSize.Y,
			TargetSize,
			TargetSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	});
}

/** Before SetupMobileBasePassAfterShadowInit, we need to update the uniform buffer and shadow info for all movable point lights.*/
void FMobileSceneRenderer::UpdateMovablePointLightUniformBufferAndShadowInfo()
{
	static auto* MobileNumDynamicPointLightsCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileNumDynamicPointLights"));
	const int32 MobileNumDynamicPointLights = MobileNumDynamicPointLightsCVar->GetValueOnRenderThread();

	static auto* MobileEnableMovableSpotlightsCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.EnableMovableSpotlights"));
	const int32 MobileEnableMovableSpotlights = MobileEnableMovableSpotlightsCVar->GetValueOnRenderThread();

	static auto* EnableMovableSpotlightShadowsCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.EnableMovableSpotlightsShadow"));
	const int32 EnableMovableSpotlightShadows = EnableMovableSpotlightShadowsCVar->GetValueOnRenderThread();

	if (MobileNumDynamicPointLights > 0)
	{
		bool bShouldDynamicShadows = ViewFamily.EngineShowFlags.DynamicShadows
			&& !IsSimpleForwardShadingEnabled(ShaderPlatform)
			&& GetShadowQuality() > 0
			&& EnableMovableSpotlightShadows != 0;

		for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
		{
			const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
			FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

			FLightSceneProxy* LightProxy = LightSceneInfo->Proxy;
			const uint8 LightType = LightProxy->GetLightType();

			const bool bIsValidLightType =
				LightType == LightType_Point
				|| LightType == LightType_Rect
				|| (LightType == LightType_Spot && MobileEnableMovableSpotlights);

			if (bIsValidLightType && LightProxy->IsMovable())
			{
				LightSceneInfo->ConditionalUpdateMobileMovablePointLightUniformBuffer(this);

				bool bDynamicShadows = bShouldDynamicShadows
					&& LightType == LightType_Spot
					&& VisibleLightInfos[LightSceneInfo->Id].AllProjectedShadows.Num() > 0
					&& VisibleLightInfos[LightSceneInfo->Id].AllProjectedShadows.Last()->bAllocated;

				if (bDynamicShadows)
				{
					FProjectedShadowInfo *ProjectedShadowInfo = VisibleLightInfos[LightSceneInfo->Id].AllProjectedShadows.Last();
					checkSlow(ProjectedShadowInfo && ProjectedShadowInfo->CacheMode != SDCM_StaticPrimitivesOnly);

					const FIntPoint ShadowBufferResolution = ProjectedShadowInfo->GetShadowBufferResolution();
					
					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
					{
						FViewInfo& View = Views[ViewIndex];

						FMobileMovableSpotLightsShadowInfo& MobileMovableSpotLightsShadowInfo = View.MobileMovableSpotLightsShadowInfo;

						checkSlow(MobileMovableSpotLightsShadowInfo.ShadowDepthTexture == nullptr
							|| MobileMovableSpotLightsShadowInfo.ShadowDepthTexture == ProjectedShadowInfo->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture.GetReference());

						if (MobileMovableSpotLightsShadowInfo.ShadowDepthTexture == nullptr)
						{
							MobileMovableSpotLightsShadowInfo.ShadowDepthTexture = ProjectedShadowInfo->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture.GetReference();
							MobileMovableSpotLightsShadowInfo.ShadowBufferSize = FVector4(ShadowBufferResolution.X, ShadowBufferResolution.Y, 1.0f / ShadowBufferResolution.X, 1.0f / ShadowBufferResolution.Y);
						}
					}
				}
			}
		}
	}
}
