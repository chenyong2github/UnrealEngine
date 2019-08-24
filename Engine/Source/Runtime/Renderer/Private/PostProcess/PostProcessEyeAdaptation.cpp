// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessEyeAdaptation.cpp: Post processing eye adaptation implementation.
=============================================================================*/

#include "PostProcess/PostProcessEyeAdaptation.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "Math/UnrealMathUtility.h"
#include "ScenePrivate.h"
#include "Curves/CurveFloat.h"
#include "RHIGPUReadback.h"

RENDERCORE_API bool UsePreExposure(EShaderPlatform Platform);

// Initialize the static CVar
TAutoConsoleVariable<float> CVarEyeAdaptationPreExposureOverride(
	TEXT("r.EyeAdaptation.PreExposureOverride"),
	0,
	TEXT("Overide the scene pre-exposure by a custom value. \n")
	TEXT("= 0 : No override\n")
	TEXT("> 0 : Override PreExposure\n"),
	ECVF_RenderThreadSafe);

// Initialize the static CVar
TAutoConsoleVariable<int32> CVarEyeAdaptationMethodOverride(
	TEXT("r.EyeAdaptation.MethodOveride"),
	-1,
	TEXT("Overide the camera metering method set in post processing volumes\n")
	TEXT("-2: override with custom settings (for testing Basic Mode)\n")
	TEXT("-1: no override\n")
	TEXT(" 1: Auto Histogram-based\n")
	TEXT(" 2: Auto Basic\n")
	TEXT(" 3: Manual"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

// Initialize the static CVar used in computing the weighting focus in basic eye-adaptation
TAutoConsoleVariable<float> CVarEyeAdaptationFocus(
	TEXT("r.EyeAdaptation.Focus"),
	1.0f,
	TEXT("Applies to basic adapation mode only\n")
	TEXT(" 0: Uniform weighting\n")
	TEXT(">0: Center focus, 1 is a good number (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarEyeAdaptationBasicCompute(
	TEXT("r.EyeAdaptation.Basic.Compute"),
	1,
	TEXT("Use Pixel or Compute Shader to compute the basic eye adaptation. \n")
	TEXT("= 0 : Pixel Shader\n")
	TEXT("> 0 : Compute Shader (default) \n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarEnablePreExposureOnlyInTheEditor(
	TEXT("r.EyeAdaptation.EditorOnly"),
	1,
	TEXT("When pre-exposure is enabled, 0 to enable it everywhere, 1 to enable it only in the editor (default).\n")
	TEXT("This is to because it currently has an impact on the renderthread performance\n"),
	ECVF_ReadOnly);


/**
 *   Shared functionality used in computing the eye-adaptation parameters
 *   Compute the parameters used for eye-adaptation.  These will default to values
 *   that disable eye-adaptation if the hardware doesn't support the minimum feature level
 */
inline static void ComputeEyeAdaptationValues(const ERHIFeatureLevel::Type MinFeatureLevel, const FViewInfo& View, FVector4 Out[EYE_ADAPTATION_PARAMS_SIZE])
{
	static const auto VarDefaultAutoExposureExtendDefaultLuminanceRange = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
	const bool bExtendedLuminanceRange = VarDefaultAutoExposureExtendDefaultLuminanceRange->GetValueOnRenderThread() == 1;

	const FPostProcessSettings& Settings = View.FinalPostProcessSettings;
	const FEngineShowFlags& EngineShowFlags = View.Family->EngineShowFlags;

	// ----------

	// Histogram related values.
	float LowPercent = FMath::Clamp(Settings.AutoExposureLowPercent, 1.0f, 99.0f) * 0.01f;
	float HighPercent = FMath::Clamp(Settings.AutoExposureHighPercent, 1.0f, 99.0f) * 0.01f;
	float HistogramLogMin = bExtendedLuminanceRange ? EV100ToLog2(Settings.HistogramLogMin) : Settings.HistogramLogMin;
	float HistogramLogMax = bExtendedLuminanceRange ? EV100ToLog2(Settings.HistogramLogMax) : Settings.HistogramLogMax;
	HistogramLogMin = FMath::Min<float>(HistogramLogMin, HistogramLogMax - 1);

	// ----------

	// Those clamps the average luminance computed from the scene color.
	float MinAverageLuminance = 1;
	float MaxAverageLuminance = 1;
	// This scales the average luminance AFTER it gets clamped, affecting the exposure value directly.
	float AutoExposureBias = Settings.AutoExposureBias;
	if (Settings.AutoExposureBiasCurve)
	{
		float AverageSceneLuminance = View.GetLastAverageSceneLuminance();
		if (AverageSceneLuminance > 0)
		{
			AutoExposureBias += Settings.AutoExposureBiasCurve->GetFloatValue(LuminanceToEV100(AverageSceneLuminance));
		}
	}

	float LocalExposureMultipler = FMath::Pow(2.0f, AutoExposureBias);
	// This scales the average luminance BEFORE it gets clamped, used to implement the calibration constant for AEM_Basic.
	float AverageLuminanceScale = 1.f;

	// When any of those flags are set, make sure the tonemapper uses an exposure of 1.
	if (!EngineShowFlags.Lighting 
		|| (EngineShowFlags.VisualizeBuffer && View.CurrentBufferVisualizationMode != NAME_None) 
		|| View.Family->UseDebugViewPS() 
		|| EngineShowFlags.RayTracingDebug 
		|| EngineShowFlags.VisualizeDistanceFieldAO
		|| EngineShowFlags.VisualizeGlobalDistanceField
		|| EngineShowFlags.CollisionVisibility
		|| EngineShowFlags.CollisionPawn)
	{
		LocalExposureMultipler = 1.f;
	}
	// Otherwise handle the viewport override settings.
	else if (View.Family->ExposureSettings.bFixed)
	{
		LocalExposureMultipler = 1.f;
		MinAverageLuminance = MaxAverageLuminance = EV100ToLuminance(View.Family->ExposureSettings.FixedEV100);
	}
	// When !EngineShowFlags.EyeAdaptation (from "r.EyeAdaptationQuality 0") or the feature level doesn't support eye adaptation, only Settings.AutoExposureBias controls exposure.
	else if (EngineShowFlags.EyeAdaptation && View.GetFeatureLevel() >= MinFeatureLevel)
	{
		if (Settings.AutoExposureMethod == EAutoExposureMethod::AEM_Manual)
		{
			const float FixedEV100 = FMath::Log2(FMath::Square(Settings.DepthOfFieldFstop) * Settings.CameraShutterSpeed * 100 / FMath::Max(1.f, Settings.CameraISO));
			MinAverageLuminance = MaxAverageLuminance = EV100ToLuminance(FixedEV100);
		}
		else if (bExtendedLuminanceRange)
		{
			MinAverageLuminance = EV100ToLuminance(Settings.AutoExposureMinBrightness);
			MaxAverageLuminance = EV100ToLuminance(Settings.AutoExposureMaxBrightness);
		}
		else
		{
			MinAverageLuminance = Settings.AutoExposureMinBrightness;
			MaxAverageLuminance = Settings.AutoExposureMaxBrightness;
		}

		// Note that AEM_Histogram implements the calibration constant through LowPercent and HiPercent.
		if (Settings.AutoExposureMethod == EAutoExposureMethod::AEM_Basic)
		{
			const float CalibrationConstant = FMath::Clamp<float>(Settings.AutoExposureCalibrationConstant, 1.f, 100.f) * 0.01f;
			AverageLuminanceScale = 1.f / CalibrationConstant;
		}
	}

	// ----------

	LowPercent = FMath::Min<float>(LowPercent, HighPercent);
	MinAverageLuminance = FMath::Min<float>(MinAverageLuminance, MaxAverageLuminance);

	Out[0] = FVector4(LowPercent, HighPercent, MinAverageLuminance, MaxAverageLuminance);

	// ----------

	Out[1] = FVector4(LocalExposureMultipler, View.Family->DeltaWorldTime, Settings.AutoExposureSpeedUp, Settings.AutoExposureSpeedDown);

	// ----------

	float DeltaLog = HistogramLogMax - HistogramLogMin;
	float Multiply = 1.0f / DeltaLog;
	float Add = -HistogramLogMin * Multiply;
	float MinIntensity = FMath::Exp2(HistogramLogMin);
	Out[2] = FVector4(Multiply, Add, MinIntensity, 0);

	// ----------

	Out[3] = FVector4(0, AverageLuminanceScale, 0, 0);
}

// Basic AutoExposure requires at least ES3_1
static ERHIFeatureLevel::Type BasicEyeAdaptationMinFeatureLevel = ERHIFeatureLevel::ES3_1;

/** Encapsulates the histogram-based post processing eye adaptation pixel shader. */
class FPostProcessEyeAdaptationPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessEyeAdaptationPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
		OutEnvironment.SetDefine(TEXT("EYE_ADAPTATION_PARAMS_SIZE"), (uint32)EYE_ADAPTATION_PARAMS_SIZE);
	}

	/** Default constructor. */
	FPostProcessEyeAdaptationPS() {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FShaderParameter EyeAdaptationParams;
	FShaderResourceParameter EyeAdaptationTexture;

	/** Initialization constructor. */
	FPostProcessEyeAdaptationPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		EyeAdaptationParams.Bind(Initializer.ParameterMap, TEXT("EyeAdaptationParams"));
		EyeAdaptationTexture.Bind(Initializer.ParameterMap, TEXT("EyeAdaptationTexture"));
	}

	template <typename TRHICmdList>
	void SetPS(const FRenderingCompositePassContext& Context, TRHICmdList& RHICmdList, IPooledRenderTarget* LastEyeAdaptation)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());

		if (Context.View.HasValidEyeAdaptation())
		{
			SetTextureParameter(Context.RHICmdList, ShaderRHI, EyeAdaptationTexture, LastEyeAdaptation->GetRenderTargetItem().TargetableTexture);
		}
		else // some views don't have a state, thumbnail rendering?
		{
			SetTextureParameter(Context.RHICmdList, ShaderRHI, EyeAdaptationTexture, GWhiteTexture->TextureRHI);
		}

		{
			FVector4 Temp[EYE_ADAPTATION_PARAMS_SIZE];
			FRCPassPostProcessEyeAdaptation::ComputeEyeAdaptationParamsValue(Context.View, Temp);
			SetShaderValueArray(RHICmdList, ShaderRHI, EyeAdaptationParams, Temp, EYE_ADAPTATION_PARAMS_SIZE);
		}
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << EyeAdaptationParams;
		return bShaderHasOutdatedParameters;
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessEyeAdaptationPS,TEXT("/Engine/Private/PostProcessEyeAdaptation.usf"),TEXT("MainPS"),SF_Pixel);

/** Encapsulates the histogram-based post processing eye adaptation compute shader. */
class FPostProcessEyeAdaptationCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessEyeAdaptationCS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
		OutEnvironment.SetDefine(TEXT("EYE_ADAPTATION_PARAMS_SIZE"), (uint32)EYE_ADAPTATION_PARAMS_SIZE);
	}

	/** Default constructor. */
	FPostProcessEyeAdaptationCS() {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FRWShaderParameter OutComputeTex;
	FShaderParameter EyeAdaptationParams;

	/** Initialization constructor. */
	FPostProcessEyeAdaptationCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		OutComputeTex.Bind(Initializer.ParameterMap, TEXT("OutComputeTex"));
		EyeAdaptationParams.Bind(Initializer.ParameterMap, TEXT("EyeAdaptationParams"));
	}

	template <typename TRHICmdList>
	void SetParameters(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context, FRHIUnorderedAccessView* DestUAV, IPooledRenderTarget* LastEyeAdaptation)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		// CS params
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetCS(ShaderRHI, Context, RHICmdList, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
		OutComputeTex.SetTexture(RHICmdList, ShaderRHI, nullptr, DestUAV);		
		
		// PS params
		FVector4 EyeAdaptationParamValues[EYE_ADAPTATION_PARAMS_SIZE];
		FRCPassPostProcessEyeAdaptation::ComputeEyeAdaptationParamsValue(Context.View, EyeAdaptationParamValues);
		SetShaderValueArray(RHICmdList, ShaderRHI, EyeAdaptationParams, EyeAdaptationParamValues, EYE_ADAPTATION_PARAMS_SIZE);
	}

	template <typename TRHICmdList>
	void UnsetParameters(TRHICmdList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();
		OutComputeTex.UnsetUAV(RHICmdList, ShaderRHI);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << OutComputeTex << EyeAdaptationParams;
		return bShaderHasOutdatedParameters;
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessEyeAdaptationCS,TEXT("/Engine/Private/PostProcessEyeAdaptation.usf"),TEXT("MainCS"),SF_Compute);

