// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterMoviePipelineViewportPass.h"
#include "DisplayClusterMoviePipelineSettings.h"

#include "MoviePipeline.h"
#include "MoviePipelineHighResSetting.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Components/DisplayClusterCameraComponent.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewportManagerProxy.h"
#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace DisplayClusterMoviePipelineHelpers
{
	static void DisplayClusterWarpBlendImpl_RenderThread(FRHICommandListImmediate& RHICmdList, const FTextureRHIRef& RenderTargetRHI, IDisplayClusterViewportManagerProxy* InViewportManagerProxy, const FString& InViewportId, const FIntPoint& OffsetMin, const FIntPoint& OffsetMax)
	{
		if (InViewportManagerProxy)
		{
			if (IDisplayClusterViewportProxy* InViewportProxy = InViewportManagerProxy->FindViewport_RenderThread(InViewportId))
			{
				const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& InPolicy = InViewportProxy->GetProjectionPolicy_RenderThread();
				if (InPolicy.IsValid() && InPolicy->IsWarpBlendSupported())
				{
					TArray<FRHITexture2D*> WarpInputTextures, WarpOutputTextures;
					TArray<FIntRect> WarpInputRects, WarpOutputRects;

					if (InViewportProxy->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::InputShaderResource, WarpInputTextures, WarpInputRects))
					{
						if (InViewportProxy->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::AdditionalTargetableResource, WarpOutputTextures, WarpOutputRects))
						{
							const FIntRect RTTRect(FIntPoint(0, 0), RenderTargetRHI->GetSizeXY());
							const FIntRect WarpInputRect = WarpInputRects[0];
							const FIntRect WarpOutpuRect = WarpOutputRects[0];

							FRHICopyTextureInfo CopyInfo;
							CopyInfo.Size = FIntVector(
								RTTRect.Width() - OffsetMin.X - OffsetMax.X,
								RTTRect.Height() - OffsetMin.Y - OffsetMax.Y,
								0);
							{
								// Copy RTTRect->WarpInputRect
								CopyInfo.SourcePosition.X = RTTRect.Min.X + OffsetMin.X;
								CopyInfo.SourcePosition.Y = RTTRect.Min.Y + OffsetMin.Y;
								CopyInfo.DestPosition.X = WarpInputRect.Min.X;
								CopyInfo.DestPosition.Y = WarpInputRect.Min.Y;

								TransitionAndCopyTexture(RHICmdList, RenderTargetRHI, WarpInputTextures[0], CopyInfo);
							}

							// Apply Warp: WarpInputTextures->WarpOutputTextures
							InPolicy->ApplyWarpBlend_RenderThread(RHICmdList, InViewportProxy);

							{
								// Copy WarpOutputRect -> RTTRect
								CopyInfo.SourcePosition.X = WarpOutpuRect.Min.X;
								CopyInfo.SourcePosition.Y = WarpOutpuRect.Min.Y;
								CopyInfo.DestPosition.X = RTTRect.Min.X + OffsetMin.X;
								CopyInfo.DestPosition.Y = RTTRect.Min.Y + OffsetMin.Y;

								TransitionAndCopyTexture(RHICmdList, WarpOutputTextures[0], RenderTargetRHI, CopyInfo);
							}

							// Immediatelly flush RHI before PostRendererSubmission
							RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
						}
					}
				}
			}
		}
	}
};
using namespace DisplayClusterMoviePipelineHelpers;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UDisplayClusterMoviePipelineViewportPassBase
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UDisplayClusterMoviePipelineViewportPassBase::SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	InitializeDisplayCluster();

	Super::SetupImpl(InPassInitSettings);
}

