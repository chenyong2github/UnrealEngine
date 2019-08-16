// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessEyeAdaptation.cpp: Post processing eye adaptation implementation.
=============================================================================*/

#include "PostProcess/PostProcessEyeAdaptation.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "SceneTextureParameters.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "Math/UnrealMathUtility.h"
#include "ScenePrivate.h"
#include "Curves/CurveFloat.h"
#include "RHIGPUReadback.h"

RENDERCORE_API bool UsePreExposure(EShaderPlatform Platform);

namespace
{
TAutoConsoleVariable<float> CVarEyeAdaptationPreExposureOverride(
	TEXT("r.EyeAdaptation.PreExposureOverride"),
	0,
	TEXT("Overide the scene pre-exposure by a custom value. \n")
	TEXT("= 0 : No override\n")
	TEXT("> 0 : Override PreExposure\n"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarEyeAdaptationMethodOverride(
	TEXT("r.EyeAdaptation.MethodOverride"),
	-1,
	TEXT("Override the camera metering method set in post processing volumes\n")
	TEXT("-2: override with custom settings (for testing Basic Mode)\n")
	TEXT("-1: no override\n")
	TEXT(" 1: Auto Histogram-based\n")
	TEXT(" 2: Auto Basic\n")
	TEXT(" 3: Manual"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

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
}

bool IsAutoExposureMethodSupported(ERHIFeatureLevel::Type FeatureLevel, EAutoExposureMethod AutoExposureMethodId)
{
	switch (AutoExposureMethodId)
	{
	case EAutoExposureMethod::AEM_Histogram:
		return FeatureLevel >= ERHIFeatureLevel::SM5;
	case EAutoExposureMethod::AEM_Basic:
	case EAutoExposureMethod::AEM_Manual:
		return FeatureLevel >= ERHIFeatureLevel::ES3_1;
	}
	return false;
}

// Query the view for the auto exposure method, and allow for CVar override.
EAutoExposureMethod GetAutoExposureMethod(const FViewInfo& View)
{
	EAutoExposureMethod AutoExposureMethodId = View.FinalPostProcessSettings.AutoExposureMethod;

	const int32 EyeOverride = CVarEyeAdaptationMethodOverride.GetValueOnRenderThread();

	if (EyeOverride >= 0)
	{
		// Additional branching for override.
		switch (EyeOverride)
		{
		case 1:
		{
			// Only override if the platform supports it.
			if (View.GetFeatureLevel() >= ERHIFeatureLevel::SM5)
			{
				AutoExposureMethodId = EAutoExposureMethod::AEM_Histogram;
			}
			break;
		}
		case 2:
		{
			AutoExposureMethodId = EAutoExposureMethod::AEM_Basic;
			break;
		}
		case 3:
		{
			AutoExposureMethodId = EAutoExposureMethod::AEM_Manual;
			break;
		}
		}
	}

	// If auto exposure is disabled, revert to manual mode which will clamp to a reasonable default.
	if (!View.Family->EngineShowFlags.EyeAdaptation)
	{
		AutoExposureMethodId = AEM_Manual;
	}

	// We should have a valid exposure method at this stage.
	check(IsAutoExposureMethodSupported(View.GetFeatureLevel(), AutoExposureMethodId));

	return AutoExposureMethodId;
}

bool IsExtendLuminanceRangeEnabled()
{
	static const auto VarDefaultAutoExposureExtendDefaultLuminanceRange = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));

	return VarDefaultAutoExposureExtendDefaultLuminanceRange->GetValueOnRenderThread() == 1;
}

float GetBasicAutoExposureFocus()
{
	const float FocusMax = 10.f;
	const float FocusValue = CVarEyeAdaptationFocus.GetValueOnRenderThread();
	return FMath::Max(FMath::Min(FocusValue, FocusMax), 0.0f);
}

float GetAutoExposureCompensation(const FViewInfo& View)
{
	const FPostProcessSettings& Settings = View.FinalPostProcessSettings;

	const FEngineShowFlags& EngineShowFlags = View.Family->EngineShowFlags;

	// Skip the exposure bias when any of these flags are set.
	const bool bSkipExposureBias =
		View.Family->ExposureSettings.bFixed ||
		View.Family->UseDebugViewPS() ||
		!EngineShowFlags.Lighting ||
		(EngineShowFlags.VisualizeBuffer && View.CurrentBufferVisualizationMode != NAME_None) ||
		EngineShowFlags.RayTracingDebug ||
		EngineShowFlags.VisualizeDistanceFieldAO ||
		EngineShowFlags.VisualizeGlobalDistanceField ||
		EngineShowFlags.CollisionVisibility ||
		EngineShowFlags.CollisionPawn;

	if (bSkipExposureBias)
	{
		return 1.0f;
	}

	// This scales the average luminance AFTER it gets clamped, affecting the exposure value directly.
	float AutoExposureBias = Settings.AutoExposureBias;

	if (Settings.AutoExposureBiasCurve)
	{
		const float AverageSceneLuminance = View.GetLastAverageSceneLuminance();

		if (AverageSceneLuminance > 0)
		{
			AutoExposureBias += Settings.AutoExposureBiasCurve->GetFloatValue(LuminanceToEV100(AverageSceneLuminance));
		}
	}

	return FMath::Pow(2.0f, AutoExposureBias);
}

FEyeAdaptationParameters GetEyeAdaptationParameters(const FViewInfo& View, ERHIFeatureLevel::Type MinFeatureLevel)
{
	const FPostProcessSettings& Settings = View.FinalPostProcessSettings;

	const FEngineShowFlags& EngineShowFlags = View.Family->EngineShowFlags;

	const EAutoExposureMethod AutoExposureMethod = GetAutoExposureMethod(View);

	const bool bExtendedLuminanceRange = IsExtendLuminanceRangeEnabled();

	const float PercentToScale = 0.01f;

	const float ExposureHighPercent = FMath::Clamp(Settings.AutoExposureHighPercent, 1.0f, 99.0f) * PercentToScale;
	const float ExposureLowPercent = FMath::Min(FMath::Clamp(Settings.AutoExposureLowPercent, 1.0f, 99.0f) * PercentToScale, ExposureHighPercent);

	const float HistogramLogMax = bExtendedLuminanceRange ? EV100ToLog2(Settings.HistogramLogMax) : Settings.HistogramLogMax;
	const float HistogramLogMin = FMath::Min(bExtendedLuminanceRange ? EV100ToLog2(Settings.HistogramLogMin) : Settings.HistogramLogMin, HistogramLogMax - 1);

	// These clamp the average luminance computed from the scene color.
	float MinAverageLuminance = 1.0f;
	float MaxAverageLuminance = 1.0f;

	// Fixed exposure override in effect.
	if (View.Family->ExposureSettings.bFixed)
	{
		MinAverageLuminance = MaxAverageLuminance = EV100ToLuminance(View.Family->ExposureSettings.FixedEV100);
	}
	// When !EngineShowFlags.EyeAdaptation (from "r.EyeAdaptationQuality 0") or the feature level doesn't support eye adaptation, only Settings.AutoExposureBias controls exposure.
	else if (EngineShowFlags.EyeAdaptation && View.GetFeatureLevel() >= MinFeatureLevel)
	{
		if (AutoExposureMethod == EAutoExposureMethod::AEM_Manual)
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
	}

	MinAverageLuminance = FMath::Min(MinAverageLuminance, MaxAverageLuminance);

	// This scales the average luminance BEFORE it gets clamped. Note that AEM_Histogram implements the calibration constant through ExposureLowPercent and ExposureHighPercent.
	const float CalibrationConstant = FMath::Clamp(Settings.AutoExposureCalibrationConstant, 1.0f, 100.0f) * PercentToScale;

	const float ExposureCompensation = GetAutoExposureCompensation(View);

	const float WeightSlope = (AutoExposureMethod == EAutoExposureMethod::AEM_Basic) ? GetBasicAutoExposureFocus() : 0.0f;

	const float HistogramLogDelta = HistogramLogMax - HistogramLogMin;
	const float HistogramScale = 1.0f / HistogramLogDelta;
	const float HistogramBias = -HistogramLogMin * HistogramScale;
	const float LuminanceMin = FMath::Exp2(HistogramLogMin);

	FEyeAdaptationParameters Parameters;
	Parameters.ExposureLowPercent = ExposureLowPercent;
	Parameters.ExposureHighPercent = ExposureHighPercent;
	Parameters.MinAverageLuminance = MinAverageLuminance;
	Parameters.MaxAverageLuminance = MaxAverageLuminance;
	Parameters.ExposureCompensation = ExposureCompensation;
	Parameters.DeltaWorldTime = View.Family->DeltaWorldTime;
	Parameters.ExposureSpeedUp = Settings.AutoExposureSpeedUp;
	Parameters.ExposureSpeedDown = Settings.AutoExposureSpeedDown;
	Parameters.HistogramScale = HistogramScale;
	Parameters.HistogramBias = HistogramBias;
	Parameters.LuminanceMin = LuminanceMin;
	Parameters.CalibrationConstantInverse = 1.0f / CalibrationConstant;
	Parameters.WeightSlope = WeightSlope;
	return Parameters;
}

float GetEyeAdaptationFixedExposure(const FViewInfo& View)
{
	const FEyeAdaptationParameters Parameters = GetEyeAdaptationParameters(View);

	const float Exposure = (Parameters.MinAverageLuminance + Parameters.MaxAverageLuminance) * 0.5f;

	const float ExposureScale = 1.0f / FMath::Max(0.0001f, Exposure);

	return ExposureScale * Parameters.ExposureCompensation;
}

//////////////////////////////////////////////////////////////////////////
//! Histogram Eye Adaptation
//////////////////////////////////////////////////////////////////////////

class FEyeAdaptationShader : public FGlobalShader
{
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistogramTexture)
	END_SHADER_PARAMETER_STRUCT()

	static const EPixelFormat OutputFormat = PF_A32B32G32R32F;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, OutputFormat);
	}

	FEyeAdaptationShader() = default;
	FEyeAdaptationShader(const CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FEyeAdaptationPS : public FEyeAdaptationShader
{
	using Super = FEyeAdaptationShader;
public:
	DECLARE_GLOBAL_SHADER(FEyeAdaptationPS);
	SHADER_USE_PARAMETER_STRUCT(FEyeAdaptationPS, Super);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(Super::FParameters, Base)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FEyeAdaptationPS, "/Engine/Private/PostProcessEyeAdaptation.usf", "EyeAdaptationPS", SF_Pixel);

class FEyeAdaptationCS : public FEyeAdaptationShader
{
	using Super = FEyeAdaptationShader;
public:
	DECLARE_GLOBAL_SHADER(FEyeAdaptationCS);
	SHADER_USE_PARAMETER_STRUCT(FEyeAdaptationCS, Super);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(Super::FParameters, Base)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWEyeAdaptationTexture)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FEyeAdaptationCS, "/Engine/Private/PostProcessEyeAdaptation.usf", "EyeAdaptationCS", SF_Compute);

