// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CompositionLighting.cpp: The center for all deferred lighting activities.
=============================================================================*/

#include "CompositionLighting/CompositionLighting.h"
#include "ScenePrivate.h"
#include "PostProcess/RenderingCompositionGraph.h"
#include "PostProcess/PostProcessInput.h"
#include "PostProcess/PostProcessing.h"
#include "CompositionLighting/PostProcessLpvIndirect.h"
#include "CompositionLighting/PostProcessAmbientOcclusion.h"
#include "CompositionLighting/PostProcessDeferredDecals.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "LightPropagationVolumeSettings.h"
#include "DecalRenderingShared.h"
#include "VisualizeTexture.h"
#include "RayTracing/RaytracingOptions.h"

/** The global center for all deferred lighting activities. */
FCompositionLighting GCompositionLighting;

DECLARE_GPU_STAT_NAMED(CompositionBeforeBasePass, TEXT("Composition BeforeBasePass") );
DECLARE_GPU_STAT_NAMED(CompositionPreLighting, TEXT("Composition PreLighting") );
DECLARE_GPU_STAT_NAMED(CompositionLpvIndirect, TEXT("Composition LpvIndirect") );
DECLARE_GPU_STAT_NAMED(CompositionPostLighting, TEXT("Composition PostLighting") );

