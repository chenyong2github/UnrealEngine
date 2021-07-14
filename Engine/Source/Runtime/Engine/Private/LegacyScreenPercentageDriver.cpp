// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Renderer.cpp: Renderer module implementation.
=============================================================================*/

#include "LegacyScreenPercentageDriver.h"
#include "UnrealEngine.h"


FLegacyScreenPercentageDriver::FLegacyScreenPercentageDriver(
	const FSceneViewFamily& InViewFamily,
	float InGlobalResolutionFraction,
	float InGlobalResolutionFractionUpperBound)
	: ViewFamily(InViewFamily)
	, GlobalResolutionFraction(InGlobalResolutionFraction)
	, GlobalResolutionFractionUpperBound(InGlobalResolutionFractionUpperBound)
{
	if (GlobalResolutionFraction != 1.0f)
	{
		check(ViewFamily.EngineShowFlags.ScreenPercentage);
	}
}

float FLegacyScreenPercentageDriver::GetPrimaryResolutionFractionUpperBound() const
{
	if (!ViewFamily.EngineShowFlags.ScreenPercentage)
	{
		return 1.0f;
	}

	return FMath::Clamp(
		GlobalResolutionFractionUpperBound,
		ISceneViewFamilyScreenPercentage::kMinResolutionFraction,
		ISceneViewFamilyScreenPercentage::kMaxResolutionFraction);
}

float FLegacyScreenPercentageDriver::GetPrimaryResolutionFraction_RenderThread() const
{
	check(IsInRenderingThread());

	// Early return if no screen percentage should be done.
	if (!ViewFamily.EngineShowFlags.ScreenPercentage)
	{
		return 1.0f;
	}

	return FMath::Clamp(
		GlobalResolutionFraction,
		ISceneViewFamilyScreenPercentage::kMinResolutionFraction,
		ISceneViewFamilyScreenPercentage::kMaxResolutionFraction);
}

ISceneViewFamilyScreenPercentage* FLegacyScreenPercentageDriver::Fork_GameThread(
	const FSceneViewFamily& ForkedViewFamily) const
{
	check(IsInGameThread());

	return new FLegacyScreenPercentageDriver(
		ForkedViewFamily, GlobalResolutionFraction, GlobalResolutionFractionUpperBound);
}

// static
float FLegacyScreenPercentageDriver::GetCVarResolutionFraction()
{
	check(IsInGameThread());
	static const auto ScreenPercentageCVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.ScreenPercentage"));

	float GlobalFraction = ScreenPercentageCVar->GetValueOnAnyThread() / 100.0f;
	if (GlobalFraction <= 0.0)
	{
		GlobalFraction = 1.0f;
	}

	return GlobalFraction;
}
