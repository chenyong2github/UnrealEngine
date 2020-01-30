// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessTonemap.cpp: Post processing tone mapping implementation.
=============================================================================*/

#include "PostProcess/PostProcessTonemap.h"
#include "EngineGlobals.h"
#include "ScenePrivate.h"
#include "RendererModule.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessCombineLUTs.h"
#include "PostProcess/PostProcessMobile.h"
#include "PostProcess/PostProcessing.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"

namespace
{
TAutoConsoleVariable<float> CVarTonemapperSharpen(
	TEXT("r.Tonemapper.Sharpen"),
	0,
	TEXT("Sharpening in the tonemapper (not for ES2), actual implementation is work in progress, clamped at 10\n")
	TEXT("   0: off(default)\n")
	TEXT(" 0.5: half strength\n")
	TEXT("   1: full strength"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

// Note: Enables or disables HDR support for a project. Typically this would be set on a per-project/per-platform basis in defaultengine.ini
TAutoConsoleVariable<int32> CVarAllowHDR(
	TEXT("r.AllowHDR"),
	0,
	TEXT("Creates an HDR compatible swap-chain and enables HDR display output.")
	TEXT("0: Disabled (default)\n")
	TEXT("1: Allow HDR, if supported by the platform and display \n"),
	ECVF_ReadOnly);

// Note: These values are directly referenced in code. They are set in code at runtime and therefore cannot be set via ini files
// Please update all paths if changing
TAutoConsoleVariable<int32> CVarDisplayColorGamut(
	TEXT("r.HDR.Display.ColorGamut"),
	0,
	TEXT("Color gamut of the output display:\n")
	TEXT("0: Rec709 / sRGB, D65 (default)\n")
	TEXT("1: DCI-P3, D65\n")
	TEXT("2: Rec2020 / BT2020, D65\n")
	TEXT("3: ACES, D60\n")
	TEXT("4: ACEScg, D60\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarDisplayOutputDevice(
	TEXT("r.HDR.Display.OutputDevice"),
	0,
	TEXT("Device format of the output display:\n")
	TEXT("0: sRGB (LDR)\n")
	TEXT("1: Rec709 (LDR)\n")
	TEXT("2: Explicit gamma mapping (LDR)\n")
	TEXT("3: ACES 1000 nit ST-2084 (Dolby PQ) (HDR)\n")
	TEXT("4: ACES 2000 nit ST-2084 (Dolby PQ) (HDR)\n")
	TEXT("5: ACES 1000 nit ScRGB (HDR)\n")
	TEXT("6: ACES 2000 nit ScRGB (HDR)\n")
	TEXT("7: Linear EXR (HDR)\n")
	TEXT("8: Linear final color, no tone curve (HDR)\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);
	
TAutoConsoleVariable<int32> CVarHDROutputEnabled(
	TEXT("r.HDR.EnableHDROutput"),
	0,
	TEXT("Creates an HDR compatible swap-chain and enables HDR display output.")
	TEXT("0: Disabled (default)\n")
	TEXT("1: Enable hardware-specific implementation\n"),
	ECVF_RenderThreadSafe
	);

TAutoConsoleVariable<float> CVarTonemapperGamma(
	TEXT("r.TonemapperGamma"),
	0.0f,
	TEXT("0: Default behavior\n")
	TEXT("#: Use fixed gamma # instead of sRGB or Rec709 transform"),
	ECVF_Scalability | ECVF_RenderThreadSafe);	

TAutoConsoleVariable<float> CVarGamma(
	TEXT("r.Gamma"),
	1.0f,
	TEXT("Gamma on output"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarMobileTonemapperFilm(
	TEXT("r.Mobile.TonemapperFilm"),
	0,
	TEXT("Whether mobile platforms should use new film tone mapper"),
	ECVF_RenderThreadSafe);

const int32 GTonemapComputeTileSizeX = 8;
const int32 GTonemapComputeTileSizeY = 8;

namespace TonemapperPermutation
{
// Shared permutation dimensions between deferred and mobile renderer.
class FTonemapperBloomDim          : SHADER_PERMUTATION_BOOL("USE_BLOOM");
class FTonemapperGammaOnlyDim      : SHADER_PERMUTATION_BOOL("USE_GAMMA_ONLY");
class FTonemapperGrainIntensityDim : SHADER_PERMUTATION_BOOL("USE_GRAIN_INTENSITY");
class FTonemapperVignetteDim       : SHADER_PERMUTATION_BOOL("USE_VIGNETTE");
class FTonemapperSharpenDim        : SHADER_PERMUTATION_BOOL("USE_SHARPEN");
class FTonemapperGrainJitterDim    : SHADER_PERMUTATION_BOOL("USE_GRAIN_JITTER");
class FTonemapperSwitchAxis        : SHADER_PERMUTATION_BOOL("NEEDTOSWITCHVERTICLEAXIS");
class FTonemapperMsaaDim           : SHADER_PERMUTATION_BOOL("USE_MSAA");
class FTonemapperEyeAdaptationDim  : SHADER_PERMUTATION_BOOL("EYEADAPTATION_EXPOSURE_FIX");

using FCommonDomain = TShaderPermutationDomain<
	FTonemapperBloomDim,
	FTonemapperGammaOnlyDim,
	FTonemapperGrainIntensityDim,
	FTonemapperVignetteDim,
	FTonemapperSharpenDim,
	FTonemapperGrainJitterDim,
	FTonemapperSwitchAxis,
	FTonemapperMsaaDim>;

bool ShouldCompileCommonPermutation(const FGlobalShaderPermutationParameters& Parameters, const FCommonDomain& PermutationVector)
{
	// Prevent switch axis permutation on platforms that dont require it.
	if (PermutationVector.Get<FTonemapperSwitchAxis>() && !RHINeedsToSwitchVerticalAxis(Parameters.Platform))
	{
		return false;
	}

	// MSAA pre-resolve step only used on iOS atm
	if (PermutationVector.Get<FTonemapperMsaaDim>() && !IsMetalMobilePlatform(Parameters.Platform))
	{
		return false;
	}

	// If GammaOnly, don't compile any other dimmension == true.
	if (PermutationVector.Get<FTonemapperGammaOnlyDim>())
	{
		return !PermutationVector.Get<FTonemapperBloomDim>() &&
			!PermutationVector.Get<FTonemapperGrainIntensityDim>() &&
			!PermutationVector.Get<FTonemapperVignetteDim>() &&
			!PermutationVector.Get<FTonemapperSharpenDim>() &&
			!PermutationVector.Get<FTonemapperGrainJitterDim>() &&
			!PermutationVector.Get<FTonemapperMsaaDim>();
	}
	return true;
}

// Common conversion of engine settings into.
FCommonDomain BuildCommonPermutationDomain(const FViewInfo& View, bool bGammaOnly, bool bSwitchVerticalAxis)
{
	const FSceneViewFamily* Family = View.Family;

	FCommonDomain PermutationVector;

	// Gamma
	if (bGammaOnly ||
		(Family->EngineShowFlags.Tonemapper == 0) ||
		(Family->EngineShowFlags.PostProcessing == 0))
	{
		PermutationVector.Set<FTonemapperGammaOnlyDim>(true);
		return PermutationVector;
	}

	const FPostProcessSettings& Settings = View.FinalPostProcessSettings;
	PermutationVector.Set<FTonemapperGrainIntensityDim>(Settings.GrainIntensity > 0.0f);
	PermutationVector.Set<FTonemapperVignetteDim>(Settings.VignetteIntensity > 0.0f);
	PermutationVector.Set<FTonemapperBloomDim>(Settings.BloomIntensity > 0.0);
	PermutationVector.Set<FTonemapperGrainJitterDim>(Settings.GrainJitter > 0.0f);
	PermutationVector.Set<FTonemapperSharpenDim>(CVarTonemapperSharpen.GetValueOnRenderThread() > 0.0f);	
	PermutationVector.Set<FTonemapperSwitchAxis>(bSwitchVerticalAxis);

	if (IsMetalMobilePlatform(View.GetShaderPlatform()))
	{
		static const auto CVarMobileMSAA = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileMSAA"));
		bool bMSAA = (CVarMobileMSAA ? CVarMobileMSAA->GetValueOnAnyThread() > 1 : false);

		PermutationVector.Set<FTonemapperMsaaDim>(bMSAA);
	}

	return PermutationVector;
}

// Desktop renderer permutation dimensions.
class FTonemapperColorFringeDim       : SHADER_PERMUTATION_BOOL("USE_COLOR_FRINGE");
class FTonemapperGrainQuantizationDim : SHADER_PERMUTATION_BOOL("USE_GRAIN_QUANTIZATION");
class FTonemapperOutputDeviceDim      : SHADER_PERMUTATION_ENUM_CLASS("DIM_OUTPUT_DEVICE", ETonemapperOutputDevice);

using FDesktopDomain = TShaderPermutationDomain<
	FCommonDomain,
	FTonemapperColorFringeDim,
	FTonemapperGrainQuantizationDim,
	FTonemapperOutputDeviceDim>;

FDesktopDomain RemapPermutation(FDesktopDomain PermutationVector, ERHIFeatureLevel::Type FeatureLevel)
{
	FCommonDomain CommonPermutationVector = PermutationVector.Get<FCommonDomain>();

	// No remapping if gamma only.
	if (CommonPermutationVector.Get<FTonemapperGammaOnlyDim>())
	{
		return PermutationVector;
	}

	// Grain jitter or intensity looks bad anyway.
	bool bFallbackToSlowest = false;
	bFallbackToSlowest = bFallbackToSlowest || CommonPermutationVector.Get<FTonemapperGrainIntensityDim>();
	bFallbackToSlowest = bFallbackToSlowest || CommonPermutationVector.Get<FTonemapperGrainJitterDim>();

	if (bFallbackToSlowest)
	{
		CommonPermutationVector.Set<FTonemapperGrainIntensityDim>(true);
		CommonPermutationVector.Set<FTonemapperGrainJitterDim>(true);
		CommonPermutationVector.Set<FTonemapperSharpenDim>(true);

		PermutationVector.Set<FTonemapperColorFringeDim>(true);
	}

	// You most likely need Bloom anyway.
	CommonPermutationVector.Set<FTonemapperBloomDim>(true);

	// Disable grain quantization only for LinearNoToneCurve output device
	if (PermutationVector.Get<FTonemapperOutputDeviceDim>() == ETonemapperOutputDevice::LinearNoToneCurve)
		PermutationVector.Set<FTonemapperGrainQuantizationDim>(false);
	else
		PermutationVector.Set<FTonemapperGrainQuantizationDim>(true);
	
	// Mobile supports only sRGB and LinearNoToneCurve output
	if (FeatureLevel <= ERHIFeatureLevel::ES3_1 && 
		PermutationVector.Get<FTonemapperOutputDeviceDim>() != ETonemapperOutputDevice::LinearNoToneCurve)
	{
		PermutationVector.Set<FTonemapperOutputDeviceDim>(ETonemapperOutputDevice::sRGB);
	}
	
	PermutationVector.Set<FCommonDomain>(CommonPermutationVector);
	return PermutationVector;
}

bool ShouldCompileDesktopPermutation(const FGlobalShaderPermutationParameters& Parameters, FDesktopDomain PermutationVector)
{
	auto CommonPermutationVector = PermutationVector.Get<FCommonDomain>();

	if (RemapPermutation(PermutationVector, GetMaxSupportedFeatureLevel(Parameters.Platform)) != PermutationVector)
	{
		return false;
	}

	if (!ShouldCompileCommonPermutation(Parameters, CommonPermutationVector))
	{
		return false;
	}

	if (CommonPermutationVector.Get<FTonemapperGammaOnlyDim>())
	{
		return !PermutationVector.Get<FTonemapperColorFringeDim>() &&
			!PermutationVector.Get<FTonemapperGrainQuantizationDim>();
	}
	return true;
}

} // namespace TonemapperPermutation
} // namespace

void GrainPostSettings(FVector* RESTRICT const Constant, const FPostProcessSettings* RESTRICT const Settings)
{
	float GrainJitter = Settings->GrainJitter;
	float GrainIntensity = Settings->GrainIntensity;
	Constant->X = GrainIntensity;
	Constant->Y = 1.0f + (-0.5f * GrainIntensity);
	Constant->Z = GrainJitter;
}

FMobileFilmTonemapParameters GetMobileFilmTonemapParameters(const FPostProcessSettings& PostProcessSettings, bool bUseColorMatrix, bool bUseShadowTint, bool bUseContrast)
{
	// Must insure inputs are in correct range (else possible generation of NaNs).
	float InExposure = 1.0f;
	FVector InWhitePoint(PostProcessSettings.FilmWhitePoint);
	float InSaturation = FMath::Clamp(PostProcessSettings.FilmSaturation, 0.0f, 2.0f);
	FVector InLuma = FVector(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f);
	FVector InMatrixR(PostProcessSettings.FilmChannelMixerRed);
	FVector InMatrixG(PostProcessSettings.FilmChannelMixerGreen);
	FVector InMatrixB(PostProcessSettings.FilmChannelMixerBlue);
	float InContrast = FMath::Clamp(PostProcessSettings.FilmContrast, 0.0f, 1.0f) + 1.0f;
	float InDynamicRange = powf(2.0f, FMath::Clamp(PostProcessSettings.FilmDynamicRange, 1.0f, 4.0f));
	float InToe = (1.0f - FMath::Clamp(PostProcessSettings.FilmToeAmount, 0.0f, 1.0f)) * 0.18f;
	InToe = FMath::Clamp(InToe, 0.18f / 8.0f, 0.18f * (15.0f / 16.0f));
	float InHeal = 1.0f - (FMath::Max(1.0f / 32.0f, 1.0f - FMath::Clamp(PostProcessSettings.FilmHealAmount, 0.0f, 1.0f)) * (1.0f - 0.18f));
	FVector InShadowTint(PostProcessSettings.FilmShadowTint);
	float InShadowTintBlend = FMath::Clamp(PostProcessSettings.FilmShadowTintBlend, 0.0f, 1.0f) * 64.0f;

	// Shadow tint amount enables turning off shadow tinting.
	float InShadowTintAmount = FMath::Clamp(PostProcessSettings.FilmShadowTintAmount, 0.0f, 1.0f);
	InShadowTint = InWhitePoint + (InShadowTint - InWhitePoint) * InShadowTintAmount;

	// Make sure channel mixer inputs sum to 1 (+ smart dealing with all zeros).
	InMatrixR.X += 1.0f / (256.0f*256.0f*32.0f);
	InMatrixG.Y += 1.0f / (256.0f*256.0f*32.0f);
	InMatrixB.Z += 1.0f / (256.0f*256.0f*32.0f);
	InMatrixR *= 1.0f / FVector::DotProduct(InMatrixR, FVector(1.0f));
	InMatrixG *= 1.0f / FVector::DotProduct(InMatrixG, FVector(1.0f));
	InMatrixB *= 1.0f / FVector::DotProduct(InMatrixB, FVector(1.0f));

	// Conversion from linear rgb to luma (using HDTV coef).
	FVector LumaWeights = FVector(0.2126f, 0.7152f, 0.0722f);

	// Make sure white point has 1.0 as luma (so adjusting white point doesn't change exposure).
	// Make sure {0.0,0.0,0.0} inputs do something sane (default to white).
	InWhitePoint += FVector(1.0f / (256.0f*256.0f*32.0f));
	InWhitePoint *= 1.0f / FVector::DotProduct(InWhitePoint, LumaWeights);
	InShadowTint += FVector(1.0f / (256.0f*256.0f*32.0f));
	InShadowTint *= 1.0f / FVector::DotProduct(InShadowTint, LumaWeights);

	// Grey after color matrix is applied.
	FVector ColorMatrixLuma = FVector(
		FVector::DotProduct(InLuma.X * FVector(InMatrixR.X, InMatrixG.X, InMatrixB.X), FVector(1.0f)),
		FVector::DotProduct(InLuma.Y * FVector(InMatrixR.Y, InMatrixG.Y, InMatrixB.Y), FVector(1.0f)),
		FVector::DotProduct(InLuma.Z * FVector(InMatrixR.Z, InMatrixG.Z, InMatrixB.Z), FVector(1.0f)));

	FVector OutMatrixR = FVector(0.0f);
	FVector OutMatrixG = FVector(0.0f);
	FVector OutMatrixB = FVector(0.0f);
	FVector OutColorShadow_Luma = LumaWeights * InShadowTintBlend;
	FVector OutColorShadow_Tint1 = InWhitePoint;
	FVector OutColorShadow_Tint2 = InShadowTint - InWhitePoint;

	if (bUseColorMatrix)
	{
		// Final color matrix effected by saturation and exposure.
		OutMatrixR = (ColorMatrixLuma + ((InMatrixR - ColorMatrixLuma) * InSaturation)) * InExposure;
		OutMatrixG = (ColorMatrixLuma + ((InMatrixG - ColorMatrixLuma) * InSaturation)) * InExposure;
		OutMatrixB = (ColorMatrixLuma + ((InMatrixB - ColorMatrixLuma) * InSaturation)) * InExposure;
		if (!bUseShadowTint)
		{
			OutMatrixR = OutMatrixR * InWhitePoint.X;
			OutMatrixG = OutMatrixG * InWhitePoint.Y;
			OutMatrixB = OutMatrixB * InWhitePoint.Z;
		}
	}
	else
	{
		// No color matrix fast path.
		if (!bUseShadowTint)
		{
			OutMatrixB = InExposure * InWhitePoint;
		}
		else
		{
			// Need to drop exposure in.
			OutColorShadow_Luma *= InExposure;
			OutColorShadow_Tint1 *= InExposure;
			OutColorShadow_Tint2 *= InExposure;
		}
	}

	// Curve constants.
	float OutColorCurveCh3;
	float OutColorCurveCh0Cm1;
	float OutColorCurveCd2;
	float OutColorCurveCm0Cd0;
	float OutColorCurveCh1;
	float OutColorCurveCh2;
	float OutColorCurveCd1;
	float OutColorCurveCd3Cm3;
	float OutColorCurveCm2;

	// Line for linear section.
	float FilmLineOffset = 0.18f - 0.18f*InContrast;
	float FilmXAtY0 = -FilmLineOffset / InContrast;
	float FilmXAtY1 = (1.0f - FilmLineOffset) / InContrast;
	float FilmXS = FilmXAtY1 - FilmXAtY0;

	// Coordinates of linear section.
	float FilmHiX = FilmXAtY0 + InHeal * FilmXS;
	float FilmHiY = FilmHiX * InContrast + FilmLineOffset;
	float FilmLoX = FilmXAtY0 + InToe * FilmXS;
	float FilmLoY = FilmLoX * InContrast + FilmLineOffset;
	// Supported exposure range before clipping.
	float FilmHeal = InDynamicRange - FilmHiX;
	// Intermediates.
	float FilmMidXS = FilmHiX - FilmLoX;
	float FilmMidYS = FilmHiY - FilmLoY;
	float FilmSlope = FilmMidYS / (FilmMidXS);
	float FilmHiYS = 1.0f - FilmHiY;
	float FilmLoYS = FilmLoY;
	float FilmToe = FilmLoX;
	float FilmHiG = (-FilmHiYS + (FilmSlope*FilmHeal)) / (FilmSlope*FilmHeal);
	float FilmLoG = (-FilmLoYS + (FilmSlope*FilmToe)) / (FilmSlope*FilmToe);

	if (bUseContrast)
	{
		// Constants.
		OutColorCurveCh1 = FilmHiYS / FilmHiG;
		OutColorCurveCh2 = -FilmHiX * (FilmHiYS / FilmHiG);
		OutColorCurveCh3 = FilmHiYS / (FilmSlope*FilmHiG) - FilmHiX;
		OutColorCurveCh0Cm1 = FilmHiX;
		OutColorCurveCm2 = FilmSlope;
		OutColorCurveCm0Cd0 = FilmLoX;
		OutColorCurveCd3Cm3 = FilmLoY - FilmLoX * FilmSlope;
		// Handle these separate in case of FilmLoG being 0.
		if (FilmLoG != 0.0f)
		{
			OutColorCurveCd1 = -FilmLoYS / FilmLoG;
			OutColorCurveCd2 = FilmLoYS / (FilmSlope*FilmLoG);
		}
		else
		{
			// FilmLoG being zero means dark region is a linear segment (so just continue the middle section).
			OutColorCurveCd1 = 0.0f;
			OutColorCurveCd2 = 1.0f;
			OutColorCurveCm0Cd0 = 0.0f;
			OutColorCurveCd3Cm3 = 0.0f;
		}
	}
	else
	{
		// Simplified for no dark segment.
		OutColorCurveCh1 = FilmHiYS / FilmHiG;
		OutColorCurveCh2 = -FilmHiX * (FilmHiYS / FilmHiG);
		OutColorCurveCh3 = FilmHiYS / (FilmSlope*FilmHiG) - FilmHiX;
		OutColorCurveCh0Cm1 = FilmHiX;
		// Not used.
		OutColorCurveCm2 = 0.0f;
		OutColorCurveCm0Cd0 = 0.0f;
		OutColorCurveCd3Cm3 = 0.0f;
		OutColorCurveCd1 = 0.0f;
		OutColorCurveCd2 = 0.0f;
	}

	FMobileFilmTonemapParameters Parameters;
	Parameters.ColorMatrixR_ColorCurveCd1 = FVector4(OutMatrixR, OutColorCurveCd1);
	Parameters.ColorMatrixG_ColorCurveCd3Cm3 = FVector4(OutMatrixG, OutColorCurveCd3Cm3);
	Parameters.ColorMatrixB_ColorCurveCm2 = FVector4(OutMatrixB, OutColorCurveCm2);
	Parameters.ColorCurve_Cm0Cd0_Cd2_Ch0Cm1_Ch3 = FVector4(OutColorCurveCm0Cd0, OutColorCurveCd2, OutColorCurveCh0Cm1, OutColorCurveCh3);
	Parameters.ColorCurve_Ch1_Ch2 = FVector4(OutColorCurveCh1, OutColorCurveCh2, 0.0f, 0.0f);
	Parameters.ColorShadow_Luma = FVector4(OutColorShadow_Luma, 0.0f);
	Parameters.ColorShadow_Tint1 = FVector4(OutColorShadow_Tint1, 0.0f);
	Parameters.ColorShadow_Tint2 = FVector4(OutColorShadow_Tint2, 0.0f);
	return Parameters;
}

FTonemapperOutputDeviceParameters GetTonemapperOutputDeviceParameters(const FSceneViewFamily& Family)
{
	static TConsoleVariableData<int32>* CVarOutputGamut = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.ColorGamut"));
	static TConsoleVariableData<int32>* CVarOutputDevice = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.OutputDevice"));
	static TConsoleVariableData<float>* CVarOutputGamma = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.TonemapperGamma"));

	ETonemapperOutputDevice OutputDeviceValue;

	if (Family.SceneCaptureSource == SCS_FinalColorHDR)
	{
		OutputDeviceValue = ETonemapperOutputDevice::LinearNoToneCurve;
	}
	else if (Family.bIsHDR)
	{
		OutputDeviceValue = ETonemapperOutputDevice::ACES1000nitST2084;
	}
	else
	{
		OutputDeviceValue = static_cast<ETonemapperOutputDevice>(CVarOutputDevice->GetValueOnRenderThread());
		OutputDeviceValue = static_cast<ETonemapperOutputDevice>(FMath::Clamp(static_cast<int32>(OutputDeviceValue), 0, static_cast<int32>(ETonemapperOutputDevice::MAX) - 1));
	}

	float Gamma = CVarOutputGamma->GetValueOnRenderThread();

	if (PLATFORM_APPLE && Gamma == 0.0f)
	{
		Gamma = 2.2f;
	}

	// Enforce user-controlled ramp over sRGB or Rec709
	if (Gamma > 0.0f && (OutputDeviceValue == ETonemapperOutputDevice::sRGB || OutputDeviceValue == ETonemapperOutputDevice::Rec709))
	{
		OutputDeviceValue = ETonemapperOutputDevice::ExplicitGammaMapping;
	}

	FVector InvDisplayGammaValue;
	InvDisplayGammaValue.X = 1.0f / Family.RenderTarget->GetDisplayGamma();
	InvDisplayGammaValue.Y = 2.2f / Family.RenderTarget->GetDisplayGamma();
	InvDisplayGammaValue.Z = 1.0f / FMath::Max(Gamma, 1.0f);

	FTonemapperOutputDeviceParameters Parameters;
	Parameters.InverseGamma = InvDisplayGammaValue;
	Parameters.OutputDevice = static_cast<uint32>(OutputDeviceValue);
	Parameters.OutputGamut = CVarOutputGamut->GetValueOnRenderThread();
	return Parameters;
}

BEGIN_SHADER_PARAMETER_STRUCT(FFilmGrainParameters, )
	SHADER_PARAMETER(FVector, GrainRandomFull)
	SHADER_PARAMETER(FVector, GrainScaleBiasJitter)
END_SHADER_PARAMETER_STRUCT()

FFilmGrainParameters GetFilmGrainParameters(const FViewInfo& View)
{
	FVector GrainRandomFullValue;
	{
		uint8 FrameIndexMod8 = 0;
		if (View.State)
		{
			FrameIndexMod8 = View.ViewState->GetFrameIndex(8);
		}
		GrainRandomFromFrame(&GrainRandomFullValue, FrameIndexMod8);
	}

	FVector GrainScaleBiasJitter;
	GrainPostSettings(&GrainScaleBiasJitter, &View.FinalPostProcessSettings);

	FFilmGrainParameters Parameters;
	Parameters.GrainRandomFull = GrainRandomFullValue;
	Parameters.GrainScaleBiasJitter = GrainScaleBiasJitter;
	return Parameters;
}

BEGIN_SHADER_PARAMETER_STRUCT(FTonemapParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(FFilmGrainParameters, FilmGrain)
	SHADER_PARAMETER_STRUCT_INCLUDE(FTonemapperOutputDeviceParameters, OutputDevice)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Color)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Bloom)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BloomTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptation)
	SHADER_PARAMETER_RDG_TEXTURE(, ColorGradingLUT)
	SHADER_PARAMETER_TEXTURE(Texture2D, NoiseTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, BloomDirtMaskTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, ColorSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, BloomSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, NoiseSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, ColorGradingLUTSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, BloomDirtMaskSampler)
	SHADER_PARAMETER(FVector4, ColorScale0)
	SHADER_PARAMETER(FVector4, ColorScale1)
	SHADER_PARAMETER(FVector4, BloomDirtMaskTint)
	SHADER_PARAMETER(FVector4, ChromaticAberrationParams)
	SHADER_PARAMETER(FVector4, TonemapperParams)
	SHADER_PARAMETER(FVector4, LensPrincipalPointOffsetScale)
	SHADER_PARAMETER(FVector4, LensPrincipalPointOffsetScaleInverse)
	SHADER_PARAMETER(float, SwitchVerticalAxis)
	SHADER_PARAMETER(float, DefaultEyeExposure)
	SHADER_PARAMETER(float, EditorNITLevel)
	SHADER_PARAMETER(uint32, bOutputInHDR)
END_SHADER_PARAMETER_STRUCT()

class FTonemapVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTonemapVS);

	// FDrawRectangleParameters is filled by DrawScreenPass.
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FTonemapVS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<TonemapperPermutation::FTonemapperSwitchAxis, TonemapperPermutation::FTonemapperEyeAdaptationDim>;
	using FParameters = FTonemapParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		// Prevent switch axis permutation on platforms that dont require it.
		if (PermutationVector.Get<TonemapperPermutation::FTonemapperSwitchAxis>() && !RHINeedsToSwitchVerticalAxis(Parameters.Platform))
		{
			return false;
		}
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FTonemapVS, "/Engine/Private/PostProcessTonemap.usf", "MainVS", SF_Vertex);

class FTonemapPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTonemapPS);
	SHADER_USE_PARAMETER_STRUCT(FTonemapPS, FGlobalShader);

	using FPermutationDomain = TonemapperPermutation::FDesktopDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTonemapParameters, Tonemap)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1))
		{
			return false;
		}
		return TonemapperPermutation::ShouldCompileDesktopPermutation(Parameters, FPermutationDomain(Parameters.PermutationId));
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const int UseVolumeLut = PipelineVolumeTextureLUTSupportGuaranteedAtRuntime(Parameters.Platform) ? 1 : 0;
		OutEnvironment.SetDefine(TEXT("USE_VOLUME_LUT"), UseVolumeLut);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTonemapPS, "/Engine/Private/PostProcessTonemap.usf", "MainPS", SF_Pixel);

class FTonemapCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTonemapCS);
	SHADER_USE_PARAMETER_STRUCT(FTonemapCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<
		TonemapperPermutation::FDesktopDomain,
		TonemapperPermutation::FTonemapperEyeAdaptationDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTonemapParameters, Tonemap)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWOutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		return TonemapperPermutation::ShouldCompileDesktopPermutation(Parameters, PermutationVector.Get<TonemapperPermutation::FDesktopDomain>());
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GTonemapComputeTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GTonemapComputeTileSizeY);

		const int UseVolumeLut = PipelineVolumeTextureLUTSupportGuaranteedAtRuntime(Parameters.Platform) ? 1 : 0;
		OutEnvironment.SetDefine(TEXT("USE_VOLUME_LUT"), UseVolumeLut);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTonemapCS, "/Engine/Private/PostProcessTonemap.usf", "MainCS", SF_Compute);

FScreenPassTexture AddTonemapPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FTonemapInputs& Inputs)
{
	if (!Inputs.bGammaOnly)
	{
		check(Inputs.ColorGradingTexture);
	}
	check(Inputs.SceneColor.IsValid());

	const FSceneViewFamily& ViewFamily = *(View.Family);
	const FPostProcessSettings& PostProcessSettings = View.FinalPostProcessSettings;

	const bool bEyeAdaptation = Inputs.EyeAdaptationTexture != nullptr;

	const FScreenPassTextureViewport SceneColorViewport(Inputs.SceneColor);

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;

	if (!Output.IsValid())
	{
		FRDGTextureDesc OutputDesc = Inputs.SceneColor.Texture->Desc;
		OutputDesc.Reset();
		OutputDesc.TargetableFlags |= View.bUseComputePasses ? TexCreate_UAV : TexCreate_RenderTargetable;
		// RGB is the color in LDR, A is the luminance for PostprocessAA
		OutputDesc.Format = Inputs.bOutputInHDR ? GRHIHDRDisplayOutputFormat : PF_B8G8R8A8;
		OutputDesc.ClearValue = FClearValueBinding(FLinearColor(0, 0, 0, 0));
		OutputDesc.Flags |= GFastVRamConfig.Tonemap;

		const FTonemapperOutputDeviceParameters OutputDeviceParameters = GetTonemapperOutputDeviceParameters(*View.Family);
		const ETonemapperOutputDevice OutputDevice = static_cast<ETonemapperOutputDevice>(OutputDeviceParameters.OutputDevice);

		if (OutputDevice == ETonemapperOutputDevice::LinearEXR)
		{
			OutputDesc.Format = PF_A32B32G32R32F;
		}
		if (OutputDevice == ETonemapperOutputDevice::LinearNoToneCurve)
		{
			OutputDesc.Format = PF_FloatRGBA;
		}

		Output = FScreenPassRenderTarget(
			GraphBuilder.CreateTexture(OutputDesc, TEXT("Tonemap")),
			Inputs.SceneColor.ViewRect,
			View.GetOverwriteLoadAction());
	}

	const FScreenPassTextureViewport OutputViewport(Output);

	FRHITexture* NoiseTexture = nullptr;

	{
		check(GEngine);
		const auto* HighFrequencyNoiseTexture = GEngine->HighFrequencyNoiseTexture;
		check(HighFrequencyNoiseTexture);
		const auto* Resource = HighFrequencyNoiseTexture->Resource;
		check(Resource);
		NoiseTexture = Resource->TextureRHI;
		check(NoiseTexture);
	}

	FRHITexture* BloomDirtMaskTexture = GSystemTextures.BlackDummy->GetRenderTargetItem().TargetableTexture;

	if (PostProcessSettings.BloomDirtMask && PostProcessSettings.BloomDirtMask->Resource)
	{
		BloomDirtMaskTexture = PostProcessSettings.BloomDirtMask->Resource->TextureRHI;
	}

	FRHISamplerState* BilinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	const float DefaultEyeExposure = bEyeAdaptation ? 0.0f : GetEyeAdaptationFixedExposure(View);

	const float SharpenDiv6 = FMath::Clamp(CVarTonemapperSharpen.GetValueOnRenderThread(), 0.0f, 10.0f) / 6.0f;

	FVector4 ChromaticAberrationParams;

	{
		// for scene color fringe
		// from percent to fraction
		float Offset = 0.0f;
		float StartOffset = 0.0f;
		float Multiplier = 1.0f;

		if (PostProcessSettings.ChromaticAberrationStartOffset < 1.0f - KINDA_SMALL_NUMBER)
		{
			Offset = PostProcessSettings.SceneFringeIntensity * 0.01f;
			StartOffset = PostProcessSettings.ChromaticAberrationStartOffset;
			Multiplier = 1.0f / (1.0f - StartOffset);
		}

		// Wavelength of primaries in nm
		const float PrimaryR = 611.3f;
		const float PrimaryG = 549.1f;
		const float PrimaryB = 464.3f;

		// Simple lens chromatic aberration is roughly linear in wavelength
		float ScaleR = 0.007f * (PrimaryR - PrimaryB);
		float ScaleG = 0.007f * (PrimaryG - PrimaryB);
		ChromaticAberrationParams = FVector4(Offset * ScaleR * Multiplier, Offset * ScaleG * Multiplier, StartOffset, 0.f);
	}

	float EditorNITLevel = 160.0f;

	#if WITH_EDITOR
	{
		static auto CVarHDRNITLevel = IConsoleManager::Get().FindConsoleVariable(TEXT("Editor.HDRNITLevel"));
		if (CVarHDRNITLevel)
		{
			EditorNITLevel = CVarHDRNITLevel->GetFloat();
		}
	}
	#endif

	FTonemapParameters CommonParameters;
	CommonParameters.View = View.ViewUniformBuffer;
	CommonParameters.FilmGrain = GetFilmGrainParameters(View);
	CommonParameters.OutputDevice = GetTonemapperOutputDeviceParameters(ViewFamily);
	CommonParameters.Color = GetScreenPassTextureViewportParameters(SceneColorViewport);
	if (Inputs.Bloom.Texture)
	{
		const FScreenPassTextureViewport BloomViewport(Inputs.Bloom);
		CommonParameters.Bloom = GetScreenPassTextureViewportParameters(BloomViewport);
	}
	CommonParameters.Output = GetScreenPassTextureViewportParameters(OutputViewport);
	CommonParameters.ColorTexture = Inputs.SceneColor.Texture;
	CommonParameters.BloomTexture = Inputs.Bloom.Texture;
	CommonParameters.EyeAdaptation = Inputs.EyeAdaptationTexture;
	CommonParameters.ColorGradingLUT = Inputs.ColorGradingTexture;
	CommonParameters.NoiseTexture = NoiseTexture;
	CommonParameters.BloomDirtMaskTexture = BloomDirtMaskTexture;
	CommonParameters.ColorSampler = BilinearClampSampler;
	CommonParameters.BloomSampler = BilinearClampSampler;
	CommonParameters.NoiseSampler = PointClampSampler;
	CommonParameters.ColorGradingLUTSampler = BilinearClampSampler;
	CommonParameters.BloomDirtMaskSampler = BilinearClampSampler;
	CommonParameters.ColorScale0 = PostProcessSettings.SceneColorTint;
	CommonParameters.ColorScale1 = FLinearColor::White * PostProcessSettings.BloomIntensity;
	CommonParameters.BloomDirtMaskTint = PostProcessSettings.BloomDirtMaskTint * PostProcessSettings.BloomDirtMaskIntensity;
	CommonParameters.ChromaticAberrationParams = ChromaticAberrationParams;
	CommonParameters.TonemapperParams = FVector4(PostProcessSettings.VignetteIntensity, SharpenDiv6, 0.0f, 0.0f);
	CommonParameters.SwitchVerticalAxis = Inputs.bFlipYAxis;
	CommonParameters.DefaultEyeExposure = DefaultEyeExposure;
	CommonParameters.EditorNITLevel = EditorNITLevel;
	CommonParameters.bOutputInHDR = ViewFamily.bIsHDR;
	CommonParameters.LensPrincipalPointOffsetScale = View.LensPrincipalPointOffsetScale;

	// forward transformation from shader:
	//return LensPrincipalPointOffsetScale.xy + UV * LensPrincipalPointOffsetScale.zw;

	// reverse transformation from shader:
	//return UV*(1.0f/LensPrincipalPointOffsetScale.zw) - LensPrincipalPointOffsetScale.xy/LensPrincipalPointOffsetScale.zw;

	CommonParameters.LensPrincipalPointOffsetScaleInverse.X = -View.LensPrincipalPointOffsetScale.X / View.LensPrincipalPointOffsetScale.Z;
	CommonParameters.LensPrincipalPointOffsetScaleInverse.Y = -View.LensPrincipalPointOffsetScale.Y / View.LensPrincipalPointOffsetScale.W;
	CommonParameters.LensPrincipalPointOffsetScaleInverse.Z = 1.0f / View.LensPrincipalPointOffsetScale.Z;
	CommonParameters.LensPrincipalPointOffsetScaleInverse.W = 1.0f / View.LensPrincipalPointOffsetScale.W;

	// Generate permutation vector for the desktop tonemapper.
	TonemapperPermutation::FDesktopDomain DesktopPermutationVector;

	{
		TonemapperPermutation::FCommonDomain CommonDomain = TonemapperPermutation::BuildCommonPermutationDomain(View, Inputs.bGammaOnly, Inputs.bFlipYAxis);
		DesktopPermutationVector.Set<TonemapperPermutation::FCommonDomain>(CommonDomain);

		if (!CommonDomain.Get<TonemapperPermutation::FTonemapperGammaOnlyDim>())
		{
			// Grain Quantization
			{
				static TConsoleVariableData<int32>* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Tonemapper.GrainQuantization"));
				const int32 Value = CVar->GetValueOnRenderThread();
				DesktopPermutationVector.Set<TonemapperPermutation::FTonemapperGrainQuantizationDim>(Value > 0);
			}

			DesktopPermutationVector.Set<TonemapperPermutation::FTonemapperColorFringeDim>(PostProcessSettings.SceneFringeIntensity > 0.01f);
		}

		DesktopPermutationVector.Set<TonemapperPermutation::FTonemapperOutputDeviceDim>(ETonemapperOutputDevice(CommonParameters.OutputDevice.OutputDevice));

		DesktopPermutationVector = TonemapperPermutation::RemapPermutation(DesktopPermutationVector, View.GetFeatureLevel());
	}

	// Override output might not support UAVs.
	const bool bComputePass = (Output.Texture->Desc.TargetableFlags & TexCreate_UAV) == TexCreate_UAV ? View.bUseComputePasses : false;

	if (bComputePass)
	{
		FTonemapCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTonemapCS::FParameters>();
		PassParameters->Tonemap = CommonParameters;
		PassParameters->RWOutputTexture = GraphBuilder.CreateUAV(Output.Texture);

		FTonemapCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<TonemapperPermutation::FDesktopDomain>(DesktopPermutationVector);
		PermutationVector.Set<TonemapperPermutation::FTonemapperEyeAdaptationDim>(bEyeAdaptation);

		TShaderMapRef<FTonemapCS> ComputeShader(View.ShaderMap, PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Tonemap %dx%d (CS GammaOnly=%d)", OutputViewport.Rect.Width(), OutputViewport.Rect.Height(), Inputs.bGammaOnly),
			*ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(OutputViewport.Rect.Size(), FIntPoint(GTonemapComputeTileSizeX, GTonemapComputeTileSizeY)));
	}
	else
	{
		FTonemapPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTonemapPS::FParameters>();
		PassParameters->Tonemap = CommonParameters;
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		FTonemapVS::FPermutationDomain VertexPermutationVector;
		VertexPermutationVector.Set<TonemapperPermutation::FTonemapperSwitchAxis>(Inputs.bFlipYAxis);
		VertexPermutationVector.Set<TonemapperPermutation::FTonemapperEyeAdaptationDim>(bEyeAdaptation);

		TShaderMapRef<FTonemapVS> VertexShader(View.ShaderMap, VertexPermutationVector);
		TShaderMapRef<FTonemapPS> PixelShader(View.ShaderMap, DesktopPermutationVector);

		// If this is a stereo view, there's a good chance we need alpha out of the tonemapper
		// @todo: Remove this once Oculus fix the bug in their runtime that requires alpha here.
		const bool bIsStereo = IStereoRendering::IsStereoEyeView(View);
		FRHIBlendState* BlendState = Inputs.bWriteAlphaChannel || bIsStereo ? FScreenPassPipelineState::FDefaultBlendState::GetRHI() : TStaticBlendStateWriteMask<CW_RGB>::GetRHI();
		FRHIDepthStencilState* DepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();

		EScreenPassDrawFlags DrawFlags = EScreenPassDrawFlags::AllowHMDHiddenAreaMask;

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("Tonemap %dx%d (PS GammaOnly=%d)", OutputViewport.Rect.Width(), OutputViewport.Rect.Height(), Inputs.bGammaOnly),
			View,
			OutputViewport,
			SceneColorViewport,
			FScreenPassPipelineState(*VertexShader, *PixelShader, BlendState, DepthStencilState),
			DrawFlags,
			PassParameters,
			[VertexShader, PixelShader, PassParameters](FRHICommandList& RHICmdList)
		{
			SetShaderParameters(RHICmdList, *VertexShader, VertexShader->GetVertexShader(), PassParameters->Tonemap);
			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);
		});
	}

	return MoveTemp(Output);
}

