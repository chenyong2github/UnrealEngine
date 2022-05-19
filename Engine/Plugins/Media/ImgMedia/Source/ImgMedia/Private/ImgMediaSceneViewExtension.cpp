// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaSceneViewExtension.h"

#include "SceneView.h"


FImgMediaSceneViewExtension::FImgMediaSceneViewExtension(const FAutoRegister& AutoReg)
	: FSceneViewExtensionBase(AutoReg)
	, CachedViewInfos()
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
		CachedViewInfos.Reset();
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
		FImgMediaViewInfo Info;
		Info.Location = View->ViewMatrices.GetViewOrigin();
		Info.ViewMatrix = View->ViewMatrices.GetViewMatrix();
		Info.ViewProjectionMatrix = View->ViewMatrices.GetViewProjectionMatrix();
		Info.ViewportRect = View->UnconstrainedViewRect.Scale(ResolutionFraction);

		// We store hidden or show-only ids to later avoid needless calculations when objects are not in view.
		if (View->ShowOnlyPrimitives.IsSet())
		{
			Info.bPrimitiveHiddenMode = false;
			Info.PrimitiveComponentIds = View->ShowOnlyPrimitives.GetValue();
		}
		else
		{
			Info.bPrimitiveHiddenMode = true;
			Info.PrimitiveComponentIds = View->HiddenPrimitives;
		}

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

		CachedViewInfos.Add(MoveTemp(Info));
	}
}

int32 FImgMediaSceneViewExtension::GetPriority() const
{
	// Lowest priority value to ensure all other extensions are executed before ours.
	return MIN_int32;
}