static TAutoConsoleVariable<int32> CVarSSAOSmoothPass(
	TEXT("r.AmbientOcclusion.Compute.Smooth"),
	1,
	TEXT("Whether to smooth SSAO output when TAA is disabled"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarGTAODownsample(
	TEXT("r.GTAO.Downsample"),
	1,
	TEXT("Perform GTAO at Halfres \n ")
	TEXT("0: Off \n ")
	TEXT("1: On (default)\n "),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarGTAOTemporalFilter(
	TEXT("r.GTAO.TemporalFilter"),
	1,
	TEXT("Enable Temporal Filter for GTAO \n ")
	TEXT("0: Off \n ")
	TEXT("1: On (default)\n "),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarGTAOSpatialFilter(
	TEXT("r.GTAO.SpatialFilter"),
	1,
	TEXT("Enable Spatial Filter for GTAO \n ")
	TEXT("0: Off \n ")
	TEXT("1: On (default)\n "),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarGTAOCombined(
	TEXT("r.GTAO.Combined"),
	1,
	TEXT("Enable Spatial Filter for GTAO \n ")
	TEXT("0: Off \n ")
	TEXT("1: On (default)\n "),
	ECVF_RenderThreadSafe | ECVF_Scalability);

bool IsAmbientCubemapPassRequired(const FSceneView& View)
{
	FScene* Scene = (FScene*)View.Family->Scene;

	return View.FinalPostProcessSettings.ContributingCubemaps.Num() != 0 && IsUsingGBuffers(View.GetShaderPlatform());
}

bool IsLpvIndirectPassRequired(const FViewInfo& View)
{
	FScene* Scene = (FScene*)View.Family->Scene;

	const FSceneViewState* ViewState = (FSceneViewState*)View.State;

	if(ViewState)
	{
		// This check should be inclusive to stereo views
		const bool bIncludeStereoViews = true;

		FLightPropagationVolume* LightPropagationVolume = ViewState->GetLightPropagationVolume(View.GetFeatureLevel(), bIncludeStereoViews);

		if(LightPropagationVolume)
		{
			const FLightPropagationVolumeSettings& LPVSettings = View.FinalPostProcessSettings.BlendableManager.GetSingleFinalDataConst<FLightPropagationVolumeSettings>();

			if(LPVSettings.LPVIntensity > 0.0f)
			{
				return true;
			}
		}
	}

	return false;
}

static bool IsReflectionEnvironmentActive(const FSceneView& View)
{
	FScene* Scene = (FScene*)View.Family->Scene;

	// LPV & Screenspace Reflections : Reflection Environment active if either LPV (assumed true if this was called), Reflection Captures or SSR active

	bool IsReflectingEnvironment = View.Family->EngineShowFlags.ReflectionEnvironment;
	bool HasReflectionCaptures = (Scene->ReflectionSceneData.RegisteredReflectionCaptures.Num() > 0);
	bool HasSSR = View.Family->EngineShowFlags.ScreenSpaceReflections;

	return (Scene->GetFeatureLevel() == ERHIFeatureLevel::SM5 && IsReflectingEnvironment && (HasReflectionCaptures || HasSSR) && !IsAnyForwardShadingEnabled(View.GetShaderPlatform()));
}

static bool IsSkylightActive(const FViewInfo& View)
{
	FScene* Scene = (FScene*)View.Family->Scene;
	return Scene->SkyLight 
		&& Scene->SkyLight->ProcessedTexture
		&& View.Family->EngineShowFlags.SkyLighting;
}

bool ShouldRenderScreenSpaceAmbientOcclusion(const FViewInfo& View)
{
	bool bEnabled = true;

	if (!IsLpvIndirectPassRequired(View))
	{
		bEnabled = View.FinalPostProcessSettings.AmbientOcclusionIntensity > 0
			&& View.Family->EngineShowFlags.Lighting
			&& View.FinalPostProcessSettings.AmbientOcclusionRadius >= 0.1f
			&& !View.Family->UseDebugViewPS()
			&& (FSSAOHelper::IsBasePassAmbientOcclusionRequired(View) || IsAmbientCubemapPassRequired(View) || IsReflectionEnvironmentActive(View) || IsSkylightActive(View) || View.Family->EngineShowFlags.VisualizeBuffer)
			&& !IsSimpleForwardShadingEnabled(View.GetShaderPlatform());
	}
#if RHI_RAYTRACING
	bEnabled &= !ShouldRenderRayTracingAmbientOcclusion(View);
#endif
	return bEnabled;
}

static FRenderingCompositeOutputRef AddPostProcessingGTAOAsyncHorizonSearch(FRHICommandListImmediate& RHICmdList, FPostprocessContext& Context)
{
	FRenderingCompositePass* FinalOutputPass;


	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);
	uint32 DownsampleFactor = CVarGTAODownsample.GetValueOnRenderThread() > 0 ? 2 : 1;

	FIntPoint BufferSize	 = SceneContext.GetBufferSizeXY();
	FIntPoint HorizonBufferSize = FIntPoint::DivideAndRoundUp(BufferSize, DownsampleFactor);
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(HorizonBufferSize, PF_R8G8, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable, false));
	if (SceneContext.GetCurrentFeatureLevel() >= ERHIFeatureLevel::SM5)
	{
		Desc.TargetableFlags |= TexCreate_UAV;
	}
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneContext.ScreenSpaceGTAOHorizons, TEXT("ScreenSpaceGTAOHorizons"));

	FRenderingCompositePass* HZBInput = Context.Graph.RegisterPass(new FRCPassPostProcessInput(const_cast<FViewInfo&>(Context.View).HZB));
	FRenderingCompositePass* AmbientOcclusionHorizonSearch = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_HorizonSearch(Context.View, DownsampleFactor, ESSAOType::EAsyncCS));

	AmbientOcclusionHorizonSearch->SetInput(ePId_Input0, Context.SceneDepth);
	AmbientOcclusionHorizonSearch->SetInput(ePId_Input1, HZBInput);

	FinalOutputPass = AmbientOcclusionHorizonSearch;

	Context.FinalOutput = FRenderingCompositeOutputRef(FinalOutputPass);
	return FRenderingCompositeOutputRef(FinalOutputPass);
}


static FRenderingCompositeOutputRef AddPostProcessingGTAOCombined(FRHICommandListImmediate& RHICmdList, FPostprocessContext& Context)
{
	FRenderingCompositePass* FinalOutputPass;

	FRenderingCompositePass* HZBInput = Context.Graph.RegisterPass(new FRCPassPostProcessInput(const_cast<FViewInfo&>(Context.View).HZB));

	uint32 DownsampleFactor = CVarGTAODownsample.GetValueOnRenderThread() > 0 ? 2 : 1;
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	if(CVarGTAOCombined.GetValueOnRenderThread() ==1)
	{
		FRenderingCompositePass* AmbientOcclusionGTAO = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_GTAOCombined(Context.View, DownsampleFactor, false));
		AmbientOcclusionGTAO->SetInput(ePId_Input0, Context.SceneDepth);
		AmbientOcclusionGTAO->SetInput(ePId_Input1, HZBInput);
		FinalOutputPass = AmbientOcclusionGTAO;
	}
	else
	{

		FIntPoint BufferSize = SceneContext.GetBufferSizeXY();
		FIntPoint HorizonBufferSize = FIntPoint::DivideAndRoundUp(BufferSize, DownsampleFactor);
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(HorizonBufferSize, PF_R8G8, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable, false));
		if (SceneContext.GetCurrentFeatureLevel() >= ERHIFeatureLevel::SM5)
		{
			Desc.TargetableFlags |= TexCreate_UAV;
		}
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneContext.ScreenSpaceGTAOHorizons, TEXT("ScreenSpaceGTAOHorizons"));

		FRenderingCompositePass* AmbientOcclusionHorizonSearch = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_HorizonSearch(Context.View, DownsampleFactor, ESSAOType::ECS));

		AmbientOcclusionHorizonSearch->SetInput(ePId_Input0, Context.SceneDepth);
		AmbientOcclusionHorizonSearch->SetInput(ePId_Input1, HZBInput);

		FinalOutputPass = AmbientOcclusionHorizonSearch;

		FRenderingCompositePass* AmbientOcclusionInnerIntegrate = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_InnerIntegrate(Context.View, DownsampleFactor, false));
		AmbientOcclusionInnerIntegrate->SetInput(ePId_Input0, Context.SceneDepth);
		AmbientOcclusionInnerIntegrate->SetInput(ePId_Input1, FinalOutputPass);
		FinalOutputPass = AmbientOcclusionInnerIntegrate;
	}

	SceneContext.bScreenSpaceAOIsValid = true;

	FSceneViewState* ViewState = Context.View.ViewState;

	// Add spatial Filter
	if (CVarGTAOSpatialFilter.GetValueOnRenderThread() == 1)
	{
		FRenderingCompositePass* SpatialPass;
		SpatialPass = Context.Graph.RegisterPass(new (FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_GTAO_SpatialFilter(Context.View, DownsampleFactor));
		SpatialPass->SetInput(ePId_Input0, FinalOutputPass);
		SpatialPass->SetInput(ePId_Input1, HZBInput);
		FinalOutputPass = SpatialPass;
	}

	
	if (ViewState && CVarGTAOTemporalFilter.GetValueOnRenderThread() == 1)
	{
		// Add temporal filter
		FRenderingCompositePass* TemporalPass;
		TemporalPass = Context.Graph.RegisterPass(new (FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_GTAO_TemporalFilter(Context.View, DownsampleFactor,
			Context.View.PrevViewInfo.GTAOHistory,
			&ViewState->PrevFrameViewInfo.GTAOHistory));

		TemporalPass->SetInput(ePId_Input0, FinalOutputPass);
		FinalOutputPass = TemporalPass;

	}
	
	{
		FRenderingCompositePass* UpsamplePass;
		UpsamplePass = Context.Graph.RegisterPass(new (FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_GTAO_Upsample(Context.View, DownsampleFactor));
		UpsamplePass->SetInput(ePId_Input0, FinalOutputPass);
		FinalOutputPass = UpsamplePass;
	}

	Context.FinalOutput = FRenderingCompositeOutputRef(FinalOutputPass);
	return FRenderingCompositeOutputRef(FinalOutputPass);
}


static FRenderingCompositeOutputRef AddPostProcessingGTAOIntegration(FRHICommandListImmediate& RHICmdList, FPostprocessContext& Context)
{
	FRenderingCompositePass* FinalOutputPass;

	FRenderingCompositePass* HZBInput = Context.Graph.RegisterPass(new FRCPassPostProcessInput(const_cast<FViewInfo&>(Context.View).HZB));
	uint32 DownsampleFactor = CVarGTAODownsample.GetValueOnRenderThread() > 0 ? 2 : 1;

	FRenderingCompositePass* AmbientOcclusionInnerIntegrate = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_InnerIntegrate(Context.View, DownsampleFactor, false));
	AmbientOcclusionInnerIntegrate->SetInput(ePId_Input0, Context.SceneDepth);
	FinalOutputPass = AmbientOcclusionInnerIntegrate;

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SceneContext.bScreenSpaceAOIsValid = true;

	FSceneViewState* ViewState = Context.View.ViewState;

	// Add spatial Filter
	if (CVarGTAOSpatialFilter.GetValueOnRenderThread() == 1)
	{
		FRenderingCompositePass* SpatialPass;
		SpatialPass = Context.Graph.RegisterPass(new (FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_GTAO_SpatialFilter(Context.View, DownsampleFactor));
		SpatialPass->SetInput(ePId_Input0, FinalOutputPass);
		SpatialPass->SetInput(ePId_Input1, HZBInput);
		FinalOutputPass = SpatialPass;
	}

//	bool bNeedsUpsample = DownsampleFactor != 1;

	// Add temporal filter
	if (ViewState && CVarGTAOTemporalFilter.GetValueOnRenderThread() == 1)
	{
		FRenderingCompositePass* TemporalPass;
		TemporalPass = Context.Graph.RegisterPass(new (FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_GTAO_TemporalFilter(Context.View, DownsampleFactor,
			Context.View.PrevViewInfo.GTAOHistory,
			&ViewState->PrevFrameViewInfo.GTAOHistory));

		TemporalPass->SetInput(ePId_Input0, FinalOutputPass);
		FinalOutputPass = TemporalPass;

	}
	{
		FRenderingCompositePass* UpsamplePass;
		UpsamplePass = Context.Graph.RegisterPass(new (FMemStack::Get()) FRCPassPostProcessAmbientOcclusion_GTAO_Upsample(Context.View, DownsampleFactor));
		UpsamplePass->SetInput(ePId_Input0, FinalOutputPass);
		FinalOutputPass = UpsamplePass;
	}

	Context.FinalOutput = FRenderingCompositeOutputRef(FinalOutputPass);
	return FRenderingCompositeOutputRef(FinalOutputPass);

}


// @param Levels 0..3, how many different resolution levels we want to render
static FRenderingCompositeOutputRef AddPostProcessingAmbientOcclusion(FRHICommandListImmediate& RHICmdList, FPostprocessContext& Context, uint32 Levels)
{
	check(Levels >= 0 && Levels <= 3);

	FRenderingCompositePass* AmbientOcclusionInMip1 = nullptr;
	FRenderingCompositePass* AmbientOcclusionInMip2 = nullptr;
	FRenderingCompositePass* AmbientOcclusionPassMip1 = nullptr; 
	FRenderingCompositePass* AmbientOcclusionPassMip2 = nullptr;

	FRenderingCompositePass* HZBInput = Context.Graph.RegisterPass(new FRCPassPostProcessInput(const_cast<FViewInfo&>(Context.View).HZB));
	{
		// generate input in half, quarter, .. resolution
		ESSAOType DownResAOType = FSSAOHelper::IsAmbientOcclusionCompute(Context.View) ? ESSAOType::ECS : ESSAOType::EPS;
		if (Levels >= 2)
		{
			AmbientOcclusionInMip1 = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAmbientOcclusionSetup());
			AmbientOcclusionInMip1->SetInput(ePId_Input0, Context.SceneDepth);
		}

		if (Levels >= 3)
		{
			AmbientOcclusionInMip2 = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAmbientOcclusionSetup());
			AmbientOcclusionInMip2->SetInput(ePId_Input1, FRenderingCompositeOutputRef(AmbientOcclusionInMip1, ePId_Output0));
		}		

		// upsample from lower resolution

		if (Levels >= 3)
		{
			AmbientOcclusionPassMip2 = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAmbientOcclusion(Context.View, DownResAOType));
			AmbientOcclusionPassMip2->SetInput(ePId_Input0, AmbientOcclusionInMip2);
			AmbientOcclusionPassMip2->SetInput(ePId_Input1, AmbientOcclusionInMip2);
			AmbientOcclusionPassMip2->SetInput(ePId_Input3, HZBInput);
		}

		if (Levels >= 2)
		{
			AmbientOcclusionPassMip1 = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAmbientOcclusion(Context.View, DownResAOType));
			AmbientOcclusionPassMip1->SetInput(ePId_Input0, AmbientOcclusionInMip1);
			AmbientOcclusionPassMip1->SetInput(ePId_Input1, AmbientOcclusionInMip1);
			AmbientOcclusionPassMip1->SetInput(ePId_Input2, AmbientOcclusionPassMip2);
			AmbientOcclusionPassMip1->SetInput(ePId_Input3, HZBInput);
		}
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	FRenderingCompositePass* GBufferA = nullptr;
	
	// finally full resolution
	ESSAOType FullResAOType = ESSAOType::EPS;
	{
		if(FSSAOHelper::IsAmbientOcclusionCompute(Context.View))
		{
			if(FSSAOHelper::IsAmbientOcclusionAsyncCompute(Context.View, Levels) && GSupportsEfficientAsyncCompute)
			{
				FullResAOType = ESSAOType::EAsyncCS;
			}
			else
			{
				FullResAOType = ESSAOType::ECS;
			}
		}
	}

	if (SceneContext.GBufferA)
	{
		GBufferA = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessInput(SceneContext.GBufferA));
	}

	// If there is no temporal upsampling, we need a smooth pass to get rid of the grid pattern.
	// PS version has relatively smooth result so no need to do extra work
	const bool bNeedSmoothingPass = FullResAOType != ESSAOType::EPS && Context.View.AntiAliasingMethod != AAM_TemporalAA && CVarSSAOSmoothPass.GetValueOnRenderThread();
	const EPixelFormat SmoothingPassInputFormat = bNeedSmoothingPass ? PF_G8 : PF_Unknown;

	FRenderingCompositePass* AmbientOcclusionPassMip0 = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAmbientOcclusion(Context.View, FullResAOType, false, bNeedSmoothingPass, SmoothingPassInputFormat));
	AmbientOcclusionPassMip0->SetInput(ePId_Input0, GBufferA);
	AmbientOcclusionPassMip0->SetInput(ePId_Input1, AmbientOcclusionInMip1);
	AmbientOcclusionPassMip0->SetInput(ePId_Input2, AmbientOcclusionPassMip1);
	AmbientOcclusionPassMip0->SetInput(ePId_Input3, HZBInput);
	FRenderingCompositePass* FinalOutputPass = AmbientOcclusionPassMip0;

	if (bNeedSmoothingPass)
	{
		FRenderingCompositePass* SSAOSmoothPass;
		SSAOSmoothPass = Context.Graph.RegisterPass(new (FMemStack::Get()) FRCPassPostProcessAmbientOcclusionSmooth(FullResAOType, true));
		SSAOSmoothPass->SetInput(ePId_Input0, AmbientOcclusionPassMip0);
		FinalOutputPass = SSAOSmoothPass;
	}

	// to make sure this pass is processed as well (before), needed to make process decals before computing AO
	if(AmbientOcclusionInMip1)
	{
		AmbientOcclusionInMip1->AddDependency(Context.FinalOutput);
	}
	else
	{
		AmbientOcclusionPassMip0->AddDependency(Context.FinalOutput);
	}

	Context.FinalOutput = FRenderingCompositeOutputRef(FinalOutputPass);

	SceneContext.bScreenSpaceAOIsValid = true;

	return FRenderingCompositeOutputRef(FinalOutputPass);
}