//////////////////////////////////////////////////////////////////////////
//! DEPRECATED: This is only used by mobile until fully ported to RDG.

FRCPassPostProcessTonemap::FRCPassPostProcessTonemap(bool bInDoGammaOnly, bool bInDoEyeAdaptation, bool bInHDROutput)
	: bDoGammaOnly(bInDoGammaOnly)
	, bDoScreenPercentageInTonemapper(false)
	, bDoEyeAdaptation(bInDoEyeAdaptation)
	, bHDROutput(bInHDROutput)
{}

void FRCPassPostProcessTonemap::Process(FRenderingCompositePassContext& Context)
{
	const FViewInfo& View = Context.View;

	FRDGBuilder GraphBuilder(Context.RHICmdList);

	FRDGTextureRef SceneColorTexture = CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"));
	const FIntRect SceneColorViewRect = Context.SceneColorViewRect;

	FRDGTextureRef BloomTexture = CreateRDGTextureForInputWithFallback(GraphBuilder, ePId_Input1, TEXT("Bloom"), eFC_0000);
	const FIntRect BloomViewRect = Context.GetDownsampledSceneColorViewRectFromInputExtent(BloomTexture->Desc.Extent);

	FRDGTextureRef EyeAdaptationTexture = CreateRDGTextureForOptionalInput(GraphBuilder, ePId_Input2, TEXT("EyeAdaptation"));

	FRDGTextureRef ColorGradingTexture = CreateRDGTextureForOptionalInput(GraphBuilder, ePId_Input3, TEXT("ColorGrading"));

	// If we didn't calculate the LUT during post processing, see if we've cached it on the view.
	if (!ColorGradingTexture)
	{
		ColorGradingTexture = GraphBuilder.TryRegisterExternalTexture(View.GetTonemappingLUT());
	}

	FRDGTextureRef OutputTexture = FindRDGTextureForOutput(GraphBuilder, ePId_Output0, TEXT("OutputTexture"));
	ERenderTargetLoadAction OutputLoadAction = ERenderTargetLoadAction::ENoAction;
	FIntRect OutputViewRect;

	// This is the view family render target.
	if (OutputTexture)
	{
		if (View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::RawOutput)
		{
			OutputViewRect = View.ViewRect;
		}
		else
		{
			OutputViewRect = View.UnscaledViewRect;
		}
		OutputLoadAction = View.IsFirstInFamily() ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad;
	}
	else
	{
		FRDGTextureDesc OutputDesc = SceneColorTexture->Desc;
		OutputDesc.Reset();
		OutputDesc.TargetableFlags |= View.bUseComputePasses ? TexCreate_UAV : TexCreate_RenderTargetable;
		OutputDesc.Format = PF_B8G8R8A8;

		// RGB is the color in LDR, A is the luminance for PostprocessAA
		OutputDesc.Format = bHDROutput ? GRHIHDRDisplayOutputFormat : OutputDesc.Format;
		OutputDesc.ClearValue = FClearValueBinding(FLinearColor(0, 0, 0, 0));
		OutputDesc.Flags |= GFastVRamConfig.Tonemap;

		const FTonemapperOutputDeviceParameters OutputDeviceParameters = GetTonemapperOutputDeviceParameters(*View.Family);

		const ETonemapperOutputDevice OutputDevice = static_cast<ETonemapperOutputDevice>(OutputDeviceParameters.OutputDevice);

		if (OutputDevice == ETonemapperOutputDevice::LinearEXR)
		{
			OutputDesc.Format = PF_A32B32G32R32F;
		}
		if (OutputDevice == ETonemapperOutputDevice::LinearNoToneCurve)
		{
			OutputDesc.Format = PF_FloatRGBA;
		}

		OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("Tonemap"));
		OutputLoadAction = View.GetOverwriteLoadAction();
		OutputViewRect = SceneColorViewRect;
	}

	FTonemapInputs Inputs;
	Inputs.SceneColor.Texture = SceneColorTexture;
	Inputs.SceneColor.ViewRect = SceneColorViewRect;
	Inputs.Bloom.Texture = BloomTexture;
	Inputs.Bloom.ViewRect = BloomViewRect;
	Inputs.EyeAdaptationTexture = EyeAdaptationTexture;
	Inputs.ColorGradingTexture = ColorGradingTexture;
	Inputs.OverrideOutput.Texture = OutputTexture;
	Inputs.OverrideOutput.ViewRect = OutputViewRect;
	Inputs.OverrideOutput.LoadAction = OutputLoadAction;
	Inputs.bWriteAlphaChannel = View.AntiAliasingMethod == AAM_FXAA || IsPostProcessingWithAlphaChannelSupported();
	Inputs.bFlipYAxis = ShouldMobilePassFlipVerticalAxis(Context, this);
	Inputs.bGammaOnly = bDoGammaOnly;

	AddTonemapPass(GraphBuilder, View, Inputs);

	ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, OutputTexture);

	GraphBuilder.Execute();

	// We only release the SceneColor after the last view was processed (SplitScreen)
	if(View.IsLastInFamily() && !GIsEditor)
	{
		// The RT should be released as early as possible to allow sharing of that memory for other purposes.
		// This becomes even more important with some limited VRam (XBoxOne).
		FSceneRenderTargets::Get(Context.RHICmdList).SetSceneColor(nullptr);
	}
}

