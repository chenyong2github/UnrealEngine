// Copyright Epic Games, Inc. All Rights Reserved.

#include "TranslucentRendering.h"
#include "DeferredShadingRenderer.h"
#include "BasePassRendering.h"
#include "DynamicPrimitiveDrawing.h"
#include "RendererModule.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"
#include "ScreenRendering.h"
#include "ScreenPass.h"
#include "MeshPassProcessor.inl"
#include "VolumetricRenderTarget.h"
#include "VariableRateShadingImageManager.h"

DECLARE_CYCLE_STAT(TEXT("TranslucencyTimestampQueryFence Wait"), STAT_TranslucencyTimestampQueryFence_Wait, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("TranslucencyTimestampQuery Wait"), STAT_TranslucencyTimestampQuery_Wait, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("Translucency"), STAT_CLP_Translucency, STATGROUP_ParallelCommandListMarkers);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Translucency GPU Time (MS)"), STAT_TranslucencyGPU, STATGROUP_SceneRendering);
DEFINE_GPU_DRAWCALL_STAT(Translucency);

static TAutoConsoleVariable<float> CVarSeparateTranslucencyScreenPercentage(
	TEXT("r.SeparateTranslucencyScreenPercentage"),
	100.0f,
	TEXT("Render separate translucency at this percentage of the full resolution.\n")
	TEXT("in percent, >0 and <=100, larger numbers are possible (supersampling).")
	TEXT("<0 is treated like 100."),
	ECVF_Scalability | ECVF_Default);

static TAutoConsoleVariable<int32> CVarSeparateTranslucencyAutoDownsample(
	TEXT("r.SeparateTranslucencyAutoDownsample"),
	0,
	TEXT("Whether to automatically downsample separate translucency based on last frame's GPU time.\n")
	TEXT("Automatic downsampling is only used when r.SeparateTranslucencyScreenPercentage is 100"),
	ECVF_Scalability | ECVF_Default);

static TAutoConsoleVariable<float> CVarSeparateTranslucencyDurationDownsampleThreshold(
	TEXT("r.SeparateTranslucencyDurationDownsampleThreshold"),
	1.5f,
	TEXT("When smoothed full-res translucency GPU duration is larger than this value (ms), the entire pass will be downsampled by a factor of 2 in each dimension."),
	ECVF_Scalability | ECVF_Default);

static TAutoConsoleVariable<float> CVarSeparateTranslucencyDurationUpsampleThreshold(
	TEXT("r.SeparateTranslucencyDurationUpsampleThreshold"),
	.5f,
	TEXT("When smoothed half-res translucency GPU duration is smaller than this value (ms), the entire pass will be restored to full resolution.\n")
	TEXT("This should be around 1/4 of r.SeparateTranslucencyDurationDownsampleThreshold to avoid toggling downsampled state constantly."),
	ECVF_Scalability | ECVF_Default);

static TAutoConsoleVariable<float> CVarSeparateTranslucencyMinDownsampleChangeTime(
	TEXT("r.SeparateTranslucencyMinDownsampleChangeTime"),
	1.0f,
	TEXT("Minimum time in seconds between changes to automatic downsampling state, used to prevent rapid swapping between half and full res."),
	ECVF_Scalability | ECVF_Default);

int32 GSeparateTranslucencyUpsampleMode = 1;
static FAutoConsoleVariableRef CVarSeparateTranslucencyUpsampleMode(
	TEXT("r.SeparateTranslucencyUpsampleMode"),
	GSeparateTranslucencyUpsampleMode,
	TEXT("Upsample method to use on separate translucency.  These are only used when r.SeparateTranslucencyScreenPercentage is less than 100.\n")
	TEXT("0: bilinear 1: Nearest-Depth Neighbor (only when r.SeparateTranslucencyScreenPercentage is 50)"),
	ECVF_Scalability | ECVF_Default);

static TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasksTranslucentPass(
	TEXT("r.RHICmdFlushRenderThreadTasksTranslucentPass"),
	0,
	TEXT("Wait for completion of parallel render thread tasks at the end of the translucent pass.  A more granular version of r.RHICmdFlushRenderThreadTasks. If either r.RHICmdFlushRenderThreadTasks or r.RHICmdFlushRenderThreadTasksTranslucentPass is > 0 we will flush."));

static TAutoConsoleVariable<int32> CVarParallelTranslucency(
	TEXT("r.ParallelTranslucency"),
	1,
	TEXT("Toggles parallel translucency rendering. Parallel rendering must be enabled for this to have an effect."),
	ECVF_RenderThreadSafe);

static const TCHAR* TranslucencyPassToString(ETranslucencyPass::Type TranslucencyPass)
{
	switch (TranslucencyPass)
	{
	case ETranslucencyPass::TPT_StandardTranslucency:
		return TEXT("Standard");
	case ETranslucencyPass::TPT_TranslucencyAfterDOF:
		return TEXT("AfterDOF");
	case ETranslucencyPass::TPT_TranslucencyAfterDOFModulate:
		return TEXT("AfterDOFModulate");
	case ETranslucencyPass::TPT_AllTranslucency:
		return TEXT("All");
	}
	checkNoEntry();
	return TEXT("");
}

EMeshPass::Type TranslucencyPassToMeshPass(ETranslucencyPass::Type TranslucencyPass)
{
	EMeshPass::Type TranslucencyMeshPass = EMeshPass::Num;

	switch (TranslucencyPass)
	{
	case ETranslucencyPass::TPT_StandardTranslucency: TranslucencyMeshPass = EMeshPass::TranslucencyStandard; break;
	case ETranslucencyPass::TPT_TranslucencyAfterDOF: TranslucencyMeshPass = EMeshPass::TranslucencyAfterDOF; break;
	case ETranslucencyPass::TPT_TranslucencyAfterDOFModulate: TranslucencyMeshPass = EMeshPass::TranslucencyAfterDOFModulate; break;
	case ETranslucencyPass::TPT_AllTranslucency: TranslucencyMeshPass = EMeshPass::TranslucencyAll; break;
	}

	check(TranslucencyMeshPass != EMeshPass::Num);

	return TranslucencyMeshPass;
}

ETranslucencyView GetTranslucencyView(const FViewInfo& View)
{
#if RHI_RAYTRACING
	if (ShouldRenderRayTracingTranslucency(View))
	{
		return ETranslucencyView::RayTracing;
	}
#endif
	return View.IsUnderwater() ? ETranslucencyView::UnderWater : ETranslucencyView::AboveWater;
}

ETranslucencyView GetTranslucencyViews(TArrayView<const FViewInfo> Views)
{
	ETranslucencyView TranslucencyViews = ETranslucencyView::None;
	for (const FViewInfo& View : Views)
	{
		TranslucencyViews |= GetTranslucencyView(View);
	}
	return TranslucencyViews;
}

/** Mostly used to know if debug rendering should be drawn in this pass */
static bool IsMainTranslucencyPass(ETranslucencyPass::Type TranslucencyPass)
{
	return TranslucencyPass == ETranslucencyPass::TPT_AllTranslucency || TranslucencyPass == ETranslucencyPass::TPT_StandardTranslucency;
}

