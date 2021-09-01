// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineImagePassBase.h"

// For Cine Camera Variables in Metadata
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MoviePipeline.h"
#include "GameFramework/PlayerController.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineViewFamilySetting.h"
#include "MoviePipelineQueue.h"
#include "LegacyScreenPercentageDriver.h"
#include "MoviePipelineMasterConfig.h"
#include "EngineModule.h"
#include "Engine/LocalPlayer.h"
#include "Engine/RendererSettings.h"

// For Cine Camera Variables in Metadata
#include "CineCameraActor.h"
#include "CineCameraComponent.h"

void UMoviePipelineImagePassBase::GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const
{
	OutShowFlag = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
	OutViewModeIndex = EViewModeIndex::VMI_Lit;
}

void UMoviePipelineImagePassBase::SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	Super::SetupImpl(InPassInitSettings);

	// Allocate 
	ViewState.Allocate();
}

void UMoviePipelineImagePassBase::TeardownImpl()
{
	FSceneViewStateInterface* Ref = ViewState.GetReference();
	if (Ref)
	{
		Ref->ClearMIDPool();
	}
	ViewState.Destroy();

	Super::TeardownImpl();
}

void UMoviePipelineImagePassBase::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UMoviePipelineImagePassBase& This = *CastChecked<UMoviePipelineImagePassBase>(InThis);
	FSceneViewStateInterface* Ref = This.ViewState.GetReference();
	if (Ref)
	{
		Ref->AddReferencedObjects(Collector);
	}
}

TSharedPtr<FSceneViewFamilyContext> UMoviePipelineImagePassBase::CalculateViewFamily(FMoviePipelineRenderPassMetrics& InOutSampleState)
{
	const FMoviePipelineFrameOutputState::FTimeData& TimeData = InOutSampleState.OutputState.TimeData;

	FEngineShowFlags ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
	EViewModeIndex  ViewModeIndex;
	GetViewShowFlags(ShowFlags, ViewModeIndex);
	MoviePipelineRenderShowFlagOverride(ShowFlags);
	FRenderTarget* RenderTarget = GetViewRenderTarget()->GameThread_GetRenderTargetResource();

	TSharedPtr<FSceneViewFamilyContext> OutViewFamily = MakeShared<FSceneViewFamilyContext>(FSceneViewFamily::ConstructionValues(
		RenderTarget,
		GetPipeline()->GetWorld()->Scene,
		ShowFlags)
		.SetWorldTimes(TimeData.WorldSeconds, TimeData.FrameDeltaTime, TimeData.WorldSeconds)
		.SetRealtimeUpdate(true));

	OutViewFamily->SceneCaptureSource = InOutSampleState.SceneCaptureSource;
	OutViewFamily->bWorldIsPaused = InOutSampleState.bWorldIsPaused;
	OutViewFamily->ViewMode = ViewModeIndex;
	EngineShowFlagOverride(ESFIM_Game, OutViewFamily->ViewMode, OutViewFamily->EngineShowFlags, false);
	
	const UMoviePipelineExecutorShot* Shot = GetPipeline()->GetActiveShotList()[InOutSampleState.OutputState.ShotIndex];
	
	// No need to do anything if screen percentage is not supported. 
	if (IsScreenPercentageSupported())
	{
		// Allows all Output Settings to have an access to View Family. This allows to modify rendering output settings.
		for (UMoviePipelineViewFamilySetting* Setting : GetPipeline()->FindSettingsForShot<UMoviePipelineViewFamilySetting>(Shot))
		{
			Setting->SetupViewFamily(*OutViewFamily);
		}
	}

	// If UMoviePipelineViewFamilySetting never set a Screen percentage interface we fallback to default.
	if (OutViewFamily->GetScreenPercentageInterface() == nullptr)
	{
		OutViewFamily->SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(*OutViewFamily, IsScreenPercentageSupported() ? InOutSampleState.GlobalScreenPercentageFraction : 1.f, IsScreenPercentageSupported()));
	}


	// View is added as a child of the OutViewFamily-> 
	FSceneView* View = GetSceneViewForSampleState(OutViewFamily.Get(), /*InOut*/ InOutSampleState);
#if RHI_RAYTRACING
	View->SetupRayTracedRendering();
