// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessEyeAdaptation.h"
#include "RHIGPUReadback.h"
#include "RendererUtils.h"
#include "ScenePrivate.h"
#include "Curves/CurveFloat.h"

bool IsMobileEyeAdaptationEnabled(const FViewInfo& View);

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

TAutoConsoleVariable<float> CVarEyeAdaptationBlackHistogramBucketInfluence(
	TEXT("r.EyeAdaptation.BlackHistogramBucketInfluence"),
	0.0,
	TEXT("This parameter controls how much weight to apply to completely dark 0.0 values in the exposure histogram.\n")
	TEXT("When set to 1.0, fully dark pixels will accumulate normally, whereas when set to 0.0 fully dark pixels\n")
	TEXT("will have no influence.\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarEnablePreExposureOnlyInTheEditor(
	TEXT("r.EyeAdaptation.EditorOnly"),
	0,
	TEXT("When pre-exposure is enabled, 0 to enable it everywhere, 1 to enable it only in the editor (default).\n")
	TEXT("This is to because it currently has an impact on the renderthread performance\n"),
	ECVF_ReadOnly);
}

// Function is static because EyeAdaptation is the only system that should be checking this value.
static bool UsePreExposureEnabled()
{
	static const auto CVarUsePreExposure = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.UsePreExposure"));
	return CVarUsePreExposure->GetValueOnAnyThread() != 0;
}

// Basic eye adaptation is supported everywhere except mobile when MobileHDR is disabled
static ERHIFeatureLevel::Type GetBasicEyeAdaptationMinFeatureLevel()
{
	return IsMobileHDR() ? ERHIFeatureLevel::ES3_1 : ERHIFeatureLevel::SM5;
}

