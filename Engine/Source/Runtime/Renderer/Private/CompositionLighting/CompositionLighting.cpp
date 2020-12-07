// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CompositionLighting.cpp: The center for all deferred lighting activities.
=============================================================================*/

#include "CompositionLighting/CompositionLighting.h"
#include "ScenePrivate.h"
#include "PostProcess/PostProcessing.h"
#include "CompositionLighting/PostProcessAmbientOcclusion.h"
#include "CompositionLighting/PostProcessDeferredDecals.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "DecalRenderingShared.h"
#include "VisualizeTexture.h"
#include "RayTracing/RaytracingOptions.h"
#include "SceneTextureParameters.h"
#include "RenderGraphUtils.h"

DECLARE_GPU_STAT_NAMED(CompositionBeforeBasePass, TEXT("Composition BeforeBasePass") );
DECLARE_GPU_STAT_NAMED(CompositionPreLighting, TEXT("Composition PreLighting") );
DECLARE_GPU_STAT_NAMED(CompositionPostLighting, TEXT("Composition PostLighting") );

static TAutoConsoleVariable<int32> CVarSSAOSmoothPass(
	TEXT("r.AmbientOcclusion.Compute.Smooth"),
	1,
	TEXT("Whether to smooth SSAO output when TAA is disabled"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarGTAODownsample(
	TEXT("r.GTAO.Downsample"),
	0,
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

	bEnabled = View.FinalPostProcessSettings.AmbientOcclusionIntensity > 0
		&& View.Family->EngineShowFlags.Lighting
		&& View.FinalPostProcessSettings.AmbientOcclusionRadius >= 0.1f
		&& !View.Family->UseDebugViewPS()
		&& (FSSAOHelper::IsBasePassAmbientOcclusionRequired(View) || IsAmbientCubemapPassRequired(View) || IsReflectionEnvironmentActive(View) || IsSkylightActive(View) || View.Family->EngineShowFlags.VisualizeBuffer)
		&& !IsSimpleForwardShadingEnabled(View.GetShaderPlatform());
#if RHI_RAYTRACING
	bEnabled &= !ShouldRenderRayTracingAmbientOcclusion(View);
#endif
	return bEnabled;
}

static ESSAOType GetDownscaleSSAOType(const FViewInfo& View)
{
	return FSSAOHelper::IsAmbientOcclusionCompute(View) ? ESSAOType::ECS : ESSAOType::EPS;
}

static ESSAOType GetFullscreenSSAOType(const FViewInfo& View, uint32 Levels)
{
	if (FSSAOHelper::IsAmbientOcclusionCompute(View))
	{
		if (FSSAOHelper::IsAmbientOcclusionAsyncCompute(View, Levels))
		{
			return ESSAOType::EAsyncCS;
		}

		return ESSAOType::ECS;
	}

	return ESSAOType::EPS;
}

static FSSAOCommonParameters GetSSAOCommonParameters(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	uint32 Levels)
{
	const FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder, SceneTexturesUniformBuffer);

	FSSAOCommonParameters CommonParameters;
	CommonParameters.SceneTexturesUniformBuffer = SceneTexturesUniformBuffer;
	CommonParameters.SceneTexturesViewport = FScreenPassTextureViewport(SceneTextureParameters.SceneDepthTexture, View.ViewRect);

	CommonParameters.HZBInput = FScreenPassTexture(View.HZB);
	CommonParameters.GBufferA = FScreenPassTexture(SceneTextureParameters.GBufferATexture, View.ViewRect);
	CommonParameters.SceneDepth = FScreenPassTexture(SceneTextureParameters.SceneDepthTexture, View.ViewRect);

	CommonParameters.Levels = Levels;
	CommonParameters.ShaderQuality = FSSAOHelper::GetAmbientOcclusionShaderLevel(View);
	CommonParameters.DownscaleType = GetDownscaleSSAOType(View);
	CommonParameters.FullscreenType = GetFullscreenSSAOType(View, Levels);

	// If there is no temporal upsampling, we need a smooth pass to get rid of the grid pattern.
	// Pixel shader version has relatively smooth result so no need to do extra work.
	CommonParameters.bNeedSmoothingPass = CommonParameters.FullscreenType != ESSAOType::EPS && View.AntiAliasingMethod != AAM_TemporalAA && CVarSSAOSmoothPass.GetValueOnRenderThread();

	return CommonParameters;
}

FGTAOCommonParameters GetGTAOCommonParameters(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	EGTAOType GTAOType
	)
{
	const FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder, SceneTexturesUniformBuffer);

	FGTAOCommonParameters CommonParameters;
	CommonParameters.SceneTexturesUniformBuffer = SceneTexturesUniformBuffer;
	CommonParameters.SceneTexturesViewport = FScreenPassTextureViewport(SceneTextureParameters.SceneDepthTexture, View.ViewRect);

	CommonParameters.HZBInput = FScreenPassTexture(View.HZB);
	CommonParameters.SceneDepth = FScreenPassTexture(SceneTextureParameters.SceneDepthTexture, View.ViewRect);
	CommonParameters.SceneVelocity = FScreenPassTexture(SceneTextureParameters.GBufferVelocityTexture, View.ViewRect);

	CommonParameters.ShaderQuality = FSSAOHelper::GetAmbientOcclusionShaderLevel(View);
	CommonParameters.DownscaleFactor = CVarGTAODownsample.GetValueOnRenderThread() > 0 ? 2 : 1;
	CommonParameters.GTAOType = GTAOType;

	CommonParameters.DownsampledViewRect = GetDownscaledRect(View.ViewRect, CommonParameters.DownscaleFactor);

	return CommonParameters;
}