static bool IsParallelTranslucencyEnabled()
{
	return GRHICommandList.UseParallelAlgorithms() && CVarParallelTranslucency.GetValueOnRenderThread();
}

static bool IsTranslucencyWaitForTasksEnabled()
{
	return IsParallelTranslucencyEnabled() && (CVarRHICmdFlushRenderThreadTasksTranslucentPass.GetValueOnRenderThread() > 0 || CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() > 0);
}

static bool IsSeparateTranslucencyEnabled(ETranslucencyPass::Type TranslucencyPass, float DownsampleScale)
{
	// Currently AfterDOF is rendered earlier in the frame and must be rendered in a separate texture.
	if (TranslucencyPass == ETranslucencyPass::TPT_TranslucencyAfterDOF || TranslucencyPass == ETranslucencyPass::TPT_TranslucencyAfterDOFModulate)
	{
		return true;
	}

	// Otherwise it only gets rendered in the separate buffer if it is downsampled.
	if (DownsampleScale < 1.0f)
	{
		return true;
	}

	return false;
}

static void AddBeginTranslucencyTimerPass(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
#if STATS
	if (View.ViewState)
	{
		AddPass(GraphBuilder, [&View](FRHICommandListImmediate& RHICmdList)
		{
			View.ViewState->TranslucencyTimer.Begin(RHICmdList);
		});
	}
#endif
}

static void AddEndTranslucencyTimerPass(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
#if STATS
	if (View.ViewState)
	{
		AddPass(GraphBuilder, [&View](FRHICommandListImmediate& RHICmdList)
		{
			View.ViewState->TranslucencyTimer.End(RHICmdList);
		});
	}
#endif
}

static bool HasSeparateTranslucencyTimer(const FViewInfo& View)
{
	return View.ViewState && GSupportsTimestampRenderQueries
#if !STATS
		&& (CVarSeparateTranslucencyAutoDownsample.GetValueOnRenderThread() != 0)
#endif
		;
}

static void AddBeginSeparateTranslucencyTimerPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, ETranslucencyPass::Type TranslucencyPass)
{
	if (HasSeparateTranslucencyTimer(View))
	{
		AddPass(GraphBuilder, [&View, TranslucencyPass](FRHICommandListImmediate& RHICmdList)
		{
			if (TranslucencyPass == ETranslucencyPass::TPT_TranslucencyAfterDOFModulate)
			{
				View.ViewState->SeparateTranslucencyModulateTimer.Begin(RHICmdList);
			}
			else
			{
				View.ViewState->SeparateTranslucencyTimer.Begin(RHICmdList);
			}
		});
	}
}

static void AddEndSeparateTranslucencyTimerPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, ETranslucencyPass::Type TranslucencyPass)
{
	if (HasSeparateTranslucencyTimer(View))
	{
		AddPass(GraphBuilder, [&View, TranslucencyPass](FRHICommandListImmediate& RHICmdList)
		{
			if (TranslucencyPass == ETranslucencyPass::TPT_TranslucencyAfterDOFModulate)
			{
				View.ViewState->SeparateTranslucencyModulateTimer.End(RHICmdList);
			}
			else
			{
				View.ViewState->SeparateTranslucencyTimer.End(RHICmdList);
			}
		});
	}
}

FSeparateTranslucencyDimensions UpdateTranslucencyTimers(FRHICommandListImmediate& RHICmdList, TArrayView<const FViewInfo> Views)
{
	bool bAnyViewWantsDownsampledSeparateTranslucency = false;

#if STATS
	const bool bSeparateTranslucencyAutoDownsample = CVarSeparateTranslucencyAutoDownsample.GetValueOnRenderThread() != 0;
#else
	const bool bSeparateTranslucencyAutoDownsample = false;
#endif

	if (bSeparateTranslucencyAutoDownsample)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			SCOPED_GPU_MASK(RHICmdList, View.GPUMask);
			FSceneViewState* ViewState = View.ViewState;

			if (ViewState)
			{
				//We always tick the separate trans timer but only need the other timer for stats
				bool bSeparateTransTimerSuccess = ViewState->SeparateTranslucencyTimer.Tick(RHICmdList);
				bool bSeparateTransModulateTimerSuccess = ViewState->SeparateTranslucencyModulateTimer.Tick(RHICmdList);

				if (STATS)
				{
					ViewState->TranslucencyTimer.Tick(RHICmdList);
					//Stats are fed the most recent available time and so are lagged a little. 
					float MostRecentTotalTime = ViewState->TranslucencyTimer.GetTimeMS() +
						ViewState->SeparateTranslucencyTimer.GetTimeMS() +
						ViewState->SeparateTranslucencyModulateTimer.GetTimeMS();
					SET_FLOAT_STAT(STAT_TranslucencyGPU, MostRecentTotalTime);
				}

				if (bSeparateTranslucencyAutoDownsample && bSeparateTransTimerSuccess)
				{
					float LastFrameTranslucencyDurationMS = ViewState->SeparateTranslucencyTimer.GetTimeMS() + ViewState->SeparateTranslucencyModulateTimer.GetTimeMS();
					const bool bOriginalShouldAutoDownsampleTranslucency = ViewState->bShouldAutoDownsampleTranslucency;

					if (ViewState->bShouldAutoDownsampleTranslucency)
					{
						ViewState->SmoothedFullResTranslucencyGPUDuration = 0;
						const float LerpAlpha = ViewState->SmoothedHalfResTranslucencyGPUDuration == 0 ? 1.0f : .1f;
						ViewState->SmoothedHalfResTranslucencyGPUDuration = FMath::Lerp(ViewState->SmoothedHalfResTranslucencyGPUDuration, LastFrameTranslucencyDurationMS, LerpAlpha);

						// Don't re-asses switching for some time after the last switch
						if (View.Family->CurrentRealTime - ViewState->LastAutoDownsampleChangeTime > CVarSeparateTranslucencyMinDownsampleChangeTime.GetValueOnRenderThread())
						{
							// Downsample if the smoothed time is larger than the threshold
							ViewState->bShouldAutoDownsampleTranslucency = ViewState->SmoothedHalfResTranslucencyGPUDuration > CVarSeparateTranslucencyDurationUpsampleThreshold.GetValueOnRenderThread();

							if (!ViewState->bShouldAutoDownsampleTranslucency)
							{
								// Do 'log LogRenderer verbose' to get these
								UE_LOG(LogRenderer, Verbose, TEXT("Upsample: %.1fms < %.1fms"), ViewState->SmoothedHalfResTranslucencyGPUDuration, CVarSeparateTranslucencyDurationUpsampleThreshold.GetValueOnRenderThread());
							}
						}
					}
					else
					{
						ViewState->SmoothedHalfResTranslucencyGPUDuration = 0;
						const float LerpAlpha = ViewState->SmoothedFullResTranslucencyGPUDuration == 0 ? 1.0f : .1f;
						ViewState->SmoothedFullResTranslucencyGPUDuration = FMath::Lerp(ViewState->SmoothedFullResTranslucencyGPUDuration, LastFrameTranslucencyDurationMS, LerpAlpha);

						if (View.Family->CurrentRealTime - ViewState->LastAutoDownsampleChangeTime > CVarSeparateTranslucencyMinDownsampleChangeTime.GetValueOnRenderThread())
						{
							// Downsample if the smoothed time is larger than the threshold
							ViewState->bShouldAutoDownsampleTranslucency = ViewState->SmoothedFullResTranslucencyGPUDuration > CVarSeparateTranslucencyDurationDownsampleThreshold.GetValueOnRenderThread();

							if (ViewState->bShouldAutoDownsampleTranslucency)
							{
								UE_LOG(LogRenderer, Verbose, TEXT("Downsample: %.1fms > %.1fms"), ViewState->SmoothedFullResTranslucencyGPUDuration, CVarSeparateTranslucencyDurationDownsampleThreshold.GetValueOnRenderThread());
							}
						}
					}

					if (bOriginalShouldAutoDownsampleTranslucency != ViewState->bShouldAutoDownsampleTranslucency)
					{
						ViewState->LastAutoDownsampleChangeTime = View.Family->CurrentRealTime;
					}

					bAnyViewWantsDownsampledSeparateTranslucency = bAnyViewWantsDownsampledSeparateTranslucency || ViewState->bShouldAutoDownsampleTranslucency;
				}
			}
		}
	}

	float EffectiveScale = FMath::Clamp(CVarSeparateTranslucencyScreenPercentage.GetValueOnRenderThread() / 100.0f, 0.0f, 1.0f);

	// 'r.SeparateTranslucencyScreenPercentage' CVar wins over automatic downsampling
	if (FMath::IsNearlyEqual(EffectiveScale, 1.0f) && bAnyViewWantsDownsampledSeparateTranslucency)
	{
		EffectiveScale = 0.5f;
	}

	const FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	FSeparateTranslucencyDimensions Dimensions;
	Dimensions.Extent = GetScaledExtent(SceneContext.GetBufferSizeXY(), EffectiveScale);
	Dimensions.NumSamples = SceneContext.GetSceneDepthSurface()->GetNumSamples();
	Dimensions.Scale = EffectiveScale;
	return Dimensions;
}