#endif
	
	SetupViewForViewModeOverride(View);

	// Override the view's FrameIndex to be based on our progress through the sequence. This greatly increases
	// determinism with things like TAA.
	View->OverrideFrameIndexValue = InOutSampleState.FrameIndex;
	View->bCameraCut = InOutSampleState.bCameraCut;
	View->bIsOfflineRender = true;
	View->AntiAliasingMethod = IsAntiAliasingSupported() ? InOutSampleState.AntiAliasingMethod : EAntiAliasingMethod::AAM_None;

	// Override the Motion Blur settings since these are controlled by the movie pipeline.
	{
		FFrameRate OutputFrameRate = GetPipeline()->GetPipelineMasterConfig()->GetEffectiveFrameRate(GetPipeline()->GetTargetSequence());

		// We need to inversly scale the target FPS by time dilation to counteract slowmo. If scaling isn't applied then motion blur length
		// stays the same length despite the smaller delta time and the blur ends up too long.
		View->FinalPostProcessSettings.MotionBlurTargetFPS = FMath::RoundToInt(OutputFrameRate.AsDecimal() / FMath::Max(SMALL_NUMBER, InOutSampleState.OutputState.TimeData.TimeDilation));
		View->FinalPostProcessSettings.MotionBlurAmount = InOutSampleState.OutputState.TimeData.MotionBlurFraction;
		View->FinalPostProcessSettings.MotionBlurMax = 100.f;
		View->FinalPostProcessSettings.bOverride_MotionBlurAmount = true;
		View->FinalPostProcessSettings.bOverride_MotionBlurTargetFPS = true;
		View->FinalPostProcessSettings.bOverride_MotionBlurMax = true;

		// Skip the whole pass if they don't want motion blur.
		if (FMath::IsNearlyZero(InOutSampleState.OutputState.TimeData.MotionBlurFraction))
		{
			OutViewFamily->EngineShowFlags.SetMotionBlur(false);
		}
	}

	// Locked Exposure
	{
		if (InOutSampleState.GetTileCount() > 1 && (View->FinalPostProcessSettings.AutoExposureMethod != EAutoExposureMethod::AEM_Manual))
		{
			const URendererSettings* RenderSettings = GetDefault<URendererSettings>();
			if (RenderSettings->bDefaultFeatureAutoExposure != false)
			{
				// Auto exposure is not allowed
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("AutoExposure Method should always be Manual when using tiling!"));
				View->FinalPostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			}
		}
	}

	OutViewFamily->ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(FSceneViewExtensionContext(GetWorld()->Scene));

	AddViewExtensions(*OutViewFamily, InOutSampleState);

	for (auto ViewExt : OutViewFamily->ViewExtensions)
	{
		ViewExt->SetupViewFamily(*OutViewFamily.Get());
	}

	for (int ViewExt = 0; ViewExt < OutViewFamily->ViewExtensions.Num(); ViewExt++)
	{
		OutViewFamily->ViewExtensions[ViewExt]->SetupView(*OutViewFamily.Get(), *View);
	}

	// Anti Aliasing
	{
		// If we're not using Temporal Anti-Aliasing or Path Tracing we will apply the View Matrix projection jitter. Normally TAA sets this
		// inside FSceneRenderer::PreVisibilityFrameSetup. Path Tracing does its own anti-aliasing internally.
		if (View->AntiAliasingMethod != EAntiAliasingMethod::AAM_TemporalAA && !OutViewFamily->EngineShowFlags.PathTracing)
		{
			View->ViewMatrices.HackAddTemporalAAProjectionJitter(InOutSampleState.ProjectionMatrixJitterAmount);
		}
	}

	// Path Tracer Sampling
	if (OutViewFamily->EngineShowFlags.PathTracing)
	{
		// override whatever settings came from PostProcessVolume or Camera
		int32 SampleCount = InOutSampleState.TemporalSampleCount * InOutSampleState.SpatialSampleCount;
		int32 SampleIndex = InOutSampleState.TemporalSampleIndex * InOutSampleState.SpatialSampleCount + InOutSampleState.SpatialSampleIndex;

		// TODO: pass along FrameIndex (which includes SampleIndex) to make sure sampling is fully deterministic

		// Overwrite whatever sampling count came from the PostProcessVolume
		View->FinalPostProcessSettings.bOverride_PathTracingSamplesPerPixel = true;
		View->FinalPostProcessSettings.PathTracingSamplesPerPixel = InOutSampleState.SpatialSampleCount;

		// reset path tracer's accumulation at the start of each spatial sample
		View->bForcePathTracerReset = InOutSampleState.SpatialSampleIndex == 0;

		// discard the result, unless its the last spatial sample
		InOutSampleState.bDiscardResult |= !(InOutSampleState.SpatialSampleIndex == InOutSampleState.SpatialSampleCount - 1);
	}

	// Object Occlusion/Histories
	{
		// If we're using tiling, we force the reset of histories each frame so that we don't use the previous tile's
		// object occlusion queries, as that causes things to disappear from some views.
		if (InOutSampleState.GetTileCount() > 1)
		{
			View->bForceCameraVisibilityReset = true;
		}
	}

	// Bias all mip-mapping to pretend to be working at our target resolution and not our tile resolution
	// so that the images don't end up soft.
	{
		float EffectivePrimaryResolutionFraction = 1.f / InOutSampleState.TileCounts.X;
		View->MaterialTextureMipBias = FMath::Log2(EffectivePrimaryResolutionFraction);

		// Add an additional bias per user settings. This allows them to choose to make the textures sharper if it
		// looks better with their particular settings.
		View->MaterialTextureMipBias += InOutSampleState.TextureSharpnessBias;
	}

	return OutViewFamily;
}