void UDisplayClusterMoviePipelineViewportPassBase::TeardownImpl()
{
	ReleaseDisplayCluster();

	Super::TeardownImpl();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UE::MoviePipeline::FImagePassCameraViewData UDisplayClusterMoviePipelineViewportPassBase::GetCameraInfo(FMoviePipelineRenderPassMetrics& InOutSampleState, IViewCalcPayload* OptPayload) const
{
	UE::MoviePipeline::FImagePassCameraViewData OutCameraData;

	// This has to come from the main camera for consistency's sake, and it's not a per-camera setting in the editor.
	OutCameraData.ViewActor = GetPipeline()->GetWorld()->GetFirstPlayerController()->GetViewTarget();

	const int32 InViewportIndex = InOutSampleState.OutputState.CameraIndex;
	FString ViewportId;
	if (GetViewportId(InViewportIndex, ViewportId))
	{
		if (DCViews.Contains(InViewportIndex) && DCPrevViews.Contains(InViewportIndex))
		{
			// We override the current/previous transform
			OutCameraData.bUseCustomProjectionMatrix = true;
			OutCameraData.CustomProjectionMatrix = DCViews[InViewportIndex].ProjectionMatrix;

			OutCameraData.ViewInfo.Location = DCViews[InViewportIndex].ViewLocation;
			OutCameraData.ViewInfo.Rotation = DCViews[InViewportIndex].ViewRotation;

			OutCameraData.ViewInfo.PreviousViewTransform = FTransform(DCPrevViews[InViewportIndex].ViewRotation, DCPrevViews[InViewportIndex].ViewLocation);
		}
	}

	return OutCameraData;
}

void UDisplayClusterMoviePipelineViewportPassBase::BlendPostProcessSettings(FSceneView* InView, FMoviePipelineRenderPassMetrics& InOutSampleState, IViewCalcPayload* OptPayload)
{
	const int32 InViewportIndex = InOutSampleState.OutputState.CameraIndex;
	FString ViewportId;
	if (GetViewportId(InViewportIndex, ViewportId))
	{
		if (DCViews.Contains(InViewportIndex) && DCPrevViews.Contains(InViewportIndex))
		{
			// Use DCRA as a camera
			FVector ViewLocation = DCViews[InViewportIndex].ViewLocation;

			for (IInterface_PostProcessVolume* PPVolume : GetWorld()->PostProcessVolumes)
			{
				const FPostProcessVolumeProperties VolumeProperties = PPVolume->GetProperties();

				// Skip any volumes which are disabled
				if (!VolumeProperties.bIsEnabled)
				{
					continue;
				}

				float LocalWeight = FMath::Clamp(VolumeProperties.BlendWeight, 0.0f, 1.0f);

				if (!VolumeProperties.bIsUnbound)
				{
					float DistanceToPoint = 0.0f;
					PPVolume->EncompassesPoint(ViewLocation, 0.0f, &DistanceToPoint);

					if (DistanceToPoint >= 0 && DistanceToPoint < VolumeProperties.BlendRadius)
					{
						LocalWeight *= FMath::Clamp(1.0f - DistanceToPoint / VolumeProperties.BlendRadius, 0.0f, 1.0f);
					}
					else
					{
						LocalWeight = 0.0f;
					}
				}

				InView->OverridePostProcessSettings(*VolumeProperties.Settings, LocalWeight);
			}

			//@todo: It is necessary to discuss which post-processes from DCRA can or cannot be used for MRQ.
		}
	}
}

int32 UDisplayClusterMoviePipelineViewportPassBase::GetNumCamerasToRender() const
{
	return DisplayClusterViewports.Num();
}

FString UDisplayClusterMoviePipelineViewportPassBase::GetCameraName(const int32 InCameraIndex) const
{
	if (InCameraIndex >= 0 && InCameraIndex < DisplayClusterViewports.Num())
	{
		return DisplayClusterViewports[InCameraIndex];
	}

	return TEXT("UndefinedCameraName");
}

FString UDisplayClusterMoviePipelineViewportPassBase::GetCameraNameOverride(const int32 InCameraIndex) const
{
	if (DisplayClusterViewports.IsValidIndex(InCameraIndex))
	{
		return DisplayClusterViewports[InCameraIndex];
	}

	return TEXT("");
}

void UDisplayClusterMoviePipelineViewportPassBase::ModifyProjectionMatrixForTiling(const FMoviePipelineRenderPassMetrics& InSampleState, const bool bInOrthographic, FMatrix& InOutProjectionMatrix, float& OutDoFSensorScale) const
{
	const FMatrix InProjectionMatrix(InOutProjectionMatrix);
	Super::ModifyProjectionMatrixForTiling(InSampleState, bInOrthographic, InOutProjectionMatrix, OutDoFSensorScale);

	// Support asymmetric projection matrix (Rebuild assym projection matrix for tile + OverlappedPad)
	if(!bInOrthographic)
	{
		const FIntPoint TileCounts = InSampleState.TileCounts;
		const FIntPoint  TileSize = InSampleState.TileSize;

		const double   TileLeft = InSampleState.OverlappedOffset.X;
		const double  TileRight = TileLeft + TileSize.X + InSampleState.OverlappedPad.X * 2;

		const double    TileTop = InSampleState.OverlappedOffset.Y;
		const double TileBottom = TileTop + TileSize.Y + InSampleState.OverlappedPad.Y * 2;

		const double  Width = TileSize.X * TileCounts.X;
		const double Height = TileSize.Y * TileCounts.Y;

		const float ZNear = GNearClippingPlane;

		// Extract FOV values from matrix
		const double FOVWidth = (2.f * ZNear) / InProjectionMatrix.M[0][0];
		const double FOVHeight = (2.f * ZNear) / InProjectionMatrix.M[1][1];
		const double FOVOffsetX = InProjectionMatrix.M[2][0] * FOVWidth;
		const double FOVOffsetY = InProjectionMatrix.M[2][1] * FOVHeight;

		const double   FOVLeft = -(FOVOffsetX + FOVWidth) / 2.;
		const double  FOVRight = -FOVOffsetX - FOVLeft;
		const double FOVBottom = -(FOVOffsetY + FOVHeight) / 2.;
		const double    FOVTop = -(FOVOffsetY + FOVBottom);

		const FVector2D UnscaledFOV(FOVRight - FOVLeft, FOVBottom - FOVTop);
		const FVector2D FOVCenter(FOVLeft + UnscaledFOV.X * 0.5, FOVTop + UnscaledFOV.Y * 0.5);

		// Support camera overscan percentage
		const FVector2D OverscanFOV(UnscaledFOV * (1.0f + InSampleState.OverscanPercentage));
		const FVector2D FOVMin(FOVCenter.X - OverscanFOV.X * .5, FOVCenter.Y - OverscanFOV.Y * .5);

		// FOV values for Tile
		const double TileFOVLeft   = FOVMin.X + OverscanFOV.X * TileLeft / Width;
		const double TileFOVRight  = FOVMin.X + OverscanFOV.X* TileRight / Width;
		const double TileFOVTop    = FOVMin.Y + OverscanFOV.Y * TileTop / Height;
		const double TileFOVBottom = FOVMin.Y + OverscanFOV.Y * TileBottom / Height;

		InOutProjectionMatrix = IDisplayClusterViewport::MakeProjectionMatrix(TileFOVLeft, TileFOVRight, TileFOVTop, TileFOVBottom, ZNear, ZNear);
	}
}

FSceneView* UDisplayClusterMoviePipelineViewportPassBase::GetSceneViewForSampleState(FSceneViewFamily* ViewFamily, FMoviePipelineRenderPassMetrics& InOutSampleState, IViewCalcPayload* OptPayload)
{
	const int32 InViewportIndex = InOutSampleState.OutputState.CameraIndex;
	FString ViewportId;
	if (GetViewportId(InViewportIndex, ViewportId))
	{
		UMoviePipelineExecutorShot* CurrentShot = GetPipeline()->GetActiveShotList()[GetPipeline()->GetCurrentShotIndex()];
		UMoviePipelineHighResSetting* HighResSettings = GetPipeline()->FindOrAddSettingForShot<UMoviePipelineHighResSetting>(CurrentShot);

		bFrameWarpBlend = bEnabledWarpBlend;
		if (HighResSettings->TileCount > 1 && bFrameWarpBlend)
		{
			// WarpBlend isn't supported when using tiled rendering
			bFrameWarpBlend = false;
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("DCRA WarpBlend isn't supported when using tiling! (Viewport '%s')"), *ViewportId);
		}

		FIntPoint OffsetMin = FIntPoint::ZeroValue;
		FIntPoint OffsetMax = FIntPoint::ZeroValue;
		GetViewportCutOffset(InOutSampleState, OffsetMin, OffsetMax);

		const FIntPoint FullDestSize(InOutSampleState.TileSize.X, InOutSampleState.TileSize.Y);
		const FIntPoint DestSize = FullDestSize - OffsetMin - OffsetMax;

		FDisplayClusterViewInfo NewView, PrevView;
		if (CalculateDisplayClusterViewport(InOutSampleState, *ViewportId, DestSize, 0, NewView))
		{
			// Update view cache
			if (!DCViews.Contains(InViewportIndex))
			{
				DCViews.Emplace(InViewportIndex, NewView);
			}

			DCPrevViews.Emplace(InViewportIndex, DCViews[InViewportIndex]);
			DCViews.Emplace(InViewportIndex, NewView);
		}
		else
		{
			UE_LOG(LogMovieRenderPipeline, Error, TEXT("DCRA render failed for viewport '%s')"), *ViewportId);
		}
	}
	else
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("DCRA render failed: undefined viewport index, skipped"));
	}

	return Super::GetSceneViewForSampleState(ViewFamily, InOutSampleState, OptPayload);
}

