// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MobileShadingRenderer.cpp: Scene rendering code for the ES2 feature level.
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
#include "PostProcess/RenderingCompositionGraph.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/PostProcessMobile.h"
#include "PostProcess/PostProcessUpscale.h"
#include "PostProcess/PostProcessCompositeEditorPrimitives.h"
#include "PostProcess/PostProcessHMD.h"
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

static TAutoConsoleVariable<int32> CVarMobileMoveSubmissionHintAfterTranslucency(
	TEXT("r.Mobile.MoveSubmissionHintAfterTranslucency"),
	1,
	TEXT("0: Submission hint occurs after occlusion query.\n")
	TEXT("1: Submission hint occurs after translucency. (Default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileAdrenoOcclusionMode(
	TEXT("r.Mobile.AdrenoOcclusionMode"),
	0,
	TEXT("0: Render occlusion queries after the base pass (default).\n")
	TEXT("1: Render occlusion queries after translucency and a flush, which can help Adreno devices in GL mode."),
	ECVF_RenderThreadSafe);

DECLARE_CYCLE_STAT(TEXT("SceneStart"), STAT_CLMM_SceneStart, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("SceneEnd"), STAT_CLMM_SceneEnd, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("InitViews"), STAT_CLMM_InitViews, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Opaque"), STAT_CLMM_Opaque, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Occlusion"), STAT_CLMM_Occlusion, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Post"), STAT_CLMM_Post, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Translucency"), STAT_CLMM_Translucency, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("Shadows"), STAT_CLMM_Shadows, STATGROUP_CommandListMarkers);

FGlobalDynamicIndexBuffer FMobileSceneRenderer::DynamicIndexBuffer;
FGlobalDynamicVertexBuffer FMobileSceneRenderer::DynamicVertexBuffer;
TGlobalResource<FGlobalDynamicReadBuffer> FMobileSceneRenderer::DynamicReadBuffer;

static bool UsesCustomDepthStencilLookup(const FViewInfo& View)
{
	// Find out whether post-process materials use CustomDepth/Stencil lookups
	bool bPPUsesCustomDepth = false;
	bool bPPUsesCustomStencil = false;
	const FBlendableManager& BlendableManager = View.FinalPostProcessSettings.BlendableManager;
	FBlendableEntry* BlendableIt = nullptr;

	while (FPostProcessMaterialNode* DataPtr = BlendableManager.IterateBlendables<FPostProcessMaterialNode>(BlendableIt))
	{
		if (DataPtr->IsValid())
		{
			FMaterialRenderProxy* Proxy = DataPtr->GetMaterialInterface()->GetRenderProxy();
			check(Proxy);

			const FMaterial* Material = Proxy->GetMaterial(View.GetFeatureLevel());
			check(Material);
			const FMaterialShaderMap* MaterialShaderMap = Material->GetRenderingThreadShaderMap();

			bPPUsesCustomDepth|= MaterialShaderMap->UsesSceneTexture(PPI_CustomDepth);
			bPPUsesCustomStencil|= MaterialShaderMap->UsesSceneTexture(PPI_CustomStencil);
		}
	}

	//TODO: check if translucency uses CustomDepth
	return bPPUsesCustomDepth || bPPUsesCustomStencil;
}