void FRCPassPostProcessEyeAdaptation::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENTF(Context.RHICmdList, PostProcessEyeAdaptation, TEXT("PostProcessEyeAdaptation%s"), bIsComputePass?TEXT("Compute"):TEXT(""));
	AsyncEndFence = FComputeFenceRHIRef();

	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	// Get the custom 1x1 target used to store exposure value and Toggle the two render targets used to store new and old.
	Context.View.SwapEyeAdaptationRTs(Context.RHICmdList);

	IPooledRenderTarget* LastEyeAdaptation = Context.View.GetLastEyeAdaptationRT(Context.RHICmdList);
	IPooledRenderTarget* EyeAdaptation = Context.View.GetEyeAdaptation(Context.RHICmdList);
	check(EyeAdaptation);

	FIntPoint DestSize = EyeAdaptation->GetDesc().Extent;

	const FSceneRenderTargetItem& DestRenderTarget = EyeAdaptation->GetRenderTargetItem();

	static auto* RenderPassCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.RHIRenderPasses"));
	if (bIsComputePass)
	{
		FIntRect DestRect(0, 0, DestSize.X, DestSize.Y);

		// Common setup
		// #todo-renderpasses remove once everything is renderpasses
		UnbindRenderTargets(Context.RHICmdList);
		Context.SetViewportAndCallRHI(DestRect, 0.0f, 1.0f);
		
		static FName AsyncEndFenceName(TEXT("AsyncEyeAdaptationEndFence"));
		AsyncEndFence = Context.RHICmdList.CreateComputeFence(AsyncEndFenceName);

		if (IsAsyncComputePass())
		{
			// Async path
			FRHIAsyncComputeCommandListImmediate& RHICmdListComputeImmediate = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();
			{
				SCOPED_COMPUTE_EVENT(RHICmdListComputeImmediate, AsyncEyeAdaptation);
				WaitForInputPassComputeFences(RHICmdListComputeImmediate);
				RHICmdListComputeImmediate.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, DestRenderTarget.UAV);
				DispatchCS(RHICmdListComputeImmediate, Context, DestRenderTarget.UAV, LastEyeAdaptation);
				RHICmdListComputeImmediate.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, DestRenderTarget.UAV, AsyncEndFence);
			}
			FRHIAsyncComputeCommandListImmediate::ImmediateDispatch(RHICmdListComputeImmediate);
		}
		else
		{
			// Direct path
			WaitForInputPassComputeFences(Context.RHICmdList);
			Context.RHICmdList.BeginUpdateMultiFrameResource(DestRenderTarget.ShaderResourceTexture);

			Context.RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, DestRenderTarget.UAV);
			DispatchCS(Context.RHICmdList, Context, DestRenderTarget.UAV, LastEyeAdaptation);
			Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, DestRenderTarget.UAV, AsyncEndFence);

			Context.RHICmdList.EndUpdateMultiFrameResource(DestRenderTarget.ShaderResourceTexture);
		}
	}
	else
	{
		// Inform MultiGPU systems that we're starting to update this texture for this frame
		Context.RHICmdList.BeginUpdateMultiFrameResource(DestRenderTarget.ShaderResourceTexture);

		// we render to our own output render target, not the intermediate one created by the compositing system
		// Set the view family's render target/viewport.

		FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, ERenderTargetActions::Load_Store);
		TransitionRenderPassTargets(Context.RHICmdList, RPInfo);
		Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("EyeAdaptation"));
		{
			Context.SetViewportAndCallRHI(0, 0, 0.0f, DestSize.X, DestSize.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
			TShaderMapRef<FPostProcessEyeAdaptationPS> PixelShader(Context.GetShaderMap());

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

			PixelShader->SetPS(Context, Context.RHICmdList, LastEyeAdaptation);

			// Draw a quad mapping scene color to the view's render target
			DrawRectangle(
				Context.RHICmdList,
				0, 0,
				DestSize.X, DestSize.Y,
				0, 0,
				DestSize.X, DestSize.Y,
				DestSize,
				DestSize,
				*VertexShader,
				EDRF_UseTriangleOptimization);
		}
		Context.RHICmdList.EndRenderPass();
		Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
		// Inform MultiGPU systems that we've finished updating this texture for this frame
		Context.RHICmdList.EndUpdateMultiFrameResource(DestRenderTarget.ShaderResourceTexture);
	}

	Context.View.SetValidEyeAdaptation();
}