void UDisplayClusterMoviePipelineViewportPassBase::PostRendererSubmission(const FMoviePipelineRenderPassMetrics& InSampleState, const FMoviePipelinePassIdentifier InPassIdentifier, const int32 InSortingOrder, FCanvas& InCanvas)
{
	if (bFrameWarpBlend)
	{
		const int32 InViewportIndex = InSampleState.OutputState.CameraIndex;
		FString ViewportId;
		if (GetViewportId(InViewportIndex, ViewportId))
		{
			if (FRenderTarget* RenderTarget = InCanvas.GetRenderTarget())
			{
				if (IDisplayClusterViewportManager* ViewportManager = DisplayClusterRootActorPtr->GetViewportManager())
				{
					FIntPoint OffsetMin = FIntPoint::ZeroValue;
					FIntPoint OffsetMax = FIntPoint::ZeroValue;
					GetViewportCutOffset(InSampleState, OffsetMin, OffsetMax);

					ENQUEUE_RENDER_COMMAND(DisplayClusterMoviePipelineWarp)([
						InTextureRHIRef = RenderTarget->GetRenderTargetTexture(),
						ViewportManagerProxy = ViewportManager->GetProxy(),
						InViewportId = ViewportId,
						OffsetMin, OffsetMax](FRHICommandListImmediate& RHICmdList) mutable
					{
						DisplayClusterWarpBlendImpl_RenderThread(RHICmdList, InTextureRHIRef, ViewportManagerProxy, InViewportId, OffsetMin, OffsetMax);
					});
				}
			}
		}
	}

	// Use Deffered render pass
	Super::PostRendererSubmission(InSampleState, InPassIdentifier, InSortingOrder, InCanvas);
}