FMobileSceneRenderer::FMobileSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer)
	:	FSceneRenderer(InViewFamily, HitProxyConsumer)
{
	bModulatedShadowsInUse = false;
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
		MobileCSMVisibilityInfo.MobileCSMStaticBatchVisibility.AddZeroed(View.StaticMeshBatchVisibility.Num());

		// Init static mesh visibility info for default drawlist that excludes meshes in CSM only drawlist.
		MobileCSMVisibilityInfo.MobileNonCSMStaticMeshVisibilityMap = View.StaticMeshVisibilityMap;
		MobileCSMVisibilityInfo.MobileNonCSMStaticBatchVisibility = View.StaticMeshBatchVisibility;
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
void FMobileSceneRenderer::InitViews(FRHICommandListImmediate& RHICmdList)
{
	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_InitViews));

	SCOPED_DRAW_EVENT(RHICmdList, InitViews);

	SCOPE_CYCLE_COUNTER(STAT_InitViewsTime);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(InitViews_Scene);

	FILCUpdatePrimTaskData ILCTaskData;
	FViewVisibleCommandsPerView ViewCommandsPerView;
	ViewCommandsPerView.SetNum(Views.Num());

	const FExclusiveDepthStencil::Type BasePassDepthStencilAccess = FExclusiveDepthStencil::DepthWrite_StencilWrite;

	PreVisibilityFrameSetup(RHICmdList);
	ComputeViewVisibility(RHICmdList, BasePassDepthStencilAccess, ViewCommandsPerView, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer);

	// Initialise Sky/View resources before the view global uniform buffer is built.
	if (Scene && ShouldRenderSkyAtmosphere(Scene->GetSkyAtmosphereSceneInfo(), Scene->GetShaderPlatform()))
	{
		InitSkyAtmosphereForViews(RHICmdList);
	}

	PostVisibilityFrameSetup(ILCTaskData);

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

	SetupMobileBasePassAfterShadowInit(BasePassDepthStencilAccess, ViewCommandsPerView);

	// if we kicked off ILC update via task, wait and finalize.
	if (ILCTaskData.TaskRef.IsValid())
	{
		Scene->IndirectLightingCache.FinalizeCacheUpdates(Scene, *this, ILCTaskData);
	}

	// initialize per-view uniform buffer.  Pass in shadow info as necessary.
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		// Initialize the view's RHI resources.
		Views[ViewIndex].InitRHIResources();

		// TODO: remove when old path is removed
		// Create the directional light uniform buffers
		CreateDirectionalLightUniformBuffers(Views[ViewIndex]);
	}
	
	UpdateGPUScene(RHICmdList, *Scene);
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		UploadDynamicPrimitiveShaderDataForView(RHICmdList, *Scene, Views[ViewIndex]);
	}
	

	// update buffers used in cached mesh path
	// in case there are multiple views, these buffers will be updated before rendering each view
	if (Views.Num() > 0)
	{
		const FViewInfo& View = Views[0];
		Scene->UniformBuffers.UpdateViewUniformBuffer(View);
		UpdateOpaqueBasePassUniformBuffer(RHICmdList, View);
		UpdateTranslucentBasePassUniformBuffer(RHICmdList, View);
		UpdateDirectionalLightUniformBuffers(RHICmdList, View);

		FMobileDistortionPassUniformParameters DistortionPassParameters;
		SetupMobileDistortionPassUniformBuffer(RHICmdList, View, DistortionPassParameters);
		Scene->UniformBuffers.MobileDistortionPassUniformBuffer.UpdateUniformBufferImmediate(DistortionPassParameters);
	}
	UpdateSkyReflectionUniformBuffer();

	// Now that the indirect lighting cache is updated, we can update the uniform buffers.
	UpdatePrimitiveIndirectLightingCacheBuffers();
	
	OnStartRender(RHICmdList);
}