template <typename TRHICmdList>
void FRCPassPostProcessEyeAdaptation::DispatchCS(TRHICmdList& RHICmdList, FRenderingCompositePassContext& Context, FRHIUnorderedAccessView* DestUAV, IPooledRenderTarget* LastEyeAdaptation)
{
	auto ShaderMap = Context.GetShaderMap();
	TShaderMapRef<FPostProcessEyeAdaptationCS> ComputeShader(ShaderMap);
	RHICmdList.SetComputeShader(ComputeShader->GetComputeShader());

	ComputeShader->SetParameters(RHICmdList, Context, DestUAV, LastEyeAdaptation);
	DispatchComputeShader(RHICmdList, *ComputeShader, 1, 1, 1);
	ComputeShader->UnsetParameters(RHICmdList);
}

void FRCPassPostProcessEyeAdaptation::ComputeEyeAdaptationParamsValue(const FViewInfo& View, FVector4 Out[EYE_ADAPTATION_PARAMS_SIZE])
{
	ComputeEyeAdaptationValues(ERHIFeatureLevel::SM5, View, Out);
}

float FRCPassPostProcessEyeAdaptation::GetFixedExposure(const FViewInfo& View)
{
	FVector4 EyeAdaptationParams[EYE_ADAPTATION_PARAMS_SIZE];
	FRCPassPostProcessEyeAdaptation::ComputeEyeAdaptationParamsValue(View, EyeAdaptationParams);
	
	// like in PostProcessEyeAdaptation.usf (EyeAdaptationParams[0].ZW : Min/Max Intensity)
	const float Exposure = (EyeAdaptationParams[0].Z + EyeAdaptationParams[0].W) * 0.5f;
	const float ExposureOffsetMultipler = EyeAdaptationParams[1].X;

	const float ExposureScale = 1.0f / FMath::Max(0.0001f, Exposure);
	return ExposureScale * ExposureOffsetMultipler;
}