FRDGTextureMSAA FSeparateTranslucencyTextures::GetColorForWrite(FRDGBuilder& GraphBuilder)
{
	if (!ColorTexture.IsValid())
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Dimensions.Extent, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource, 1, Dimensions.NumSamples);
		ColorTexture = CreateTextureMSAA(GraphBuilder, Desc, TEXT("SeparateTranslucencyColor"), GFastVRamConfig.SeparateTranslucency);
	}
	return ColorTexture;
}

FRDGTextureRef FSeparateTranslucencyTextures::GetColorForRead(FRDGBuilder& GraphBuilder) const
{
	if (ColorTexture.IsValid())
	{
		return ColorTexture.Resolve;
	}
	return GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackAlphaOneDummy);
}

FRDGTextureMSAA FSeparateTranslucencyTextures::GetColorModulateForWrite(FRDGBuilder& GraphBuilder)
{
	if (!ColorModulateTexture.IsValid())
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Dimensions.Extent, PF_FloatR11G11B10, FClearValueBinding::White, TexCreate_RenderTargetable | TexCreate_ShaderResource, 1, Dimensions.NumSamples);
		ColorModulateTexture = CreateTextureMSAA(GraphBuilder, Desc, TEXT("SeparateTranslucencyModulateColor"), GFastVRamConfig.SeparateTranslucencyModulate);
	}
	return ColorModulateTexture;
}

FRDGTextureRef FSeparateTranslucencyTextures::GetColorModulateForRead(FRDGBuilder& GraphBuilder) const
{
	if (ColorModulateTexture.IsValid())
	{
		return ColorModulateTexture.Resolve;
	}
	return GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);
}

FRDGTextureMSAA FSeparateTranslucencyTextures::GetDepthForWrite(FRDGBuilder& GraphBuilder)
{
	if (!DepthTexture.IsValid())
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Dimensions.Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, 1, Dimensions.NumSamples);
		DepthTexture = CreateTextureMSAA(GraphBuilder, Desc, TEXT("SeparateTranslucencyDepth"), GFastVRamConfig.SeparateTranslucencyModulate);
	}
	return DepthTexture;
}

FRDGTextureRef FSeparateTranslucencyTextures::GetDepthForRead(FRDGBuilder& GraphBuilder) const
{
	if (DepthTexture.IsValid())
	{
		return DepthTexture.Resolve;
	}
	return GraphBuilder.RegisterExternalTexture(GSystemTextures.MaxFP16Depth);
}

FRDGTextureMSAA FSeparateTranslucencyTextures::GetForWrite(FRDGBuilder& GraphBuilder, ETranslucencyPass::Type TranslucencyPass)
{
	if (TranslucencyPass == ETranslucencyPass::TPT_TranslucencyAfterDOFModulate)
	{
		return GetColorModulateForWrite(GraphBuilder);
	}
	else
	{
		return GetColorForWrite(GraphBuilder);
	}
}

/** Pixel shader used to copy scene color into another texture so that materials can read from scene color with a node. */
class FCopySceneColorPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FCopySceneColorPS);
	SHADER_USE_PARAMETER_STRUCT(FCopySceneColorPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCopySceneColorPS, "/Engine/Private/TranslucentLightingShaders.usf", "CopySceneColorMain", SF_Pixel);

static FRDGTextureRef AddCopySceneColorPass(FRDGBuilder& GraphBuilder, TArrayView<const FViewInfo> Views, FRDGTextureMSAA SceneColor)
{
	FRDGTextureRef SceneColorCopyTexture = nullptr;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::ENoAction;

	RDG_EVENT_SCOPE(GraphBuilder, "CopySceneColor");

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (View.IsUnderwater())
		{
			continue;
		}

		bool bNeedsResolve = false;
		for (int32 TranslucencyPass = 0; TranslucencyPass < ETranslucencyPass::TPT_MAX; ++TranslucencyPass)
		{
			if (View.TranslucentPrimCount.UseSceneColorCopy((ETranslucencyPass::Type)TranslucencyPass))
			{
				bNeedsResolve = true;
				break;
			}
		}

		if (bNeedsResolve)
		{
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

			AddCopyToResolveTargetPass(GraphBuilder, SceneColor.Target, SceneColor.Resolve, FResolveRect(View.ViewRect));

			const FIntPoint SceneColorExtent = SceneColor.Target->Desc.Extent;

			if (!SceneColorCopyTexture)
			{
				SceneColorCopyTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneColorExtent, PF_B8G8R8A8, FClearValueBinding::White, TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("SceneColorCopy"));
			}

			const FScreenPassTextureViewport Viewport(SceneColorCopyTexture, View.ViewRect);

			TShaderMapRef<FScreenVS> VertexShader(View.ShaderMap);
			TShaderMapRef<FCopySceneColorPS> PixelShader(View.ShaderMap);

			auto* PassParameters = GraphBuilder.AllocParameters<FCopySceneColorPS::FParameters>();
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->SceneColorTexture = SceneColor.Resolve;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorCopyTexture, LoadAction);

			if (!View.Family->bMultiGPUForkAndJoin)
			{
				LoadAction = ERenderTargetLoadAction::ELoad;
			}

			AddDrawScreenPass(GraphBuilder, {}, View, Viewport, Viewport, VertexShader, PixelShader, PassParameters);
		}
	}

	return SceneColorCopyTexture;
}

