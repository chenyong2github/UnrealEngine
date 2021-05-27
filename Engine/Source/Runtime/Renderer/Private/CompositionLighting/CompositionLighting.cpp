// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "SceneTextureParameters.h"
#include "RenderGraphUtils.h"

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
	TUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBufferRHI,
	uint32 Levels,
	bool bAllowGBufferRead)
{
	const FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder, SceneTexturesUniformBuffer);

	FSSAOCommonParameters CommonParameters;
	CommonParameters.SceneTexturesUniformBuffer = SceneTexturesUniformBuffer;
	CommonParameters.SceneTexturesUniformBufferRHI = SceneTexturesUniformBufferRHI;
	CommonParameters.SceneTexturesViewport = FScreenPassTextureViewport(SceneTextureParameters.SceneDepthTexture, View.ViewRect);

	CommonParameters.HZBInput = FScreenPassTexture(GraphBuilder.RegisterExternalTexture(View.HZB, TEXT("HZBInput")));
	CommonParameters.GBufferA = bAllowGBufferRead ? FScreenPassTexture(SceneTextureParameters.GBufferATexture, View.ViewRect) : FScreenPassTexture();
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
	TUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBufferRHI,
	EGTAOType GTAOType
	)
{
	const FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder, SceneTexturesUniformBuffer);

	FGTAOCommonParameters CommonParameters;
	CommonParameters.SceneTexturesUniformBuffer = SceneTexturesUniformBuffer;
	CommonParameters.SceneTexturesUniformBufferRHI = SceneTexturesUniformBufferRHI;
	CommonParameters.SceneTexturesViewport = FScreenPassTextureViewport(SceneTextureParameters.SceneDepthTexture, View.ViewRect);

	CommonParameters.HZBInput = FScreenPassTexture(GraphBuilder.RegisterExternalTexture(View.HZB, TEXT("HZBInput")));
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

void FCompositionLighting::Reset()
{
	DecalPassTextures = {};
}

void FCompositionLighting::ProcessBeforeBasePass(
	FRDGBuilder& GraphBuilder,
	FPersistentUniformBuffers& UniformBuffers,
	const FViewInfo& View,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	bool bDBuffer,
	uint32 SSAOLevels)
{
	check(IsInRenderingThread());

	const bool bNeedSSAO = SSAOLevels && FSSAOHelper::GetGTAOPassType(View, SSAOLevels) != EGTAOType::ENonAsync;

	// so that the passes can register themselves to the graph
	if (bDBuffer || bNeedSSAO)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

		RDG_EVENT_SCOPE(GraphBuilder, "CompositionBeforeBasePass");
		RDG_GPU_STAT_SCOPE(GraphBuilder, CompositionBeforeBasePass);

		AddPass(GraphBuilder, [&UniformBuffers, &View](FRHICommandList&)
		{
			UniformBuffers.UpdateViewUniformBuffer(View);
		});

		// decals are before AmbientOcclusion so the decal can output a normal that AO is affected by
		if (bDBuffer)
		{
			if (!DecalPassTextures.DecalPassUniformBuffer)
			{
				DecalPassTextures = GetDeferredDecalPassTextures(GraphBuilder, View);
			}
			AddDeferredDecalPass(GraphBuilder, View, DecalPassTextures, DRS_BeforeBasePass);
		}

		if (bNeedSSAO)
		{
			TUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBufferRHI = CreateSceneTextureUniformBuffer(GraphBuilder.RHICmdList, View.FeatureLevel, ESceneTextureSetupMode::SceneDepth);
			FSSAOCommonParameters Parameters = GetSSAOCommonParameters(GraphBuilder, View, SceneTexturesUniformBuffer, SceneTexturesUniformBufferRHI, SSAOLevels, false);
			FScreenPassRenderTarget FinalTarget = FScreenPassRenderTarget(GraphBuilder.RegisterExternalTexture(SceneContext.ScreenSpaceAO, TEXT("AmbientOcclusionDirect")), View.ViewRect, ERenderTargetLoadAction::ENoAction);

			AddPostProcessingAmbientOcclusion(
				GraphBuilder,
				View,
				Parameters,
				FinalTarget);
			SceneContext.bScreenSpaceAOIsValid = true;
		}
	}
}