FRDGTextureRef AddHistogramEyeAdaptationPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	FRDGTextureRef HistogramTexture)
{
	const FViewInfo& View = ScreenPassView.View;

	View.SwapEyeAdaptationRTs(GraphBuilder.RHICmdList);
	View.SetValidEyeAdaptation();

	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(View.GetEyeAdaptation(GraphBuilder.RHICmdList), TEXT("EyeAdaptation"), ERDGResourceFlags::MultiFrame);

	FEyeAdaptationShader::FParameters PassBaseParameters;
	PassBaseParameters.EyeAdaptation = GetEyeAdaptationParameters(View);
	PassBaseParameters.HistogramTexture = HistogramTexture;

	if (ScreenPassView.bUseComputePasses)
	{
		FEyeAdaptationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FEyeAdaptationCS::FParameters>();
		PassParameters->Base = PassBaseParameters;
		PassParameters->RWEyeAdaptationTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FEyeAdaptationCS> ComputeShader(ScreenPassView.View.ShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HistogramEyeAdaptation (CS)"),
			*ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}
	else
	{
		FEyeAdaptationPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FEyeAdaptationPS::FParameters>();
		PassParameters->Base = PassBaseParameters;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FEyeAdaptationPS> PixelShader(ScreenPassView.View.ShaderMap);

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("HistogramEyeAdaptation (PS)"),
			ScreenPassView,
			FScreenPassTextureViewport(OutputTexture),
			FScreenPassTextureViewport(HistogramTexture),
			*PixelShader,
			PassParameters);
	}

	return OutputTexture;
}