FPooledRenderTargetDesc FRCPassPostProcessTonemap::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	return FPooledRenderTargetDesc();
}

// ES2 version

class FPostProcessTonemapPS_ES2 : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPostProcessTonemapPS_ES2);

	// Mobile renderer specific permutation dimensions.
	class FTonemapperDOFDim         : SHADER_PERMUTATION_BOOL("USE_DOF");
	class FTonemapperLightShaftsDim : SHADER_PERMUTATION_BOOL("USE_LIGHT_SHAFTS");
	class FTonemapper32BPPHDRDim    : SHADER_PERMUTATION_BOOL("USE_32BPP_HDR");
	class FTonemapperColorMatrixDim : SHADER_PERMUTATION_BOOL("USE_COLOR_MATRIX");
	class FTonemapperShadowTintDim  : SHADER_PERMUTATION_BOOL("USE_SHADOW_TINT");
	class FTonemapperContrastDim    : SHADER_PERMUTATION_BOOL("USE_CONTRAST");
	
	using FPermutationDomain = TShaderPermutationDomain<
		TonemapperPermutation::FCommonDomain,
		FTonemapperDOFDim,
		FTonemapperLightShaftsDim,
		FTonemapper32BPPHDRDim,
		FTonemapperColorMatrixDim,
		FTonemapperShadowTintDim,
		FTonemapperContrastDim>;
	
	template<class TPermDim, class TPermVector>
	static void EnableIfSet(const TPermVector& SourceDomain, TPermVector& DestDomain)
	{
		if (SourceDomain.template Get<TPermDim>())
		{
			DestDomain.template Set<TPermDim>(true);
		}
	}

	// Reduce the number of permutations by combining common states
	static FPermutationDomain RemapPermutationVector(FPermutationDomain WantedPermutationVector)
	{
		TonemapperPermutation::FCommonDomain WantedCommonPermutationVector = WantedPermutationVector.Get<TonemapperPermutation::FCommonDomain>();
		FPermutationDomain RemappedPermutationVector;
		TonemapperPermutation::FCommonDomain RemappedCommonPermutationVector;

		// Note: FTonemapperSharpenDim, FTonemapperGrainJitterDim are not supported.

		// 32 bit hdr (encoding)
		EnableIfSet<FTonemapper32BPPHDRDim>(WantedPermutationVector, RemappedPermutationVector);

		// Gamma only
		if (WantedCommonPermutationVector.Get<TonemapperPermutation::FTonemapperGammaOnlyDim>())
		{
			RemappedCommonPermutationVector.Set<TonemapperPermutation::FTonemapperGammaOnlyDim>(true);

			// Mutually exclusive - clear the wanted vector. 
			WantedPermutationVector = FPermutationDomain();
			WantedCommonPermutationVector = WantedPermutationVector.Get<TonemapperPermutation::FCommonDomain>();
		}
		else
		{
			// Always enable contrast.
			RemappedPermutationVector.Set<FTonemapperContrastDim>(true);
		}

 		// Bloom permutation
		EnableIfSet<TonemapperPermutation::FTonemapperBloomDim>(WantedCommonPermutationVector, RemappedCommonPermutationVector);
 		// Vignette permutation
		EnableIfSet<TonemapperPermutation::FTonemapperVignetteDim>(WantedCommonPermutationVector, RemappedCommonPermutationVector);
 		// Grain intensity permutation
		EnableIfSet<TonemapperPermutation::FTonemapperGrainIntensityDim>(WantedCommonPermutationVector, RemappedCommonPermutationVector);
		// Switch Y axis
		EnableIfSet<TonemapperPermutation::FTonemapperSwitchAxis>(WantedCommonPermutationVector, RemappedCommonPermutationVector);
 		// msaa permutation.
		EnableIfSet<TonemapperPermutation::FTonemapperMsaaDim>(WantedCommonPermutationVector, RemappedCommonPermutationVector);

 		// Color matrix
		EnableIfSet<FTonemapperColorMatrixDim>(WantedPermutationVector, RemappedPermutationVector);

		// DoF
		if (WantedPermutationVector.Get<FTonemapperDOFDim>())
		{
			RemappedPermutationVector.Set<FTonemapperDOFDim>(true);
			RemappedPermutationVector.Set<FTonemapperLightShaftsDim>(true);
			RemappedCommonPermutationVector.Set<TonemapperPermutation::FTonemapperVignetteDim>(true);
			RemappedCommonPermutationVector.Set<TonemapperPermutation::FTonemapperBloomDim>(true);
		}

		// light shafts
		if (WantedPermutationVector.Get<FTonemapperLightShaftsDim>())
		{
			RemappedPermutationVector.Set<FTonemapperLightShaftsDim>(true);
			RemappedCommonPermutationVector.Set<TonemapperPermutation::FTonemapperVignetteDim>(true);
			RemappedCommonPermutationVector.Set<TonemapperPermutation::FTonemapperBloomDim>(true);
		}

		// Shadow tint
		if (WantedPermutationVector.Get<FTonemapperShadowTintDim>())
		{
			RemappedPermutationVector.Set<FTonemapperShadowTintDim>(true);
			RemappedPermutationVector.Set<FTonemapperColorMatrixDim>(true);
		}

		if (RemappedPermutationVector.Get<FTonemapper32BPPHDRDim>())
		{
			// 32 bpp hdr does not support:
			RemappedPermutationVector.Set<FTonemapperDOFDim>(false);
			RemappedPermutationVector.Set<FTonemapperLightShaftsDim>(false);
			RemappedCommonPermutationVector.Set<TonemapperPermutation::FTonemapperMsaaDim>(false);
		}

		RemappedPermutationVector.Set<TonemapperPermutation::FCommonDomain>(RemappedCommonPermutationVector);
		return RemappedPermutationVector;
	}

	static FPermutationDomain BuildPermutationVector(const FViewInfo& View, bool bNeedsToSwitchVerticalAxis)
	{
		TonemapperPermutation::FCommonDomain CommonPermutationVector = TonemapperPermutation::BuildCommonPermutationDomain(View, /* bGammaOnly = */ false, bNeedsToSwitchVerticalAxis);

		FPostProcessTonemapPS_ES2::FPermutationDomain MobilePermutationVector;
		MobilePermutationVector.Set<TonemapperPermutation::FCommonDomain>(CommonPermutationVector);

		bool bUse32BPPHDR = IsMobileHDR32bpp();

		// Must early exit if gamma only.
		if (CommonPermutationVector.Get<TonemapperPermutation::FTonemapperGammaOnlyDim>())
		{
			MobilePermutationVector.Set<FTonemapper32BPPHDRDim>(bUse32BPPHDR);
			return RemapPermutationVector(MobilePermutationVector);
		}

		const FFinalPostProcessSettings& Settings = View.FinalPostProcessSettings;
		{
			FVector MixerR(Settings.FilmChannelMixerRed);
			FVector MixerG(Settings.FilmChannelMixerGreen);
			FVector MixerB(Settings.FilmChannelMixerBlue);
			if (
				(Settings.FilmSaturation != 1.0f) ||
				((MixerR - FVector(1.0f, 0.0f, 0.0f)).GetAbsMax() != 0.0f) ||
				((MixerG - FVector(0.0f, 1.0f, 0.0f)).GetAbsMax() != 0.0f) ||
				((MixerB - FVector(0.0f, 0.0f, 1.0f)).GetAbsMax() != 0.0f))
			{
				MobilePermutationVector.Set<FTonemapperColorMatrixDim>(true);
			}
		}
		MobilePermutationVector.Set<FTonemapperShadowTintDim>(Settings.FilmShadowTintAmount > 0.0f);
		MobilePermutationVector.Set<FTonemapperContrastDim>(Settings.FilmContrast > 0.0f);

		if (IsMobileHDRMosaic())
		{
			MobilePermutationVector.Set<FTonemapper32BPPHDRDim>(true);
			return MobilePermutationVector;
		}

		static const auto CVarMobileMSAA = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileMSAA"));
		const EShaderPlatform ShaderPlatform = View.GetShaderPlatform();
		if (IsMetalMobilePlatform(ShaderPlatform))
		{
			bool bMSAA = (CVarMobileMSAA ? CVarMobileMSAA->GetValueOnAnyThread() > 1 : false);
			CommonPermutationVector.Set<TonemapperPermutation::FTonemapperMsaaDim>(bMSAA);
		}

		if (bUse32BPPHDR)
		{
			// add limited post for 32 bit encoded hdr.
			MobilePermutationVector.Set<FTonemapper32BPPHDRDim>(true);
		}
		else if (GSupportsRenderTargetFormat_PF_FloatRGBA)
		{
			bool bUseDof = GetMobileDepthOfFieldScale(View) > 0.0f && (!Settings.bMobileHQGaussian || (View.GetFeatureLevel() < ERHIFeatureLevel::ES3_1));

			MobilePermutationVector.Set<FTonemapperDOFDim>(bUseDof);
			MobilePermutationVector.Set<FTonemapperLightShaftsDim>(View.bLightShaftUse);
		}
		else
		{
			// Override Bloom because is not supported.
			CommonPermutationVector.Set<TonemapperPermutation::FTonemapperBloomDim>(false);
		}

		// Mobile is not currently supporting these.
		CommonPermutationVector.Set<TonemapperPermutation::FTonemapperGrainJitterDim>(false);
		CommonPermutationVector.Set<TonemapperPermutation::FTonemapperSharpenDim>(false);
		MobilePermutationVector.Set<TonemapperPermutation::FCommonDomain>(CommonPermutationVector);

		// We're not supporting every possible permutation, remap the permutation vector to combine common effects.
		return RemapPermutationVector(MobilePermutationVector);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		auto CommonPermutationVector = PermutationVector.Get<TonemapperPermutation::FCommonDomain>();
		if (!TonemapperPermutation::ShouldCompileCommonPermutation(Parameters, CommonPermutationVector))
		{
			return false;
		}

		// If this permutation vector is remapped at runtime, we can avoid the compile.
		if (RemapPermutationVector(PermutationVector) != PermutationVector)
		{
			return false;
		}

		// Only cache for ES2/3.1 shader platforms, and only compile 32bpp shaders for Android or PC emulation
		return (IsMobilePlatform(Parameters.Platform) && (!PermutationVector.Get<FTonemapper32BPPHDRDim>()))
			|| Parameters.Platform == SP_OPENGL_ES2_ANDROID
			|| (IsMobilePlatform(Parameters.Platform) && IsPCPlatform(Parameters.Platform));
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//Need to hack in exposure scale for < SM5
		OutEnvironment.SetDefine(TEXT("NO_EYEADAPTATION_EXPOSURE_FIX"), 1);
	}

	/** Default constructor. */
	FPostProcessTonemapPS_ES2() {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FShaderParameter ColorScale0;
	FShaderParameter ColorScale1;
	FShaderParameter TexScale;
	FShaderParameter GrainScaleBiasJitter;
	FShaderParameter InverseGamma;
	FShaderParameter TonemapperParams;

	FShaderParameter ColorMatrixR_ColorCurveCd1;
	FShaderParameter ColorMatrixG_ColorCurveCd3Cm3;
	FShaderParameter ColorMatrixB_ColorCurveCm2;
	FShaderParameter ColorCurve_Cm0Cd0_Cd2_Ch0Cm1_Ch3;
	FShaderParameter ColorCurve_Ch1_Ch2;
	FShaderParameter ColorShadow_Luma;
	FShaderParameter ColorShadow_Tint1;
	FShaderParameter ColorShadow_Tint2;

	FShaderParameter OverlayColor;
	FShaderParameter FringeIntensity;
	FShaderParameter SRGBAwareTargetParam;
	FShaderParameter DefaultEyeExposure;

	/** Initialization constructor. */
	FPostProcessTonemapPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		ColorScale0.Bind(Initializer.ParameterMap, TEXT("ColorScale0"));
		ColorScale1.Bind(Initializer.ParameterMap, TEXT("ColorScale1"));
		TexScale.Bind(Initializer.ParameterMap, TEXT("TexScale"));
		TonemapperParams.Bind(Initializer.ParameterMap, TEXT("TonemapperParams"));
		GrainScaleBiasJitter.Bind(Initializer.ParameterMap, TEXT("GrainScaleBiasJitter"));
		InverseGamma.Bind(Initializer.ParameterMap,TEXT("InverseGamma"));

		ColorMatrixR_ColorCurveCd1.Bind(Initializer.ParameterMap, TEXT("ColorMatrixR_ColorCurveCd1"));
		ColorMatrixG_ColorCurveCd3Cm3.Bind(Initializer.ParameterMap, TEXT("ColorMatrixG_ColorCurveCd3Cm3"));
		ColorMatrixB_ColorCurveCm2.Bind(Initializer.ParameterMap, TEXT("ColorMatrixB_ColorCurveCm2"));
		ColorCurve_Cm0Cd0_Cd2_Ch0Cm1_Ch3.Bind(Initializer.ParameterMap, TEXT("ColorCurve_Cm0Cd0_Cd2_Ch0Cm1_Ch3"));
		ColorCurve_Ch1_Ch2.Bind(Initializer.ParameterMap, TEXT("ColorCurve_Ch1_Ch2"));
		ColorShadow_Luma.Bind(Initializer.ParameterMap, TEXT("ColorShadow_Luma"));
		ColorShadow_Tint1.Bind(Initializer.ParameterMap, TEXT("ColorShadow_Tint1"));
		ColorShadow_Tint2.Bind(Initializer.ParameterMap, TEXT("ColorShadow_Tint2"));

		OverlayColor.Bind(Initializer.ParameterMap, TEXT("OverlayColor"));
		FringeIntensity.Bind(Initializer.ParameterMap, TEXT("FringeIntensity"));

		SRGBAwareTargetParam.Bind(Initializer.ParameterMap, TEXT("SRGBAwareTarget"));

		DefaultEyeExposure.Bind(Initializer.ParameterMap, TEXT("DefaultEyeExposure"));
	}
	
	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar  << PostprocessParameter << ColorScale0 << ColorScale1 << InverseGamma
			<< TexScale << GrainScaleBiasJitter << TonemapperParams
			<< ColorMatrixR_ColorCurveCd1 << ColorMatrixG_ColorCurveCd3Cm3 << ColorMatrixB_ColorCurveCm2 << ColorCurve_Cm0Cd0_Cd2_Ch0Cm1_Ch3 << ColorCurve_Ch1_Ch2 << ColorShadow_Luma << ColorShadow_Tint1 << ColorShadow_Tint2
			<< OverlayColor
			<< FringeIntensity
			<< SRGBAwareTargetParam
			<< DefaultEyeExposure;

		return bShaderHasOutdatedParameters;
	}

	template <typename TRHICmdList>
	void SetPS(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context, const FPermutationDomain& PermutationVector, bool bSRGBAwareTarget)
	{
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		const FSceneViewFamily& ViewFamily = *(Context.View.Family);

		FRHIPixelShader* ShaderRHI = GetPixelShader();
		
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		if (PermutationVector.Get<FTonemapper32BPPHDRDim>() && IsMobileHDRMosaic())
		{
			PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		}
		else
		{
			PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		}
			
		SetShaderValue(RHICmdList, ShaderRHI, OverlayColor, Context.View.OverlayColor);
		SetShaderValue(RHICmdList, ShaderRHI, FringeIntensity, fabsf(Settings.SceneFringeIntensity) * 0.01f); // Interpreted as [0-1] percentage

		{
			FLinearColor Col = Settings.SceneColorTint;
			FVector4 ColorScale(Col.R, Col.G, Col.B, 0);
			SetShaderValue(RHICmdList, ShaderRHI, ColorScale0, ColorScale);
		}
		
		{
			FLinearColor Col = FLinearColor::White * Settings.BloomIntensity;
			FVector4 ColorScale(Col.R, Col.G, Col.B, 0);
			SetShaderValue(RHICmdList, ShaderRHI, ColorScale1, ColorScale);
		}

		{
			const FPooledRenderTargetDesc* InputDesc = Context.Pass->GetInputDesc(ePId_Input0);

			// we assume the this pass runs in 1:1 pixel
			FVector2D TexScaleValue = FVector2D(InputDesc->Extent) / FVector2D(Context.View.ViewRect.Size());

			SetShaderValue(RHICmdList, ShaderRHI, TexScale, TexScaleValue);
		}

		{
			float Sharpen = FMath::Clamp(CVarTonemapperSharpen.GetValueOnRenderThread(), 0.0f, 10.0f);

			FVector2D Value(Settings.VignetteIntensity, Sharpen);

			SetShaderValue(RHICmdList, ShaderRHI, TonemapperParams, Value);
		}

		FVector GrainValue;
		GrainPostSettings(&GrainValue, &Settings);
		SetShaderValue(RHICmdList, ShaderRHI, GrainScaleBiasJitter, GrainValue);

		{
			FVector InvDisplayGammaValue;
			InvDisplayGammaValue.X = 1.0f / ViewFamily.RenderTarget->GetDisplayGamma();
			InvDisplayGammaValue.Y = 2.2f / ViewFamily.RenderTarget->GetDisplayGamma();
			InvDisplayGammaValue.Z = 1.0; // Unused on mobile.
			SetShaderValue(RHICmdList, ShaderRHI, InverseGamma, InvDisplayGammaValue);
		}

		{
			const FMobileFilmTonemapParameters Parameters = GetMobileFilmTonemapParameters(
				Context.View.FinalPostProcessSettings,
				/* UseColorMatrix = */ PermutationVector.Get<FTonemapperColorMatrixDim>(),
				/* UseShadowTint = */ PermutationVector.Get<FTonemapperShadowTintDim>(),
				/* UseContrast = */ PermutationVector.Get<FTonemapperContrastDim>());

			SetShaderValue(RHICmdList, ShaderRHI, ColorMatrixR_ColorCurveCd1, Parameters.ColorMatrixR_ColorCurveCd1);
			SetShaderValue(RHICmdList, ShaderRHI, ColorMatrixG_ColorCurveCd3Cm3, Parameters.ColorMatrixG_ColorCurveCd3Cm3);
			SetShaderValue(RHICmdList, ShaderRHI, ColorMatrixB_ColorCurveCm2, Parameters.ColorMatrixB_ColorCurveCm2);
			SetShaderValue(RHICmdList, ShaderRHI, ColorCurve_Cm0Cd0_Cd2_Ch0Cm1_Ch3, Parameters.ColorCurve_Cm0Cd0_Cd2_Ch0Cm1_Ch3);
			SetShaderValue(RHICmdList, ShaderRHI, ColorCurve_Ch1_Ch2, Parameters.ColorCurve_Ch1_Ch2);
			SetShaderValue(RHICmdList, ShaderRHI, ColorShadow_Luma, Parameters.ColorShadow_Luma);
			SetShaderValue(RHICmdList, ShaderRHI, ColorShadow_Tint1, Parameters.ColorShadow_Tint1);
			SetShaderValue(RHICmdList, ShaderRHI, ColorShadow_Tint2, Parameters.ColorShadow_Tint2);
		}

		SetShaderValue(RHICmdList, ShaderRHI, SRGBAwareTargetParam, bSRGBAwareTarget ? 1.0f : 0.0f );

		float FixedExposure = GetEyeAdaptationFixedExposure(Context.View);
		SetShaderValue(RHICmdList, ShaderRHI, DefaultEyeExposure, FixedExposure);
	}
};

