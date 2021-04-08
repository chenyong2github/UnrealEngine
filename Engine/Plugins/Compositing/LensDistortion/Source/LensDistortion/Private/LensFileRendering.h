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

	/** Center shift parameter to offset resulting map */
	FVector2D CenterShift = { 0.5f, 0.5f };
};

namespace LensFileRendering
{
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
