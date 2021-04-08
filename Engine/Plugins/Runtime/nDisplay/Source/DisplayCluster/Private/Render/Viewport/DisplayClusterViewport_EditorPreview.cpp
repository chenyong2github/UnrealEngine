// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterPreviewComponent.h"
#include "Render/Viewport/DisplayClusterViewport.h"

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

#include "Misc/DisplayClusterLog.h"

#if WITH_EDITOR

#include "EngineModule.h"
#include "CanvasTypes.h"
#include "LegacyScreenPercentageDriver.h"
#include "SceneView.h"
#include "SceneViewExtension.h"

#include "Engine/Scene.h"
#include "GameFramework/WorldSettings.h"

#include "ScenePrivate.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"


#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewport.h"


FSceneView* FDisplayClusterViewport::ImplCalcScenePreview(FSceneViewFamilyContext& InOutViewFamily, uint32 InContextNum)
{
	check(InContextNum >= 0 && InContextNum < (uint32)Contexts.Num());

	const float WorldToMeters = 100.f;
	const float MaxViewDistance = 1000000;
	const bool bUseSceneColorTexture = false;
	const float LODDistanceFactor = 1.0f;

	static FPostProcessSettings PostProcessSettings;
	float PostProcessBlendWeight = 1.0f;

	const AActor* ViewOwner = nullptr;

	FVector  ViewLocation;
	FRotator ViewRotation;

	if (ImplPreview_CalculateStereoViewOffset(InContextNum, ViewRotation, WorldToMeters, ViewLocation))
	{
		FMatrix ProjectionMatrix = ImplPreview_GetStereoProjectionMatrix(InContextNum);

		FMatrix ViewRotationMatrix = FInverseRotationMatrix(ViewRotation);
		ViewRotationMatrix = ViewRotationMatrix * FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));

		float StereoIPD = 0.f;
		FIntRect ViewRect = Contexts[InContextNum].RenderTargetRect;

		FEngineShowFlags ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);

		FSceneViewInitOptions ViewInitOptions;

		ViewInitOptions.SetViewRectangle(ViewRect);
		ViewInitOptions.ViewFamily = &InOutViewFamily;
		ViewInitOptions.ViewActor = ViewOwner;

		ViewInitOptions.ViewOrigin = ViewLocation;
		ViewInitOptions.ViewRotationMatrix = ViewRotationMatrix;
		ViewInitOptions.ProjectionMatrix = ProjectionMatrix;

		ViewInitOptions.OverrideFarClippingPlaneDistance = MaxViewDistance;
		ViewInitOptions.StereoPass = Contexts[InContextNum].StereoscopicPass;

		ViewInitOptions.LODDistanceFactor = FMath::Clamp(LODDistanceFactor, 0.01f, 100.0f);

		if (InOutViewFamily.Scene->GetWorld() != nullptr && InOutViewFamily.Scene->GetWorld()->GetWorldSettings() != nullptr)
		{
			ViewInitOptions.WorldToMetersScale = InOutViewFamily.Scene->GetWorld()->GetWorldSettings()->WorldToMeters;
		}

		ViewInitOptions.StereoIPD = StereoIPD * (ViewInitOptions.WorldToMetersScale / 100.0f);

		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		//ViewInitOptions.OverlayColor = FLinearColor::Black;

		FSceneView* View = new FSceneView(ViewInitOptions);

		View->bIsSceneCapture = true;
		View->bSceneCaptureUsesRayTracing = false;
		View->bIsPlanarReflection = false;

		// Note: this has to be set before EndFinalPostprocessSettings
		// Needs to be reconfigured now that bIsPlanarReflection has changed.
		View->SetupAntiAliasingMethod();

		InOutViewFamily.Views.Add(View);

		View->StartFinalPostprocessSettings(ViewLocation);
		View->OverridePostProcessSettings(PostProcessSettings, PostProcessBlendWeight);
		View->EndFinalPostprocessSettings(ViewInitOptions);

		// Setup view extension for this view
		for (int ViewExt = 0; ViewExt < InOutViewFamily.ViewExtensions.Num(); ViewExt++)
		{
			InOutViewFamily.ViewExtensions[ViewExt]->SetupView(InOutViewFamily, *View);
		}

		return View;
	}

	return nullptr;
}