// Async Passes of the GTAO.
// This can either just be the Horizon search if GBuffer Normals are needed or it can be
// Combined Horizon search and Integrate followed by the Spatial filter if no normals are needed
static FGTAOHorizonSearchOutputs AddPostProcessingGTAOAsyncPasses(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FGTAOCommonParameters& CommonParameters,
	FScreenPassRenderTarget GTAOHorizons
	)
{
	check(CommonParameters.GTAOType == EGTAOType::EAsyncHorizonSearch || CommonParameters.GTAOType == EGTAOType::EAsyncCombinedSpatial);

	const bool bSpatialPass = (CVarGTAOSpatialFilter.GetValueOnRenderThread() == 1);

	FGTAOHorizonSearchOutputs HorizonSearchOutputs;

	if (CommonParameters.GTAOType == EGTAOType::EAsyncHorizonSearch)
	{
		HorizonSearchOutputs =
			AddGTAOHorizonSearchPass(
				GraphBuilder,
				View,
				CommonParameters,
				CommonParameters.SceneDepth,
				CommonParameters.HZBInput,
				GTAOHorizons);
	}
	else
	{
		HorizonSearchOutputs =
			AddGTAOHorizonSearchIntegratePass(
				GraphBuilder,
				View,
				CommonParameters,
				CommonParameters.SceneDepth,
				CommonParameters.HZBInput);

		if (bSpatialPass)
		{
			FScreenPassTexture SpatialOutput =
				AddGTAOSpatialFilter(
					GraphBuilder,
					View,
					CommonParameters,
					HorizonSearchOutputs.Color,
					CommonParameters.SceneDepth,
					GTAOHorizons);
		}
	}

	return MoveTemp(HorizonSearchOutputs);
}

