// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaSceneViewExtension.h"

#include "SceneView.h"


FImgMediaSceneViewExtension::FImgMediaSceneViewExtension(const FAutoRegister& AutoReg)
	: FSceneViewExtensionBase(AutoReg)
	, CachedCameraInfos()
	, LastFrameNumber(0)
{
}

void FImgMediaSceneViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
}

void FImgMediaSceneViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
}

void FImgMediaSceneViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (LastFrameNumber != InViewFamily.FrameNumber)
	{
		CachedCameraInfos.Reset();
		LastFrameNumber = InViewFamily.FrameNumber;
	}

	float ResolutionFraction = InViewFamily.SecondaryViewFraction;

	if (InViewFamily.GetScreenPercentageInterface())
	{
		ResolutionFraction *= InViewFamily.GetPrimaryResolutionFractionUpperBound();
	}

	static const auto CVarMinAutomaticViewMipBiasOffset = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.ViewTextureMipBias.Offset"));
	static const auto CVarMinAutomaticViewMipBias = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.ViewTextureMipBias.Min"));

	for (const FSceneView* View : InViewFamily.Views)
	{
		FImgMediaMipMapCameraInfo Info;
		Info.Location = View->ViewMatrices.GetViewOrigin();
		Info.ViewMatrix = View->ViewMatrices.GetViewMatrix();
		Info.ViewProjectionMatrix = View->ViewMatrices.GetViewProjectionMatrix();
		Info.ViewportRect = View->UnconstrainedViewRect.Scale(ResolutionFraction);

		// View->MaterialTextureMipBias is only set later in rendering: we replicate the logic here.
		if (View->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale)
		{
			Info.MaterialTextureMipBias = -(FMath::Max(-FMath::Log2(ResolutionFraction), 0.0f)) + CVarMinAutomaticViewMipBiasOffset->GetValueOnGameThread();
			Info.MaterialTextureMipBias = FMath::Max(Info.MaterialTextureMipBias, CVarMinAutomaticViewMipBias->GetValueOnGameThread());
		}
		else
		{
			Info.MaterialTextureMipBias = 0.0f;
		}

		CachedCameraInfos.Add(MoveTemp(Info));
	}
}