void FCompositionLighting::ProcessBeforeBasePass(FRHICommandListImmediate& RHICmdList, FViewInfo& View, bool bDBuffer, uint32 SSAOLevels)
{
	check(IsInRenderingThread());

	// so that the passes can register themselves to the graph
	if (bDBuffer || SSAOLevels)
	{
		FMemMark Mark(FMemStack::Get());
		FRenderingCompositePassContext CompositeContext(RHICmdList, View);

		FPostprocessContext Context(RHICmdList, CompositeContext.Graph, View);

		// Add the passes we want to add to the graph (commenting a line means the pass is not inserted into the graph) ----------

		// decals are before AmbientOcclusion so the decal can output a normal that AO is affected by
		if (bDBuffer) 
		{
			FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessDeferredDecals(DRS_BeforeBasePass));
			Pass->SetInput(ePId_Input0, Context.FinalOutput);

			Context.FinalOutput = FRenderingCompositeOutputRef(Pass);
		}

		if (SSAOLevels)
		{

			if (FSSAOHelper::GetGTAOPassType(View) != EGTAOType::ECombinedNonAsync)
			{
				AddPostProcessingAmbientOcclusion(RHICmdList, Context, SSAOLevels);
			}
		}

		// The graph setup should be finished before this line ----------------------------------------

		SCOPED_DRAW_EVENT(RHICmdList, CompositionBeforeBasePass);
		SCOPED_GPU_STAT(RHICmdList, CompositionBeforeBasePass);

		CompositeContext.Process(Context.FinalOutput.GetPass(), TEXT("Composition_BeforeBasePass"));
	}
}