/** 
* Renders the view family. 
*/
void FMobileSceneRenderer::Render(FRHICommandListImmediate& RHICmdList)
{
	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_SceneStart));

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
	else
	{
		Scene->ResetAtmosphereLightsProperties();
	}

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderOther);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMobileSceneRenderer_Render);
	//FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

	if(!ViewFamily.EngineShowFlags.Rendering)
	{
		return;
	}

	WaitOcclusionTests(RHICmdList);
	FRHICommandListExecutor::GetImmediateCommandList().PollOcclusionQueries();
	RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);


	const ERHIFeatureLevel::Type ViewFeatureLevel = ViewFamily.GetFeatureLevel();

	// Initialize global system textures (pass-through if already initialized).
	GSystemTextures.InitializeTextures(RHICmdList, ViewFeatureLevel);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// Allocate the maximum scene render target space for the current view family.
	SceneContext.Allocate(RHICmdList, this);

	//make sure all the targets we're going to use will be safely writable.
	GRenderTargetPool.TransitionTargetsWritable(RHICmdList);

	// Find the visible primitives.
	InitViews(RHICmdList);

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

	// Notify the FX system that the scene is about to be rendered.
	if (Scene->FXSystem && ViewFamily.EngineShowFlags.Particles)
	{
		Scene->FXSystem->PreRender(RHICmdList, NULL, !Views[0].bIsPlanarReflection);
	}
	FRHICommandListExecutor::GetImmediateCommandList().PollOcclusionQueries();
	RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_Shadows));

	RenderShadowDepthMaps(RHICmdList);
	FRHICommandListExecutor::GetImmediateCommandList().PollOcclusionQueries();
	RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

	// This might eventually be a problem with multiple views.
	// Using only view 0 to check to do on-chip transform of alpha.
	FViewInfo& View = Views[0];

	// Default view list
	TArray<const FViewInfo*> ViewList;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++) 
	{
		ViewList.Add(&Views[ViewIndex]);
	}

	const bool bGammaSpace = !IsMobileHDR();

	// Generate the Sky/Atmosphere look up tables
	const bool bShouldRenderSkyAtmosphere = ShouldRenderSkyAtmosphere(Scene->GetSkyAtmosphereSceneInfo(), Scene->GetShaderPlatform());
	if (bShouldRenderSkyAtmosphere)
	{
		RenderSkyAtmosphereLookUpTables(RHICmdList);
	}
	
	// Custom depth
	if (!bGammaSpace)
	{
		// see if anything uses CustomDepth, and skip custom depth rendering if possible
		bool bUsesCustomDepthStencil = false;
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++) 
		{
			bUsesCustomDepthStencil = UsesCustomDepthStencilLookup(Views[ViewIndex]);
		}

		if (bUsesCustomDepthStencil)
		{
			RenderCustomDepthPass(RHICmdList);
		}
	}
		
	const FIntPoint RenderTargetSize = (ViewFamily.RenderTarget->GetRenderTargetTexture().IsValid()) ? ViewFamily.RenderTarget->GetRenderTargetTexture()->GetSizeXY() : ViewFamily.RenderTarget->GetSizeXY();
	const bool bRequiresUpscale = ((int32)RenderTargetSize.X > FamilySize.X || (int32)RenderTargetSize.Y > FamilySize.Y);

	// ES2 requires that the back buffer and depth match dimensions.
	// For the most part this is not the case when using scene captures. Thus scene captures always render to scene color target.
	const bool bStereoRenderingAndHMD = View.Family->EngineShowFlags.StereoRendering && View.Family->EngineShowFlags.HMDDistortion;
	const bool bRenderToSceneColor = bStereoRenderingAndHMD || bRequiresUpscale || FSceneRenderer::ShouldCompositeEditorPrimitives(View) || View.bIsSceneCapture || View.bIsReflectionCapture;

	// Whether we need to render translucency in a separate render pass
	// On mobile it's better to render as much as possible in a single pass
	const bool bRequiresTranslucencyPass = RequiresTranslucencyPass(RHICmdList, View);
	// Whether we need to store depth for post-processing
	// On PowerVR we see flickering of shadows and depths not updating correctly if targets are discarded.
	// See CVarMobileForceDepthResolve use in ConditionalResolveSceneDepth.
	const bool bForceDepthResolve = CVarMobileForceDepthResolve.GetValueOnRenderThread() == 1;
	const bool bSeparateTranslucencyActive = IsMobileSeparateTranslucencyActive(View);
	bool bKeepDepthContent = bRenderToSceneColor && 
		(bForceDepthResolve || bSeparateTranslucencyActive || (View.bIsSceneCapture && (ViewFamily.SceneCaptureSource == ESceneCaptureSource::SCS_SceneColorHDR || ViewFamily.SceneCaptureSource == ESceneCaptureSource::SCS_SceneColorSceneDepth)));

	//
	FRHITexture* SceneColor = nullptr;
	FRHITexture* SceneColorResolve = nullptr;
	FRHITexture* SceneDepth = nullptr;
	ERenderTargetActions ColorTargetAction = ERenderTargetActions::Clear_Store;
	EDepthStencilTargetActions DepthTargetAction = EDepthStencilTargetActions::ClearDepthStencil_DontStoreDepthStencil;
	bool bMobileMSAA = SceneContext.GetSceneColorSurface()->GetNumSamples() > 1;
	
	if (bGammaSpace && !bRenderToSceneColor)
	{
		if (bMobileMSAA)
		{
			SceneColor = (View.bIsMobileMultiViewEnabled) ? SceneContext.MobileMultiViewSceneColor->GetRenderTargetItem().TargetableTexture : SceneContext.GetSceneColorSurface();
			SceneColorResolve = GetMultiViewSceneColor(SceneContext);
			ColorTargetAction = ERenderTargetActions::Clear_Resolve;
			// Rendering to a backbuffer, make sure it's writeable
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, SceneColorResolve);
		}
		else
		{
			SceneColor = GetMultiViewSceneColor(SceneContext);
			// Rendering to a backbuffer, make sure it's writeable
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, SceneColor);
		}
		SceneDepth = (View.bIsMobileMultiViewEnabled) ? SceneContext.MobileMultiViewSceneDepthZ->GetRenderTargetItem().TargetableTexture : static_cast<FTextureRHIRef>(SceneContext.GetSceneDepthSurface());
	}
	else
	{
		SceneColor = SceneContext.GetSceneColorSurface();
		SceneColorResolve = bMobileMSAA ? SceneContext.GetSceneColorTexture() : nullptr;
		ColorTargetAction = bMobileMSAA ? ERenderTargetActions::Clear_Resolve : ERenderTargetActions::Clear_Store;
		SceneDepth = SceneContext.GetSceneDepthSurface();
				
		if (bRequiresTranslucencyPass)
		{	
			// store targets after opaque so translucency render pass can be restarted
			ColorTargetAction = ERenderTargetActions::Clear_Store;
			DepthTargetAction = EDepthStencilTargetActions::ClearDepthStencil_StoreDepthStencil;
		}
						
		if (bKeepDepthContent && !bMobileMSAA)
		{
			// store depth if post-processing/capture needs it
			DepthTargetAction = EDepthStencilTargetActions::ClearDepthStencil_StoreDepthStencil;
		}
	}

	FRHIRenderPassInfo SceneColorRenderPassInfo(
		SceneColor,
		ColorTargetAction,
		SceneColorResolve,
		SceneDepth,
		DepthTargetAction, 
		nullptr, // we never resolve scene depth on mobile
		FExclusiveDepthStencil::DepthWrite_StencilWrite
	);
	SceneColorRenderPassInfo.SubpassHint = ESubpassHint::DepthReadSubpass;
	SceneColorRenderPassInfo.NumOcclusionQueries = ComputeNumOcclusionQueriesToBatch();
	SceneColorRenderPassInfo.bOcclusionQueries = SceneColorRenderPassInfo.NumOcclusionQueries != 0;
	SceneColorRenderPassInfo.bMultiviewPass = View.bIsMobileMultiViewEnabled;
	RHICmdList.BeginRenderPass(SceneColorRenderPassInfo, TEXT("SceneColorRendering"));

	if (GIsEditor && !View.bIsSceneCapture)
	{
		DrawClearQuad(RHICmdList, Views[0].BackgroundColor);
	}
	
	// Opaque and masked
	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_Opaque));
	RenderMobileBasePass(RHICmdList, ViewList);
	RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (ViewFamily.UseDebugViewPS())
	{
		// Here we use the base pass depth result to get z culling for opaque and masque.
		// The color needs to be cleared at this point since shader complexity renders in additive.
		DrawClearQuad(RHICmdList, FLinearColor::Black);
		RenderMobileDebugView(RHICmdList, ViewList);
		RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	const bool bAdrenoOcclusionMode = CVarMobileAdrenoOcclusionMode.GetValueOnRenderThread() != 0;
	if (!bAdrenoOcclusionMode)
	{
	    // Issue occlusion queries
	    RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_Occlusion));
	    RenderOcclusion(RHICmdList);
	    RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}

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
	}

	// scene depth is read only and can be fetched
	RHICmdList.NextSubpass();
		
	// Split if we need to render translucency in a separate render pass
	if (bRequiresTranslucencyPass)
	{
		RHICmdList.EndRenderPass();
	}
	   
	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_Translucency));

		
	// Restart translucency render pass if needed
	if (bRequiresTranslucencyPass)
	{
		check(RHICmdList.IsOutsideRenderPass());

		// Make a copy of the scene depth if the current hardware doesn't support reading and writing to the same depth buffer
		ConditionalResolveSceneDepth(RHICmdList, View);

		DepthTargetAction = EDepthStencilTargetActions::LoadDepthStencil_DontStoreDepthStencil;
		FExclusiveDepthStencil::Type ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilRead;
		if (bModulatedShadowsInUse)
		{
			// FIXME: modulated shadows write to stencil
			ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;
		}
		
		if (bKeepDepthContent && !bMobileMSAA)
		{
			DepthTargetAction = EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil;
		}

		FRHIRenderPassInfo TranslucentRenderPassInfo(
			SceneColor,
			SceneColorResolve ? ERenderTargetActions::Load_Resolve : ERenderTargetActions::Load_Store,
			SceneColorResolve,
			SceneDepth,
			DepthTargetAction, 
			nullptr,
			ExclusiveDepthStencil
		);
		TranslucentRenderPassInfo.NumOcclusionQueries = 0;
		TranslucentRenderPassInfo.bOcclusionQueries = false;
		RHICmdList.BeginRenderPass(TranslucentRenderPassInfo, TEXT("SceneColorTranslucencyRendering"));
	}
			
	if (!View.bIsPlanarReflection)
	{
		if (ViewFamily.EngineShowFlags.Decals)
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderDecals);
			RenderDecals(RHICmdList);
		}

		if (ViewFamily.EngineShowFlags.DynamicShadows)
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderShadowProjections);
			RenderModulatedShadowProjections(RHICmdList);
		}
	}
	
	// Draw translucency.
	if (ViewFamily.EngineShowFlags.Translucency)
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderTranslucency);
		SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);
		RenderTranslucency(RHICmdList, ViewList, !bGammaSpace || bRenderToSceneColor);
		FRHICommandListExecutor::GetImmediateCommandList().PollOcclusionQueries();
		RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}

	if (bAdrenoOcclusionMode)
	{
	    RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_Occlusion));
		// flush
		RHICmdList.SubmitCommandsHint();
		// Issue occlusion queries
	    RenderOcclusion(RHICmdList);
	    RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}

	// Pre-tonemap before MSAA resolve (iOS only)
	if (!bGammaSpace)
	{
		PreTonemapMSAA(RHICmdList);
	}
	
	// End of scene color rendering
	RHICmdList.EndRenderPass();

	if (!bGammaSpace || bRenderToSceneColor)
	{
		// transition scene color to Readable for post-processing
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, SceneColor);
	}

	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_Post));

	if (!View.bIsMobileMultiViewDirectEnabled)
	{
		CopyMobileMultiViewSceneColor(RHICmdList);
	}

	if (ViewFamily.bResolveScene)
	{
		if (!bGammaSpace)
		{
			// Finish rendering for each view, or the full stereo buffer if enabled
			{
				SCOPED_DRAW_EVENT(RHICmdList, PostProcessing);
				SCOPE_CYCLE_COUNTER(STAT_FinishRenderViewTargetTime);
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);
					GPostProcessing.ProcessES2(RHICmdList, Scene, Views[ViewIndex]);
				}
			}
		}
		else if (bRenderToSceneColor)
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				BasicPostProcess(RHICmdList, Views[ViewIndex], bRequiresUpscale, FSceneRenderer::ShouldCompositeEditorPrimitives(Views[ViewIndex]));
			}
		}
	}

	RHICmdList.SetCurrentStat(GET_STATID(STAT_CLMM_SceneEnd));

	RenderFinish(RHICmdList);

	FRHICommandListExecutor::GetImmediateCommandList().PollOcclusionQueries();
	FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
}