// The whole GTAO stack is run on the Gfx Pipe
static FScreenPassTexture AddPostProcessingGTAOAllPasses(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FGTAOCommonParameters& CommonParameters,
	FScreenPassRenderTarget FinalTarget)
{
	FSceneViewState* ViewState = View.ViewState;

	const bool bSpatialPass = (CVarGTAOSpatialFilter.GetValueOnRenderThread() == 1);
	const bool bTemporalPass = (ViewState && CVarGTAOTemporalFilter.GetValueOnRenderThread() == 1);

	{
		FGTAOHorizonSearchOutputs HorizonSearchOutputs =
			AddGTAOHorizonSearchIntegratePass(
				GraphBuilder,
				View,
				CommonParameters,
				CommonParameters.SceneDepth,
				CommonParameters.HZBInput);

		FScreenPassTexture CurrentOutput = HorizonSearchOutputs.Color;
		if (bSpatialPass)
		{
			CurrentOutput =
				AddGTAOSpatialFilter(
					GraphBuilder,
					View,
					CommonParameters,
					CommonParameters.SceneDepth,
					CurrentOutput);
		}

		if (bTemporalPass)
		{
			const FGTAOTAAHistory& InputHistory = View.PrevViewInfo.GTAOHistory;
			FGTAOTAAHistory* OutputHistory = &View.ViewState->PrevFrameViewInfo.GTAOHistory;

			FScreenPassTextureViewport HistoryViewport(InputHistory.ReferenceBufferSize, InputHistory.ViewportRect);

			FScreenPassTexture HistoryColor;

			if (InputHistory.IsValid())
			{
				HistoryColor = FScreenPassTexture(GraphBuilder.RegisterExternalTexture(InputHistory.RT, TEXT("GTAOHistoryColor")), HistoryViewport.Rect);
			}
			else
			{
				HistoryColor = FScreenPassTexture(GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy, TEXT("GTAODummyTexture")));
			}

			FGTAOTemporalOutputs TemporalOutputs =
				AddGTAOTemporalPass(
					GraphBuilder,
					View,
					CommonParameters,
					CurrentOutput,
					CommonParameters.SceneDepth,
					CommonParameters.SceneVelocity,
					HistoryColor,
					HistoryViewport);

			OutputHistory->SafeRelease();
			GraphBuilder.QueueTextureExtraction(TemporalOutputs.OutputAO.Texture, &OutputHistory->RT);

			OutputHistory->ReferenceBufferSize = TemporalOutputs.TargetExtent;
			OutputHistory->ViewportRect = TemporalOutputs.ViewportRect;

			CurrentOutput = TemporalOutputs.OutputAO;
		}

		FScreenPassTexture FinalOutput = CurrentOutput;
		// TODO: Can't switch outputs since it's an external texture. Won't be a problem when we're fully over to RDG.
		//if (DownsampleFactor > 1)
		{
			FinalOutput =
				AddGTAOUpsamplePass(
					GraphBuilder,
					View,
					CommonParameters,
					CurrentOutput,
					CommonParameters.SceneDepth,
					FinalTarget);
		}
	}

	return MoveTemp(FinalTarget);
}

// These are the passes run after Async where some are run before on the Async pipe
static FScreenPassTexture AddPostProcessingGTAOPostAsync(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FGTAOCommonParameters& CommonParameters,
	FScreenPassTexture GTAOHorizons,
	FScreenPassRenderTarget FinalTarget)
{
	FSceneViewState* ViewState = View.ViewState;

	const bool bSpatialPass = (CVarGTAOSpatialFilter.GetValueOnRenderThread() == 1);
	const bool bTemporalPass = (ViewState && CVarGTAOTemporalFilter.GetValueOnRenderThread() == 1);

	{
		FScreenPassTexture CurrentOutput;

		if (CommonParameters.GTAOType == EGTAOType::EAsyncHorizonSearch)
		{
			CurrentOutput =
				AddGTAOInnerIntegratePass(
					GraphBuilder,
					View,
					CommonParameters,
					CommonParameters.SceneDepth,
					GTAOHorizons);

			if (bSpatialPass)
			{
				CurrentOutput =
					AddGTAOSpatialFilter(
						GraphBuilder,
						View,
						CommonParameters,
						CommonParameters.SceneDepth,
						CurrentOutput);
			}
		}
		else
		{
			// If the Spatial Filter is running as part of the async then we'll render to the R channel of the horizons texture so it can be read in as part of the temporal
			CurrentOutput = GTAOHorizons;
		}

		if (bTemporalPass)
		{
			const FGTAOTAAHistory& InputHistory = View.PrevViewInfo.GTAOHistory;
			FGTAOTAAHistory* OutputHistory = &ViewState->PrevFrameViewInfo.GTAOHistory;

			FScreenPassTextureViewport HistoryViewport(InputHistory.ReferenceBufferSize, InputHistory.ViewportRect);

			FScreenPassTexture HistoryColor;

			if (InputHistory.IsValid())
			{
				HistoryColor = FScreenPassTexture(GraphBuilder.RegisterExternalTexture(InputHistory.RT, TEXT("GTAOHistoryColor")), HistoryViewport.Rect);
			}
			else
			{
				HistoryColor = FScreenPassTexture(GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy, TEXT("GTAODummyTexture")));
			}

			FGTAOTemporalOutputs TemporalOutputs =
				AddGTAOTemporalPass(
					GraphBuilder,
					View,
					CommonParameters,
					CurrentOutput,
					CommonParameters.SceneDepth,
					CommonParameters.SceneVelocity,
					HistoryColor,
					HistoryViewport);

			OutputHistory->SafeRelease();
			GraphBuilder.QueueTextureExtraction(TemporalOutputs.OutputAO.Texture, &OutputHistory->RT);

			OutputHistory->ReferenceBufferSize = TemporalOutputs.TargetExtent;
			OutputHistory->ViewportRect = TemporalOutputs.ViewportRect;

			CurrentOutput = TemporalOutputs.OutputAO;
		}

		FScreenPassTexture FinalOutput = CurrentOutput;

		// TODO: Can't switch outputs since it's an external texture. Won't be a problem when we're fully over to RDG.
		//if (DownsampleFactor > 1)
		{
			FinalOutput =
				AddGTAOUpsamplePass(
					GraphBuilder,
					View,
					CommonParameters,
					CurrentOutput,
					CommonParameters.SceneDepth,
					FinalTarget);
		}
	}

	return MoveTemp(FinalTarget);
}