bool IsAutoExposureMethodSupported(ERHIFeatureLevel::Type FeatureLevel, EAutoExposureMethod AutoExposureMethodId)
{
	switch (AutoExposureMethodId)
	{
	case EAutoExposureMethod::AEM_Histogram:
	case EAutoExposureMethod::AEM_Basic:
		return FeatureLevel > ERHIFeatureLevel::ES3_1 || IsMobileHDR();
	case EAutoExposureMethod::AEM_Manual:
		return true;
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

	// Fallback to basic (or manual) if the requested mode is not supported by the feature level.
	if (!IsAutoExposureMethodSupported(View.GetFeatureLevel(), AutoExposureMethod))
	{
		AutoExposureMethod = IsAutoExposureMethodSupported(View.GetFeatureLevel(), EAutoExposureMethod::AEM_Basic) ? EAutoExposureMethod::AEM_Basic : EAutoExposureMethod::AEM_Manual;
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

float GetAutoExposureCompensationFromSettings(const FViewInfo& View)
{
	const FPostProcessSettings& Settings = View.FinalPostProcessSettings;

	// This scales the average luminance AFTER it gets clamped, affecting the exposure value directly.
	float AutoExposureBias = Settings.AutoExposureBias;

	// AutoExposureBias need to minus 1 if it is used for mobile LDR, because we don't go through the postprocess eye adaptation pass. 
	if (IsMobilePlatform(View.GetShaderPlatform()) && !IsMobileHDR())
	{
		AutoExposureBias = AutoExposureBias - 1.0f;
	}

	return FMath::Pow(2.0f, AutoExposureBias);
}


float GetAutoExposureCompensationFromCurve(const FViewInfo& View)
{
	const FPostProcessSettings& Settings = View.FinalPostProcessSettings;

	const float LuminanceMax = LuminanceMaxFromLensAttenuation();

	float AutoExposureBias = 0.0f;

	if (Settings.AutoExposureBiasCurve)
	{
		// Note that we are using View.GetLastAverageSceneLuminance() instead of the alternatives. GetLastAverageSceneLuminance()
		// immediately converges because it is calculated from the current frame's average luminance (without any history).
		// 
		// Note that when there is an abrupt change, there will be an immediate change in exposure compensation. But this is
		// fine because the shader will recalculate a new target exposure. The next result is that the smoothed exposure (purple
		// line in HDR visualization) will have sudden shifts, but the actual output exposure value (white line in HDR visualization)
		// will be smooth.
		const float AverageSceneLuminance = View.GetLastAverageSceneLuminance();

		if (AverageSceneLuminance > 0)
		{
			// We need the Log2(0.18) to convert from average luminance to saturation luminance
			const float LuminanceEV100 = LuminanceToEV100(LuminanceMax, AverageSceneLuminance) + FMath::Log2(1.0f / 0.18f);
			AutoExposureBias += Settings.AutoExposureBiasCurve->GetFloatValue(LuminanceEV100);
		}
	}

	return FMath::Pow(2.0f, AutoExposureBias);
}

bool IsAutoExposureDebugMode(const FViewInfo& View)
{
	const FEngineShowFlags& EngineShowFlags = View.Family->EngineShowFlags;

	return View.Family->UseDebugViewPS() ||
		!EngineShowFlags.Lighting ||
		(EngineShowFlags.VisualizeBuffer && View.CurrentBufferVisualizationMode != NAME_None) ||
		EngineShowFlags.RayTracingDebug ||
		EngineShowFlags.VisualizeDistanceFieldAO ||
		EngineShowFlags.VisualizeMeshDistanceFields ||
		EngineShowFlags.VisualizeGlobalDistanceField ||
		EngineShowFlags.VisualizeVolumetricCloudConservativeDensity ||
		EngineShowFlags.CollisionVisibility ||
		EngineShowFlags.CollisionPawn ||
		!EngineShowFlags.PostProcessing;
}

float CalculateFixedAutoExposure(const FViewInfo& View)
{
	const bool bExtendedLuminanceRange = IsExtendLuminanceRangeEnabled();
	const float LuminanceMax = bExtendedLuminanceRange ? LuminanceMaxFromLensAttenuation() : 1.0f;
	return EV100ToLuminance(LuminanceMax, View.Family->ExposureSettings.FixedEV100);
}

// on mobile, we are never using the Physical Camera, which is why we need the bForceDisablePhysicalCamera
float CalculateManualAutoExposure(const FViewInfo& View, bool bForceDisablePhysicalCamera)
{
	const bool bExtendedLuminanceRange = IsExtendLuminanceRangeEnabled();
	const float LuminanceMax = bExtendedLuminanceRange ? LuminanceMaxFromLensAttenuation() : 1.0f;

	const FPostProcessSettings& Settings = View.FinalPostProcessSettings;

	const float BasePhysicalCameraEV100 = FMath::Log2(FMath::Square(Settings.DepthOfFieldFstop) * Settings.CameraShutterSpeed * 100 / FMath::Max(1.f, Settings.CameraISO));
	const float PhysicalCameraEV100 = (!bForceDisablePhysicalCamera && Settings.AutoExposureApplyPhysicalCameraExposure) ? BasePhysicalCameraEV100 : 0.0f;

	float FoundLuminance = EV100ToLuminance(LuminanceMax, PhysicalCameraEV100);
	return FoundLuminance;
}

FEyeAdaptationParameters GetEyeAdaptationParameters(const FViewInfo& View, ERHIFeatureLevel::Type MinFeatureLevel)
{
	const bool bExtendedLuminanceRange = IsExtendLuminanceRangeEnabled();

	const FPostProcessSettings& Settings = View.FinalPostProcessSettings;

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

	// Get the exposure compensation from the post process volume settings (everything except the curve)
	float ExposureCompensationSettings = GetAutoExposureCompensationFromSettings(View);

	// Get the exposure compensation from the curve
	float ExposureCompensationCurve = GetAutoExposureCompensationFromCurve(View);
	const float BlackHistogramBucketInfluence = CVarEyeAdaptationBlackHistogramBucketInfluence.GetValueOnRenderThread();

	const float kMiddleGrey = 0.18f;

	// AEM_Histogram and AEM_Basic adjust their ExposureCompensation to middle grey (0.18). AEM_Manual ExposureCompensation is already calibrated to 1.0.
	const float GreyMult = (AutoExposureMethod == AEM_Manual) ? 1.0f : kMiddleGrey;

	const bool IsDebugViewMode = IsAutoExposureDebugMode(View);

	if (IsDebugViewMode)
	{
		ExposureCompensationSettings = 1.0f;
		ExposureCompensationCurve = 1.0f;
	}
	// Fixed exposure override in effect.
	else if (View.Family->ExposureSettings.bFixed)
	{
		ExposureCompensationSettings = 1.0f;
		ExposureCompensationCurve = 1.0f;

		// ignores bExtendedLuminanceRange
		MinWhitePointLuminance = MaxWhitePointLuminance = CalculateFixedAutoExposure(View);
	}
	// The feature level check should always pass unless on mobile with MobileHDR is false
	else if (EngineShowFlags.EyeAdaptation && View.GetFeatureLevel() >= MinFeatureLevel)
	{
		if (AutoExposureMethod == EAutoExposureMethod::AEM_Manual)
		{
			// ignores bExtendedLuminanceRange
			MinWhitePointLuminance = MaxWhitePointLuminance = CalculateManualAutoExposure(View, false);
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
	else
	{
		// if eye adaptation is off, then set everything to 1.0
		ExposureCompensationSettings = 1.0f;
		ExposureCompensationCurve = 1.0f;

		// GetAutoExposureMethod() should return Manual in this case.
		check(AutoExposureMethod == AEM_Manual);

		// just lock to 1.0, it's not possible to guess a reasonable value using the min and max.
		MinWhitePointLuminance = MaxWhitePointLuminance = 1.0;
	}

	MinWhitePointLuminance = FMath::Min(MinWhitePointLuminance, MaxWhitePointLuminance);

	const float HistogramLogDelta = HistogramLogMax - HistogramLogMin;
	const float HistogramScale = 1.0f / HistogramLogDelta;
	const float HistogramBias = -HistogramLogMin * HistogramScale;

	// If we are in histogram mode, then we want to set the minimum to the bottom end of the histogram. But if we are in basic mode,
	// we want to simply use a small epsilon to keep true black values from returning a NaN and/or a very low value. Also, basic
	// mode does the calculation in pre-exposure space, which is why we need to multiply by View.PreExposure.
	const float LuminanceMin = (AutoExposureMethod == AEM_Basic) ? 0.0001f : FMath::Exp2(HistogramLogMin);

	//AutoExposureMeterMask
	const FTextureRHIRef MeterMask = Settings.AutoExposureMeterMask ?
									Settings.AutoExposureMeterMask->Resource->TextureRHI :
									GWhiteTexture->TextureRHI;

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
	Parameters.ExposureCompensationSettings = ExposureCompensationSettings;
	Parameters.ExposureCompensationCurve = ExposureCompensationCurve;
	Parameters.DeltaWorldTime = View.Family->DeltaWorldTime;
	Parameters.ExposureSpeedUp = Settings.AutoExposureSpeedUp;
	Parameters.ExposureSpeedDown = Settings.AutoExposureSpeedDown;
	Parameters.HistogramScale = HistogramScale;
	Parameters.HistogramBias = HistogramBias;
	Parameters.LuminanceMin = LuminanceMin;
	Parameters.BlackHistogramBucketInfluence = BlackHistogramBucketInfluence; // no calibration constant because it is now baked into ExposureCompensation
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
	const FEyeAdaptationParameters Parameters = GetEyeAdaptationParameters(View, GetBasicEyeAdaptationMinFeatureLevel());

	const float Exposure = (Parameters.MinAverageLuminance + Parameters.MaxAverageLuminance) * 0.5f;

	const float kMiddleGrey = 0.18f;
	const float ExposureScale = kMiddleGrey / FMath::Max(0.0001f, Exposure);

	// We're ignoring any curve influence
	return ExposureScale * Parameters.ExposureCompensationSettings;
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
	View.SwapEyeAdaptationTextures(GraphBuilder);

	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(View.GetEyeAdaptationTexture(GraphBuilder.RHICmdList), ERenderTargetTexture::Targetable, ERDGTextureFlags::MultiFrame);

	FEyeAdaptationShader::FParameters PassBaseParameters;
	PassBaseParameters.EyeAdaptation = GetEyeAdaptationParameters(View, ERHIFeatureLevel::SM5);
	PassBaseParameters.HistogramTexture = HistogramTexture;

	if (View.bUseComputePasses)
	{
		FEyeAdaptationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FEyeAdaptationCS::FParameters>();
		PassParameters->Base = PassBaseParameters;
		PassParameters->RWEyeAdaptationTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FEyeAdaptationCS> ComputeShader(View.ShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HistogramEyeAdaptation (CS)"),
			ComputeShader,
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
			PixelShader,
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
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::Type(GetBasicEyeAdaptationMinFeatureLevel()));
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
		PixelShader,
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
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::Type(GetBasicEyeAdaptationMinFeatureLevel()));
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
	View.SwapEyeAdaptationTextures(GraphBuilder);

	const FScreenPassTextureViewport SceneColorViewport(SceneColor);

	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(View.GetEyeAdaptationTexture(GraphBuilder.RHICmdList), ERenderTargetTexture::Targetable, ERDGTextureFlags::MultiFrame);

	FBasicEyeAdaptationShader::FParameters PassBaseParameters;
	PassBaseParameters.View = View.ViewUniformBuffer;
	PassBaseParameters.EyeAdaptation = EyeAdaptationParameters;
	PassBaseParameters.Color = GetScreenPassTextureViewportParameters(SceneColorViewport);
	PassBaseParameters.ColorTexture = SceneColor.Texture;
	PassBaseParameters.EyeAdaptationTexture = EyeAdaptationTexture;

	if (View.bUseComputePasses || CVarEyeAdaptationBasicCompute.GetValueOnRenderThread())
	{
		FBasicEyeAdaptationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBasicEyeAdaptationCS::FParameters>();
		PassParameters->Base = PassBaseParameters;
		PassParameters->RWEyeAdaptationTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FBasicEyeAdaptationCS> ComputeShader(View.ShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BasicEyeAdaptation (CS)"),
			ComputeShader,
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
			PixelShader,
			PassParameters);
	}

	return OutputTexture;
}

FSceneViewState::FEyeAdaptationManager::~FEyeAdaptationManager() {}

void FSceneViewState::FEyeAdaptationManager::SafeRelease()
{
	CurrentBuffer = 0;

	for (int32 Index = 0; Index < 3; Index++)
	{
		PooledRenderTarget[Index].SafeRelease();
		ExposureTextureReadback[Index] = nullptr;
	}

	ExposureBufferData[0].SafeRelease();
	ExposureBufferData[1].SafeRelease();
	ExposureBufferReadback = nullptr;

}

void FSceneViewState::FEyeAdaptationManager::SwapTextures(FRDGBuilder& GraphBuilder, bool bInUpdateLastExposure)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FEyeAdaptationRTManager_SwapRTs);

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	// When reading back last frame's exposure data in AFR, make sure we do it on the
	// GPUs that were actually used last frame.
	const FRHIGPUMask ThisFrameGPUMask = RHICmdList.GetGPUMask();
	const FRHIGPUMask LastFrameGPUMask = AFRUtils::GetPrevSiblingGPUMask(ThisFrameGPUMask);
	const FRHIGPUMask LastLastFrameGPUMask = AFRUtils::GetPrevSiblingGPUMask(LastFrameGPUMask);

	if (bInUpdateLastExposure && PooledRenderTarget[CurrentBuffer].IsValid() && (GIsEditor || CVarEnablePreExposureOnlyInTheEditor.GetValueOnRenderThread() == 0))
	{
		FRDGTextureRef CurrentTexture = GraphBuilder.RegisterExternalTexture(PooledRenderTarget[CurrentBuffer], ERenderTargetTexture::ShaderResource, ERDGTextureFlags::MultiFrame);

		// first, read the value from two frames ago
		int32 PreviousPreviousBuffer = GetPreviousPreviousIndex();
		if (ExposureTextureReadback[PreviousPreviousBuffer] != nullptr &&
			ExposureTextureReadback[PreviousPreviousBuffer]->IsReady())
		{
			// Workaround until FRHIGPUTextureReadback::Lock has multigpu support
			FRHIGPUMask ReadBackGPUMask = LastLastFrameGPUMask;

			RDG_GPU_MASK_SCOPE(GraphBuilder, ReadBackGPUMask);

			// Read the last request results.
			FVector4* ReadbackData = (FVector4*)ExposureTextureReadback[PreviousPreviousBuffer]->Lock(sizeof(FVector4));
			if (ReadbackData)
			{
				LastExposure = ReadbackData->X;
				LastAverageSceneLuminance = ReadbackData->Z;

				ExposureTextureReadback[PreviousPreviousBuffer]->Unlock();
			}
		}

		if (!ExposureTextureReadback[CurrentBuffer])
		{
			static const FName ExposureValueName(TEXT("Scene view state exposure readback"));
			ExposureTextureReadback[CurrentBuffer].Reset(new FRHIGPUTextureReadback(ExposureValueName));
			// Send the first request.
			AddEnqueueCopyPass(GraphBuilder, ExposureTextureReadback[CurrentBuffer].Get(), CurrentTexture);
		}
		else // it exists, so just enqueue, don't  reset
		{
			// Send the request for next update.
			AddEnqueueCopyPass(GraphBuilder, ExposureTextureReadback[CurrentBuffer].Get(), CurrentTexture);
		}
	}

	CurrentBuffer = (CurrentBuffer+1)%3;
}

const TRefCountPtr<IPooledRenderTarget>& FSceneViewState::FEyeAdaptationManager::GetTexture(uint32 TextureIndex) const
{
	check(0 <= TextureIndex && TextureIndex < 3);
	return PooledRenderTarget[TextureIndex];
}

const TRefCountPtr<IPooledRenderTarget>& FSceneViewState::FEyeAdaptationManager::GetOrCreateTexture(FRHICommandList& RHICmdList, uint32 TextureIndex)
{
	check(0 <= TextureIndex && TextureIndex < 3);

	// Create textures if needed.
	if (!PooledRenderTarget[TextureIndex].IsValid())
	{
		// Create the texture needed for EyeAdaptation
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
		if (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5)
		{
			Desc.TargetableFlags |= TexCreate_UAV;
		}
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, PooledRenderTarget[TextureIndex], TEXT("EyeAdaptation"), ERenderTargetTransience::NonTransient);
	}

	return PooledRenderTarget[TextureIndex];
}

const FExposureBufferData* FSceneViewState::FEyeAdaptationManager::GetBuffer(uint32 BufferIndex) const
{
	check(BufferIndex==0 || BufferIndex==1);

	const FExposureBufferData& Instance = ExposureBufferData[BufferIndex];
	return Instance.IsValid() ? &Instance : nullptr;
}

FExposureBufferData* FSceneViewState::FEyeAdaptationManager::GetOrCreateBuffer(FRHICommandListImmediate& RHICmdList, uint32 BufferIndex)
{
	check(BufferIndex==0 || BufferIndex==1);

	// Create textures if needed.
	if (!ExposureBufferData[BufferIndex].IsValid())
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("ExposureBuffer"));
		ExposureBufferData[BufferIndex].Buffer = RHICreateVertexBuffer(sizeof(FVector4), BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess | BUF_SourceCopy, CreateInfo);
		
		FVector4* BufferData = (FVector4*)RHICmdList.LockVertexBuffer(ExposureBufferData[BufferIndex].Buffer, 0, sizeof(FVector4), RLM_WriteOnly);
		*BufferData = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		RHICmdList.UnlockVertexBuffer(ExposureBufferData[BufferIndex].Buffer);

		ExposureBufferData[BufferIndex].SRV = RHICmdList.CreateShaderResourceView(ExposureBufferData[BufferIndex].Buffer, sizeof(FVector4), PF_A32B32G32R32F);
		ExposureBufferData[BufferIndex].UAV = RHICmdList.CreateUnorderedAccessView(ExposureBufferData[BufferIndex].Buffer, PF_A32B32G32R32F);

		RHICmdList.Transition(FRHITransitionInfo(ExposureBufferData[BufferIndex].UAV, ERHIAccess::UAVMask, ERHIAccess::SRVMask));
	}

	return &ExposureBufferData[BufferIndex];
}