void FSceneViewState::UpdatePreExposure(FViewInfo& View)
{
	const FSceneViewFamily& ViewFamily = *View.Family;
	// One could use the IsRichView functionality to check if we need to update pre-exposure, 
	// but this is too limiting for certain view. For instance shader preview doesn't have 
	// volumetric lighting enabled, which makes the view be flagged as rich, and not updating 
	// the pre-exposition value.
	const bool bIsPreExposureRelevant =
		ViewFamily.EngineShowFlags.EyeAdaptation && // Controls whether scene luminance is computed at all.
		ViewFamily.EngineShowFlags.Lighting &&
		ViewFamily.EngineShowFlags.PostProcessing &&
		ViewFamily.bResolveScene &&
		!ViewFamily.EngineShowFlags.LightMapDensity &&
		!ViewFamily.EngineShowFlags.StationaryLightOverlap &&
		!ViewFamily.EngineShowFlags.LightComplexity &&
		!ViewFamily.EngineShowFlags.LODColoration &&
		!ViewFamily.EngineShowFlags.HLODColoration &&
		!ViewFamily.EngineShowFlags.LevelColoration &&
		!ViewFamily.EngineShowFlags.VisualizeBloom;


	PreExposure = 1.f;
	bUpdateLastExposure = false;

	if (IsMobilePlatform(View.GetShaderPlatform()))
	{
		if (!IsMobileHDR())
		{
			// In gamma space, the exposure is fully applied in the pre-exposure (no post-exposure compensation)
			PreExposure = FRCPassPostProcessEyeAdaptation::GetFixedExposure(View);
		}
	}
	else if (bIsPreExposureRelevant)
	{
		if (UsePreExposure(View.GetShaderPlatform()))
		{
			const float PreExposureOverride = CVarEyeAdaptationPreExposureOverride.GetValueOnRenderThread();
			const float LastExposure = View.GetLastEyeAdaptationExposure();
			if (PreExposureOverride > 0)
			{
				PreExposure = PreExposureOverride;
			}
			else if (LastExposure > 0)
			{
				PreExposure = LastExposure;
			}

			bUpdateLastExposure = true;
		}
		// The exposure compensation curves require the scene average luminance
		else if (View.FinalPostProcessSettings.AutoExposureBiasCurve)
		{
			bUpdateLastExposure = true;
		}
	}

	// Update the pre-exposure value on the actual view
	View.PreExposure = PreExposure;

	// Update the pre exposure of all temporal histories.
	if (!View.bViewStateIsReadOnly)
	{
		PrevFrameViewInfo.SceneColorPreExposure = PreExposure;
	}
}