// @param Levels 0..3, how many different resolution levels we want to render
static FScreenPassTexture AddPostProcessingAmbientOcclusion(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FSSAOCommonParameters& CommonParameters,
	FScreenPassRenderTarget FinalTarget)
{
	check(CommonParameters.Levels >= 0 && CommonParameters.Levels <= 3);

	FScreenPassTexture AmbientOcclusionInMip1;
	FScreenPassTexture AmbientOcclusionPassMip1;
	if (CommonParameters.Levels >= 2)
	{
		AmbientOcclusionInMip1 =
			AddAmbientOcclusionSetupPass(
				GraphBuilder,
				View,
				CommonParameters,
				CommonParameters.SceneDepth);

		FScreenPassTexture AmbientOcclusionPassMip2;
		if (CommonParameters.Levels >= 3)
		{
			FScreenPassTexture AmbientOcclusionInMip2 =
				AddAmbientOcclusionSetupPass(
					GraphBuilder,
					View,
					CommonParameters,
					AmbientOcclusionInMip1);

			AmbientOcclusionPassMip2 =
				AddAmbientOcclusionStepPass(
					GraphBuilder,
					View,
					CommonParameters,
					AmbientOcclusionInMip2,
					AmbientOcclusionInMip2,
					FScreenPassTexture(),
					CommonParameters.HZBInput);
		}

		AmbientOcclusionPassMip1 =
			AddAmbientOcclusionStepPass(
				GraphBuilder,
				View,
				CommonParameters,
				AmbientOcclusionInMip1,
				AmbientOcclusionInMip1,
				AmbientOcclusionPassMip2,
				CommonParameters.HZBInput);
	}

	FScreenPassTexture FinalOutput =
		AddAmbientOcclusionFinalPass(
			GraphBuilder,
			View,
			CommonParameters,
			CommonParameters.GBufferA,
			AmbientOcclusionInMip1,
			AmbientOcclusionPassMip1,
			CommonParameters.HZBInput,
			FinalTarget);

	return FinalOutput;
}