// Perform simple upscale and/or editor primitive composite if the fully-featured post process is not in use.
void FMobileSceneRenderer::BasicPostProcess(FRHICommandListImmediate& RHICmdList, FViewInfo &View, bool bDoUpscale, bool bDoEditorPrimitives)
{
	FRenderingCompositePassContext CompositeContext(RHICmdList, View);
	FPostprocessContext Context(RHICmdList, CompositeContext.Graph, View);

	const bool bBlitRequired = !bDoUpscale && !bDoEditorPrimitives;

	if (bDoUpscale || bBlitRequired)
	{	// blit from sceneRT to view family target, simple bilinear if upscaling otherwise point filtered.
		uint32 UpscaleQuality = bDoUpscale ? 1 : 0;
		FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessUpscaleES2(View, UpscaleQuality, false));

		Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
		Node->SetInput(ePId_Input1, FRenderingCompositeOutputRef(Context.FinalOutput));

		Context.FinalOutput = FRenderingCompositeOutputRef(Node);
	}

#if WITH_EDITOR
	// Composite editor primitives if we had any to draw and compositing is enabled
	if (bDoEditorPrimitives)
	{
		FRenderingCompositePass* EditorCompNode = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessCompositeEditorPrimitives(false));
		EditorCompNode->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
		//Node->SetInput(ePId_Input1, FRenderingCompositeOutputRef(Context.SceneDepth));
		Context.FinalOutput = FRenderingCompositeOutputRef(EditorCompNode);
	}
