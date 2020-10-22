// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRenderingManager.h"
#include "DisplayClusterRenderingViewExtension.h"
#include "DisplayClusterRenderingLog.h"

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

#include "EngineModule.h"
#include "CanvasTypes.h"
#include "LegacyScreenPercentageDriver.h"
#include "SceneView.h"
#include "SceneViewExtension.h"

#include "Engine/Scene.h"
#include "GameFramework/WorldSettings.h"

#include "ScenePrivate.h"


void FDisplayClusterRenderingManager::RenderSceneToTexture(const FDisplayClusterRenderingParameters& RenderInfo)
{
	check(RenderInfo.RenderTarget);

	const float MaxViewDistanceOverride = 1000000;
	const bool bUseSceneColorTexture = false;

	static FPostProcessSettings PostProcessSettings;
	float PostProcessBlendWeight = 1.0f;
	const AActor* ViewOwner = nullptr;

	FMatrix ViewRotationMatrix = FInverseRotationMatrix(RenderInfo.ViewRotation);

	ViewRotationMatrix = ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	FDisplayClusterRenderingViewParameters ViewInfo;
	ViewInfo.ViewRotationMatrix = ViewRotationMatrix;
	ViewInfo.ViewLocation = RenderInfo.ViewLocation;
	ViewInfo.ProjectionMatrix = RenderInfo.ProjectionMatrix;
	ViewInfo.StereoPass = EStereoscopicPass::eSSP_FULL;
	ViewInfo.StereoIPD = 0.f;
	ViewInfo.ViewRect = RenderInfo.RenderTargetRect;

	FEngineShowFlags ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);

	FSceneViewFamilyContext ViewFamily(
		FSceneViewFamily::ConstructionValues(RenderInfo.RenderTarget, RenderInfo.Scene->GetRenderScene(), ShowFlags)
		.SetResolveScene(!bUseSceneColorTexture)
		.SetRealtimeUpdate(false));

	if (RenderInfo.bAllowWarpBlend && RenderInfo.ProjectionPolicy && RenderInfo.ProjectionPolicy->IsWarpBlendSupported())
	{
		TSharedPtr<FDisplayClusterRenderingViewExtension, ESPMode::ThreadSafe> ViewExtension = AddViewExtension(RenderInfo.RenderTarget, RenderInfo.ProjectionPolicy);
		ViewFamily.ViewExtensions.Add(ViewExtension.ToSharedRef());
	}

	SetupViewFamily(
		ViewFamily,
		RenderInfo,
		MakeArrayView(&ViewInfo, 1),
		MaxViewDistanceOverride,
		bUseSceneColorTexture,
		false,
		&PostProcessSettings,
		PostProcessBlendWeight,
		ViewOwner);

	// Screen percentage is still not supported in scene capture.
	ViewFamily.EngineShowFlags.ScreenPercentage = false;
	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, 1.0f, false));

	ViewFamily.SceneCaptureSource = ESceneCaptureSource::SCS_SceneColorHDRNoAlpha;
	ViewFamily.SceneCaptureCompositeMode = ESceneCaptureCompositeMode::SCCM_Overwrite;
	ViewFamily.bResolveScene = true;

	FCanvas Canvas(RenderInfo.RenderTarget, nullptr, RenderInfo.Scene->GetWorld(), ERHIFeatureLevel::SM5, FCanvas::CDM_DeferDrawing /*FCanvas::CDM_ImmediateDrawing*/, 1.0f);
	Canvas.Clear(FLinearColor::Transparent);

	GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);
}