void UDisplayClusterMoviePipelineViewportPassBase::GetViewportCutOffset(const FMoviePipelineRenderPassMetrics& InSampleState, FIntPoint& OutOffsetMin, FIntPoint& OutOffsetMax) const
{
	// Difference between the clipped constrained rect and the tile rect
	OutOffsetMin = FIntPoint::ZeroValue;
	OutOffsetMax = FIntPoint::ZeroValue;

	APlayerCameraManager* PlayerCameraManager = GetPipeline()->GetWorld()->GetFirstPlayerController()->PlayerCameraManager;
	if (PlayerCameraManager && PlayerCameraManager->GetCameraCacheView().bConstrainAspectRatio)
	{
		const FMinimalViewInfo CameraCache = PlayerCameraManager->GetCameraCacheView();

		// Taking overscan into account.
		FIntPoint FullOutputSize = UMoviePipelineBlueprintLibrary::GetEffectiveOutputResolution(GetPipeline()->GetPipelineMasterConfig(), GetPipeline()->GetActiveShotList()[GetPipeline()->GetCurrentShotIndex()]);

		float OutputSizeAspectRatio = FullOutputSize.X / (float)FullOutputSize.Y;
		const FIntPoint ConstrainedFullSize = CameraCache.AspectRatio > OutputSizeAspectRatio ?
			FIntPoint(FullOutputSize.X, FMath::CeilToInt((double)FullOutputSize.X / (double)CameraCache.AspectRatio)) :
			FIntPoint(FMath::CeilToInt(CameraCache.AspectRatio * FullOutputSize.Y), FullOutputSize.Y);

		const FIntPoint TileViewMin = InSampleState.OverlappedOffset;
		const FIntPoint TileViewMax = TileViewMin + InSampleState.BackbufferSize;

		// Camera ratio constrained rect, clipped by the tile rect
		FIntPoint ConstrainedViewMin = (FullOutputSize - ConstrainedFullSize) / 2;
		FIntPoint ConstrainedViewMax = ConstrainedViewMin + ConstrainedFullSize;
		ConstrainedViewMin = FIntPoint(FMath::Clamp(ConstrainedViewMin.X, TileViewMin.X, TileViewMax.X),
			FMath::Clamp(ConstrainedViewMin.Y, TileViewMin.Y, TileViewMax.Y));
		ConstrainedViewMax = FIntPoint(FMath::Clamp(ConstrainedViewMax.X, TileViewMin.X, TileViewMax.X),
			FMath::Clamp(ConstrainedViewMax.Y, TileViewMin.Y, TileViewMax.Y));

		// Difference between the clipped constrained rect and the tile rect
		OutOffsetMin = ConstrainedViewMin - TileViewMin;
		OutOffsetMax = TileViewMax - ConstrainedViewMax;

		// Support camera overscan
		const float BorderRatio = FMath::Clamp((InSampleState.OverscanPercentage / (1.0f + InSampleState.OverscanPercentage)) / 2.0f, 0.0f, 0.5f);
		const FIntPoint InnerSize = FullOutputSize - OutOffsetMin - OutOffsetMax;
		const int32 OverscanBorderWidth = FMath::RoundToInt(InnerSize.X* BorderRatio);
		const int32 OverscanBorderHeight = FMath::RoundToInt(InnerSize.Y * BorderRatio);

		OutOffsetMin.X += OverscanBorderWidth;
		OutOffsetMin.Y += OverscanBorderHeight;
		OutOffsetMax.X += OverscanBorderWidth;
		OutOffsetMax.Y += OverscanBorderHeight;
	}
}