#endif

	bool bStereoRenderingAndHMD = View.Family->EngineShowFlags.StereoRendering && View.Family->EngineShowFlags.HMDDistortion;
	if (bStereoRenderingAndHMD)
	{
		const IHeadMountedDisplay* HMD =  GEngine->XRSystem->GetHMDDevice();
		checkf(HMD, TEXT("EngineShowFlags.HMDDistortion can not be true when IXRTrackingSystem::GetHMDDevice returns null"));
		FRenderingCompositePass* Node = Context.Graph.RegisterPass(new FRCPassPostProcessHMD());

		if (Node)
		{
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}
	}

	// currently created on the heap each frame but View.Family->RenderTarget could keep this object and all would be cleaner
	TRefCountPtr<IPooledRenderTarget> Temp;
	FSceneRenderTargetItem Item;
	Item.TargetableTexture = (FTextureRHIRef&)View.Family->RenderTarget->GetRenderTargetTexture();
	Item.ShaderResourceTexture = (FTextureRHIRef&)View.Family->RenderTarget->GetRenderTargetTexture();

	FPooledRenderTargetDesc Desc;

	Desc.Extent = View.Family->RenderTarget->GetSizeXY();
	// todo: this should come from View.Family->RenderTarget
	Desc.Format = PF_B8G8R8A8;
	Desc.NumMips = 1;

	GRenderTargetPool.CreateUntrackedElement(Desc, Temp, Item);

	Context.FinalOutput.GetOutput()->PooledRenderTarget = Temp;
	Context.FinalOutput.GetOutput()->RenderTargetDesc = Desc;

	CompositeContext.Process(Context.FinalOutput.GetPass(), TEXT("ES2BasicPostProcess"));
}