/** Pixel shader to upsample separate translucency. */
class FTranslucencyUpsamplePS : public FGlobalShader
{
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LowResColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, LowResDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, FullResDepthTexture)
		SHADER_PARAMETER(FVector2D, LowResExtentInverse)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FTranslucencyUpsamplePS() = default;
	FTranslucencyUpsamplePS(const FGlobalShaderType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FTranslucencySimpleUpsamplePS : public FTranslucencyUpsamplePS
{
protected:
	DECLARE_GLOBAL_SHADER(FTranslucencySimpleUpsamplePS);
	SHADER_USE_PARAMETER_STRUCT(FTranslucencySimpleUpsamplePS, FTranslucencyUpsamplePS);
};

IMPLEMENT_GLOBAL_SHADER(FTranslucencySimpleUpsamplePS, "/Engine/Private/TranslucencyUpsampling.usf", "SimpleUpsamplingPS", SF_Pixel);

class FTranslucencyNearestDepthNeighborUpsamplePS : public FTranslucencyUpsamplePS
{
public:
	DECLARE_GLOBAL_SHADER(FTranslucencyNearestDepthNeighborUpsamplePS);
	SHADER_USE_PARAMETER_STRUCT(FTranslucencyNearestDepthNeighborUpsamplePS, FTranslucencyUpsamplePS);
};

IMPLEMENT_GLOBAL_SHADER(FTranslucencyNearestDepthNeighborUpsamplePS, "/Engine/Private/TranslucencyUpsampling.usf", "NearestDepthNeighborUpsamplingPS", SF_Pixel);

bool GetUseTranslucencyNearestDepthNeighborUpsample(float DownsampleScale)
{
	const bool bHalfResDownsample = FMath::IsNearlyEqual(DownsampleScale, 0.5f);
	const bool bUseNearestDepthNeighborUpsample = GSeparateTranslucencyUpsampleMode > 0 && bHalfResDownsample;
	return bUseNearestDepthNeighborUpsample;
}

static void AddTranslucencyUpsamplePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FScreenPassRenderTarget Output,
	FScreenPassTexture DownsampledTranslucencyColor,
	FRDGTextureRef DownsampledTranslucencyDepthTexture,
	FRDGTextureRef SceneDepthTexture,
	float DownsampleScale)
{
	TShaderMapRef<FScreenVS> VertexShader(View.ShaderMap);
	TShaderRef<FTranslucencyUpsamplePS> PixelShader;
	FRHIBlendState* BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();

	if (GetUseTranslucencyNearestDepthNeighborUpsample(DownsampleScale))
	{
		PixelShader = TShaderMapRef<FTranslucencyNearestDepthNeighborUpsamplePS>(View.ShaderMap);
	}
	else
	{
		PixelShader = TShaderMapRef<FTranslucencySimpleUpsamplePS>(View.ShaderMap);
	}

	const FScreenPassTextureViewport OutputViewport(Output);
	const FScreenPassTextureViewport InputViewport(DownsampledTranslucencyColor);
	const FIntPoint LowResExtent = DownsampledTranslucencyColor.Texture->Desc.Extent;

	auto* PassParameters = GraphBuilder.AllocParameters<FTranslucencyUpsamplePS::FParameters>();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->FullResDepthTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMetaData(SceneDepthTexture, ERDGTextureMetaDataAccess::Depth));
	PassParameters->LowResColorTexture = DownsampledTranslucencyColor.Texture;
	PassParameters->LowResDepthTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMetaData(DownsampledTranslucencyDepthTexture, ERDGTextureMetaDataAccess::Depth));
	PassParameters->LowResExtentInverse = FVector2D(1.0f / LowResExtent.X, 1.0f / LowResExtent.Y);
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("TranslucencyUpsample"), View, OutputViewport, InputViewport, VertexShader, PixelShader, BlendState, PassParameters);
}

bool FSceneRenderer::ShouldRenderTranslucency() const
{
	return  ViewFamily.EngineShowFlags.Translucency
		&& !ViewFamily.EngineShowFlags.VisualizeLightCulling
		&& !ViewFamily.UseDebugViewPS();
}

bool FSceneRenderer::ShouldRenderTranslucency(ETranslucencyPass::Type TranslucencyPass) const
{
	extern int32 GLightShaftRenderAfterDOF;

	// Change this condition to control where simple elements should be rendered.
	if (IsMainTranslucencyPass(TranslucencyPass))
	{
		if (ViewFamily.EngineShowFlags.VisualizeLPV)
		{
			return true;
		}

		for (const FViewInfo& View : Views)
		{
			if (View.bHasTranslucentViewMeshElements || View.SimpleElementCollector.BatchedElements.HasPrimsToDraw())
			{
				return true;
			}
		}
	}

	// If lightshafts are rendered in low res, we must reset the offscreen buffer in case is was also used in TPT_StandardTranslucency.
	if (GLightShaftRenderAfterDOF && TranslucencyPass == ETranslucencyPass::TPT_TranslucencyAfterDOF)
	{
		return true;
	}

	for (const FViewInfo& View : Views)
	{
		if (View.TranslucentPrimCount.Num(TranslucencyPass) > 0)
		{
			return true;
		}
	}

	return false;
}

FScreenPassTextureViewport FSeparateTranslucencyDimensions::GetInstancedStereoViewport(const FViewInfo& View, float InstancedStereoWidth) const
{
	FIntRect ViewRect = View.ViewRect;
	if (View.IsInstancedStereoPass() && !View.bIsMultiViewEnabled)
	{
		ViewRect.Max.X = ViewRect.Min.X + InstancedStereoWidth;
	}
	ViewRect = GetScaledRect(ViewRect, Scale);
	return FScreenPassTextureViewport(Extent, ViewRect);
}

void SetupDownsampledTranslucencyViewParameters(
	const FViewInfo& View,
	FIntPoint TextureExtent,
	FIntRect ViewRect,
	FViewUniformShaderParameters& DownsampledTranslucencyViewParameters)
{
	DownsampledTranslucencyViewParameters = *View.CachedViewUniformShaderParameters;

	// Update the parts of DownsampledTranslucencyParameters which are dependent on the buffer size and view rect
	View.SetupViewRectUniformBufferParameters(
		DownsampledTranslucencyViewParameters,
		TextureExtent,
		ViewRect,
		View.ViewMatrices,
		View.PrevViewInfo.ViewMatrices);

	// instead of using the expected ratio, use the actual dimentions to avoid rounding errors
	float ActualDownsampleX = float(ViewRect.Width()) / float(View.ViewRect.Width());
	float ActualDownsampleY = float(ViewRect.Height()) / float(View.ViewRect.Height());
	DownsampledTranslucencyViewParameters.LightProbeSizeRatioAndInvSizeRatio = FVector4(ActualDownsampleX, ActualDownsampleY, 1.0f / ActualDownsampleX, 1.0f / ActualDownsampleY);
}