void UMoviePipelineImagePassBase::SetupViewForViewModeOverride(FSceneView* View)
{
	if (View->Family->EngineShowFlags.Wireframe)
	{
		// Wireframe color is emissive-only, and mesh-modifying materials do not use material substitution, hence...
		View->DiffuseOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
		View->SpecularOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
	}
	else if (View->Family->EngineShowFlags.OverrideDiffuseAndSpecular)
	{
		View->DiffuseOverrideParameter = FVector4(GEngine->LightingOnlyBrightness.R, GEngine->LightingOnlyBrightness.G, GEngine->LightingOnlyBrightness.B, 0.0f);
		View->SpecularOverrideParameter = FVector4(.1f, .1f, .1f, 0.0f);
	}
	else if (View->Family->EngineShowFlags.LightingOnlyOverride)
	{
		View->DiffuseOverrideParameter = FVector4(GEngine->LightingOnlyBrightness.R, GEngine->LightingOnlyBrightness.G, GEngine->LightingOnlyBrightness.B, 0.0f);
		View->SpecularOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
	}
	else if (View->Family->EngineShowFlags.ReflectionOverride)
	{
		View->DiffuseOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
		View->SpecularOverrideParameter = FVector4(1, 1, 1, 0.0f);
		View->NormalOverrideParameter = FVector4(0, 0, 1, 0.0f);
		View->RoughnessOverrideParameter = FVector2D(0.0f, 0.0f);
	}

	if (!View->Family->EngineShowFlags.Diffuse)
	{
		View->DiffuseOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
	}

	if (!View->Family->EngineShowFlags.Specular)
	{
		View->SpecularOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
	}
	FName BufferVisualizationMode = "WorldNormal";
	View->CurrentBufferVisualizationMode = BufferVisualizationMode;
}

void UMoviePipelineImagePassBase::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	Super::GatherOutputPassesImpl(ExpectedRenderPasses);
	ExpectedRenderPasses.Add(PassIdentifier);
}