class FPostProcessTonemapVS_ES2 : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPostProcessTonemapVS_ES2);

	class FTonemapperSwitchAxis : SHADER_PERMUTATION_BOOL("NEEDTOSWITCHVERTICLEAXIS");

	using FPermutationDomain = TShaderPermutationDomain<
		FTonemapperSwitchAxis
	>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// Prevent switch axis permutation on platforms that dont require it.
		if (PermutationVector.Get<FTonemapperSwitchAxis>() && !RHINeedsToSwitchVerticalAxis(Parameters.Platform))
		{
			return false;
		}

		return !IsConsolePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static FPermutationDomain BuildPermutationVector(bool bNeedsToSwitchVerticalAxis)
	{
		FPermutationDomain PermutationVector;
		PermutationVector.Set<FTonemapperSwitchAxis>(bNeedsToSwitchVerticalAxis);
		return PermutationVector;
	}

	FPostProcessTonemapVS_ES2() { }

public:
	FPostProcessPassParameters PostprocessParameter;
	FShaderResourceParameter EyeAdaptation;
	FShaderParameter GrainRandomFull;
	FShaderParameter FringeIntensity;
	FShaderParameter ScreenPosToViewportBias;
	FShaderParameter ScreenPosToViewportScale;
	bool bUsedFramebufferFetch;

	FPostProcessTonemapVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		GrainRandomFull.Bind(Initializer.ParameterMap, TEXT("GrainRandomFull"));
		FringeIntensity.Bind(Initializer.ParameterMap, TEXT("FringeIntensity"));
		ScreenPosToViewportBias.Bind(Initializer.ParameterMap, TEXT("Color_ScreenPosToViewportBias"));
		ScreenPosToViewportScale.Bind(Initializer.ParameterMap, TEXT("Color_ScreenPosToViewportScale"));
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = GetVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());

		FVector GrainRandomFullValue;
		{
			uint8 FrameIndexMod8 = 0;
			if (Context.View.State)
			{
				FrameIndexMod8 = Context.View.ViewState->GetFrameIndex(8);
			}
			GrainRandomFromFrame(&GrainRandomFullValue, FrameIndexMod8);
		}

		// TODO: Don't use full on mobile with framebuffer fetch.
		GrainRandomFullValue.Z = bUsedFramebufferFetch ? 0.0f : 1.0f;
		SetShaderValue(Context.RHICmdList, ShaderRHI, GrainRandomFull, GrainRandomFullValue);

		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		SetShaderValue(Context.RHICmdList, ShaderRHI, FringeIntensity, fabsf(Settings.SceneFringeIntensity) * 0.01f); // Interpreted as [0-1] percentage


		{
			FIntPoint ViewportOffset = Context.SceneColorViewRect.Min;
			FIntPoint ViewportExtent = Context.SceneColorViewRect.Size();
			FVector4 ScreenPosToScenePixelValue(
				ViewportExtent.X * 0.5f,
				-ViewportExtent.Y * 0.5f,
				ViewportExtent.X * 0.5f - 0.5f + ViewportOffset.X,
				ViewportExtent.Y * 0.5f - 0.5f + ViewportOffset.Y);
			SetShaderValue(Context.RHICmdList, ShaderRHI, ScreenPosToViewportScale, FVector2D(ScreenPosToScenePixelValue.X, ScreenPosToScenePixelValue.Y));
			SetShaderValue(Context.RHICmdList, ShaderRHI, ScreenPosToViewportBias, FVector2D(ScreenPosToScenePixelValue.Z, ScreenPosToScenePixelValue.W));
		}
	}
	
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << GrainRandomFull << FringeIntensity << ScreenPosToViewportBias << ScreenPosToViewportScale;
		return bShaderHasOutdatedParameters;
	}
};