FRenderingCompositeOutputRef AddHistogramEyeAdaptationPass(FPostprocessContext& Context, FRenderingCompositeOutputRef Histogram)
{
	const FViewInfo& View = Context.View;

	FRenderingCompositePass* EyeAdaptationPass = Context.Graph.RegisterPass(
		new(FMemStack::Get()) TRCPassForRDG<1, 1>(
			[&View](FRenderingCompositePass* Pass, FRenderingCompositePassContext& InContext)
	{
		FRDGBuilder GraphBuilder(InContext.RHICmdList);

		FRDGTextureRef HistogramTexture = Pass->CreateRDGTextureForOptionalInput(GraphBuilder, ePId_Input0, TEXT("Histogram"));

		if (!HistogramTexture)
		{
			HistogramTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		}

		const FEyeAdaptationParameters EyeAdaptationParameters = GetEyeAdaptationParameters(View);

		FRDGTextureRef OutputTexture = AddHistogramEyeAdaptationPass(GraphBuilder, FScreenPassViewInfo(View), EyeAdaptationParameters, HistogramTexture);

		Pass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, OutputTexture);

		GraphBuilder.Execute();
	}));

	EyeAdaptationPass->SetInput(ePId_Input0, Histogram);

	return FRenderingCompositeOutputRef(EyeAdaptationPass);
}