bool FDisplayClusterViewport::ImplPreview_CalculateStereoViewOffset(const uint32 InContextNum, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation)
{
	check(IsInGameThread());
	check(WorldToMeters > 0.f);

	if (!GetOwner().IsSceneOpened() || !ProjectionPolicy.IsValid())
	{
		return false;
	}

	// Get root actor from viewport
	ADisplayClusterRootActor* const RootActor = GetOwner().GetRootActor();
	if (!RootActor)
	{
		UE_LOG(LogDisplayClusterViewport, Warning, TEXT("No root actor found in game manager"));
		return false;
	}

	FDisplayClusterViewport_Context& InOutViewportContext = Contexts[InContextNum];

	UE_LOG(LogDisplayClusterViewport, VeryVerbose, TEXT("OLD ViewLoc: %s, ViewRot: %s"), *ViewLocation.ToString(), *ViewRotation.ToString());
	UE_LOG(LogDisplayClusterViewport, VeryVerbose, TEXT("WorldToMeters: %f"), WorldToMeters);


	// Get camera ID assigned to the viewport
	const FString& CameraId = RenderSettings.CameraId;

	// Get camera component assigned to the viewport (or default camera if nothing assigned)
	UDisplayClusterCameraComponent* const ViewCamera = (CameraId.IsEmpty() ? RootActor->GetDefaultCamera() : RootActor->GetCameraById(CameraId));
	if (!ViewCamera)
	{
		UE_LOG(LogDisplayClusterViewport, Warning, TEXT("No camera found for viewport '%s'"), *GetId());
		return false;
	}

	if (CameraId.Len() > 0)
	{
		UE_LOG(LogDisplayClusterViewport, Verbose, TEXT("Viewport '%s' has assigned camera '%s'"), *GetId(), *CameraId);
	}

	// Get the actual camera settings
	const float CfgEyeDist = ViewCamera->GetInterpupillaryDistance();
	const bool  bCfgEyeSwap = ViewCamera->GetSwapEyes();
	const float CfgNCP = 1.f;

	float PassOffset = 0.f;
	float PassOffsetSwap = 0.f;

	FVector ViewOffset = FVector::ZeroVector;
	if (ViewCamera)
	{
		// View base location
		ViewLocation = ViewCamera->GetComponentLocation();
		ViewRotation = ViewCamera->GetComponentRotation();
		// Apply computed offset to the view location
		const FQuat EyeQuat = ViewRotation.Quaternion();
		ViewOffset = EyeQuat.RotateVector(FVector(0.0f, PassOffsetSwap, 0.0f));
		ViewLocation += ViewOffset;
	}

	// Perform view calculations on a policy side
	if (!ProjectionPolicy->CalculateView(this, InContextNum, ViewLocation, ViewRotation, ViewOffset, WorldToMeters, CfgNCP, CfgNCP))
	{
		UE_LOG(LogDisplayClusterViewport, Warning, TEXT("Couldn't compute view parameters for Viewport %s, ViewIdx: %d"), *GetId(), InContextNum);
	}

	// Store the view location/rotation
	InOutViewportContext.ViewLocation = ViewLocation;
	InOutViewportContext.ViewRotation = ViewRotation;
	InOutViewportContext.WorldToMeters = WorldToMeters;

	UE_LOG(LogDisplayClusterViewport, VeryVerbose, TEXT("ViewLoc: %s, ViewRot: %s"), *ViewLocation.ToString(), *ViewRotation.ToString());

	return true;
}

FMatrix FDisplayClusterViewport::ImplPreview_GetStereoProjectionMatrix(const uint32 InContextNum)
{
	check(IsInGameThread());

	FMatrix PrjMatrix = FMatrix::Identity;

	if (GetOwner().IsSceneOpened() && ProjectionPolicy.IsValid())
	{
		FDisplayClusterViewport_Context& InOutViewportContext = GetContexts()[InContextNum];

		if (ProjectionPolicy->GetProjectionMatrix(this, InContextNum, PrjMatrix))
		{
			InOutViewportContext.ProjectionMatrix = PrjMatrix;
		}
		else
		{
			UE_LOG(LogDisplayClusterViewport, Warning, TEXT("Got invalid projection matrix: Viewport %s, ViewIdx: %d"), *GetId(), InContextNum);
		}
	}

	return PrjMatrix;
}

#endif