void SetupTranslucentBasePassUniformParameters(
	FRDGBuilder* GraphBuilder,
	FRHICommandListImmediate& RHICmdList,
	const FSceneRenderTargets& SceneRenderTargets,
	const FViewInfo& View,
	FRDGTextureRef SceneColorCopyTexture,
	ESceneTextureSetupMode SceneTextureSetupMode,
	const int32 ViewIndex,
	FTranslucentBasePassUniformParameters& BasePassParameters)
{
	const auto GetRDG = [&](const TRefCountPtr<IPooledRenderTarget>& PooledRenderTarget, ERDGTextureFlags Flags = ERDGTextureFlags::None)
	{
		return RegisterExternalOrPassthroughTexture(GraphBuilder, PooledRenderTarget, Flags);
	};

	SetupSharedBasePassParameters(GraphBuilder, RHICmdList, View, BasePassParameters.Shared);
	SetupSceneTextureUniformParameters(GraphBuilder, View.FeatureLevel, SceneRenderTargets, SceneTextureSetupMode, BasePassParameters.SceneTextures);

	FRDGTextureRef BlackDummyTexture = GetRDG(GSystemTextures.BlackDummy);
	FRDGTextureRef WhiteDummyTexture = GetRDG(GSystemTextures.WhiteDummy);

	// Material SSR
	{
		float PrevSceneColorPreExposureInvValue = 1.0f / View.PreExposure;

		if (View.HZB)
		{
			BasePassParameters.HZBTexture = GetRDG(View.HZB);
			BasePassParameters.HZBSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			FRDGTextureRef PrevSceneColorTexture = BlackDummyTexture;

			if (View.PrevViewInfo.CustomSSRInput.IsValid())
			{
				PrevSceneColorTexture = GetRDG(View.PrevViewInfo.CustomSSRInput);
				PrevSceneColorPreExposureInvValue = 1.0f / View.PrevViewInfo.SceneColorPreExposure;
			}
			else if (View.PrevViewInfo.TemporalAAHistory.IsValid())
			{
				PrevSceneColorTexture = GetRDG(View.PrevViewInfo.TemporalAAHistory.RT[0]);
				PrevSceneColorPreExposureInvValue = 1.0f / View.PrevViewInfo.SceneColorPreExposure;
			}

			BasePassParameters.PrevSceneColor = PrevSceneColorTexture;
			BasePassParameters.PrevSceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			const FVector2D HZBUvFactor(
				float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
				float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y)
			);
			const FVector4 HZBUvFactorAndInvFactorValue(
				HZBUvFactor.X,
				HZBUvFactor.Y,
				1.0f / HZBUvFactor.X,
				1.0f / HZBUvFactor.Y
			);

			BasePassParameters.HZBUvFactorAndInvFactor = HZBUvFactorAndInvFactorValue;
		}
		else
		{
			BasePassParameters.HZBTexture = BlackDummyTexture;
			BasePassParameters.HZBSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			BasePassParameters.PrevSceneColor = BlackDummyTexture;
			BasePassParameters.PrevSceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		}

		BasePassParameters.ApplyVolumetricCloudOnTransparent = 0.0f;
		BasePassParameters.VolumetricCloudColor = nullptr;
		BasePassParameters.VolumetricCloudDepth = nullptr;
		BasePassParameters.VolumetricCloudColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		BasePassParameters.VolumetricCloudDepthSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		if (IsVolumetricRenderTargetEnabled() && View.ViewState)
		{
			TRefCountPtr<IPooledRenderTarget> VolumetricReconstructRT = View.ViewState->VolumetricCloudRenderTarget.GetDstVolumetricReconstructRT();
			if (VolumetricReconstructRT.IsValid())
			{
				TRefCountPtr<IPooledRenderTarget> VolumetricReconstructRTDepth = View.ViewState->VolumetricCloudRenderTarget.GetDstVolumetricReconstructRTDepth();

				BasePassParameters.VolumetricCloudColor = VolumetricReconstructRT->GetRenderTargetItem().ShaderResourceTexture;
				BasePassParameters.VolumetricCloudDepth = VolumetricReconstructRTDepth->GetRenderTargetItem().ShaderResourceTexture;
				BasePassParameters.ApplyVolumetricCloudOnTransparent = 1.0f;
			}
		}
		if (BasePassParameters.VolumetricCloudColor == nullptr)
		{
			BasePassParameters.VolumetricCloudColor = GSystemTextures.BlackAlphaOneDummy->GetRenderTargetItem().ShaderResourceTexture;
			BasePassParameters.VolumetricCloudDepth = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
		}

		FIntPoint ViewportOffset = View.ViewRect.Min;
		FIntPoint ViewportExtent = View.ViewRect.Size();

		// Scene render targets might not exist yet; avoids NaNs.
		FIntPoint EffectiveBufferSize = SceneRenderTargets.GetBufferSizeXY();
		EffectiveBufferSize.X = FMath::Max(EffectiveBufferSize.X, 1);
		EffectiveBufferSize.Y = FMath::Max(EffectiveBufferSize.Y, 1);

		if (View.PrevViewInfo.TemporalAAHistory.IsValid())
		{
			ViewportOffset = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Min;
			ViewportExtent = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Size();
			EffectiveBufferSize = View.PrevViewInfo.TemporalAAHistory.RT[0]->GetDesc().Extent;
		}

		FVector2D InvBufferSize(1.0f / float(EffectiveBufferSize.X), 1.0f / float(EffectiveBufferSize.Y));

		FVector4 ScreenPosToPixelValue(
			ViewportExtent.X * 0.5f * InvBufferSize.X,
			-ViewportExtent.Y * 0.5f * InvBufferSize.Y,
			(ViewportExtent.X * 0.5f + ViewportOffset.X) * InvBufferSize.X,
			(ViewportExtent.Y * 0.5f + ViewportOffset.Y) * InvBufferSize.Y);

		BasePassParameters.PrevScreenPositionScaleBias = ScreenPosToPixelValue;
		BasePassParameters.PrevSceneColorPreExposureInv = PrevSceneColorPreExposureInvValue;
	}

	// Translucency Lighting Volume
	{
		if (SceneRenderTargets.GetTranslucencyVolumeAmbient(TVC_Inner) != nullptr)
		{
			BasePassParameters.TranslucencyLightingVolumeAmbientInner = GetRDG(SceneRenderTargets.GetTranslucencyVolumeAmbient(TVC_Inner, ViewIndex));
			BasePassParameters.TranslucencyLightingVolumeAmbientOuter = GetRDG(SceneRenderTargets.GetTranslucencyVolumeAmbient(TVC_Outer, ViewIndex));
			BasePassParameters.TranslucencyLightingVolumeDirectionalInner = GetRDG(SceneRenderTargets.GetTranslucencyVolumeDirectional(TVC_Inner, ViewIndex));
			BasePassParameters.TranslucencyLightingVolumeDirectionalOuter = GetRDG(SceneRenderTargets.GetTranslucencyVolumeDirectional(TVC_Outer, ViewIndex));
		}
		else
		{
			BasePassParameters.TranslucencyLightingVolumeAmbientInner = BlackDummyTexture;
			BasePassParameters.TranslucencyLightingVolumeAmbientOuter = BlackDummyTexture;
			BasePassParameters.TranslucencyLightingVolumeDirectionalInner = BlackDummyTexture;
			BasePassParameters.TranslucencyLightingVolumeDirectionalOuter = BlackDummyTexture;
		}

		BasePassParameters.TranslucencyLightingVolumeAmbientInnerSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		BasePassParameters.TranslucencyLightingVolumeAmbientOuterSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		BasePassParameters.TranslucencyLightingVolumeDirectionalInnerSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		BasePassParameters.TranslucencyLightingVolumeDirectionalOuterSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}

	BasePassParameters.SceneColorCopyTexture = BlackDummyTexture;
	BasePassParameters.SceneColorCopySampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	if (SceneColorCopyTexture)
	{
		BasePassParameters.SceneColorCopyTexture = SceneColorCopyTexture;
	}

	BasePassParameters.EyeAdaptationTexture = WhiteDummyTexture;

	// Setup by passes that support it
	if (View.HasValidEyeAdaptationTexture())
	{
		BasePassParameters.EyeAdaptationTexture = GetRDG(View.GetEyeAdaptationTexture(), ERDGTextureFlags::MultiFrame);
	}

	BasePassParameters.PreIntegratedGFTexture = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
	BasePassParameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
}