//////////////////////////////////////////////////////////////////////////
//! Basic Eye Adaptation
//////////////////////////////////////////////////////////////////////////

const ERHIFeatureLevel::Type BasicEyeAdaptationMinFeatureLevel = ERHIFeatureLevel::ES3_1;

/** Computes scaled and biased luma for the input scene color and puts it in the alpha channel. */
class FBasicEyeAdaptationSetupPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FBasicEyeAdaptationSetupPS);
	SHADER_USE_PARAMETER_STRUCT(FBasicEyeAdaptationSetupPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ColorSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, BasicEyeAdaptationMinFeatureLevel);
	}
};

IMPLEMENT_GLOBAL_SHADER(FBasicEyeAdaptationSetupPS, "/Engine/Private/PostProcessEyeAdaptation.usf", "BasicEyeAdaptationSetupPS", SF_Pixel);

FRDGTextureRef AddBasicEyeAdaptationSetupPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	FRDGTextureRef SceneColorTexture,
	FIntRect SceneColorViewRect)
{
	check(SceneColorTexture);

	FRDGTextureDesc OutputDesc = SceneColorTexture->Desc;
	OutputDesc.Reset();
	OutputDesc.DebugName = TEXT("EyeAdaptationBasicSetup");
	// Require alpha channel for log2 information.
	OutputDesc.Format = PF_FloatRGBA;
	OutputDesc.Flags |= GFastVRamConfig.EyeAdaptation;

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("BasicEyeAdaptationSetup"));

	const FScreenPassTextureViewport Viewport(SceneColorViewRect, OutputTexture);

	FBasicEyeAdaptationSetupPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBasicEyeAdaptationSetupPS::FParameters>();
	PassParameters->EyeAdaptation = GetEyeAdaptationParameters(ScreenPassView.View, BasicEyeAdaptationMinFeatureLevel);
	PassParameters->ColorTexture = SceneColorTexture;
	PassParameters->ColorSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ScreenPassView.GetOverwriteLoadAction());

	TShaderMapRef<FBasicEyeAdaptationSetupPS> PixelShader(ScreenPassView.View.ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("BasicEyeAdaptationSetup (PS) %dx%d", Viewport.Rect.Width(), Viewport.Rect.Height()),
		ScreenPassView,
		Viewport,
		Viewport,
		*PixelShader,
		PassParameters);

	return OutputTexture;
}

