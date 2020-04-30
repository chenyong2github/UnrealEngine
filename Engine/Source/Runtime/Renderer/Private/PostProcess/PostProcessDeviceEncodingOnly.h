// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PostProcess/RenderingCompositionGraph.h"
#include "PostProcess/PostProcessEyeAdaptation.h"
#include "ScreenPass.h"
#include "OverridePassSequence.h"
#include "Math/Halton.h"

// You must update values in PostProcessTonemap.usf when changing this enum.
enum class EDeviceEncodingOnlyOutputDevice
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
	LinearWithToneCurve,

	MAX
};

BEGIN_SHADER_PARAMETER_STRUCT(FDeviceEncodingOnlyOutputDeviceParameters, )
	SHADER_PARAMETER(FVector, InverseGamma)
	SHADER_PARAMETER(uint32, OutputDevice)
	SHADER_PARAMETER(uint32, OutputGamut)
END_SHADER_PARAMETER_STRUCT()

FDeviceEncodingOnlyOutputDeviceParameters GetDeviceEncodingOnlyOutputDeviceParameters(const FSceneViewFamily& Family);

struct FDeviceEncodingOnlyInputs
{
	// [Optional] Render to the specified output. If invalid, a new texture is created and returned.
	FScreenPassRenderTarget OverrideOutput;

	// [Required] HDR scene color to tonemap.
	FScreenPassTexture SceneColor;

	// Whether to leave the final output in HDR.
	bool bOutputInHDR = false;
};

FScreenPassTexture AddDeviceEncodingOnlyPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FDeviceEncodingOnlyInputs& Inputs);