TRDGUniformBufferRef<FTranslucentBasePassUniformParameters> CreateTranslucentBasePassUniformBuffer(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef SceneColorCopyTexture,
	ESceneTextureSetupMode SceneTextureSetupMode,
	const int32 ViewIndex)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	FTranslucentBasePassUniformParameters* BasePassParameters = GraphBuilder.AllocParameters<FTranslucentBasePassUniformParameters>();
	SetupTranslucentBasePassUniformParameters(&GraphBuilder, GraphBuilder.RHICmdList, SceneRenderTargets, View, SceneColorCopyTexture, SceneTextureSetupMode, ViewIndex, *BasePassParameters);
	return GraphBuilder.CreateUniformBuffer(BasePassParameters);
}

TUniformBufferRef<FTranslucentBasePassUniformParameters> CreateTranslucentBasePassUniformBuffer(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	ESceneTextureSetupMode SceneTextureSetupMode,
	const int32 ViewIndex)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);
	FTranslucentBasePassUniformParameters BasePassParameters;
	SetupTranslucentBasePassUniformParameters(nullptr, RHICmdList, SceneRenderTargets, View, nullptr, SceneTextureSetupMode, ViewIndex, BasePassParameters);
	return TUniformBufferRef<FTranslucentBasePassUniformParameters>::CreateUniformBufferImmediate(BasePassParameters, EUniformBufferUsage::UniformBuffer_SingleFrame);
}

static void UpdateSeparateTranslucencyViewState(FScene* Scene, const FViewInfo& View, FIntPoint TextureExtent, float ViewportScale, FMeshPassProcessorRenderState& DrawRenderState)
{
	Scene->UniformBuffers.UpdateViewUniformBuffer(View);

	FViewUniformShaderParameters DownsampledTranslucencyViewParameters;
	SetupDownsampledTranslucencyViewParameters(View, TextureExtent, GetScaledRect(View.ViewRect, ViewportScale), DownsampledTranslucencyViewParameters);
	Scene->UniformBuffers.UpdateViewUniformBufferImmediate(DownsampledTranslucencyViewParameters);
	DrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);

	if ((View.IsInstancedStereoPass() || View.bIsMobileMultiViewEnabled) && View.Family->Views.Num() > 0)
	{
		// When drawing the left eye in a stereo scene, copy the right eye view values into the instanced view uniform buffer.
		const EStereoscopicPass StereoPassIndex = IStereoRendering::IsStereoEyeView(View) ? eSSP_RIGHT_EYE : eSSP_FULL;

		const FViewInfo& InstancedView = static_cast<const FViewInfo&>(View.Family->GetStereoEyeView(StereoPassIndex));
		SetupDownsampledTranslucencyViewParameters(InstancedView, TextureExtent, GetScaledRect(InstancedView.ViewRect, ViewportScale), DownsampledTranslucencyViewParameters);
		Scene->UniformBuffers.InstancedViewUniformBuffer.UpdateUniformBufferImmediate(reinterpret_cast<FInstancedViewUniformShaderParameters&>(DownsampledTranslucencyViewParameters));
		DrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	}
}