FPooledRenderTargetDesc FRCPassPostProcessEyeAdaptation::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	// Specify invalid description to avoid getting intermediate rendertargets created.
	// We want to use ViewState->GetEyeAdaptation() instead
	FPooledRenderTargetDesc Ret;

	Ret.DebugName = TEXT("EyeAdaptation");

	return Ret;
}


/** Encapsulates the post process computation of Log2 Luminance pixel shader. */
class FPostProcessBasicEyeAdaptationSetupPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessBasicEyeAdaptationSetupPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, BasicEyeAdaptationMinFeatureLevel);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("EYE_ADAPTATION_PARAMS_SIZE"), (uint32)EYE_ADAPTATION_PARAMS_SIZE);
	}

	/** Default constructor. */
	FPostProcessBasicEyeAdaptationSetupPS() {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FShaderParameter EyeAdaptationParams;

	/** Initialization constructor. */
	FPostProcessBasicEyeAdaptationSetupPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		EyeAdaptationParams.Bind(Initializer.ParameterMap, TEXT("EyeAdaptationParams"));
	}

	void SetPS(const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

		{
			FVector4 Temp[EYE_ADAPTATION_PARAMS_SIZE];
			ComputeEyeAdaptationValues(BasicEyeAdaptationMinFeatureLevel, Context.View, Temp);
			SetShaderValueArray(Context.RHICmdList, ShaderRHI, EyeAdaptationParams, Temp, EYE_ADAPTATION_PARAMS_SIZE);
		}
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << EyeAdaptationParams;
		return bShaderHasOutdatedParameters;
	}
};

IMPLEMENT_SHADER_TYPE(, FPostProcessBasicEyeAdaptationSetupPS, TEXT("/Engine/Private/PostProcessEyeAdaptation.usf"), TEXT("MainBasicEyeAdaptationSetupPS"), SF_Pixel);