ADisplayClusterRootActor* UDisplayClusterMoviePipelineViewportPassBase::GetRootActor(const UDisplayClusterMoviePipelineSettings& InDCSettings)
{
	if (DisplayClusterRootActorPtr)
	{
		const bool bWorldEqual = DisplayClusterRootActorPtr->GetWorld() == GetWorld();
		if (!bWorldEqual)
		{
			DisplayClusterRootActorPtr = nullptr;
		}
	}

	if (DisplayClusterRootActorPtr == nullptr)
	{
		DisplayClusterRootActorPtr = InDCSettings.GetRootActor(GetWorld());
	}

	return DisplayClusterRootActorPtr;
}

bool UDisplayClusterMoviePipelineViewportPassBase::InitializeDisplayCluster()
{
	// Reset all runtime values
	{
		DCViews.Empty();
		DCPrevViews.Empty();

		bFrameInitialized = false;
		bFrameWarpBlend = false;

		DisplayClusterRootActorPtr = nullptr;

		bDisplayClusterInitialized = false;

		DisplayClusterViewports.Empty();
	}

	UMoviePipelineExecutorShot* CurrentShot = GetPipeline()->GetActiveShotList()[GetPipeline()->GetCurrentShotIndex()];
	UDisplayClusterMoviePipelineSettings* DisplayClusterCSettings = GetPipeline()->FindOrAddSettingForShot<UDisplayClusterMoviePipelineSettings>(CurrentShot);

	// Initialize
	if (DisplayClusterCSettings && GetRootActor(*DisplayClusterCSettings) && DisplayClusterCSettings->GetViewports(GetWorld(), DisplayClusterViewports))
	{
		bDisplayClusterInitialized = true;

		return true;
	}

	UE_LOG(LogMovieRenderPipeline, Error, TEXT("Failed to find nDisplay viewports"));

	return false;
}