void FCompositionLighting::ProcessAfterBasePass(
	FRDGBuilder& GraphBuilder,
	FPersistentUniformBuffers& UniformBuffers,
	const FViewInfo& View,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	if (CanOverlayRayTracingOutput(View))
	{
		const FSceneViewFamily& ViewFamily = *View.Family;

		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE(GraphBuilder, "LightCompositionTasks_PreLighting");
		RDG_GPU_STAT_SCOPE(GraphBuilder, CompositionPreLighting);

		AddPass(GraphBuilder, [&UniformBuffers, &View](FRHICommandList&)
		{
			UniformBuffers.UpdateViewUniformBuffer(View);
		});

		// decal are distracting when looking at LightCulling.
		const bool bDoDecal = ViewFamily.EngineShowFlags.Decals && !ViewFamily.EngineShowFlags.VisualizeLightCulling;

		if (!DecalPassTextures.DecalPassUniformBuffer)
		{
			DecalPassTextures = GetDeferredDecalPassTextures(GraphBuilder, View);
		}

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
			FScreenPassRenderTarget FinalTarget = FScreenPassRenderTarget(GraphBuilder.RegisterExternalTexture(SceneContext.ScreenSpaceAO, TEXT("AmbientOcclusionDirect")), View.ViewRect, ERenderTargetLoadAction::ENoAction);

			FScreenPassTexture AmbientOcclusion;
#if RHI_RAYTRACING
			if (ShouldRenderRayTracingAmbientOcclusion(View) && SceneContext.bScreenSpaceAOIsValid)
			{
				AmbientOcclusion = FinalTarget;
			}
#endif

			const uint32 SSAOLevels = FSSAOHelper::ComputeAmbientOcclusionPassCount(View);
			if (SSAOLevels)
			{
				const EGTAOType GTAOType = FSSAOHelper::GetGTAOPassType(View, SSAOLevels);

				TUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBufferRHI = CreateSceneTextureUniformBuffer(GraphBuilder.RHICmdList, View.FeatureLevel);

				// If doing the Split GTAO method then we need to do the second part here.
				if (GTAOType == EGTAOType::EAsyncHorizonSearch || GTAOType == EGTAOType::EAsyncCombinedSpatial)
				{
					FGTAOCommonParameters Parameters = GetGTAOCommonParameters(GraphBuilder, View, SceneTexturesUniformBuffer, SceneTexturesUniformBufferRHI, GTAOType);

					FScreenPassTexture GTAOHorizons(GraphBuilder.RegisterExternalTexture(SceneContext.ScreenSpaceGTAOHorizons, TEXT("GTAOHorizons")), Parameters.DownsampledViewRect);
					AmbientOcclusion = AddPostProcessingGTAOPostAsync(GraphBuilder, View, Parameters, GTAOHorizons, FinalTarget);

					ensureMsgf(
						FDecalRendering::BuildVisibleDecalList(*(FScene*)View.Family->Scene, View, DRS_AmbientOcclusion, nullptr) == false,
						TEXT("Ambient occlusion decals are not supported with Async compute SSAO."));
				}
				else
				{
					if (GTAOType == EGTAOType::ENonAsync)
					{
						FGTAOCommonParameters Parameters = GetGTAOCommonParameters(GraphBuilder, View, SceneTexturesUniformBuffer, SceneTexturesUniformBufferRHI, GTAOType);
						AmbientOcclusion = AddPostProcessingGTAOAllPasses(GraphBuilder, View, Parameters, FinalTarget);
					}
					else
					{
						FSSAOCommonParameters Parameters = GetSSAOCommonParameters(GraphBuilder, View, SceneTexturesUniformBuffer, SceneTexturesUniformBufferRHI, SSAOLevels, true);
						AmbientOcclusion = AddPostProcessingAmbientOcclusion(GraphBuilder, View, Parameters, FinalTarget);
					}

					if (bDoDecal)
					{
						DecalPassTextures.ScreenSpaceAO = AmbientOcclusion.Texture;
						AddDeferredDecalPass(GraphBuilder, View, DecalPassTextures, DRS_AmbientOcclusion);
					}
				}

				SceneContext.bScreenSpaceAOIsValid = true;
			}
		}
	}
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

bool FCompositionLighting::CanProcessAsyncSSAO(const TArray<FViewInfo>& Views)
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

void FCompositionLighting::ProcessAsyncSSAO(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer)
{
	RDG_ASYNC_COMPUTE_BUDGET_SCOPE(GraphBuilder, FSSAOHelper::GetAmbientOcclusionAsyncComputeBudget());

	for (const FViewInfo& View : Views)
	{
		const uint32 Levels = FSSAOHelper::ComputeAmbientOcclusionPassCount(View);
		const EGTAOType GTAOType = FSSAOHelper::GetGTAOPassType(View, Levels);

		if (GTAOType == EGTAOType::EAsyncCombinedSpatial || GTAOType == EGTAOType::EAsyncHorizonSearch)
		{
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

			TUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBufferRHI = CreateSceneTextureUniformBuffer(GraphBuilder.RHICmdList, View.FeatureLevel, ESceneTextureSetupMode::SceneDepth);
			FGTAOCommonParameters CommonParameters = GetGTAOCommonParameters(GraphBuilder, View, SceneTexturesUniformBuffer, SceneTexturesUniformBufferRHI, GTAOType);

			FRDGTextureRef GTAOHorizonsTexture = nullptr;

			{
				FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
				FIntPoint BufferSize = SceneContext.GetBufferSizeXY();
				FIntPoint HorizonBufferSize = FIntPoint::DivideAndRoundUp(BufferSize, CommonParameters.DownscaleFactor);

				FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(HorizonBufferSize, PF_R32_FLOAT, FClearValueBinding::White, TexCreate_RenderTargetable));
				if (SceneContext.GetCurrentFeatureLevel() >= ERHIFeatureLevel::SM5)
				{
					Desc.Flags |= TexCreate_UAV;
				}

				Desc.Format = PF_R8G8;
				GTAOHorizonsTexture = GraphBuilder.CreateTexture(Desc, TEXT("ScreenSpaceGTAOHorizons"));
				ConvertToExternalTexture(GraphBuilder, GTAOHorizonsTexture, SceneContext.ScreenSpaceGTAOHorizons);
			}

			FScreenPassRenderTarget GTAOHorizons(GTAOHorizonsTexture, CommonParameters.DownsampledViewRect, ERenderTargetLoadAction::ENoAction);

			AddPostProcessingGTAOAsyncPasses(
				GraphBuilder,
				View,
				CommonParameters,
				GTAOHorizons);
		}
	}
}