void FDisplayClusterRenderingManager::SetupViewFamily(
	FSceneViewFamily& ViewFamily,
	const FDisplayClusterRenderingParameters& PreviewRenderInfo,
	const TArrayView<FDisplayClusterRenderingViewParameters> Views,
	float MaxViewDistance,
	bool bCaptureSceneColor,
	bool bIsPlanarReflection,
	FPostProcessSettings* PostProcessSettings,
	float PostProcessBlendWeight,
	const AActor* ViewActor)
{
	check(!ViewFamily.GetScreenPercentageInterface());

	for (const FDisplayClusterRenderingViewParameters& ViewParams : Views)
	{
		FSceneViewInitOptions ViewInitOptions;

		ViewInitOptions.SetViewRectangle(ViewParams.ViewRect);
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.ViewActor = ViewActor;
		ViewInitOptions.ViewOrigin = ViewParams.ViewLocation;
		ViewInitOptions.ViewRotationMatrix = ViewParams.ViewRotationMatrix;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverrideFarClippingPlaneDistance = MaxViewDistance;
		ViewInitOptions.StereoPass = ViewParams.StereoPass;
		ViewInitOptions.ProjectionMatrix = ViewParams.ProjectionMatrix;
		ViewInitOptions.LODDistanceFactor = FMath::Clamp(PreviewRenderInfo.LODDistanceFactor, 0.01f, 100.0f);

		SetupViewVisibility(PreviewRenderInfo, ViewInitOptions);

		if (ViewFamily.Scene->GetWorld() != nullptr && ViewFamily.Scene->GetWorld()->GetWorldSettings() != nullptr)
		{
			ViewInitOptions.WorldToMetersScale = ViewFamily.Scene->GetWorld()->GetWorldSettings()->WorldToMeters;
		}

		ViewInitOptions.StereoIPD = ViewParams.StereoIPD * (ViewInitOptions.WorldToMetersScale / 100.0f);

		if (bCaptureSceneColor)
		{
			ViewFamily.EngineShowFlags.PostProcessing = 0;
			ViewInitOptions.OverlayColor = FLinearColor::Black;
		}

		FSceneView* View = new FSceneView(ViewInitOptions);

		View->bIsSceneCapture = true;
		View->bSceneCaptureUsesRayTracing = false;
		// Note: this has to be set before EndFinalPostprocessSettings
		View->bIsPlanarReflection = bIsPlanarReflection;
		// Needs to be reconfigured now that bIsPlanarReflection has changed.
		View->SetupAntiAliasingMethod();

		ViewFamily.Views.Add(View);

		View->StartFinalPostprocessSettings(ViewParams.ViewLocation);
		View->OverridePostProcessSettings(*PostProcessSettings, PostProcessBlendWeight);
		View->EndFinalPostprocessSettings(ViewInitOptions);
	}
}


void FDisplayClusterRenderingManager::SetupViewVisibility(const FDisplayClusterRenderingParameters& PreviewRenderInfo, FSceneViewInitOptions& ViewInitOptions)
{
	for (const AActor* Actor : PreviewRenderInfo.HiddenActors)
	{
		if (Actor)
		{
			for (const UActorComponent* Component : Actor->GetComponents())
			{
				if (const UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
				{
					ViewInitOptions.HiddenPrimitives.Add(PrimComp->ComponentId);
				}
			}
		}
	}
}

TSharedPtr<FDisplayClusterRenderingViewExtension, ESPMode::ThreadSafe> FDisplayClusterRenderingManager::AddViewExtension(FTextureRenderTargetResource* RenderTarget, IDisplayClusterProjectionPolicy* InProjectionPolicy)
{
	const TSharedPtr<FDisplayClusterRenderingViewExtension, ESPMode::ThreadSafe>* Extension =
	DisplayExtensions.FindByPredicate([RenderTarget, InProjectionPolicy](const TSharedPtr<FDisplayClusterRenderingViewExtension, ESPMode::ThreadSafe>& Item)
	{
		return  Item.IsValid() && RenderTarget == Item->GetAssociatedRTT() && InProjectionPolicy == Item->GetAssociatedProjection();
	});

	if (Extension)
	{
		return *Extension;
	}

	// Extension not found, create it and return its config
	TSharedPtr<FDisplayClusterRenderingViewExtension, ESPMode::ThreadSafe> DisplayExtension = FSceneViewExtensions::NewExtension<FDisplayClusterRenderingViewExtension>(RenderTarget, InProjectionPolicy);
	DisplayExtensions.Add(DisplayExtension);

	return DisplayExtension;
}

bool FDisplayClusterRenderingManager::RemoveViewExtension(FTextureRenderTargetResource* RenderTarget)
{
	return 0 < DisplayExtensions.RemoveAll([RenderTarget](const TSharedPtr<FDisplayClusterRenderingViewExtension, ESPMode::ThreadSafe>& Item)
	{
		return  Item.IsValid() && RenderTarget == Item->GetAssociatedRTT();
	});
}