namespace CompositionLighting
{

void ProcessBeforeBasePass(
	FRDGBuilder& GraphBuilder,
	TArrayView<const FViewInfo> Views,
	const FSceneTextures& SceneTextures,
	FDBufferTextures& DBufferTextures)
{
	check(Views.Num());

	const EShaderPlatform ShaderPlatform = Views[0].GetShaderPlatform();
	const bool bDBuffer = IsDBufferEnabled(*Views[0].Family, ShaderPlatform);

	const auto ProcessView = [&](const FViewInfo& View, uint32 SSAOLevels)
	{
		const bool bNeedSSAO = SSAOLevels && FSSAOHelper::GetGTAOPassType(View, SSAOLevels) != EGTAOType::ENonAsync;

		// so that the passes can register themselves to the graph
		if (bDBuffer || bNeedSSAO)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "CompositionBeforeBasePass");
			RDG_GPU_STAT_SCOPE(GraphBuilder, CompositionBeforeBasePass);
			View.BeginRenderView();

			// decals are before AmbientOcclusion so the decal can output a normal that AO is affected by
			if (bDBuffer)
			{
				FDeferredDecalPassTextures DecalPassTextures = GetDeferredDecalPassTextures(GraphBuilder, SceneTextures, &DBufferTextures);
				AddDeferredDecalPass(GraphBuilder, View, DecalPassTextures, DRS_BeforeBasePass);
			}

			if (bNeedSSAO)
			{
				FSSAOCommonParameters Parameters = GetSSAOCommonParameters(GraphBuilder, View, SceneTextures.UniformBuffer, SSAOLevels);
				FScreenPassRenderTarget FinalTarget = FScreenPassRenderTarget(SceneTextures.ScreenSpaceAO, View.ViewRect, ERenderTargetLoadAction::ENoAction);

				AddPostProcessingAmbientOcclusion(
					GraphBuilder,
					View,
					Parameters,
					FinalTarget);
			}
		}
	};

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		uint32 SSAOLevels = FSSAOHelper::ComputeAmbientOcclusionPassCount(View);

		// In forward, if depth prepass is off - as SSAO here requires a valid HZB buffer - disable SSAO.
		if (!IsForwardShadingEnabled(ShaderPlatform) || !View.HZB || FSSAOHelper::IsAmbientOcclusionAsyncCompute(View, SSAOLevels))
		{
			SSAOLevels = 0;
		}

		ProcessView(View, SSAOLevels);
	}
}

void ProcessAfterBasePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FSceneTextures& SceneTextures,
	const FAsyncResults& AsyncResults,
	bool bEnableSSAO)
{
	if (!CanOverlayRayTracingOutput(View))
	{
		return;
	}

	const FSceneViewFamily& ViewFamily = *View.Family;

	RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
	RDG_EVENT_SCOPE(GraphBuilder, "LightCompositionTasks_PreLighting");
	RDG_GPU_STAT_SCOPE(GraphBuilder, CompositionPreLighting);

	View.BeginRenderView();

	// decal are distracting when looking at LightCulling.
	const bool bDoDecal = ViewFamily.EngineShowFlags.Decals && !ViewFamily.EngineShowFlags.VisualizeLightCulling;

	FDeferredDecalPassTextures DecalPassTextures = GetDeferredDecalPassTextures(GraphBuilder, SceneTextures, nullptr);

	if (ViewFamily.EngineShowFlags.Decals && !ViewFamily.EngineShowFlags.ShaderComplexity)
	{
		AddDeferredDecalPass(GraphBuilder, View, DecalPassTextures, DRS_AfterBasePass);
	}

	if (bDoDecal && IsUsingGBuffers(View.GetShaderPlatform()))
	{
		// decals are before AmbientOcclusion so the decal can output a normal that AO is affected by
		AddDeferredDecalPass(GraphBuilder, View, DecalPassTextures, DRS_BeforeLighting);
	}

	if (bDoDecal && !IsSimpleForwardShadingEnabled(View.GetShaderPlatform()))
	{
		// DBuffer decals with emissive component
		AddDeferredDecalPass(GraphBuilder, View, DecalPassTextures, DRS_Emissive);
	}

	// Forward shading SSAO is applied before the base pass using only the depth buffer.
	if (!IsForwardShadingEnabled(View.GetShaderPlatform()))
	{
		const uint32 SSAOLevels = FSSAOHelper::ComputeAmbientOcclusionPassCount(View);
		if (SSAOLevels && bEnableSSAO)
		{
			const bool bScreenSpaceAOIsProduced = SceneTextures.ScreenSpaceAO->HasBeenProduced();
			FScreenPassRenderTarget FinalTarget = FScreenPassRenderTarget(SceneTextures.ScreenSpaceAO, View.ViewRect, bScreenSpaceAOIsProduced ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction);

			FScreenPassTexture AmbientOcclusion;
#if RHI_RAYTRACING
			if (ShouldRenderRayTracingAmbientOcclusion(View) && bScreenSpaceAOIsProduced)
			{
				AmbientOcclusion = FinalTarget;
			}
#endif

			const EGTAOType GTAOType = FSSAOHelper::GetGTAOPassType(View, SSAOLevels);

			// If doing the Split GTAO method then we need to do the second part here.
			if (GTAOType == EGTAOType::EAsyncHorizonSearch || GTAOType == EGTAOType::EAsyncCombinedSpatial)
			{
				check(AsyncResults.HorizonsTexture);

				FGTAOCommonParameters Parameters = GetGTAOCommonParameters(GraphBuilder, View, SceneTextures.UniformBuffer, GTAOType);

				FScreenPassTexture GTAOHorizons(AsyncResults.HorizonsTexture, Parameters.DownsampledViewRect);
				AmbientOcclusion = AddPostProcessingGTAOPostAsync(GraphBuilder, View, Parameters, GTAOHorizons, FinalTarget);

				ensureMsgf(
					FDecalRendering::BuildVisibleDecalList(*(FScene*)View.Family->Scene, View, DRS_AmbientOcclusion, nullptr) == false,
					TEXT("Ambient occlusion decals are not supported with Async compute SSAO."));
			}
			else
			{
				if (GTAOType == EGTAOType::ENonAsync)
				{
					FGTAOCommonParameters Parameters = GetGTAOCommonParameters(GraphBuilder, View, SceneTextures.UniformBuffer, GTAOType);
					AmbientOcclusion = AddPostProcessingGTAOAllPasses(GraphBuilder, View, Parameters, FinalTarget);
				}
				else
				{
					FSSAOCommonParameters Parameters = GetSSAOCommonParameters(GraphBuilder, View, SceneTextures.UniformBuffer, SSAOLevels);
					AmbientOcclusion = AddPostProcessingAmbientOcclusion(GraphBuilder, View, Parameters, FinalTarget);
				}

				if (bDoDecal)
				{
					DecalPassTextures.ScreenSpaceAO = AmbientOcclusion.Texture;
					AddDeferredDecalPass(GraphBuilder, View, DecalPassTextures, DRS_AmbientOcclusion);
				}
			}
		}
	}
}

