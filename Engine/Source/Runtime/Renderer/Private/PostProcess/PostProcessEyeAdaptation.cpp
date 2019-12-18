// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessEyeAdaptation.h"
#include "RHIGPUReadback.h"
#include "Curves/CurveFloat.h"

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

TAutoConsoleVariable<float> CVarEyeAdaptationExponentialTransitionDistance(
	TEXT("r.EyeAdaptation.ExponentialTransitionDistance"),
	1.5,
	TEXT("The auto exposure moves linearly, but when it gets ExponentialTransitionDistance F-stops away from the\n")
	TEXT("target exposure it switches to as slower exponential function.\n"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int> CVarEyeAdaptationVisualizeDebugType(
	TEXT("r.EyeAdaptation.VisualizeDebugType"),
	0,
	TEXT("When enabling Show->Visualize->HDR (Eye Adaptation) is enabled, this flag controls the scene color.\n")
	TEXT("    0: Scene Color after tonemapping (default).\n")
	TEXT("    1: Histogram Debug\n"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarEyeAdaptationLensAttenuation(
	TEXT("r.EyeAdaptation.LensAttenuation"),
	0.78,
	TEXT("The camera lens attenuation (q). Set this number to 0.78 for lighting to be unitless (1.0cd/m^2 becomes 1.0 at EV100) or 0.65 to match previous versions (1.0cd/m^2 becomes 1.2 at EV100)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarEnablePreExposureOnlyInTheEditor(
	TEXT("r.EyeAdaptation.EditorOnly"),
	0,
	TEXT("When pre-exposure is enabled, 0 to enable it everywhere, 1 to enable it only in the editor (default).\n")
	TEXT("This is to because it currently has an impact on the renderthread performance\n"),
	ECVF_ReadOnly);
const ERHIFeatureLevel::Type BasicEyeAdaptationMinFeatureLevel = ERHIFeatureLevel::ES3_1;
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

bool IsExtendLuminanceRangeEnabled()
{
	static const auto VarDefaultAutoExposureExtendDefaultLuminanceRange = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));

	return VarDefaultAutoExposureExtendDefaultLuminanceRange->GetValueOnRenderThread() == 1;
}

float LuminanceMaxFromLensAttenuation()
{
	const bool bExtendedLuminanceRange = IsExtendLuminanceRangeEnabled();

	float LensAttenuation = CVarEyeAdaptationLensAttenuation.GetValueOnRenderThread();

	// 78 is defined in the ISO 12232:2006 standard.
	const float kISOSaturationSpeedConstant = 0.78f;

	const float LuminanceMax = kISOSaturationSpeedConstant / FMath::Max<float>(LensAttenuation, .01f);
	
	// if we do not have luminance range extended, the math is hardcoded to 1.0 scale.
	return bExtendedLuminanceRange ? LuminanceMax : 1.0f;
}

// Query the view for the auto exposure method, and allow for CVar override.
EAutoExposureMethod GetAutoExposureMethod(const FViewInfo& View)
{
	EAutoExposureMethod AutoExposureMethod = View.FinalPostProcessSettings.AutoExposureMethod;

	// Fallback to basic if the requested mode is not supported by the feature level.
	if (!IsAutoExposureMethodSupported(View.GetFeatureLevel(), AutoExposureMethod))
	{
		AutoExposureMethod = EAutoExposureMethod::AEM_Basic;
	}

	const int32 EyeOverride = CVarEyeAdaptationMethodOverride.GetValueOnRenderThread();

	EAutoExposureMethod OverrideAutoExposureMethod = AutoExposureMethod;

	if (EyeOverride >= 0)
	{
		// Additional branching for override.
		switch (EyeOverride)
		{
		case 1:
		{
			OverrideAutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
			break;
		}
		case 2:
		{
			OverrideAutoExposureMethod = EAutoExposureMethod::AEM_Basic;
			break;
		}
		case 3:
		{
			OverrideAutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			break;
		}
		}
	}

	if (IsAutoExposureMethodSupported(View.GetFeatureLevel(), OverrideAutoExposureMethod))
	{
		AutoExposureMethod = OverrideAutoExposureMethod;
	}

	// If auto exposure is disabled, revert to manual mode which will clamp to a reasonable default.
	if (!View.Family->EngineShowFlags.EyeAdaptation)
	{
		AutoExposureMethod = AEM_Manual;
	}

	return AutoExposureMethod;
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

	const float LuminanceMax = LuminanceMaxFromLensAttenuation();

	// This scales the average luminance AFTER it gets clamped, affecting the exposure value directly.
	float AutoExposureBias = Settings.AutoExposureBias;

	if (Settings.AutoExposureBiasCurve)
	{
		const float AverageSceneLuminance = View.GetLastAverageSceneLuminance();

		if (AverageSceneLuminance > 0)
		{
			const float LuminanceEV100 = LuminanceToEV100(LuminanceMax, AverageSceneLuminance);
			AutoExposureBias += Settings.AutoExposureBiasCurve->GetFloatValue(LuminanceEV100);
		}
	}

	return FMath::Pow(2.0f, AutoExposureBias);
}


FEyeAdaptationParameters GetEyeAdaptationParameters(const FViewInfo& View, ERHIFeatureLevel::Type MinFeatureLevel)
{
	const bool bExtendedLuminanceRange = IsExtendLuminanceRangeEnabled();

	const FPostProcessSettings& Settings = View.FinalPostProcessSettings;

	float ExtraExposureCompensation = 0.0f;

	const FEngineShowFlags& EngineShowFlags = View.Family->EngineShowFlags;

	const EAutoExposureMethod AutoExposureMethod = GetAutoExposureMethod(View);

	
	const float LuminanceMax = bExtendedLuminanceRange ? LuminanceMaxFromLensAttenuation() : 1.0f;

	const float PercentToScale = 0.01f;

	const float ExposureHighPercent = FMath::Clamp(Settings.AutoExposureHighPercent, 1.0f, 99.0f) * PercentToScale;
	const float ExposureLowPercent = FMath::Min(FMath::Clamp(Settings.AutoExposureLowPercent, 1.0f, 99.0f) * PercentToScale, ExposureHighPercent);

	const float HistogramLogMax = bExtendedLuminanceRange ? EV100ToLog2(LuminanceMax, Settings.HistogramLogMax) : Settings.HistogramLogMax;
	const float HistogramLogMin = FMath::Min(bExtendedLuminanceRange ? EV100ToLog2(LuminanceMax, Settings.HistogramLogMin) : Settings.HistogramLogMin, HistogramLogMax - 1);

	// These clamp the average luminance computed from the scene color. We are going to calculate the white point first, and then
	// figure out the average grey point later. I.e. if the white point is 1.0, the middle grey point should be 0.18.
	float MinWhitePointLuminance = 1.0f; 
	float MaxWhitePointLuminance = 1.0f;
	float ExposureCompensation = GetAutoExposureCompensation(View) * FMath::Exp2(ExtraExposureCompensation);

	const float BasePhysicalCameraEV100 = FMath::Log2(FMath::Square(Settings.DepthOfFieldFstop) * Settings.CameraShutterSpeed * 100 / FMath::Max(1.f, Settings.CameraISO));

	const float PhysicalCameraEV100 = Settings.AutoExposureApplyPhysicalCameraExposure ? BasePhysicalCameraEV100 : 0.0f;

	const float kMiddleGrey = 0.18f;

	// AEM_Histogram and AEM_Basic adjust their ExposureCompensation to middle grey (0.18). AEM_Manual ExposureCompensation is already calibrated to 1.0.
	const float GreyMult = (AutoExposureMethod == AEM_Manual) ? 1.0f : kMiddleGrey;

	if (View.Family->UseDebugViewPS() ||
		!EngineShowFlags.Lighting ||
		(EngineShowFlags.VisualizeBuffer && View.CurrentBufferVisualizationMode != NAME_None) ||
		EngineShowFlags.RayTracingDebug ||
		EngineShowFlags.VisualizeDistanceFieldAO ||
		EngineShowFlags.VisualizeMeshDistanceFields ||
		EngineShowFlags.VisualizeGlobalDistanceField ||
		EngineShowFlags.CollisionVisibility ||
		EngineShowFlags.CollisionPawn)
	{
		ExposureCompensation = 1.0f;
	}
	// Fixed exposure override in effect.
	else if (View.Family->ExposureSettings.bFixed)
	{
		ExposureCompensation = 1.0f;

		// ignores bExtendedLuminanceRange
		MinWhitePointLuminance = MaxWhitePointLuminance = EV100ToLuminance(LuminanceMax, View.Family->ExposureSettings.FixedEV100);
	}
	// When !EngineShowFlags.EyeAdaptation (from "r.EyeAdaptationQuality 0") or the feature level doesn't support eye adaptation, only Settings.AutoExposureBias controls exposure.
	else if (EngineShowFlags.EyeAdaptation && View.GetFeatureLevel() >= MinFeatureLevel)
	{
		if (AutoExposureMethod == EAutoExposureMethod::AEM_Manual)
		{
			// ignores bExtendedLuminanceRange
			MinWhitePointLuminance = MaxWhitePointLuminance = EV100ToLuminance(LuminanceMax, PhysicalCameraEV100);
		}
		else
		{
			if (bExtendedLuminanceRange)
			{
				MinWhitePointLuminance = EV100ToLuminance(LuminanceMax, Settings.AutoExposureMinBrightness);
				MaxWhitePointLuminance = EV100ToLuminance(LuminanceMax, Settings.AutoExposureMaxBrightness);
			}
			else
			{
				MinWhitePointLuminance = Settings.AutoExposureMinBrightness;
				MaxWhitePointLuminance = Settings.AutoExposureMaxBrightness;
			}
		}
	}

	MinWhitePointLuminance = FMath::Min(MinWhitePointLuminance, MaxWhitePointLuminance);

	// This scales the average luminance BEFORE it gets clamped. Note that AEM_Histogram implements the calibration constant through ExposureLowPercent and ExposureHighPercent.
	//const float CalibrationConstant = FMath::Clamp(Settings.AutoExposureCalibrationConstant, 1.0f, 100.0f) * PercentToScale;
	//const float CalibrationScale = (AutoExposureMethod == EAutoExposureMethod::AEM_Basic || AutoExposureMethod == EAutoExposureMethod::AEM_Histogram) ? CalibrationConstant : 1.0f;

	const float WeightSlope = (AutoExposureMethod == EAutoExposureMethod::AEM_Basic) ? GetBasicAutoExposureFocus() : 0.0f;

	const float HistogramLogDelta = HistogramLogMax - HistogramLogMin;
	const float HistogramScale = 1.0f / HistogramLogDelta;
	const float HistogramBias = -HistogramLogMin * HistogramScale;
	const float LuminanceMin = FMath::Exp2(HistogramLogMin);

	//AutoExposureMeterMask
	const FTextureRHIRef MeterMask = Settings.AutoExposureMeterMask ?
									Settings.AutoExposureMeterMask->Resource->TextureRHI :
									GSystemTextures.WhiteDummy->GetRenderTargetItem().ShaderResourceTexture;

	// The distance at which we switch from linear to exponential. I.e. at StartDistance=1.5, when linear is 1.5 f-stops away from hitting the 
	// target, we switch to exponential.
	const float StartDistance = CVarEyeAdaptationExponentialTransitionDistance.GetValueOnRenderThread();
	const float StartTimeUp = StartDistance / FMath::Max(Settings.AutoExposureSpeedUp,0.001f);
	const float StartTimeDown = StartDistance / FMath::Max(Settings.AutoExposureSpeedDown,0.001f);

	// We want to ensure that at time=StartT, that the derivative of the exponential curve is the same as the derivative of the linear curve.
	// For the linear curve, the step will be AdaptationSpeed * FrameTime.
	// For the exponential curve, the step will be at t=StartT, M is slope modifier:
	//      slope(t) = M * (1.0f - exp2(-FrameTime * AdaptionSpeed)) * AdaptionSpeed * StartT
	//      AdaptionSpeed * FrameTime = M * (1.0f - exp2(-FrameTime * AdaptionSpeed)) * AdaptationSpeed * StartT
	//      M = FrameTime / (1.0f - exp2(-FrameTime * AdaptionSpeed)) * StartT
	//
	// To be technically correct, we should take the limit as FrameTime->0, but for simplicity we can make FrameTime a small number. So:
	const float kFrameTimeEps = 1.0f/60.0f;
	const float ExponentialUpM = kFrameTimeEps / ((1.0f - exp2(-kFrameTimeEps * Settings.AutoExposureSpeedUp)) * StartTimeUp);
	const float ExponentialDownM = kFrameTimeEps / ((1.0f - exp2(-kFrameTimeEps * Settings.AutoExposureSpeedDown)) * StartTimeDown);

	// If the white point luminance is 1.0, then the middle grey luminance should be 0.18.
	const float MinAverageLuminance = MinWhitePointLuminance * kMiddleGrey;
	const float MaxAverageLuminance = MaxWhitePointLuminance * kMiddleGrey;


	const bool bValidRange = View.FinalPostProcessSettings.AutoExposureMinBrightness < View.FinalPostProcessSettings.AutoExposureMaxBrightness;

	// if it is a camera cut we force the exposure to go all the way to the target exposure without blending.
	// if it is manual mode, we also force the exposure to hit the target, which matters for HDR Visualization
	// if we don't have a valid range (AutoExposureMinBrightness == AutoExposureMaxBrightness) then force it like Manual as well.
	const float ForceTarget = (View.bCameraCut || AutoExposureMethod == EAutoExposureMethod::AEM_Manual || !bValidRange) ? 1.0f : 0.0f;

	FEyeAdaptationParameters Parameters;
	Parameters.ExposureLowPercent = ExposureLowPercent;
	Parameters.ExposureHighPercent = ExposureHighPercent;
	Parameters.MinAverageLuminance = MinAverageLuminance;
	Parameters.MaxAverageLuminance = MaxAverageLuminance;
	Parameters.ExposureCompensation = ExposureCompensation;// *CalibrationScale;
	Parameters.DeltaWorldTime = View.Family->DeltaWorldTime;
	Parameters.ExposureSpeedUp = Settings.AutoExposureSpeedUp;
	Parameters.ExposureSpeedDown = Settings.AutoExposureSpeedDown;
	Parameters.HistogramScale = HistogramScale;
	Parameters.HistogramBias = HistogramBias;
	Parameters.LuminanceMin = LuminanceMin;
	Parameters.CalibrationConstantInverse = 1.0f; // no calibration constant because it is now baked into ExposureCompensation
	Parameters.WeightSlope = WeightSlope;
	Parameters.GreyMult = GreyMult;
	Parameters.ExponentialDownM = ExponentialDownM;
	Parameters.ExponentialUpM = ExponentialUpM;
	Parameters.StartDistance = StartDistance;
	Parameters.LuminanceMax = LuminanceMax;
	Parameters.ForceTarget = ForceTarget;
	Parameters.VisualizeDebugType = CVarEyeAdaptationVisualizeDebugType.GetValueOnRenderThread();
	Parameters.MeterMaskTexture = MeterMask;
	Parameters.MeterMaskSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	return Parameters;
}

float GetEyeAdaptationFixedExposure(const FViewInfo& View)
{
	const FEyeAdaptationParameters Parameters = GetEyeAdaptationParameters(View, BasicEyeAdaptationMinFeatureLevel);

	const float Exposure = (Parameters.MinAverageLuminance + Parameters.MaxAverageLuminance) * 0.5f;

	const float kMiddleGrey = 0.18f;
	const float ExposureScale = kMiddleGrey / FMath::Max(0.0001f, Exposure);

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
	const FViewInfo& View,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	FRDGTextureRef HistogramTexture)
{
	View.SwapEyeAdaptationRTs(GraphBuilder.RHICmdList);
	View.SetValidEyeAdaptation();

	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(View.GetEyeAdaptation(GraphBuilder.RHICmdList), TEXT("EyeAdaptation"), ERDGResourceFlags::MultiFrame);

	FEyeAdaptationShader::FParameters PassBaseParameters;
	PassBaseParameters.EyeAdaptation = GetEyeAdaptationParameters(View, ERHIFeatureLevel::SM5);
	PassBaseParameters.HistogramTexture = HistogramTexture;

#if WITH_MGPU
	static const FName NameForTemporalEffect("HistogramEyeAdaptationPass");
	GraphBuilder.SetNameForTemporalEffect(FName(NameForTemporalEffect, View.ViewState ? View.ViewState->UniqueID : 0));
#endif

	if (View.bUseComputePasses)
	{
		FEyeAdaptationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FEyeAdaptationCS::FParameters>();
		PassParameters->Base = PassBaseParameters;
		PassParameters->RWEyeAdaptationTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FEyeAdaptationCS> ComputeShader(View.ShaderMap);

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

		TShaderMapRef<FEyeAdaptationPS> PixelShader(View.ShaderMap);

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("HistogramEyeAdaptation (PS)"),
			View,
			FScreenPassTextureViewport(OutputTexture),
			FScreenPassTextureViewport(HistogramTexture),
			*PixelShader,
			PassParameters);
	}

	return OutputTexture;
}

//////////////////////////////////////////////////////////////////////////
//! Basic Eye Adaptation
//////////////////////////////////////////////////////////////////////////

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

FScreenPassTexture AddBasicEyeAdaptationSetupPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	FScreenPassTexture SceneColor)
{
	check(SceneColor.IsValid());

	FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
	OutputDesc.Reset();
	OutputDesc.DebugName = TEXT("EyeAdaptationBasicSetup");
	// Require alpha channel for log2 information.
	OutputDesc.Format = PF_FloatRGBA;
	OutputDesc.Flags |= GFastVRamConfig.EyeAdaptation;

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("BasicEyeAdaptationSetup"));

	const FScreenPassTextureViewport Viewport(SceneColor);

	FBasicEyeAdaptationSetupPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBasicEyeAdaptationSetupPS::FParameters>();
	PassParameters->EyeAdaptation = EyeAdaptationParameters;
	PassParameters->ColorTexture = SceneColor.Texture;
	PassParameters->ColorSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, View.GetOverwriteLoadAction());

	TShaderMapRef<FBasicEyeAdaptationSetupPS> PixelShader(View.ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("BasicEyeAdaptationSetup (PS) %dx%d", Viewport.Rect.Width(), Viewport.Rect.Height()),
		View,
		Viewport,
		Viewport,
		*PixelShader,
		PassParameters,
		EScreenPassDrawFlags::AllowHMDHiddenAreaMask);

	return FScreenPassTexture(OutputTexture, SceneColor.ViewRect);
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
	const FViewInfo& View,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	FScreenPassTexture SceneColor,
	FRDGTextureRef EyeAdaptationTexture)
{
	View.SwapEyeAdaptationRTs(GraphBuilder.RHICmdList);
	View.SetValidEyeAdaptation();

	const FScreenPassTextureViewport SceneColorViewport(SceneColor);

	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(View.GetEyeAdaptation(GraphBuilder.RHICmdList), TEXT("EyeAdaptation"), ERDGResourceFlags::MultiFrame);

	FBasicEyeAdaptationShader::FParameters PassBaseParameters;
	PassBaseParameters.View = View.ViewUniformBuffer;
	PassBaseParameters.EyeAdaptation = EyeAdaptationParameters;
	PassBaseParameters.Color = GetScreenPassTextureViewportParameters(SceneColorViewport);
	PassBaseParameters.ColorTexture = SceneColor.Texture;
	PassBaseParameters.EyeAdaptationTexture = EyeAdaptationTexture;

#if WITH_MGPU
	static const FName NameForTemporalEffect("BasicEyeAdaptationPass");
	GraphBuilder.SetNameForTemporalEffect(FName(NameForTemporalEffect, View.ViewState ? View.ViewState->UniqueID : 0));
#endif

	if (View.bUseComputePasses)
	{
		FBasicEyeAdaptationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBasicEyeAdaptationCS::FParameters>();
		PassParameters->Base = PassBaseParameters;
		PassParameters->RWEyeAdaptationTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FBasicEyeAdaptationCS> ComputeShader(View.ShaderMap);

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

		TShaderMapRef<FBasicEyeAdaptationPS> PixelShader(View.ShaderMap);

		const FScreenPassTextureViewport OutputViewport(OutputTexture);

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("BasicEyeAdaptation (PS)"),
			View,
			OutputViewport,
			OutputViewport,
			*PixelShader,
			PassParameters);
	}

	return OutputTexture;
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
			// Workaround until FRHIGPUTextureReadback::Lock has multigpu support
			FRHIGPUMask ReadBackGPUMask = RHICmdList.GetGPUMask();
			if (!ReadBackGPUMask.HasSingleIndex())
			{
				ReadBackGPUMask = FRHIGPUMask::GPU0();
			}

			SCOPED_GPU_MASK(RHICmdList, ReadBackGPUMask);

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
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
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
		!ViewFamily.EngineShowFlags.LevelColoration;

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