void FMobileSceneRenderer::RenderMobileDebugView(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderDebugView);
	SCOPED_DRAW_EVENT(RHICmdList, MobileDebugView);
	SCOPE_CYCLE_COUNTER(STAT_BasePassDrawTime);

	for (int32 ViewIndex = 0; ViewIndex < PassViews.Num(); ViewIndex++)
	{
		SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);
		const FViewInfo& View = *PassViews[ViewIndex];
		if (!View.ShouldRenderView())
		{
			continue;
		}

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		FDebugViewModePassPassUniformParameters PassParameters;
		SetupDebugViewModePassUniformBuffer(SceneContext, View.GetFeatureLevel(), PassParameters);
		Scene->UniformBuffers.DebugViewModePassUniformBuffer.UpdateUniformBufferImmediate(PassParameters);
		
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
		View.ParallelMeshDrawCommandPasses[EMeshPass::DebugViewMode].DispatchDraw(nullptr, RHICmdList);
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

void FMobileSceneRenderer::RenderOcclusion(FRHICommandListImmediate& RHICmdList)
{
	if (!DoOcclusionQueries(FeatureLevel))
	{
		return;
	}

	BeginOcclusionTests(RHICmdList, true);
	FenceOcclusionTests(RHICmdList);

	// Optionally hint submission later to avoid render pass churn but delay query results
	const bool bSubmissionAfterTranslucency = (CVarMobileMoveSubmissionHintAfterTranslucency.GetValueOnRenderThread() == 1);
	if (!bSubmissionAfterTranslucency)
	{	
		RHICmdList.SubmitCommandsHint();
	}
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

// Whether we need a separate translucency render pass
bool FMobileSceneRenderer::RequiresTranslucencyPass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View) const
{
	// Translucency needs to fetch scene depth, 
	// we render opaque and translucency in a single pass if device supports frame_buffer_fetch

	// All iOS support frame_buffer_fetch
	if (IsMetalMobilePlatform(ShaderPlatform))
	{
		return false;
	}
	
	// Vulkan uses subpasses for depth fetch
	if (IsVulkanPlatform(ShaderPlatform))
	{
		return false;
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
	if (!IsMobileHDR() && !IsHTML5Platform())
	{
		return false;
	}

	// MSAA depth can't be sampled or resolved, unless we are on PC (no vulkan)
	static const auto CVarMobileMSAA = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileMSAA"));
	const bool bMobileMSAA = (CVarMobileMSAA ? CVarMobileMSAA->GetValueOnAnyThread() > 1 : false);
	if (bMobileMSAA && !IsSimulatedPlatform(ShaderPlatform))
	{
		return false;
	}

	return true;
}


void FMobileSceneRenderer::ConditionalResolveSceneDepth(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SceneContext.ResolveSceneDepthToAuxiliaryTexture(RHICmdList);
	
	if (IsSimulatedPlatform(ShaderPlatform)) // mobile emulation on PC
	{
		// resolve MSAA depth for translucency
		SceneContext.ResolveSceneDepthTexture(RHICmdList, FResolveRect(0, 0, FamilySize.X, FamilySize.Y));
	}
	else if (IsHTML5Platform() || IsAndroidOpenGLESPlatform(ShaderPlatform))
	{
		const bool bSceneDepthInAlpha = (SceneContext.GetSceneColor()->GetDesc().Format == PF_FloatRGBA);
		const bool bAlwaysResolveDepth = CVarMobileAlwaysResolveDepth.GetValueOnRenderThread() == 1;
		// Only these features require depth texture
		bool bDecals = ViewFamily.EngineShowFlags.Decals && Scene->Decals.Num();
		bool bModulatedShadows = ViewFamily.EngineShowFlags.DynamicShadows && bModulatedShadowsInUse;

		if (bDecals || bModulatedShadows || bAlwaysResolveDepth || View.bUsesSceneDepth)
		{
			SCOPED_DRAW_EVENT(RHICmdList, ConditionalResolveSceneDepth);

			// WEBGL copies depth from SceneColor alpha to a separate texture
			if (ShaderPlatform == SP_OPENGL_ES2_WEBGL)
			{
				if (bSceneDepthInAlpha)
				{
					CopySceneAlpha(RHICmdList, View);
				}
			}
			else
			{
				// Switch target to force hardware flush current depth to texture
				FTextureRHIRef DummySceneColor = GSystemTextures.BlackDummy->GetRenderTargetItem().TargetableTexture;
				FTextureRHIRef DummyDepthTarget = GSystemTextures.DepthDummy->GetRenderTargetItem().TargetableTexture;
					
				FRHIRenderPassInfo RPInfo(DummySceneColor, ERenderTargetActions::DontLoad_DontStore);
				RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::ClearDepthStencil_StoreDepthStencil;
				RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DummyDepthTarget;
				RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
				RHICmdList.BeginRenderPass(RPInfo, TEXT("ResolveDepth"));
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
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*ScreenVertexShader);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
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
						*ScreenVertexShader,
						EDRF_UseTriangleOptimization);
				} // force depth resolve
				RHICmdList.EndRenderPass();
			}
		}
	}
}