void FCompositionLighting::ProcessAfterBasePass(FRHICommandListImmediate& RHICmdList, FViewInfo& View)
{
	check(IsInRenderingThread());
	
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	// might get renamed to refracted or ...WithAO
	SceneContext.GetSceneColor()->SetDebugName(TEXT("SceneColor"));
	// to be able to observe results with VisualizeTexture

	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.GetSceneColor());
	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.GBufferA);
	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.GBufferB);
	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.GBufferC);
	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.GBufferD);
	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.GBufferE);
	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.SceneVelocity);
	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.ScreenSpaceAO);
	
	// so that the passes can register themselves to the graph
	if(CanOverlayRayTracingOutput(View))
	{
		FMemMark Mark(FMemStack::Get());
		FRenderingCompositePassContext CompositeContext(RHICmdList, View);

		FPostprocessContext Context(RHICmdList, CompositeContext.Graph, View);

		// Add the passes we want to add to the graph ----------
		
		if( Context.View.Family->EngineShowFlags.Decals &&
			!Context.View.Family->EngineShowFlags.ShaderComplexity)
		{
			// DRS_AfterBasePass is for Volumetric decals which don't support ShaderComplexity yet
			FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessDeferredDecals(DRS_AfterBasePass));
			Pass->SetInput(ePId_Input0, Context.FinalOutput);

			Context.FinalOutput = FRenderingCompositeOutputRef(Pass);
		}

		// decal are distracting when looking at LightCulling.
		bool bDoDecal = Context.View.Family->EngineShowFlags.Decals && !Context.View.Family->EngineShowFlags.VisualizeLightCulling;

		if (bDoDecal && IsUsingGBuffers(View.GetShaderPlatform()))
		{
			// decals are before AmbientOcclusion so the decal can output a normal that AO is affected by
			FRenderingCompositePass* BeforeLightingPass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessDeferredDecals(DRS_BeforeLighting));
			BeforeLightingPass->SetInput(ePId_Input0, Context.FinalOutput);
			Context.FinalOutput = FRenderingCompositeOutputRef(BeforeLightingPass);
		}

		if (bDoDecal && !IsSimpleForwardShadingEnabled(View.GetShaderPlatform()))
		{
			// DBuffer decals with emissive component
			FRenderingCompositePass* EmissivePass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessDeferredDecals(DRS_Emissive));
			EmissivePass->SetInput(ePId_Input0, Context.FinalOutput);
			Context.FinalOutput = FRenderingCompositeOutputRef(EmissivePass);
		}

		// Forwared shading SSAO is applied before the basepass using only the depth buffer.
		if (!IsForwardShadingEnabled(View.GetShaderPlatform()))
		{
			FRenderingCompositeOutputRef AmbientOcclusion;
#if RHI_RAYTRACING
			if (ShouldRenderRayTracingAmbientOcclusion(View) && SceneContext.bScreenSpaceAOIsValid)
			{
				AmbientOcclusion = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessInput(SceneContext.ScreenSpaceAO));
			}