IMPLEMENT_GLOBAL_SHADER(FPostProcessTonemapVS_ES2, "/Engine/Private/PostProcessTonemap.usf", "MainVS_ES2", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FPostProcessTonemapPS_ES2, "/Engine/Private/PostProcessTonemap.usf", "MainPS_ES2", SF_Pixel);


FRCPassPostProcessTonemapES2::FRCPassPostProcessTonemapES2(const FViewInfo& InView, bool bInUsedFramebufferFetch, bool bInSRGBAwareTarget)
	: bDoScreenPercentageInTonemapper(false)
	, View(InView)
	, bUsedFramebufferFetch(bInUsedFramebufferFetch)
	, bSRGBAwareTarget(bInSRGBAwareTarget)
{ }

void FRCPassPostProcessTonemapES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENTF(Context.RHICmdList, PostProcessTonemapES2, TEXT("Tonemapper(ES2 FramebufferFetch=%s)"), bUsedFramebufferFetch ? TEXT("0") : TEXT("1"));

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

	const FSceneViewFamily& ViewFamily = *(View.Family);
	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);
	const FPooledRenderTargetDesc& OutputDesc = PassOutputs[0].RenderTargetDesc;

	// no upscale if separate ren target is used.
	FIntRect SrcRect = View.ViewRect;
	FIntRect DestRect = bDoScreenPercentageInTonemapper ? View.UnscaledViewRect : View.ViewRect;
	FIntPoint SrcSize = InputDesc->Extent;
	FIntPoint DstSize = OutputDesc.Extent;

	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::ELoad;

	// Set the view family's render target/viewport.
	{
		// clear target when processing first view in case of splitscreen
		const bool bFirstView = (&View == View.Family->Views[0]);
		
		// Full clear to avoid restore
		if (IStereoRendering::IsAPrimaryView(View) && (bFirstView || IStereoRendering::IsStereoEyeView(View)))
		{
			LoadAction = ERenderTargetLoadAction::EClear;
		}
	}

	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("TonemapES2"));
	{
		Context.SetViewportAndCallRHI(DestRect);

		bool bNeedsToSwitchVerticalAxis = ShouldMobilePassFlipVerticalAxis(Context, this);

		auto VertexShaderPermutationVector = FPostProcessTonemapVS_ES2::BuildPermutationVector(bNeedsToSwitchVerticalAxis);
		auto PixelShaderPermutationVector = FPostProcessTonemapPS_ES2::BuildPermutationVector(View, bNeedsToSwitchVerticalAxis);

		TShaderMapRef<FPostProcessTonemapVS_ES2> VertexShader(Context.GetShaderMap(), VertexShaderPermutationVector);
		TShaderMapRef<FPostProcessTonemapPS_ES2> PixelShader(Context.GetShaderMap(), PixelShaderPermutationVector);

		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			VertexShader->bUsedFramebufferFetch = bUsedFramebufferFetch;

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

			VertexShader->SetVS(Context);
			PixelShader->SetPS(Context.RHICmdList, Context, PixelShaderPermutationVector, bSRGBAwareTarget);
		}

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstSize.X, DstSize.Y,
			SrcRect.Min.X, SrcRect.Min.Y,
			SrcRect.Width(), SrcRect.Height(),
			DstSize,
			SrcSize,
			*VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessTonemapES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;

	Ret.Reset();
	Ret.Format = PF_B8G8R8A8;
	Ret.DebugName = TEXT("Tonemap");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	if (bDoScreenPercentageInTonemapper)
	{
		Ret.Extent = View.UnscaledViewRect.Max;
	}
	return Ret;
}