void FRCPassPostProcessBasicEyeAdaptationSetUp::Process(FRenderingCompositePassContext& Context)
{
	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

	if (!InputDesc)
	{
		// input is not hooked up correctly
		return;
	}

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	ERenderTargetLoadAction LoadAction = Context.GetLoadActionForRenderTarget(DestRenderTarget);

	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("PostProcessBasicEyeAdaptationSetUp"));
	{
		const FViewInfo& View = Context.View;
		const FSceneViewFamily& ViewFamily = *(View.Family);

		FIntPoint SrcSize = InputDesc->Extent;
		FIntPoint DestSize = PassOutputs[0].RenderTargetDesc.Extent;

		// e.g. 4 means the input texture is 4x smaller than the buffer size
		uint32 ScaleFactor = Context.ReferenceBufferSize.X / SrcSize.X;

		FIntRect SrcRect = Context.SceneColorViewRect / ScaleFactor;
		FIntRect DestRect = SrcRect;

		SCOPED_DRAW_EVENTF(Context.RHICmdList, PostProcessBasicEyeAdaptationSetup, TEXT("PostProcessBasicEyeAdaptationSetup %dx%d"), DestRect.Width(), DestRect.Height());

		Context.SetViewportAndCallRHI(0, 0, 0.0f, DestSize.X, DestSize.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
		TShaderMapRef<FPostProcessBasicEyeAdaptationSetupPS> PixelShader(Context.GetShaderMap());

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		PixelShader->SetPS(Context);

		DrawPostProcessPass(
			Context.RHICmdList,
			DestRect.Min.X, DestRect.Min.Y,
			DestRect.Width(), DestRect.Height(),
			SrcRect.Min.X, SrcRect.Min.Y,
			SrcRect.Width(), SrcRect.Height(),
			DestSize,
			SrcSize,
			*VertexShader,
			View.StereoPass,
			Context.HasHmdMesh(),
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessBasicEyeAdaptationSetUp::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;

	Ret.Reset();
	Ret.DebugName = TEXT("EyeAdaptationBasicSetup");
	// Require alpha channel for log2 information.
	Ret.Format = PF_FloatRGBA;
	Ret.Flags |= GFastVRamConfig.EyeAdaptation;
	return Ret;
}



class FPostProcessLogLuminance2ExposureScaleBase: public FGlobalShader
{
public:

	FPostProcessLogLuminance2ExposureScaleBase() {}

	FPostProcessLogLuminance2ExposureScaleBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		EyeAdaptationTexture.Bind(Initializer.ParameterMap, TEXT("EyeAdaptationTexture"));
		EyeAdaptationParams.Bind(Initializer.ParameterMap, TEXT("EyeAdaptationParams"));
		EyeAdaptationSrcRect.Bind(Initializer.ParameterMap, TEXT("EyeAdaptionSrcRect"));
	}

	/** Static Shader boilerplate */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
		OutEnvironment.SetDefine(TEXT("EYE_ADAPTATION_PARAMS_SIZE"), (uint32)EYE_ADAPTATION_PARAMS_SIZE);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << EyeAdaptationTexture
			<< EyeAdaptationParams << EyeAdaptationSrcRect;
		return bShaderHasOutdatedParameters;
	}

protected:
	FPostProcessPassParameters	PostprocessParameter;
	FShaderResourceParameter	EyeAdaptationTexture;
	FShaderParameter			EyeAdaptationParams;
	FShaderParameter			EyeAdaptationSrcRect;
};



/** Encapsulates the post process computation of the exposure scale pixel shader. */
class FPostProcessLogLuminance2ExposureScalePS : public FPostProcessLogLuminance2ExposureScaleBase
{
	DECLARE_SHADER_TYPE(FPostProcessLogLuminance2ExposureScalePS, Global);

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, BasicEyeAdaptationMinFeatureLevel);
	}


	/** Default constructor. */
	FPostProcessLogLuminance2ExposureScalePS()
		: FPostProcessLogLuminance2ExposureScaleBase()
	{}

	/** Initialization constructor. */
	FPostProcessLogLuminance2ExposureScalePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FPostProcessLogLuminance2ExposureScaleBase(Initializer)
	{
	}


	void SetPS(const FRenderingCompositePassContext& Context, const FIntRect& SrcRect, IPooledRenderTarget* LastEyeAdaptation)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

		// Associate the eye adaptation buffer from the previous frame with a texture to be read in this frame. 
		
		if (Context.View.HasValidEyeAdaptation())
		{
			SetTextureParameter(Context.RHICmdList, ShaderRHI, EyeAdaptationTexture, LastEyeAdaptation->GetRenderTargetItem().TargetableTexture);
		}
		else // some views don't have a state, thumbnail rendering?
		{
			SetTextureParameter(Context.RHICmdList, ShaderRHI, EyeAdaptationTexture, GWhiteTexture->TextureRHI);
		}

		// Pack the eye adaptation parameters for the shader
		{
			FVector4 Temp[EYE_ADAPTATION_PARAMS_SIZE];
			// static computation function
			ComputeEyeAdaptationValues(BasicEyeAdaptationMinFeatureLevel, Context.View, Temp);
			// Log-based computation of the exposure scale has a built in scaling.
			//Temp[1].X *= 0.16;  
			//Encode the eye-focus slope
			// Get the focus value for the eye-focus weighting
			Temp[2].W = GetBasicAutoExposureFocus();
			SetShaderValueArray(Context.RHICmdList, ShaderRHI, EyeAdaptationParams, Temp, EYE_ADAPTATION_PARAMS_SIZE);
		}

		// Set the src extent for the shader
		SetShaderValue(Context.RHICmdList, ShaderRHI, EyeAdaptationSrcRect, SrcRect);
	}
};
IMPLEMENT_SHADER_TYPE(, FPostProcessLogLuminance2ExposureScalePS, TEXT("/Engine/Private/PostProcessEyeAdaptation.usf"), TEXT("MainLogLuminance2ExposureScalePS"), SF_Pixel);


class FPostProcessLogLuminance2ExposureScaleCS : public FPostProcessLogLuminance2ExposureScaleBase
{
	DECLARE_SHADER_TYPE(FPostProcessLogLuminance2ExposureScaleCS, Global);


public:

	/** Default constructor. */
	FPostProcessLogLuminance2ExposureScaleCS()
		: FPostProcessLogLuminance2ExposureScaleBase()
	{}

