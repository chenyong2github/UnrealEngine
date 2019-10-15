// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OverridePassSequence.h"
#include "PostProcess/RenderingCompositionGraph.h"

struct FPaniniProjectionConfig
{
	static const FPaniniProjectionConfig Default;

	FPaniniProjectionConfig() = default;
	FPaniniProjectionConfig(const FViewInfo& View);

	bool IsEnabled() const
	{
		return D > 0.01f;
	}

	void Sanitize()
	{
		D = FMath::Max(D, 0.0f);
		ScreenFit = FMath::Max(ScreenFit, 0.0f);
	}

	// 0=none..1=full, must be >= 0.
	float D = 0.0f;

	// Panini hard vertical compression lerp (0=no vertical compression, 1=hard compression).
	float S = 0.0f;

	// Panini screen fit factor (lerp between vertical and horizontal).
	float ScreenFit = 1.0f;
};

enum class EUpscaleMethod : uint8
{
	Nearest,
	Bilinear,
	Directional,
	CatmullRom,
	Lanczos,
	Gaussian,
	SmoothStep,
	MAX
};

EUpscaleMethod GetUpscaleMethod();

enum class EUpscaleStage
{
	// Upscaling from the primary to the secondary view rect. The override output cannot be valid when using this stage.
	PrimaryToSecondary,

	// Upscaling in one pass to the final target size.
	PrimaryToOutput,

	// Upscaling from the secondary view rect to the final view size.
	SecondaryToOutput,

	MAX
};

struct FUpscaleInputs
{
	// [Optional] Render to the specified output. If invalid, a new texture is created and returned.
	FScreenPassRenderTarget OverrideOutput;

	// [Required] The input scene color and view rect.
	FScreenPassTexture SceneColor;

	// [Required] The method to use when upscaling.
	EUpscaleMethod Method = EUpscaleMethod::MAX;

	// [Optional] A configuration used to control Panini projection. Disabled in the default state.
	FPaniniProjectionConfig PaniniConfig;

	// Whether this is a secondary upscale to the final view family target.
	EUpscaleStage Stage = EUpscaleStage::MAX;
};

FScreenPassTexture AddUpscalePass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FUpscaleInputs& Inputs);

FRenderingCompositeOutputRef AddUpscalePass(FRenderingCompositionGraph& Graph, FRenderingCompositeOutputRef Input, EUpscaleMethod Method, EUpscaleStage Stage);