void FMobileSceneRenderer::UpdateOpaqueBasePassUniformBuffer(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	FMobileBasePassUniformParameters Parameters;
	SetupMobileBasePassUniformParameters(RHICmdList, View, false, Parameters);
	Scene->UniformBuffers.MobileOpaqueBasePassUniformBuffer.UpdateUniformBufferImmediate(Parameters);
}

void FMobileSceneRenderer::UpdateTranslucentBasePassUniformBuffer(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	FMobileBasePassUniformParameters Parameters;
	SetupMobileBasePassUniformParameters(RHICmdList, View, true, Parameters);
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
		&& Scene->SkyLight->ProcessedTexture->TextureRHI)
	{
		SkyLight = Scene->SkyLight;
	}

	FMobileReflectionCaptureShaderParameters Parameters;
	SetupMobileSkyReflectionUniformParameters(SkyLight, Parameters);
	Scene->UniformBuffers.MobileSkyReflectionUniformBuffer.UpdateUniformBufferImmediate(Parameters);
}

void FMobileSceneRenderer::CreateDirectionalLightUniformBuffers(FViewInfo& View)
{
	bool bDynamicShadows = ViewFamily.EngineShowFlags.DynamicShadows;
	// First array entry is used for primitives with no lighting channel set
	View.MobileDirectionalLightUniformBuffers[0] = TUniformBufferRef<FMobileDirectionalLightShaderParameters>::CreateUniformBufferImmediate(FMobileDirectionalLightShaderParameters(), UniformBuffer_SingleFrame);
	// Fill in the other entries based on the lights
	for (int32 ChannelIdx = 0; ChannelIdx < UE_ARRAY_COUNT(Scene->MobileDirectionalLights); ChannelIdx++)
	{
		FMobileDirectionalLightShaderParameters Params;
		SetupMobileDirectionalLightUniformParameters(*Scene, View, VisibleLightInfos, ChannelIdx, bDynamicShadows, Params);
		View.MobileDirectionalLightUniformBuffers[ChannelIdx + 1] = TUniformBufferRef<FMobileDirectionalLightShaderParameters>::CreateUniformBufferImmediate(Params, UniformBuffer_SingleFrame);
	}
}

class FCopyMobileMultiViewSceneColorPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopyMobileMultiViewSceneColorPS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;;
	}

	FCopyMobileMultiViewSceneColorPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		MobileMultiViewSceneColorTexture.Bind(Initializer.ParameterMap, TEXT("MobileMultiViewSceneColorTexture"));
		MobileMultiViewSceneColorTextureSampler.Bind(Initializer.ParameterMap, TEXT("MobileMultiViewSceneColorTextureSampler"));
	}

	FCopyMobileMultiViewSceneColorPS() {}

	void SetParameters(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, FTextureRHIRef InMobileMultiViewSceneColorTexture)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, ViewUniformBuffer);
		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			MobileMultiViewSceneColorTexture,
			MobileMultiViewSceneColorTextureSampler,
			TStaticSamplerState<SF_Bilinear>::GetRHI(),
			InMobileMultiViewSceneColorTexture);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		const bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << MobileMultiViewSceneColorTexture;
		Ar << MobileMultiViewSceneColorTextureSampler;
		return bShaderHasOutdatedParameters;
	}

	FShaderResourceParameter MobileMultiViewSceneColorTexture;
	FShaderResourceParameter MobileMultiViewSceneColorTextureSampler;
};

IMPLEMENT_SHADER_TYPE(, FCopyMobileMultiViewSceneColorPS, TEXT("/Engine/Private/MobileMultiView.usf"), TEXT("MainPS"), SF_Pixel);

void FMobileSceneRenderer::CopyMobileMultiViewSceneColor(FRHICommandListImmediate& RHICmdList)
{
	if (Views.Num() <= 1 || !Views[0].bIsMobileMultiViewEnabled)
	{
		return;
	}

	RHICmdList.DiscardRenderTargets(true, true, 0);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// Switching from the multi-view scene color render target array to side by side scene color
	FRHIRenderPassInfo RPInfo(ViewFamily.RenderTarget->GetRenderTargetTexture(), ERenderTargetActions::Clear_Store);
	RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::ClearDepthStencil_DontStoreDepthStencil;
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SceneContext.GetSceneDepthTexture();
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilNop;

	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyMobileMultiViewColor"));
	{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FCopyMobileMultiViewSceneColorPS> PixelShader(ShaderMap);
	extern TGlobalResource<FFilterVertexDeclaration> GFilterVertexDeclaration;

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		// Multi-view color target is our input texture array
		PixelShader->SetParameters(RHICmdList, View.ViewUniformBuffer, SceneContext.MobileMultiViewSceneColor->GetRenderTargetItem().ShaderResourceTexture);

		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Min.X + View.ViewRect.Width(), View.ViewRect.Min.Y + View.ViewRect.Height(), 1.0f);
		const FIntPoint TargetSize(View.ViewRect.Width(), View.ViewRect.Height());

		DrawRectangle(
			RHICmdList,
			0, 0,
			View.ViewRect.Width(), View.ViewRect.Height(),
			0, 0,
			View.ViewRect.Width(), View.ViewRect.Height(),
			TargetSize,
			TargetSize,
			*VertexShader,
			EDRF_UseTriangleOptimization);
	}

	}
	RHICmdList.EndRenderPass();
}

class FPreTonemapMSAA_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPreTonemapMSAA_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	FPreTonemapMSAA_ES2() {}

public:
	FPreTonemapMSAA_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

IMPLEMENT_SHADER_TYPE(,FPreTonemapMSAA_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("PreTonemapMSAA_ES2"),SF_Pixel);

void FMobileSceneRenderer::PreTonemapMSAA(FRHICommandListImmediate& RHICmdList)
{
	// iOS only
	static const auto CVarMobileMSAA = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileMSAA"));
	bool bOnChipPP = GSupportsRenderTargetFormat_PF_FloatRGBA && GSupportsShaderFramebufferFetch &&	ViewFamily.EngineShowFlags.PostProcessing;
	bool bOnChipPreTonemapMSAA = bOnChipPP && IsMetalMobilePlatform(ViewFamily.GetShaderPlatform()) && (CVarMobileMSAA ? CVarMobileMSAA->GetValueOnAnyThread() > 1 : false);
	if (!bOnChipPreTonemapMSAA)
	{
		return;
	}
	
	// Part of scene rendering pass
	check (RHICmdList.IsInsideRenderPass());

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FPreTonemapMSAA_ES2> PixelShader(ShaderMap);

	extern TGlobalResource<FFilterVertexDeclaration> GFilterVertexDeclaration;
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	
	const FIntPoint TargetSize = SceneContext.GetBufferSizeXY();
	RHICmdList.SetViewport(0, 0, 0.0f, TargetSize.X, TargetSize.Y, 1.0f);

	DrawRectangle(
		RHICmdList,
		0, 0,
		TargetSize.X, TargetSize.Y,
		0, 0,
		TargetSize.X, TargetSize.Y,
		TargetSize,
		TargetSize,
		*VertexShader,
		EDRF_UseTriangleOptimization);
}