	/** Initialization constructor. */
	FPostProcessLogLuminance2ExposureScaleCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FPostProcessLogLuminance2ExposureScaleBase(Initializer)
	{
		OutComputeTex.Bind(Initializer.ParameterMap, TEXT("OutputComputeTex"));
	}

	/** Static Shader boilerplate */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << EyeAdaptationTexture
			<< EyeAdaptationParams << EyeAdaptationSrcRect << OutComputeTex;
		return bShaderHasOutdatedParameters;
	}


	void UnsetParameters(const FRenderingCompositePassContext& Context)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();
		OutComputeTex.UnsetUAV(Context.RHICmdList, ShaderRHI);
	}

	void SetParameters(const FRenderingCompositePassContext& Context, const FIntRect& SrcRect, IPooledRenderTarget* LastEyeAdaptation, FRHIUnorderedAccessView* DestUAV)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetCS(ShaderRHI, Context, Context.RHICmdList, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

		// Associate the eye adaptation buffer from the previous frame with a texture to be read in this frame. 
		if (Context.View.HasValidEyeAdaptation())
		{
			SetTextureParameter(Context.RHICmdList, ShaderRHI, EyeAdaptationTexture, LastEyeAdaptation->GetRenderTargetItem().TargetableTexture);
		}
		else // some views don't have a state, thumbnail rendering?
		{
			SetTextureParameter(Context.RHICmdList, ShaderRHI, EyeAdaptationTexture, GWhiteTexture->TextureRHI);
		}

		// Pack the eye adaptation parameters for the shader
		{
			FVector4 Temp[EYE_ADAPTATION_PARAMS_SIZE];
			// static computation function
			ComputeEyeAdaptationValues(BasicEyeAdaptationMinFeatureLevel, Context.View, Temp);
			// Log-based computation of the exposure scale has a built in scaling.
			//Temp[1].X *= 0.16;  
			//Encode the eye-focus slope
			// Get the focus value for the eye-focus weighting
			Temp[2].W = GetBasicAutoExposureFocus();
			SetShaderValueArray(Context.RHICmdList, ShaderRHI, EyeAdaptationParams, Temp, EYE_ADAPTATION_PARAMS_SIZE);
		}

		// Set the src extent for the shader
		SetShaderValue(Context.RHICmdList, ShaderRHI, EyeAdaptationSrcRect, SrcRect);
		OutComputeTex.SetTexture(Context.RHICmdList, ShaderRHI, nullptr, DestUAV);
	}
	private:

	FRWShaderParameter			OutComputeTex;
};
IMPLEMENT_SHADER_TYPE(, FPostProcessLogLuminance2ExposureScaleCS, TEXT("/Engine/Private/PostProcessEyeAdaptation.usf"), TEXT("MainLogLuminance2ExposureScaleCS"), SF_Compute);


void FRCPassPostProcessBasicEyeAdaptation::Process(FRenderingCompositePassContext& Context)
{
	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	// Get the custom 1x1 target used to store exposure value and Toggle the two render targets used to store new and old.
	Context.View.SwapEyeAdaptationRTs(Context.RHICmdList);
	IPooledRenderTarget* EyeAdaptationThisFrameRT = Context.View.GetEyeAdaptationRT(Context.RHICmdList);
	IPooledRenderTarget* EyeAdaptationLastFrameRT = Context.View.GetLastEyeAdaptationRT(Context.RHICmdList);

	check(EyeAdaptationThisFrameRT && EyeAdaptationLastFrameRT);

	FIntPoint DestSize = EyeAdaptationThisFrameRT->GetDesc().Extent;

	// The input texture sample size.  Averaged in the pixel shader.
	FIntPoint SrcSize = GetInputDesc(ePId_Input0)->Extent;

	// Compute the region of interest in the source texture.
	uint32 ScaleFactor = FMath::DivideAndRoundUp(FSceneRenderTargets::Get(Context.RHICmdList).GetBufferSizeXY().Y, SrcSize.Y);

	FIntRect SrcRect = View.ViewRect / ScaleFactor;

	SCOPED_DRAW_EVENTF(Context.RHICmdList, PostProcessBasicEyeAdaptation, TEXT("PostProcessBasicEyeAdaptation %dx%d"), SrcSize.X, SrcSize.Y);

	const FSceneRenderTargetItem& DestRenderTarget = EyeAdaptationThisFrameRT->GetRenderTargetItem();

	// Inform MultiGPU systems that we're starting to update this texture for this frame
	Context.RHICmdList.BeginUpdateMultiFrameResource(DestRenderTarget.ShaderResourceTexture);

	// we render to our own output render target, not the intermediate one created by the compositing system
	// Set the view family's render target/viewport.

	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, ERenderTargetActions::Load_Store);
	TransitionRenderPassTargets(Context.RHICmdList, RPInfo);
		
	bool IsCompute = (CVarEyeAdaptationBasicCompute.GetValueOnRenderThread()>0) && (Context.View.GetFeatureLevel() >= ERHIFeatureLevel::SM5);
	if (IsCompute)
	{
		WaitForInputPassComputeFences(Context.RHICmdList);

		// #todo-renderpasses remove once everything is renderpasses
		UnbindRenderTargets(Context.RHICmdList);
		TShaderMapRef<FPostProcessLogLuminance2ExposureScaleCS> ComputeShader(Context.GetShaderMap());
		Context.RHICmdList.SetComputeShader(ComputeShader->GetComputeShader());

		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, DestRenderTarget.UAV);
		ComputeShader->SetParameters(Context, SrcRect, EyeAdaptationLastFrameRT, DestRenderTarget.UAV);
		DispatchComputeShader(Context.RHICmdList, *ComputeShader, 1, 1, 1);
		ComputeShader->UnsetParameters(Context);
		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, DestRenderTarget.UAV);
	}
	else
	{
		Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("BasicEyeAdaptation"));
		{
			Context.SetViewportAndCallRHI(0, 0, 0.0f, DestSize.X, DestSize.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
			TShaderMapRef<FPostProcessLogLuminance2ExposureScalePS> PixelShader(Context.GetShaderMap());

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

			// Set the parameters used by the pixel shader.

			PixelShader->SetPS(Context, SrcRect, EyeAdaptationLastFrameRT);

			// Draw a quad mapping scene color to the view's render target
			DrawRectangle(
				Context.RHICmdList,
				0, 0,
				DestSize.X, DestSize.Y,
				0, 0,
				DestSize.X, DestSize.Y,
				DestSize,
				DestSize,
				*VertexShader,
				EDRF_UseTriangleOptimization);
		}
		Context.RHICmdList.EndRenderPass();
		Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
	}

	// Inform MultiGPU systems that we've finished with this texture for this frame
	Context.RHICmdList.EndUpdateMultiFrameResource(DestRenderTarget.ShaderResourceTexture);

	Context.View.SetValidEyeAdaptation();
}