FRenderingCompositeOutputRef AddBasicEyeAdaptationSetupPass(
	FPostprocessContext& Context,
	FRenderingCompositeOutputRef SceneColor,
	FIntRect SceneColorViewRect)
{
	const FViewInfo& View = Context.View;

	FRenderingCompositePass* Pass = Context.Graph.RegisterPass(
		new(FMemStack::Get()) TRCPassForRDG<1, 1>(
			[&View, SceneColorViewRect](FRenderingCompositePass* InPass, FRenderingCompositePassContext& InContext)
	{
		FRDGBuilder GraphBuilder(InContext.RHICmdList);

		const FEyeAdaptationParameters EyeAdaptationParameters = GetEyeAdaptationParameters(View);

		FRDGTextureRef SceneColorTexture = InPass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"));

		FRDGTextureRef OutputTexture = AddBasicEyeAdaptationSetupPass(GraphBuilder, FScreenPassViewInfo(View), EyeAdaptationParameters, SceneColorTexture, SceneColorViewRect);

		InPass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, OutputTexture);

		GraphBuilder.Execute();
	}));

	Pass->SetInput(ePId_Input0, SceneColor);

	return FRenderingCompositeOutputRef(Pass);
}

class FBasicEyeAdaptationShader : public FGlobalShader
{
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Color)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptationTexture)
	END_SHADER_PARAMETER_STRUCT()

	static const EPixelFormat OutputFormat = PF_A32B32G32R32F;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, OutputFormat);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, BasicEyeAdaptationMinFeatureLevel);
	}

	FBasicEyeAdaptationShader() = default;
	FBasicEyeAdaptationShader(const CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FBasicEyeAdaptationPS : public FBasicEyeAdaptationShader
{
	using Super = FBasicEyeAdaptationShader;
public:
	DECLARE_GLOBAL_SHADER(FBasicEyeAdaptationPS);
	SHADER_USE_PARAMETER_STRUCT(FBasicEyeAdaptationPS, Super);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(Super::FParameters, Base)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FBasicEyeAdaptationPS, "/Engine/Private/PostProcessEyeAdaptation.usf", "BasicEyeAdaptationPS", SF_Pixel);

class FBasicEyeAdaptationCS : public FBasicEyeAdaptationShader
{
	using Super = FBasicEyeAdaptationShader;
public:
	DECLARE_GLOBAL_SHADER(FBasicEyeAdaptationCS);
	SHADER_USE_PARAMETER_STRUCT(FBasicEyeAdaptationCS, Super);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(Super::FParameters, Base)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWEyeAdaptationTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FBasicEyeAdaptationCS, "/Engine/Private/PostProcessEyeAdaptation.usf", "BasicEyeAdaptationCS", SF_Compute);

FRDGTextureRef AddBasicEyeAdaptationPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	FRDGTextureRef SceneColorTexture,
	FIntRect SceneColorViewRect,
	FRDGTextureRef EyeAdaptationTexture)
{
	const FViewInfo& View = ScreenPassView.View;

	View.SwapEyeAdaptationRTs(GraphBuilder.RHICmdList);
	View.SetValidEyeAdaptation();

	const FScreenPassTextureViewport SceneColorViewport(SceneColorViewRect, SceneColorTexture);

	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(View.GetEyeAdaptation(GraphBuilder.RHICmdList), TEXT("EyeAdaptation"), ERDGResourceFlags::MultiFrame);

	FBasicEyeAdaptationShader::FParameters PassBaseParameters;
	PassBaseParameters.View = ScreenPassView.View.ViewUniformBuffer;
	PassBaseParameters.EyeAdaptation = GetEyeAdaptationParameters(View);
	PassBaseParameters.Color = GetScreenPassTextureViewportParameters(SceneColorViewport);
	PassBaseParameters.ColorTexture = SceneColorTexture;
	PassBaseParameters.EyeAdaptationTexture = EyeAdaptationTexture;

	if (ScreenPassView.bUseComputePasses)
	{
		FBasicEyeAdaptationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBasicEyeAdaptationCS::FParameters>();
		PassParameters->Base = PassBaseParameters;
		PassParameters->RWEyeAdaptationTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FBasicEyeAdaptationCS> ComputeShader(ScreenPassView.View.ShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BasicEyeAdaptation (CS)"),
			*ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}
	else
	{
		FBasicEyeAdaptationPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBasicEyeAdaptationPS::FParameters>();
		PassParameters->Base = PassBaseParameters;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FBasicEyeAdaptationPS> PixelShader(ScreenPassView.View.ShaderMap);

		const FScreenPassTextureViewport OutputViewport(OutputTexture);

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("BasicEyeAdaptation (PS)"),
			ScreenPassView,
			OutputViewport,
			OutputViewport,
			*PixelShader,
			PassParameters);
	}

	return OutputTexture;
}