#endif
			uint32 SSAOLevels = FSSAOHelper::ComputeAmbientOcclusionPassCount(Context.View);
			if (SSAOLevels)
			{
				if(!FSSAOHelper::IsAmbientOcclusionAsyncCompute(Context.View, SSAOLevels))
				{
					if (FSSAOHelper::GetGTAOPassType(View) == EGTAOType::ECombinedNonAsync)
					{
						AmbientOcclusion = AddPostProcessingGTAOCombined(RHICmdList, Context);
					}
					else
					{
						AmbientOcclusion = AddPostProcessingAmbientOcclusion(RHICmdList, Context, SSAOLevels);
					}

					if (bDoDecal)
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessDeferredDecals(DRS_AmbientOcclusion));
						Pass->AddDependency(Context.FinalOutput);

						Context.FinalOutput = FRenderingCompositeOutputRef(Pass);
					}
				}
				else
				{
					// If doing the Split GTAO method then we need to do the second part here.
					if (FSSAOHelper::GetGTAOPassType(View) == EGTAOType::ESplitAsync)
					{
						AmbientOcclusion = AddPostProcessingGTAOIntegration(RHICmdList, Context);
					}

					ensureMsgf(
						FDecalRendering::BuildVisibleDecalList(*(FScene*)Context.View.Family->Scene, Context.View, DRS_AmbientOcclusion, nullptr) == false,
						TEXT("Ambient occlusion decals are not supported with Async compute SSAO."));
				}

			}
		}

		// The graph setup should be finished before this line ----------------------------------------

		SCOPED_DRAW_EVENT(RHICmdList, LightCompositionTasks_PreLighting);
		SCOPED_GPU_STAT(RHICmdList, CompositionPreLighting);

		TRefCountPtr<IPooledRenderTarget>& SceneColor = SceneContext.GetSceneColor();

		Context.FinalOutput.GetOutput()->RenderTargetDesc = SceneColor->GetDesc();
		Context.FinalOutput.GetOutput()->PooledRenderTarget = SceneColor;

		CompositeContext.Process(Context.FinalOutput.GetPass(), TEXT("CompositionLighting_AfterBasePass"));
	}

	SceneContext.ScreenSpaceGTAOHorizons.SafeRelease();
}


