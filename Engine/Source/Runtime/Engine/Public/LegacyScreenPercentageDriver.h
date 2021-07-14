// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RendererPrivate.h: Renderer interface private definitions.
=============================================================================*/

#pragma once

#include "SceneView.h"


/**
 * Default screen percentage interface that just apply View->FinalPostProcessSettings.ScreenPercentage.
 */
class ENGINE_API FLegacyScreenPercentageDriver : public ISceneViewFamilyScreenPercentage
{
public:
	FORCEINLINE FLegacyScreenPercentageDriver(
		const FSceneViewFamily& InViewFamily,
		float InGlobalResolutionFraction)
		: FLegacyScreenPercentageDriver(InViewFamily, InGlobalResolutionFraction, InGlobalResolutionFraction)
	{ }

	FLegacyScreenPercentageDriver(
		const FSceneViewFamily& InViewFamily,
		float InGlobalResolutionFraction,
		float InGlobalResolutionFractionUpperBound);

	/** Gets the view rect fraction from the r.ScreenPercentage cvar. */
	static float GetCVarResolutionFraction();


private:
	// View family to take care of.
	const FSceneViewFamily& ViewFamily;

	// ViewRect fraction to apply to all view of the view family.
	const float GlobalResolutionFraction;

	// ViewRect fraction to apply to all view of the view family.
	const float GlobalResolutionFractionUpperBound;


	// Implements ISceneViewFamilyScreenPercentage
	virtual float GetPrimaryResolutionFractionUpperBound() const override;
	virtual float GetPrimaryResolutionFraction_RenderThread() const override;
	virtual ISceneViewFamilyScreenPercentage* Fork_GameThread(const class FSceneViewFamily& ForkedViewFamily) const override;
};