static void RenderViewTranslucencyInner(
	FRHICommandListImmediate& RHICmdList,
	const FSceneRenderer& SceneRenderer,
	const FViewInfo& View,
	const FScreenPassTextureViewport Viewport,
	const float ViewportScale,
	ETranslucencyPass::Type TranslucencyPass,
	FRDGParallelCommandListSet* ParallelCommandListSet)
{
	FMeshPassProcessorRenderState DrawRenderState(View);
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
	UpdateSeparateTranslucencyViewState(SceneRenderer.Scene, View, Viewport.Extent, ViewportScale, DrawRenderState);
	SceneRenderer.SetStereoViewport(RHICmdList, View, ViewportScale);

	if (!View.Family->UseDebugViewPS())
	{
		QUICK_SCOPE_CYCLE_COUNTER(RenderTranslucencyParallel_Start_FDrawSortedTransAnyThreadTask);

		const EMeshPass::Type MeshPass = TranslucencyPassToMeshPass(TranslucencyPass);
		View.ParallelMeshDrawCommandPasses[MeshPass].DispatchDraw(ParallelCommandListSet, RHICmdList);
	}

	if (IsMainTranslucencyPass(TranslucencyPass))
	{
		if (ParallelCommandListSet)
		{
			ParallelCommandListSet->SetStateOnCommandList(RHICmdList);
		}

		View.SimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::Translucent, SDPG_World);
		View.SimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::Translucent, SDPG_Foreground);

		// editor and debug rendering
		if (View.bHasTranslucentViewMeshElements)
		{
			{
				QUICK_SCOPE_CYCLE_COUNTER(RenderTranslucencyParallel_SDPG_World);

				DrawDynamicMeshPass(View, RHICmdList,
					[&View, &DrawRenderState, TranslucencyPass](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
				{
					FBasePassMeshProcessor PassMeshProcessor(
						View.Family->Scene->GetRenderScene(),
						View.GetFeatureLevel(),
						&View,
						DrawRenderState,
						DynamicMeshPassContext,
						FBasePassMeshProcessor::EFlags::CanUseDepthStencil,
						TranslucencyPass);

					const uint64 DefaultBatchElementMask = ~0ull;

					for (int32 MeshIndex = 0; MeshIndex < View.ViewMeshElements.Num(); MeshIndex++)
					{
						const FMeshBatch& MeshBatch = View.ViewMeshElements[MeshIndex];
						PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
					}
				});
			}

			{
				QUICK_SCOPE_CYCLE_COUNTER(RenderTranslucencyParallel_SDPG_Foreground);

				DrawDynamicMeshPass(View, RHICmdList,
					[&View, &DrawRenderState, TranslucencyPass](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
				{
					FBasePassMeshProcessor PassMeshProcessor(
						View.Family->Scene->GetRenderScene(),
						View.GetFeatureLevel(),
						&View,
						DrawRenderState,
						DynamicMeshPassContext,
						FBasePassMeshProcessor::EFlags::CanUseDepthStencil,
						TranslucencyPass);

					const uint64 DefaultBatchElementMask = ~0ull;

					for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
					{
						const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
						PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
					}
				});
			}
		}

		const FSceneViewState* ViewState = (const FSceneViewState*)View.State;
		if (ViewState && View.Family->EngineShowFlags.VisualizeLPV)
		{
			FLightPropagationVolume* LightPropagationVolume = ViewState->GetLightPropagationVolume(View.GetFeatureLevel());

			if (LightPropagationVolume)
			{
				LightPropagationVolume->Visualise(RHICmdList, View);
			}
		}

		if (ParallelCommandListSet)
		{
			RHICmdList.EndRenderPass();
		}
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FTranslucentBasePassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FTranslucentBasePassUniformParameters, BasePass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

static void RenderTranslucencyViewInner(
	FRDGBuilder& GraphBuilder,
	const FSceneRenderer& SceneRenderer,
	const FViewInfo& View,
	FScreenPassTextureViewport Viewport,
	float ViewportScale,
	FRDGTextureMSAA SceneColorTexture,
	ERenderTargetLoadAction SceneColorLoadAction,
	FRDGTextureRef SceneDepthTexture,
	TRDGUniformBufferRef<FTranslucentBasePassUniformParameters> BasePassParameters,
	ETranslucencyPass::Type TranslucencyPass,
	bool bResolveColorTexture,
	bool bRenderInParallel)
{
	if (SceneColorLoadAction == ERenderTargetLoadAction::EClear)
	{
		AddClearRenderTargetPass(GraphBuilder, SceneColorTexture.Target);
	}

	FTranslucentBasePassParameters* PassParameters = GraphBuilder.AllocParameters<FTranslucentBasePassParameters>();
	PassParameters->BasePass = BasePassParameters;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture.Target, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilWrite);
	PassParameters->RenderTargets.ShadingRateTexture = GVRSImageManager.GetVariableRateShadingImage(GraphBuilder, SceneRenderer.ViewFamily, nullptr);

	if (bRenderInParallel)
	{
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SeparateTranslucencyParallel"),
			PassParameters,
			ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
			[&SceneRenderer, &View, PassParameters, ViewportScale, Viewport, TranslucencyPass](FRHICommandListImmediate& RHICmdList)
		{
			FRDGParallelCommandListSet ParallelCommandListSet(RHICmdList, GET_STATID(STAT_CLP_Translucency), SceneRenderer, View, FParallelCommandListBindings(PassParameters), ViewportScale);
			RenderViewTranslucencyInner(RHICmdList, SceneRenderer, View, Viewport, ViewportScale, TranslucencyPass, &ParallelCommandListSet);
		});
	}
	else
	{
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SeparateTranslucency"),
			PassParameters,
			ERDGPassFlags::Raster,
			[&SceneRenderer, &View, ViewportScale, Viewport, TranslucencyPass](FRHICommandListImmediate& RHICmdList)
		{
			RenderViewTranslucencyInner(RHICmdList, SceneRenderer, View, Viewport, ViewportScale, TranslucencyPass, nullptr);
		});
	}

	if (bResolveColorTexture)
	{
		AddResolveSceneColorPass(GraphBuilder, View, SceneColorTexture);
	}
}