void FCompositionLighting::ProcessLpvIndirect(FRHICommandListImmediate& RHICmdList, FViewInfo& View)
{
	check(IsInRenderingThread());
	
	FMemMark Mark(FMemStack::Get());
	FRenderingCompositePassContext CompositeContext(RHICmdList, View);
	FPostprocessContext Context(RHICmdList, CompositeContext.Graph, View);

	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		FRenderingCompositePass* SSAO = Context.Graph.RegisterPass(new FRCPassPostProcessInput(SceneContext.ScreenSpaceAO));

		FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new FRCPassPostProcessLpvIndirect());
		Pass->SetInput(ePId_Input0, Context.FinalOutput);
		Pass->SetInput(ePId_Input1, SSAO );

		Context.FinalOutput = FRenderingCompositeOutputRef(Pass);
	}

	// The graph setup should be finished before this line ----------------------------------------

	SCOPED_DRAW_EVENT(RHICmdList, CompositionLpvIndirect);
	SCOPED_GPU_STAT(RHICmdList, CompositionLpvIndirect);

	// we don't replace the final element with the scenecolor because this is what those passes should do by themself

	CompositeContext.Process(Context.FinalOutput.GetPass(), TEXT("CompositionLighting"));
}

bool FCompositionLighting::CanProcessAsyncSSAO(TArray<FViewInfo>& Views)
{
	bool bAnyAsyncSSAO = true;
	for (int32 i = 0; i < Views.Num(); ++i)
	{
		uint32 Levels = FSSAOHelper::ComputeAmbientOcclusionPassCount(Views[i]);
		if (!FSSAOHelper::IsAmbientOcclusionAsyncCompute(Views[i], Levels))
		{
			bAnyAsyncSSAO = false;
			break;
		}
	}
	return bAnyAsyncSSAO;
}