FSceneView* UMoviePipelineImagePassBase::GetSceneViewForSampleState(FSceneViewFamily* ViewFamily, FMoviePipelineRenderPassMetrics& InOutSampleState)
{
	APlayerController* LocalPlayerController = GetPipeline()->GetWorld()->GetFirstPlayerController();

	int32 TileSizeX = InOutSampleState.BackbufferSize.X;
	int32 TileSizeY = InOutSampleState.BackbufferSize.Y;

	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = ViewFamily;
	ViewInitOptions.ViewOrigin = InOutSampleState.FrameInfo.CurrViewLocation;
	ViewInitOptions.SetViewRectangle(FIntRect(FIntPoint(0, 0), FIntPoint(TileSizeX, TileSizeY)));
	ViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(InOutSampleState.FrameInfo.CurrViewRotation);
	ViewInitOptions.ViewActor = LocalPlayerController ? LocalPlayerController->GetViewTarget() : nullptr;

	// Rotate the view 90 degrees (reason: unknown)
	ViewInitOptions.ViewRotationMatrix = ViewInitOptions.ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
	float ViewFOV = 90.f;
	if (GetPipeline()->GetWorld()->GetFirstPlayerController()->PlayerCameraManager)
	{
		ViewFOV = GetPipeline()->GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetFOVAngle();
	}

	// Inflate our FOV to support the overscan 
	ViewFOV = 2.0f * FMath::RadiansToDegrees(FMath::Atan((1.0f + InOutSampleState.OverscanPercentage) * FMath::Tan(FMath::DegreesToRadians( ViewFOV * 0.5f ))));

	float DofSensorScale = 1.0f;

	// Calculate a Projection Matrix
	{
		float XAxisMultiplier;
		float YAxisMultiplier;

		check(GetPipeline()->GetWorld());
		check(GetPipeline()->GetWorld()->GetFirstPlayerController());
		APlayerCameraManager* PlayerCameraManager = GetPipeline()->GetWorld()->GetFirstPlayerController()->PlayerCameraManager;
		
		// Stretch the fovs if the view is constrained to the camera's aspect ratio
		if (PlayerCameraManager && PlayerCameraManager->GetCameraCachePOV().bConstrainAspectRatio)
		{
			const FMinimalViewInfo CameraCache = PlayerCameraManager->GetCameraCachePOV();
			const float DestAspectRatio = ViewInitOptions.GetViewRect().Width() / (float)ViewInitOptions.GetViewRect().Height();

			// If the camera's aspect ratio has a thinner width, then stretch the horizontal fov more than usual to 
			// account for the extra with of (before constraining - after constraining)
			if (CameraCache.AspectRatio < DestAspectRatio)
			{
				const float ConstrainedWidth = ViewInitOptions.GetViewRect().Height() * CameraCache.AspectRatio;
				XAxisMultiplier = ConstrainedWidth / (float)ViewInitOptions.GetViewRect().Width();
				YAxisMultiplier = CameraCache.AspectRatio;
			}
			// Simplified some math here but effectively functions similarly to the above, the unsimplified code would look like:
			// const float ConstrainedHeight = ViewInitOptions.GetViewRect().Width() / CameraCache.AspectRatio;
			// YAxisMultiplier = (ConstrainedHeight / ViewInitOptions.GetViewRect.Height()) * CameraCache.AspectRatio;
			else
			{
				XAxisMultiplier = 1.0f;
				YAxisMultiplier = ViewInitOptions.GetViewRect().Width() / (float)ViewInitOptions.GetViewRect().Height();
			}
		}
		else
		{
			const int32 DestSizeX = ViewInitOptions.GetViewRect().Width();
			const int32 DestSizeY = ViewInitOptions.GetViewRect().Height();
			const EAspectRatioAxisConstraint AspectRatioAxisConstraint = GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint;
			if (((DestSizeX > DestSizeY) && (AspectRatioAxisConstraint == AspectRatio_MajorAxisFOV)) || (AspectRatioAxisConstraint == AspectRatio_MaintainXFOV))
			{
				//if the viewport is wider than it is tall
				XAxisMultiplier = 1.0f;
				YAxisMultiplier = ViewInitOptions.GetViewRect().Width() / (float)ViewInitOptions.GetViewRect().Height();
			}
			else
			{
				//if the viewport is taller than it is wide
				XAxisMultiplier = ViewInitOptions.GetViewRect().Height() / (float)ViewInitOptions.GetViewRect().Width();
				YAxisMultiplier = 1.0f;
			}
		}

		const float MinZ = GNearClippingPlane;
		const float MaxZ = MinZ;
		// Avoid zero ViewFOV's which cause divide by zero's in projection matrix
		const float MatrixFOV = FMath::Max(0.001f, ViewFOV) * (float)PI / 360.0f;

		FMatrix BaseProjMatrix;

		if ((bool)ERHIZBuffer::IsInverted)
		{
			BaseProjMatrix = FReversedZPerspectiveMatrix(
				MatrixFOV,
				MatrixFOV,
				XAxisMultiplier,
				YAxisMultiplier,
				MinZ,
				MaxZ
			);
		}
		else
		{
			BaseProjMatrix = FPerspectiveMatrix(
				MatrixFOV,
				MatrixFOV,
				XAxisMultiplier,
				YAxisMultiplier,
				MinZ,
				MaxZ
			);
		}

		// overlapped tile adjustment
		{
			float PadRatioX = 1.0f;
			float PadRatioY = 1.0f;

			if (InOutSampleState.OverlappedPad.X > 0 && InOutSampleState.OverlappedPad.Y > 0)
			{
				PadRatioX = float(InOutSampleState.OverlappedPad.X * 2 + InOutSampleState.TileSize.X) / float(InOutSampleState.TileSize.X);
				PadRatioY = float(InOutSampleState.OverlappedPad.Y * 2 + InOutSampleState.TileSize.Y) / float(InOutSampleState.TileSize.Y);
			}

			float ScaleX = PadRatioX / float(InOutSampleState.TileCounts.X);
			float ScaleY = PadRatioY / float(InOutSampleState.TileCounts.Y);

			BaseProjMatrix.M[0][0] /= ScaleX;
			BaseProjMatrix.M[1][1] /= ScaleY;
			DofSensorScale = ScaleX;

			// this offset would be correct with no pad
			float OffsetX = -((float(InOutSampleState.TileIndexes.X) + 0.5f - float(InOutSampleState.TileCounts.X) / 2.0f) * 2.0f);
			float OffsetY = ((float(InOutSampleState.TileIndexes.Y) + 0.5f - float(InOutSampleState.TileCounts.Y) / 2.0f) * 2.0f);

			BaseProjMatrix.M[2][0] += OffsetX / PadRatioX;
			BaseProjMatrix.M[2][1] += OffsetY / PadRatioX;
		}

		ViewInitOptions.ProjectionMatrix = BaseProjMatrix;
	}

	ViewInitOptions.SceneViewStateInterface = GetSceneViewStateInterface();
	ViewInitOptions.FOV = ViewFOV;

	FSceneView* View = new FSceneView(ViewInitOptions);
	ViewFamily->Views.Add(View);
	View->ViewLocation = InOutSampleState.FrameInfo.CurrViewLocation;
	View->ViewRotation = InOutSampleState.FrameInfo.CurrViewRotation;
	// Override previous/current view transforms so that tiled renders don't use the wrong occlusion/motion blur information.
	View->PreviousViewTransform = FTransform(InOutSampleState.FrameInfo.PrevViewRotation, InOutSampleState.FrameInfo.PrevViewLocation);

	View->StartFinalPostprocessSettings(View->ViewLocation);
	BlendPostProcessSettings(View);

	// Scaling sensor size inversely with the the projection matrix [0][0] should physically
	// cause the circle of confusion to be unchanged.
	View->FinalPostProcessSettings.DepthOfFieldSensorWidth *= DofSensorScale;

	{
		// We need our final view parameters to be in the space of [-1,1], including all the tiles.
		// Starting with a single tile, the middle of the tile in offset screen space is:
		FVector2D TilePrincipalPointOffset;

		TilePrincipalPointOffset.X = (float(InOutSampleState.TileIndexes.X) + 0.5f - (0.5f * float(InOutSampleState.TileCounts.X))) * 2.0f;
		TilePrincipalPointOffset.Y = (float(InOutSampleState.TileIndexes.Y) + 0.5f - (0.5f * float(InOutSampleState.TileCounts.Y))) * 2.0f;

		// For the tile size ratio, we have to multiply by (1.0 + overlap) and then divide by tile num
		FVector2D OverlapScale;
		OverlapScale.X = (1.0f + float(2 * InOutSampleState.OverlappedPad.X) / float(InOutSampleState.TileSize.X));
		OverlapScale.Y = (1.0f + float(2 * InOutSampleState.OverlappedPad.Y) / float(InOutSampleState.TileSize.Y));

		TilePrincipalPointOffset.X /= OverlapScale.X;
		TilePrincipalPointOffset.Y /= OverlapScale.Y;

		FVector2D TilePrincipalPointScale;
		TilePrincipalPointScale.X = OverlapScale.X / float(InOutSampleState.TileCounts.X);
		TilePrincipalPointScale.Y = OverlapScale.Y / float(InOutSampleState.TileCounts.Y);

		TilePrincipalPointOffset.X *= TilePrincipalPointScale.X;
		TilePrincipalPointOffset.Y *= TilePrincipalPointScale.Y;

		View->LensPrincipalPointOffsetScale = FVector4(TilePrincipalPointOffset.X, -TilePrincipalPointOffset.Y, TilePrincipalPointScale.X, TilePrincipalPointScale.Y);
	}

	View->EndFinalPostprocessSettings(ViewInitOptions);


	// This metadata is per-file and not per-view, but we need the blended result from the view to actually match what we rendered.
	// To solve this, we'll insert metadata per renderpass, separated by render pass name.
	InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/fstop"), *PassIdentifier.Name), FString::SanitizeFloat(View->FinalPostProcessSettings.DepthOfFieldFstop));
	InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/fov"), *PassIdentifier.Name), FString::SanitizeFloat(ViewInitOptions.FOV));
	InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/focalDistance"), *PassIdentifier.Name), FString::SanitizeFloat(View->FinalPostProcessSettings.DepthOfFieldFocalDistance));
	InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/sensorWidth"), *PassIdentifier.Name), FString::SanitizeFloat(View->FinalPostProcessSettings.DepthOfFieldSensorWidth));
	InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/overscanPercent"), *PassIdentifier.Name), FString::SanitizeFloat(InOutSampleState.OverscanPercentage));

	if (GetWorld()->GetFirstPlayerController()->PlayerCameraManager)
	{
		// This only works if you use a Cine Camera (which is almost guranteed with Sequencer) and it's easier (and less human error prone) than re-deriving the information
		ACineCameraActor* CineCameraActor = Cast<ACineCameraActor>(GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetViewTarget());
		if (CineCameraActor)
		{
			UCineCameraComponent* CineCameraComponent = CineCameraActor->GetCineCameraComponent();
			if (CineCameraComponent)
			{
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/sensorWidth"), *PassIdentifier.Name), FString::SanitizeFloat(CineCameraComponent->Filmback.SensorWidth));
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/sensorHeight"), *PassIdentifier.Name), FString::SanitizeFloat(CineCameraComponent->Filmback.SensorHeight));
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/sensorAspectRatio"), *PassIdentifier.Name), FString::SanitizeFloat(CineCameraComponent->Filmback.SensorAspectRatio));
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/minFocalLength"), *PassIdentifier.Name), FString::SanitizeFloat(CineCameraComponent->LensSettings.MinFocalLength));
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/maxFocalLength"), *PassIdentifier.Name), FString::SanitizeFloat(CineCameraComponent->LensSettings.MaxFocalLength));
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/minFStop"), *PassIdentifier.Name), FString::SanitizeFloat(CineCameraComponent->LensSettings.MinFStop));
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/maxFStop"), *PassIdentifier.Name), FString::SanitizeFloat(CineCameraComponent->LensSettings.MaxFStop));
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/dofDiaphragmBladeCount"), *PassIdentifier.Name), FString::FromInt(CineCameraComponent->LensSettings.DiaphragmBladeCount));
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/focalLength"), *PassIdentifier.Name), FString::SanitizeFloat(CineCameraComponent->CurrentFocalLength));
			}
		}
	}


	return View;
}