void FSceneViewState::FEyeAdaptationManager::SwapBuffers(bool bInUpdateLastExposure)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FEyeAdaptationManager_SwapBuffers);

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	

	check(CurrentBuffer==0 || CurrentBuffer==1);

	if (bInUpdateLastExposure && ExposureBufferData[CurrentBuffer].IsValid() && (GIsEditor || CVarEnablePreExposureOnlyInTheEditor.GetValueOnRenderThread() == 0))
	{
		bool bReadbackCompleted = false;
		if (ExposureBufferReadback != nullptr && ExposureBufferReadback->IsReady())
		{
			// Workaround until FRHIGPUTextureReadback::Lock has multigpu support
			FRHIGPUMask ReadBackGPUMask = RHICmdList.GetGPUMask();
			if (!ReadBackGPUMask.HasSingleIndex())
			{
				ReadBackGPUMask = FRHIGPUMask::GPU0();
			}

			SCOPED_GPU_MASK(RHICmdList, ReadBackGPUMask);

			// Read the last request results.
			FVector4* ReadbackData = (FVector4*)ExposureBufferReadback->Lock(sizeof(FVector4));
			if (ReadbackData)
			{
				LastExposure = ReadbackData->X;
				LastAverageSceneLuminance = ReadbackData->Z;

				ExposureBufferReadback->Unlock();
			}

			bReadbackCompleted = true;
		}

		if (bReadbackCompleted || !ExposureBufferReadback)
		{
			if (!ExposureBufferReadback)
			{
				static const FName ExposureValueName(TEXT("Scene view state exposure readback"));
				ExposureBufferReadback.Reset(new FRHIGPUBufferReadback(ExposureValueName));
			}

			RHICmdList.Transition(FRHITransitionInfo(ExposureBufferData[CurrentBuffer].UAV, ERHIAccess::UAVMask, ERHIAccess::CopySrc));

			ExposureBufferReadback->EnqueueCopy(RHICmdList, ExposureBufferData[CurrentBuffer].Buffer);

			RHICmdList.Transition(FRHITransitionInfo(ExposureBufferData[CurrentBuffer].UAV, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
		}
	}

	CurrentBuffer = 1 - CurrentBuffer;
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
		((!ViewFamily.EngineShowFlags.VisualizeBuffer) || View.CurrentBufferVisualizationMode != NAME_None); // disable pre-exposure for the debug visualization modes

	PreExposure = 1.f;
	bUpdateLastExposure = false;

	bool bMobilePlatform = IsMobilePlatform(View.GetShaderPlatform());
	bool bEnableAutoExposure = !bMobilePlatform || IsMobileEyeAdaptationEnabled(View);
	
	if (bIsPreExposureRelevant && bEnableAutoExposure)
	{
		if (UsePreExposureEnabled())
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
		else
		{
			if (View.FinalPostProcessSettings.AutoExposureBiasCurve)
			{
				// The exposure compensation curves require the scene average luminance
				bUpdateLastExposure = true;
			}

			// Whe PreExposure is not used, we will override the PreExposure value to 1.0, which simulates PreExposure being off.
			PreExposure = 1.0f;
		}
	}

	// Mobile LDR does not support post-processing but still can apply Exposure during basepass
	if (bMobilePlatform && !IsMobileHDR())
	{
		PreExposure = GetEyeAdaptationFixedExposure(View);
	}

	// Update the pre-exposure value on the actual view
	View.PreExposure = PreExposure;

	// Update the pre exposure of all temporal histories.
	if (!View.bStatePrevViewInfoIsReadOnly)
	{
		PrevFrameViewInfo.SceneColorPreExposure = PreExposure;
	}
}

#if WITH_MGPU
static const FName NAME_EyeAdaptation(TEXT("EyeAdaptation"));

void FSceneViewState::BroadcastEyeAdaptationTemporalEffect(FRHICommandList& RHICmdList)
{
	FRHITexture* EyeAdaptation = GetCurrentEyeAdaptationTexture(RHICmdList)->GetRenderTargetItem().ShaderResourceTexture.GetReference();
	RHICmdList.BroadcastTemporalEffect(FName(NAME_EyeAdaptation, UniqueID), { &EyeAdaptation, 1 });
}

DECLARE_GPU_STAT(AFRWaitForEyeAdaptation);

void FSceneViewState::WaitForEyeAdaptationTemporalEffect(FRHICommandList& RHICmdList)
{
	SCOPED_GPU_STAT(RHICmdList, AFRWaitForEyeAdaptation);
	RHICmdList.WaitForTemporalEffect(FName(NAME_EyeAdaptation, UniqueID));
}
#endif // WITH_MGPU