void UDisplayClusterMoviePipelineViewportPassBase::ReleaseDisplayCluster()
{
	bDisplayClusterInitialized = false;
	DisplayClusterViewports.Empty();
	DCViews.Empty();
}

bool UDisplayClusterMoviePipelineViewportPassBase::GetViewportId(int32 InViewportIndex, FString& OutViewportId) const
{
	if (bDisplayClusterInitialized && InViewportIndex >= 0 && InViewportIndex < DisplayClusterViewports.Num())
	{
		OutViewportId = DisplayClusterViewports[InViewportIndex];

		return true;
	}

	return false;
}

bool UDisplayClusterMoviePipelineViewportPassBase::CalculateDisplayClusterViewport(const FMoviePipelineRenderPassMetrics& InSampleState, const FString& InViewportId, const FIntPoint& InDestSize, const uint32 InContextNum, FDisplayClusterViewInfo& OutView)
{
	const EDisplayClusterRenderFrameMode RenderFrameMode = EDisplayClusterRenderFrameMode::Mono;

	bFrameInitialized = false;
	if (IDisplayClusterViewportManager* ViewportManager = DisplayClusterRootActorPtr->GetViewportManager())
	{
		if (UWorld* InWorld = GetWorld())
		{
			const AWorldSettings* WorldSettings = InWorld->GetWorldSettings();
			const float InWorldToMeters = (WorldSettings) ? WorldSettings->WorldToMeters : 100.f;

			if (UGameViewportClient* GameViewportClient = InWorld->GetGameViewport())
			{
				// Update local node viewports (update\create\delete) and build new render frame
				if (ViewportManager->UpdateCustomConfiguration(RenderFrameMode, DisplayClusterViewports, DisplayClusterRootActorPtr))
				{
					// Force custom viewport size and settings:
					if (IDisplayClusterViewport* ViewportPtr = ViewportManager->FindViewport(InViewportId))
					{
						FDisplayClusterViewport_RenderSettings RenderSettings;
						RenderSettings.Rect = FIntRect(FIntPoint(0, 0), InDestSize);
						RenderSettings.CaptureMode = EDisplayClusterViewportCaptureMode::MoviePipeline;

						if (!bFrameWarpBlend)
						{
							// Create internal resources only when warpblend will be used
							RenderSettings.bSkipRendering = true;
						}

						// Disable DCRA frustum modifier features
						RenderSettings.bDisableCustomFrustumFeature = true;
						RenderSettings.bDisableFrustumOverscanFeature = true;

						ViewportPtr->SetRenderSettings(RenderSettings);

						FDisplayClusterRenderFrame RenderFrame;
						if (ViewportManager->BeginNewFrame(GameViewportClient->Viewport, InWorld, RenderFrame))
						{
							// InDestSize
							bFrameInitialized = true;

							if (bFrameWarpBlend)
							{
								// Initialize proxy only when warpblend will be used
								ViewportManager->FinalizeNewFrame();
							}

							if (!ViewportPtr->GetProjectionPolicy().IsValid())
							{
								// ignore viewports with uninitialized prj policy
								return false;
							}

							const TArray<FDisplayClusterViewport_Context>& ViewportContexts = ViewportPtr->GetContexts();
							const FDisplayClusterViewport_Context& ViewportContext = ViewportContexts[InContextNum];

							UDisplayClusterCameraComponent* ViewCamera = nullptr;
							{
								// Get camera ID assigned to the viewport
								const FString& CameraId = ViewportPtr->GetRenderSettings().CameraId;

								// Get camera component assigned to the viewport (or default camera if nothing assigned)
								ViewCamera = (CameraId.IsEmpty() ?
									DisplayClusterRootActorPtr->GetDefaultCamera() :
									DisplayClusterRootActorPtr->GetComponentByName<UDisplayClusterCameraComponent>(CameraId));
							}

							if (!ViewCamera)
							{
								UE_LOG(LogMovieRenderPipeline, Error, TEXT("No camera found for viewport '%s'"), *ViewportPtr->GetId());
								return false;
							}

							// Get the actual camera settings
							const float CfgEyeDist = ViewCamera->GetInterpupillaryDistance();
							const bool  CfgEyeSwap = ViewCamera->GetSwapEyes();
							const float CfgNCP = GNearClippingPlane;
							const EDisplayClusterEyeStereoOffset CfgEyeOffset = ViewCamera->GetStereoOffset();

							// Calculate eye offset considering the world scale
							const float EyeOffset = CfgEyeDist / 2.f;
							const float EyeOffsetValues[] = { -EyeOffset, 0.f, EyeOffset };

							enum class EDisplayClusterEyeType : uint8
							{
								StereoLeft = 0,
								Mono = 1,
								StereoRight = 2,
								COUNT
							};

							// Decode current eye type
							const EDisplayClusterEyeType DCEyeType = (ViewportContexts.Num() > 1)
								? ((InContextNum == 0) ? EDisplayClusterEyeType::StereoLeft : EDisplayClusterEyeType::StereoRight)
								: EDisplayClusterEyeType::Mono;

							const int32 EyeIndex = (uint8)DCEyeType;

							float PassOffset = 0.f;
							float PassOffsetSwap = 0.f;

							switch (DCEyeType)
							{
							case EDisplayClusterEyeType::Mono:
							{
								// For monoscopic camera let's check if the "force offset" feature is used
								// * Force left (-1) ==> 0 left eye
								// * Force right (1) ==> 2 right eye
								// * Default (0) ==> 1 mono
								const int32 EyeOffsetIdx = (CfgEyeOffset == EDisplayClusterEyeStereoOffset::None ? 0 : (CfgEyeOffset == EDisplayClusterEyeStereoOffset::Left ? -1 : 1));

								PassOffset = EyeOffsetValues[EyeOffsetIdx + 1];
								// Eye swap is not available for monoscopic so just save the value
								PassOffsetSwap = PassOffset;
							}
							break;

							default:
							{
								// For stereo camera we can only swap eyes if required (no "force offset" allowed)
								PassOffset = EyeOffsetValues[EyeIndex];
								PassOffsetSwap = (CfgEyeSwap ? -PassOffset : PassOffset);
							}
							break;
							}

							FVector ViewOffset = FVector::ZeroVector;
							if (ViewCamera)
							{
								// View base location
								OutView.ViewLocation = ViewCamera->GetComponentLocation();
								OutView.ViewRotation = ViewCamera->GetComponentRotation();
								// Apply computed offset to the view location
								const FQuat EyeQuat = OutView.ViewRotation.Quaternion();
								ViewOffset = EyeQuat.RotateVector(FVector(0.0f, PassOffsetSwap, 0.0f));
								OutView.ViewLocation += ViewOffset;
							}

							// Perform view calculations on a policy side
							if (ViewportPtr->CalculateView(InContextNum, OutView.ViewLocation, OutView.ViewRotation, ViewOffset, InWorldToMeters, CfgNCP, CfgNCP))
							{
								OutView.ProjectionMatrix = FMatrix::Identity;

								return ViewportPtr->GetOwner().IsSceneOpened() && ViewportPtr->GetProjectionMatrix(InContextNum, OutView.ProjectionMatrix);
							}
						}
					}
				}
			}
		}
	}

	return false;
}