FRenderingCompositeOutputRef AddBasicEyeAdaptationPass(
	FPostprocessContext& Context,
	FRenderingCompositeOutputRef SceneColor,
	FIntRect SceneColorViewRect)
{
	const FViewInfo& View = Context.View;

	FRenderingCompositePass* Pass = Context.Graph.RegisterPass(
		new(FMemStack::Get()) TRCPassForRDG<1, 1>(
			[&View, SceneColorViewRect](FRenderingCompositePass* InPass, FRenderingCompositePassContext& InContext)
	{
		FRDGBuilder GraphBuilder(InContext.RHICmdList);

		const FEyeAdaptationParameters EyeAdaptationParameters = GetEyeAdaptationParameters(View);

		FRDGTextureRef SceneColorTexture = InPass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"));

		FRDGTextureRef EyeAdaptationTexture = GetEyeAdaptationTexture(GraphBuilder, View);

		FRDGTextureRef OutputTexture = AddBasicEyeAdaptationPass(
			GraphBuilder,
			FScreenPassViewInfo(View),
			EyeAdaptationParameters,
			SceneColorTexture,
			SceneColorViewRect,
			EyeAdaptationTexture);

		InPass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, OutputTexture);

		GraphBuilder.Execute();
	}));

	Pass->SetInput(ePId_Input0, SceneColor);

	return FRenderingCompositeOutputRef(Pass);
}

FSceneViewState::FEyeAdaptationRTManager::~FEyeAdaptationRTManager() {}

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
	if (bInUpdateLastExposure && PooledRenderTarget[CurrentBuffer].IsValid())
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

TRefCountPtr<IPooledRenderTarget>& FSceneViewState::FEyeAdaptationRTManager::GetRTRef(FRHICommandList* RHICmdList, const int BufferNumber)
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
			PreExposure = GetEyeAdaptationFixedExposure(View);
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
	if (!View.bStatePrevViewInfoIsReadOnly)
	{
		PrevFrameViewInfo.SceneColorPreExposure = PreExposure;
	}
}