bool CanProcessAsync(TArrayView<const FViewInfo> Views)
{
	bool bAnyAsyncSSAO = true;
	for (int32 i = 0; i < Views.Num(); ++i)
	{
		uint32 Levels = FSSAOHelper::ComputeAmbientOcclusionPassCount(Views[i]);
		if (!FSSAOHelper::IsAmbientOcclusionAsyncCompute(Views[i], Levels) || !CanOverlayRayTracingOutput(Views[i]))
		{
			bAnyAsyncSSAO = false;
			break;
		}
	}
	return bAnyAsyncSSAO;
}

FAsyncResults ProcessAsync(FRDGBuilder& GraphBuilder, TArrayView<const FViewInfo> Views, const FMinimalSceneTextures& SceneTextures)
{
	FAsyncResults AsyncResults;

	RDG_ASYNC_COMPUTE_BUDGET_SCOPE(GraphBuilder, FSSAOHelper::GetAmbientOcclusionAsyncComputeBudget());

	for (const FViewInfo& View : Views)
	{
		const uint32 Levels = FSSAOHelper::ComputeAmbientOcclusionPassCount(View);
		const EGTAOType GTAOType = FSSAOHelper::GetGTAOPassType(View, Levels);

		if (GTAOType == EGTAOType::EAsyncCombinedSpatial || GTAOType == EGTAOType::EAsyncHorizonSearch)
		{
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

			FGTAOCommonParameters CommonParameters = GetGTAOCommonParameters(GraphBuilder, View, SceneTextures.UniformBuffer, GTAOType);

			{
				const FIntPoint HorizonTextureSize = FIntPoint::DivideAndRoundUp(SceneTextures.Extent, CommonParameters.DownscaleFactor);
				const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(HorizonTextureSize, PF_R8G8, FClearValueBinding::White, TexCreate_UAV | TexCreate_RenderTargetable);
				AsyncResults.HorizonsTexture = GraphBuilder.CreateTexture(Desc, TEXT("ScreenSpaceGTAOHorizons"));
			}

			FScreenPassRenderTarget GTAOHorizons(AsyncResults.HorizonsTexture, CommonParameters.DownsampledViewRect, ERenderTargetLoadAction::ENoAction);

			AddPostProcessingGTAOAsyncPasses(
				GraphBuilder,
				View,
				CommonParameters,
				GTAOHorizons);
		}
	}

	return AsyncResults;
}

} //! namespace CompositionLighting