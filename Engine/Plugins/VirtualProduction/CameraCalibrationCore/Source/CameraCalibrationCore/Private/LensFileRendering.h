// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/Vector2D.h"

class UTextureRenderTarget2D;

/** Types of blending used which drives permutation */
enum class EDisplacementMapBlendType :uint8
{
	Passthrough,
	Linear,
	Bilinear,
};

/** Single struct containing blending params for all types */
struct FDisplacementMapBlendingParams
{
	/** Active type of blending */
	EDisplacementMapBlendType BlendType = EDisplacementMapBlendType::Passthrough;

	/** Linear blend parameter */
	float LinearBlendFactor = 0.0f;

	/** Bilinear blend parameters */
	float MainCoefficient = 0.0f;
	float DeltaMinX = 0.0f;
	float DeltaMaxX = 0.0f;
	float DeltaMinY = 0.0f;
	float DeltaMaxY = 0.0f;

	/** Scale parameter that allows displacement maps for one sensor size to be applied to camera's with a different sensor size */
	FVector2D FxFyScale = { 1.0f, 1.0f };

	/** Image center parameter to compute center shift needed to offset resulting map */
	FVector2D PrincipalPoint = { 0.5f, 0.5f };
};

namespace LensFileRendering
{
	/** Clears the given render target. Useful when no distortion can be applied and the RT has to be resetted */
	void ClearDisplacementMap(UTextureRenderTarget2D* OutRenderTarget);

	/**
	 * Draws the blended result of displacement map from input textures based on blend parameters
	 * One texture is always needed to do a passthrough. Up to four textures can be blended using bilinear
	 */
	bool DrawBlendedDisplacementMap(UTextureRenderTarget2D* OutRenderTarget
		, const FDisplacementMapBlendingParams& BlendParams
		, UTextureRenderTarget2D* SourceTextureOne
		, UTextureRenderTarget2D* SourceTextureTwo = nullptr
		, UTextureRenderTarget2D* SourceTextureThree = nullptr
		, UTextureRenderTarget2D* SourceTextureFour = nullptr);
}