void UMoviePipelineImagePassBase::BlendPostProcessSettings(FSceneView* InView)
{
	check(InView);

	APlayerController* LocalPlayerController = GetPipeline()->GetWorld()->GetFirstPlayerController();
	// CameraAnim override
	if (LocalPlayerController->PlayerCameraManager)
	{
		TArray<FPostProcessSettings> const* CameraAnimPPSettings;
		TArray<float> const* CameraAnimPPBlendWeights;
		LocalPlayerController->PlayerCameraManager->GetCachedPostProcessBlends(CameraAnimPPSettings, CameraAnimPPBlendWeights);

		if (LocalPlayerController->PlayerCameraManager->bEnableFading)
		{
			InView->OverlayColor = LocalPlayerController->PlayerCameraManager->FadeColor;
			InView->OverlayColor.A = FMath::Clamp(LocalPlayerController->PlayerCameraManager->FadeAmount, 0.f, 1.f);
		}

		if (LocalPlayerController->PlayerCameraManager->bEnableColorScaling)
		{
			FVector ColorScale = LocalPlayerController->PlayerCameraManager->ColorScale;
			InView->ColorScale = FLinearColor(ColorScale.X, ColorScale.Y, ColorScale.Z);
		}

		FMinimalViewInfo ViewInfo = LocalPlayerController->PlayerCameraManager->GetCameraCachePOV();
		for (int32 PPIdx = 0; PPIdx < CameraAnimPPBlendWeights->Num(); ++PPIdx)
		{
			InView->OverridePostProcessSettings((*CameraAnimPPSettings)[PPIdx], (*CameraAnimPPBlendWeights)[PPIdx]);
		}

		InView->OverridePostProcessSettings(ViewInfo.PostProcessSettings, ViewInfo.PostProcessBlendWeight);
	}
}
