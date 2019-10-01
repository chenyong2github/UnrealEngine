// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PostProcess/RenderingCompositionGraph.h"
#include "PostProcess/PostProcessEyeAdaptation.h"
#include "ScreenPass.h"
#include "OverridePassSequence.h"
#include "Math/Halton.h"

// You must update values in PostProcessTonemap.usf when changing this enum.
enum class ETonemapperOutputDevice
{
	sRGB,
	Rec709,
	ExplicitGammaMapping,
	ACES1000nitST2084,
	ACES2000nitST2084,
	ACES1000nitScRGB,
	ACES2000nitScRGB,
	LinearEXR,
	LinearNoToneCurve,

	MAX
};

BEGIN_SHADER_PARAMETER_STRUCT(FTonemapperOutputDeviceParameters, )
	SHADER_PARAMETER(FVector, InverseGamma)
	SHADER_PARAMETER(uint32, OutputDevice)
	SHADER_PARAMETER(uint32, OutputGamut)
END_SHADER_PARAMETER_STRUCT()

FTonemapperOutputDeviceParameters GetTonemapperOutputDeviceParameters(const FSceneViewFamily& Family);

static void GrainRandomFromFrame(FVector* RESTRICT const Constant, uint32 FrameNumber)
{
	Constant->X = Halton(FrameNumber & 1023, 2);
	Constant->Y = Halton(FrameNumber & 1023, 3);
}

BEGIN_SHADER_PARAMETER_STRUCT(FMobileFilmTonemapParameters, )
	SHADER_PARAMETER(FVector4, ColorMatrixR_ColorCurveCd1)
	SHADER_PARAMETER(FVector4, ColorMatrixG_ColorCurveCd3Cm3)
	SHADER_PARAMETER(FVector4, ColorMatrixB_ColorCurveCm2)
	SHADER_PARAMETER(FVector4, ColorCurve_Cm0Cd0_Cd2_Ch0Cm1_Ch3)
	SHADER_PARAMETER(FVector4, ColorCurve_Ch1_Ch2)
	SHADER_PARAMETER(FVector4, ColorShadow_Luma)
	SHADER_PARAMETER(FVector4, ColorShadow_Tint1)
	SHADER_PARAMETER(FVector4, ColorShadow_Tint2)
END_SHADER_PARAMETER_STRUCT()

FMobileFilmTonemapParameters GetMobileFilmTonemapParameters(const FPostProcessSettings& PostProcessSettings, bool bUseColorMatrix, bool bUseShadowTint, bool bUseContrast);

struct FTonemapInputs
{
	// [Optional] Render to the specified output. If invalid, a new texture is created and returned.
	FScreenPassRenderTarget OverrideOutput;

	// [Required] HDR scene color to tonemap.
	FScreenPassTexture SceneColor;

	// [Required] Filtered bloom texture to composite with tonemapped scene color. This should be transparent black for no bloom.
	FScreenPassTexture Bloom;

	// [Required] Color grading texture used to remap colors.
	FRDGTextureRef ColorGradingTexture = nullptr;

	// [Optional] Eye adaptation texture used to compute exposure. If this is null, a default exposure value is used instead.
	FRDGTextureRef EyeAdaptationTexture = nullptr;

	// [Raster Only, Mobile] Flips the image vertically on output.
	bool bFlipYAxis = false;

	// [Raster Only] Controls whether the alpha channel of the scene texture should be written to the output texture.
	bool bWriteAlphaChannel = false;

	// Configures the tonemapper to only perform gamma correction.
	bool bGammaOnly = false;

	// Whether to leave the final output in HDR.
	bool bOutputInHDR = false;
};

FScreenPassTexture AddTonemapPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FTonemapInputs& Inputs);

// derives from TRenderingCompositePassBase<InputCount, OutputCount>
// ePId_Input0: SceneColor
// ePId_Input1: BloomCombined (not needed for bDoGammaOnly)
// ePId_Input2: EyeAdaptation (not needed for bDoGammaOnly)
// ePId_Input3: LUTsCombined (not needed for bDoGammaOnly)
class FRCPassPostProcessTonemap : public TRenderingCompositePassBase<4, 1>
{
public:
	FRCPassPostProcessTonemap(bool bInDoGammaOnly, bool bDoEyeAdaptation, bool bHDROutput);

	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

	bool bDoGammaOnly;
	bool bDoScreenPercentageInTonemapper;
private:
	bool bDoEyeAdaptation;
	bool bHDROutput;
};

// derives from TRenderingCompositePassBase<InputCount, OutputCount>
// ePId_Input0: SceneColor
// ePId_Input1: BloomCombined (not needed for bDoGammaOnly)
// ePId_Input2: Dof (not needed for bDoGammaOnly)
class FRCPassPostProcessTonemapES2 : public TRenderingCompositePassBase<3, 1>
{
public:
	FRCPassPostProcessTonemapES2(const FViewInfo& View, bool bInUsedFramebufferFetch, bool bInSRGBAwareTarget);

	// interface FRenderingCompositePass ---------

	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	
	bool bDoScreenPercentageInTonemapper;

private:
	const FViewInfo& View;

	bool bUsedFramebufferFetch;
	bool bSRGBAwareTarget;
};