void FDeferredShadingSceneRenderer::RenderTranslucencyInner(
	FRDGBuilder& GraphBuilder,
	FRDGTextureMSAA SceneColorTexture,
	FRDGTextureMSAA SceneDepthTexture,
	FSeparateTranslucencyTextures* OutSeparateTranslucencyTextures,
	ETranslucencyView ViewsToRender,
	FRDGTextureRef SceneColorCopyTexture,
	ETranslucencyPass::Type TranslucencyPass)
{
	if (!ShouldRenderTranslucency(TranslucencyPass))
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "%s", TranslucencyPassToString(TranslucencyPass));
	RDG_GPU_STAT_SCOPE(GraphBuilder, Translucency);
	RDG_WAIT_FOR_TASKS_CONDITIONAL(GraphBuilder, IsTranslucencyWaitForTasksEnabled());

	const bool bRenderInParallel = IsParallelTranslucencyEnabled();
	const bool bRenderInSeparateTranslucency = IsSeparateTranslucencyEnabled(TranslucencyPass, SeparateTranslucencyDimensions.Scale);

	const auto ShouldRenderView = [&](const FViewInfo& View, ETranslucencyView TranslucencyView)
	{
		return View.ShouldRenderView() && EnumHasAnyFlags(TranslucencyView, ViewsToRender);
	};

	// Can't reference scene color in scene textures. Scene color copy is used instead.
	ESceneTextureSetupMode SceneTextureSetupMode = ESceneTextureSetupMode::All;
	EnumRemoveFlags(SceneTextureSetupMode, ESceneTextureSetupMode::SceneColor);

	if (bRenderInSeparateTranslucency)
	{
		// Create resources shared by each view (each view data is tiled into each of the render target resources)
		FSeparateTranslucencyTextures LocalSeparateTranslucencyTextures(SeparateTranslucencyDimensions);

		for (int32 ViewIndex = 0, NumProcessedViews = 0; ViewIndex < Views.Num(); ++ViewIndex, ++NumProcessedViews)
		{
			const FViewInfo& View = Views[ViewIndex];
			const ETranslucencyView TranslucencyView = GetTranslucencyView(View);

			if (!ShouldRenderView(View, TranslucencyView))
			{
				continue;
			}

			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

			const FScreenPassTextureViewport SeparateTranslucencyViewport = SeparateTranslucencyDimensions.GetInstancedStereoViewport(View, InstancedStereoWidth);
			const bool bCompositeBackToSceneColor = IsMainTranslucencyPass(TranslucencyPass) || EnumHasAnyFlags(TranslucencyView, ETranslucencyView::UnderWater);
			checkf(bCompositeBackToSceneColor || OutSeparateTranslucencyTextures, TEXT("OutSeparateTranslucencyTextures is null, but we aren't compositing immediately back to scene color."));

			/** Separate translucency color is either composited immediately or later during post processing. If done immediately, it's because the view doesn't support
			 *  compositing (e.g. we're rendering an underwater view) or because we're downsampling the main translucency pass. In this case, we use a local set of
			 *  textures instead of the external ones passed in.
			 */
			FRDGTextureMSAA SeparateTranslucencyColorTexture;
			if (bCompositeBackToSceneColor)
			{
				SeparateTranslucencyColorTexture = LocalSeparateTranslucencyTextures.GetForWrite(GraphBuilder, TranslucencyPass);
			}
			else
			{
				SeparateTranslucencyColorTexture = OutSeparateTranslucencyTextures->GetForWrite(GraphBuilder, TranslucencyPass);
			}

			// When rendering to a 1-to-1 separate translucency target, use the existing scene depth.
			FRDGTextureMSAA SeparateTranslucencyDepthTexture = SceneDepthTexture;

			// Rendering to a downscaled target; allocate a new depth texture and downsample depth.
			if (SeparateTranslucencyDimensions.Scale < 1.0f)
			{
				if (bCompositeBackToSceneColor)
				{
					SeparateTranslucencyDepthTexture = LocalSeparateTranslucencyTextures.GetDepthForWrite(GraphBuilder); 
				}
				else
				{
					SeparateTranslucencyDepthTexture = OutSeparateTranslucencyTextures->GetDepthForWrite(GraphBuilder);
				}

				AddDownsampleDepthPass(
					GraphBuilder, View,
					FScreenPassTexture(SceneDepthTexture.Resolve, View.ViewRect),
					FScreenPassRenderTarget(SeparateTranslucencyDepthTexture.Target, SeparateTranslucencyViewport.Rect, ERenderTargetLoadAction::ENoAction),
					EDownsampleDepthFilter::Point);
			}

			AddBeginSeparateTranslucencyTimerPass(GraphBuilder, View, TranslucencyPass);

			const ERenderTargetLoadAction SeparateTranslucencyColorLoadAction = NumProcessedViews == 0 || View.Family->bMultiGPUForkAndJoin
				? ERenderTargetLoadAction::EClear
				: ERenderTargetLoadAction::ELoad;

			RenderTranslucencyViewInner(
				GraphBuilder,
				*this,
				View,
				SeparateTranslucencyViewport,
				SeparateTranslucencyDimensions.Scale,
				SeparateTranslucencyColorTexture,
				SeparateTranslucencyColorLoadAction,
				SeparateTranslucencyDepthTexture.Target,
				CreateTranslucentBasePassUniformBuffer(GraphBuilder, View, SceneColorCopyTexture, SceneTextureSetupMode, ViewIndex),
				TranslucencyPass,
				!bCompositeBackToSceneColor,
				bRenderInParallel);

			if (bCompositeBackToSceneColor)
			{
				::AddResolveSceneDepthPass(GraphBuilder, View, SeparateTranslucencyDepthTexture);

				AddTranslucencyUpsamplePass(
					GraphBuilder, View,
					FScreenPassRenderTarget(SceneColorTexture.Target, View.ViewRect, ERenderTargetLoadAction::ELoad),
					FScreenPassTexture(SeparateTranslucencyColorTexture.Resolve, SeparateTranslucencyViewport.Rect),
					SeparateTranslucencyDepthTexture.Resolve,
					SceneDepthTexture.Resolve,
					SeparateTranslucencyDimensions.Scale);
			}

			AddEndSeparateTranslucencyTimerPass(GraphBuilder, View, TranslucencyPass);
		}
	}
	else
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewInfo& View = Views[ViewIndex];
			const ETranslucencyView TranslucencyView = GetTranslucencyView(View);

			if (!ShouldRenderView(View, TranslucencyView))
			{
				continue;
			}

			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

			AddBeginTranslucencyTimerPass(GraphBuilder, View);

			const ERenderTargetLoadAction SceneColorLoadAction = ERenderTargetLoadAction::ELoad;
			const FScreenPassTextureViewport Viewport(SceneColorTexture.Target, View.ViewRect);
			const float ViewportScale = 1.0f;
			const bool bResolveColorTexture = false;

			RenderTranslucencyViewInner(
				GraphBuilder,
				*this,
				View,
				Viewport,
				ViewportScale,
				SceneColorTexture,
				SceneColorLoadAction,
				SceneDepthTexture.Target,
				CreateTranslucentBasePassUniformBuffer(GraphBuilder, View, SceneColorCopyTexture, SceneTextureSetupMode, ViewIndex),
				TranslucencyPass,
				bResolveColorTexture,
				bRenderInParallel);

			AddEndTranslucencyTimerPass(GraphBuilder, View);
		}
	}
}

void FDeferredShadingSceneRenderer::RenderTranslucency(
	FRDGBuilder& GraphBuilder,
	FRDGTextureMSAA SceneColorTexture,
	FRDGTextureMSAA SceneDepthTexture,
	const FHairStrandsRenderingData* HairDatas,
	FSeparateTranslucencyTextures* OutSeparateTranslucencyTextures,
	ETranslucencyView ViewsToRender)
{
	if (!EnumHasAnyFlags(ViewsToRender, ETranslucencyView::UnderWater | ETranslucencyView::AboveWater))
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "Translucency");

	FRDGTextureRef SceneColorCopyTexture = nullptr;

	if (EnumHasAnyFlags(ViewsToRender, ETranslucencyView::AboveWater))
	{
		SceneColorCopyTexture = AddCopySceneColorPass(GraphBuilder, Views, SceneColorTexture);
	}

	if (ViewFamily.AllowTranslucencyAfterDOF())
	{
		RenderTranslucencyInner(GraphBuilder, SceneColorTexture, SceneDepthTexture, OutSeparateTranslucencyTextures, ViewsToRender, SceneColorCopyTexture, ETranslucencyPass::TPT_StandardTranslucency);
		if (GetHairStrandsComposition() == EHairStrandsCompositionType::AfterTranslucentTranslucentBeforeAfterDOF)
		{
			RenderHairComposition(GraphBuilder, Views, HairDatas, SceneColorTexture.Target, SceneDepthTexture.Target);
		}
		RenderTranslucencyInner(GraphBuilder, SceneColorTexture, SceneDepthTexture, OutSeparateTranslucencyTextures, ViewsToRender, SceneColorCopyTexture, ETranslucencyPass::TPT_TranslucencyAfterDOF);
		RenderTranslucencyInner(GraphBuilder, SceneColorTexture, SceneDepthTexture, OutSeparateTranslucencyTextures, ViewsToRender, SceneColorCopyTexture, ETranslucencyPass::TPT_TranslucencyAfterDOFModulate);
	}
	else // Otherwise render translucent primitives in a single bucket.
	{
		RenderTranslucencyInner(GraphBuilder, SceneColorTexture, SceneDepthTexture, OutSeparateTranslucencyTextures, ViewsToRender, SceneColorCopyTexture, ETranslucencyPass::TPT_AllTranslucency);
	}
}