void FCompositionLighting::PrepareAsyncSSAO(FRHICommandListImmediate& RHICmdList, TArray<FViewInfo>& Views)
{
	//clear out last frame's fence.
	ensureMsgf(AsyncSSAOFence == nullptr, TEXT("Old AsyncCompute SSAO fence has not been cleared."));

	static FName AsyncSSAOFenceName(TEXT("AsyncSSAOFence"));
	AsyncSSAOFence = RHICmdList.CreateComputeFence(AsyncSSAOFenceName);

	//Grab the async compute commandlist.
	FRHIAsyncComputeCommandListImmediate& RHICmdListComputeImmediate = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();
	RHICmdListComputeImmediate.SetAsyncComputeBudget(FSSAOHelper::GetAmbientOcclusionAsyncComputeBudget());
}

void FCompositionLighting::ProcessAsyncSSAO(FRHICommandListImmediate& RHICmdList, TArray<FViewInfo>& Views)
{
	check(IsInRenderingThread());
	if (GSupportsEfficientAsyncCompute)
	{
		PrepareAsyncSSAO(RHICmdList, Views);

		// so that the passes can register themselves to the graph
		for (int32 i = 0; i < Views.Num(); ++i)
		{
			FViewInfo& View = Views[i];
			FMemMark Mark(FMemStack::Get());
			FRenderingCompositePassContext CompositeContext(RHICmdList, View);

			// Add the passes we want to add to the graph (commenting a line means the pass is not inserted into the graph) ----------		
			uint32 Levels = FSSAOHelper::ComputeAmbientOcclusionPassCount(View);		
			if (FSSAOHelper::IsAmbientOcclusionAsyncCompute(View, Levels))
			{
				SCOPED_GPU_MASK(RHICmdList, View.GPUMask);
				SCOPED_GPU_MASK(FRHICommandListExecutor::GetImmediateAsyncComputeCommandList(), View.GPUMask);

				FPostprocessContext Context(RHICmdList, CompositeContext.Graph, View);

				if (FSSAOHelper::GetGTAOPassType(View) == EGTAOType::ESplitAsync)
				{
					FRenderingCompositeOutputRef AmbientOcclusion = AddPostProcessingGTAOAsyncHorizonSearch(RHICmdList, Context);
					Context.FinalOutput = FRenderingCompositeOutputRef(AmbientOcclusion);
				}
				else
				{
					FRenderingCompositeOutputRef AmbientOcclusion = AddPostProcessingAmbientOcclusion(RHICmdList, Context, Levels);
					Context.FinalOutput = FRenderingCompositeOutputRef(AmbientOcclusion);
				}
		
				// The graph setup should be finished before this line ----------------------------------------
				CompositeContext.Process(Context.FinalOutput.GetPass(), TEXT("Composition_ProcessAsyncSSAO"));
			}		
		}
		FinishAsyncSSAO(RHICmdList);
	}
	else
	{
		// so that the passes can register themselves to the graph
		for (int32 i = 0; i < Views.Num(); ++i)
		{
			FViewInfo& View = Views[i];
			FMemMark Mark(FMemStack::Get());
			FRenderingCompositePassContext CompositeContext(RHICmdList, View);

			// Add the passes we want to add to the graph (commenting a line means the pass is not inserted into the graph) ----------		
			if (FSSAOHelper::IsAmbientOcclusionCompute(View))
			{
				SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

				FPostprocessContext Context(RHICmdList, CompositeContext.Graph, View);

				FRenderingCompositeOutputRef AmbientOcclusion = AddPostProcessingAmbientOcclusion(RHICmdList, Context, 1);
				Context.FinalOutput = FRenderingCompositeOutputRef(AmbientOcclusion);			

				// The graph setup should be finished before this line ----------------------------------------
				CompositeContext.Process(Context.FinalOutput.GetPass(), TEXT("Composition_ProcessSSAO"));
			}		
		}
	}
}

void FCompositionLighting::FinishAsyncSSAO(FRHICommandListImmediate& RHICmdList)
{
	if (AsyncSSAOFence)
	{
		//Grab the async compute commandlist.
		FRHIAsyncComputeCommandListImmediate& RHICmdListComputeImmediate = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();

		RHICmdListComputeImmediate.SetAsyncComputeBudget(EAsyncComputeBudget::EAll_4);
		RHICmdListComputeImmediate.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, nullptr, 0, AsyncSSAOFence);
		FRHIAsyncComputeCommandListImmediate::ImmediateDispatch(RHICmdListComputeImmediate);		
	}
}

void FCompositionLighting::GfxWaitForAsyncSSAO(FRHICommandListImmediate& RHICmdList)
{
	if (AsyncSSAOFence)
	{
		RHICmdList.WaitComputeFence(AsyncSSAOFence);
		AsyncSSAOFence = nullptr;
	}
}