FPooledRenderTargetDesc FRCPassPostProcessBasicEyeAdaptation::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	// Specify invalid description to avoid getting intermediate rendertargets created.
	// We want to use ViewState->GetEyeAdaptation() instead
	FPooledRenderTargetDesc Ret;

	Ret.DebugName = TEXT("EyeAdaptationBasic");
	Ret.Flags |= GFastVRamConfig.EyeAdaptation;

	if (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		Ret.Flags |= TexCreate_UAV;
	}
	return Ret;
}

//
FSceneViewState::FEyeAdaptationRTManager::~FEyeAdaptationRTManager()
{
}

void FSceneViewState::FEyeAdaptationRTManager::SafeRelease()
{
	PooledRenderTarget[0].SafeRelease();
	PooledRenderTarget[1].SafeRelease();

	ExposureTextureReadback = nullptr;
}

void FSceneViewState::FEyeAdaptationRTManager::SwapRTs(bool bInUpdateLastExposure)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FEyeAdaptationRTManager_SwapRTs);

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (bInUpdateLastExposure && PooledRenderTarget[CurrentBuffer].IsValid() && (GIsEditor || CVarEnablePreExposureOnlyInTheEditor.GetValueOnRenderThread() == 0))
	{
		if (!ExposureTextureReadback)
		{
			static const FName ExposureValueName(TEXT("Scene view state exposure readback"));
			ExposureTextureReadback.Reset(new FRHIGPUTextureReadback(ExposureValueName));
			// Send the first request.
			ExposureTextureReadback->EnqueueCopy(RHICmdList, PooledRenderTarget[CurrentBuffer]->GetRenderTargetItem().TargetableTexture);
		}
		else if (ExposureTextureReadback->IsReady())
		{
			// Read the last request results.
			FVector4* ReadbackData = (FVector4*)ExposureTextureReadback->Lock(sizeof(FVector4));
			if (ReadbackData)
			{
				LastExposure = ReadbackData->X;
				LastAverageSceneLuminance = ReadbackData->Z;

				ExposureTextureReadback->Unlock();
			}

			// Send the request for next update.
			ExposureTextureReadback->EnqueueCopy(RHICmdList, PooledRenderTarget[CurrentBuffer]->GetRenderTargetItem().TargetableTexture);
		}
	}

	CurrentBuffer = 1 - CurrentBuffer;
}

TRefCountPtr<IPooledRenderTarget>&  FSceneViewState::FEyeAdaptationRTManager::GetRTRef(FRHICommandList* RHICmdList, const int BufferNumber)
{
	check(BufferNumber == 0 || BufferNumber == 1);

	// Create textures if needed.
	if (!PooledRenderTarget[BufferNumber].IsValid() && RHICmdList)
	{
		// Create the texture needed for EyeAdaptation
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable, false));
		if (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5)
		{
			Desc.TargetableFlags |= TexCreate_UAV;
		}
		GRenderTargetPool.FindFreeElement(*RHICmdList, Desc, PooledRenderTarget[BufferNumber], TEXT("EyeAdaptation"), true, ERenderTargetTransience::NonTransient);
	}

	return PooledRenderTarget[BufferNumber];
}
