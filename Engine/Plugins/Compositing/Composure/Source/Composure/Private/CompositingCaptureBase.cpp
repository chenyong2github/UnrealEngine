// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositingCaptureBase.h"

#include "CameraCalibrationSubsystem.h"
#include "CineCameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "LensDistortionModelHandlerBase.h"


ACompositingCaptureBase::ACompositingCaptureBase()
{
	// Create the SceneCapture component and assign its parent to be the RootComponent (the ComposurePostProcessingPassProxy)
	SceneCaptureComponent2D = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCaptureComponent"));
	SceneCaptureComponent2D->SetupAttachment(RootComponent);

	// The SceneCaptureComponent2D default constructor disables TAA, but CG Compositing Elements enable it by default
	SceneCaptureComponent2D->ShowFlags.TemporalAA = true;
}

void ACompositingCaptureBase::UpdateDistortion()
{
	// Get the TargetCameraActor associated with this CG Layer
	ACameraActor* TargetCamera = FindTargetCamera();
	if (TargetCamera == nullptr)
	{
		return;
	}
	
 	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCamera->GetCameraComponent()))
	{
		DistortionSource.TargetCameraComponent = CineCameraComponent;

		// Query the camera calibration subsystem for a handler associated with the TargetCamera and matching the user selection
		ULensDistortionModelHandlerBase* LensDistortionHandler = nullptr;
		UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
		if (SubSystem)
		{
			LensDistortionHandler = SubSystem->FindDistortionModelHandler(DistortionSource);

			// Get the focal length of the target camera (before any overscan could have been applied) to prevent double overscan
			const bool bCouldBeDistorted = SubSystem->GetOriginalFocalLength(CineCameraComponent, OriginalFocalLength);
			if (!bCouldBeDistorted)
			{
				OriginalFocalLength = CineCameraComponent->CurrentFocalLength;
			}
		}

		if (LensDistortionHandler)
		{
			// Get the current distortion MID from the lens distortion handler, and if it has changed, remove the old MID from the scene capture's post process materials
			UMaterialInstanceDynamic* NewDistortionMID = LensDistortionHandler->GetDistortionMID();
			if (LastDistortionMID != NewDistortionMID)
			{
				if (SceneCaptureComponent2D)
				{
					SceneCaptureComponent2D->RemoveBlendable(LastDistortionMID);
				}
			}

			// Cache the latest distortion MID
			LastDistortionMID = NewDistortionMID;

			// If distortion should be applied, add/update the distortion MID to the scene capture's post process materials. Otherwise, remove it.
			if (SceneCaptureComponent2D)
			{
				if (bApplyDistortion)
				{
					OverscanFactor = LensDistortionHandler->GetOverscanFactor();
					SceneCaptureComponent2D->AddOrUpdateBlendable(NewDistortionMID);
				}
				else
				{
					OverscanFactor = 1.0f;
					SceneCaptureComponent2D->RemoveBlendable(NewDistortionMID);
				}
			}
		}
		else
		{
			OverscanFactor = 1.0f;

			if (SceneCaptureComponent2D)
			{
				SceneCaptureComponent2D->RemoveBlendable(LastDistortionMID);
			}
			LastDistortionMID = nullptr;
		}
	}
}

#if WITH_EDITOR
void ACompositingCaptureBase::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if ((PropertyName == GET_MEMBER_NAME_CHECKED(ACompositingCaptureBase, TargetCameraActor)))
	{
		if (TargetCameraActor)
		{
			if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraActor->GetCameraComponent()))
			{
				DistortionSource.TargetCameraComponent = CineCameraComponent;

				ULensDistortionModelHandlerBase* LensDistortionHandler = nullptr;
				UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
				if (SubSystem)
				{
					LensDistortionHandler = SubSystem->FindDistortionModelHandler(DistortionSource);
				}

				if (LensDistortionHandler == nullptr)
				{
					DistortionSource.DistortionProducerID.Invalidate();
					DistortionSource.HandlerDisplayName.Empty();
				}
			}
		}

		// If there is no target camera, remove the last distortion post-process MID from the scene capture
		if (TargetCameraActor == nullptr)
		{
			if (SceneCaptureComponent2D)
			{
				SceneCaptureComponent2D->RemoveBlendable(LastDistortionMID);
			}

			LastDistortionMID = nullptr;

			return;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ACompositingCaptureBase::SetApplyDistortion(bool bInApplyDistortion)
{
	bApplyDistortion = bInApplyDistortion;
	UpdateDistortion();
}

void ACompositingCaptureBase::SetDistortionHandler(ULensDistortionModelHandlerBase* InDistortionHandler)
{
	if (InDistortionHandler)
	{
		DistortionSource.DistortionProducerID = InDistortionHandler->GetDistortionProducerID();
		DistortionSource.HandlerDisplayName = InDistortionHandler->GetDisplayName();
	}
	else
	{
		DistortionSource.DistortionProducerID = FGuid();
		DistortionSource.HandlerDisplayName = FString();
	}

	UpdateDistortion();
}

ULensDistortionModelHandlerBase* ACompositingCaptureBase::GetDistortionHandler()
{
	UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();

	if (!SubSystem)
	{
		return nullptr;
	}

	return SubSystem->FindDistortionModelHandler(DistortionSource);
}
