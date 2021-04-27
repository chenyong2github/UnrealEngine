// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkLensController.h"

#include "CameraCalibrationSubsystem.h"
#include "LiveLinkLensRole.h"
#include "LiveLinkLensTypes.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiveLinkLensController, Log, All);


void ULiveLinkLensController::Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData)
{
	const FLiveLinkLensStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkLensStaticData>();
	const FLiveLinkLensFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkLensFrameData>();

	if (StaticData && FrameData)
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
		{
			UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
			TSubclassOf<ULensModel> LensModel = SubSystem->GetRegisteredLensModel(StaticData->LensModel);
			LensDistortionHandler = SubSystem->FindOrCreateDistortionModelHandler(CineCameraComponent, LensModel, EHandlerOverrideMode::SoftOverride);

			//To keep track of an updated MID from the handler
			UMaterialInstanceDynamic* NewDistortionMID = nullptr;

			UpdateCachedFocalLength(CineCameraComponent);

			if (LensDistortionHandler)
			{
				// Update the lens distortion handler with the latest frame of data from the LiveLink source
				FLensDistortionState DistortionState;

				DistortionState.DistortionInfo = FrameData->DistortionInfo;
				DistortionState.PrincipalPoint = FrameData->PrincipalPoint;
				DistortionState.FxFy = FrameData->FxFy;

				//Update the distortion state based on incoming LL data.
				//Recompute overscan factor for the distortion state
				//Make sure the displacement map is up to date
				LensDistortionHandler->SetDistortionState(DistortionState);
				LensDistortionHandler->SetOverscanFactor(LensDistortionHandler->ComputeOverscanFactor());
				LensDistortionHandler->ProcessCurrentDistortion();

				NewDistortionMID = LensDistortionHandler->GetDistortionMID();

				// Get the computed overscan factor and scale the camera's sensor dimensions to simulate a wider FOV
				if (bApplyDistortion)
				{
					const float OverscanFactor = LensDistortionHandler->GetOverscanFactor();
					const float OverscanSensorWidth = CineCameraComponent->Filmback.SensorWidth * OverscanFactor;
					const float OverscanFOV = FMath::RadiansToDegrees(2.0f * FMath::Atan(OverscanSensorWidth / (2.0f * UndistortedFocalLength)));
					CineCameraComponent->SetFieldOfView(OverscanFOV);
				}
			}

			//Cleanup distortion MIDs we could have setup
			if (NewDistortionMID != LastDistortionMID)
			{
				CleanupDistortion();
			}

			//Stamp last MID used for distortion
			LastDistortionMID = NewDistortionMID;

			// If distortion should be applied to the attached cinecamera, fetch the distortion MID from the Lens Distortion Handler and add it to the camera's post-process materials
			if (bApplyDistortion && (bIsDistortionSetup == false))
			{
				CineCameraComponent->AddOrUpdateBlendable(LastDistortionMID);
				bIsDistortionSetup = true;
			}
			// If distortion should not be applied, remove the distortion MID from the camera's post process materials
			else if (bIsDistortionSetup && (bApplyDistortion == false))
			{
				CleanupDistortion();
			}

			//Stamp last applied focal length to detect if user has changed it
			LastFocalLength = CineCameraComponent->CurrentFocalLength;
		}
	}
}

bool ULiveLinkLensController::IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport)
{
	return RoleToSupport == ULiveLinkLensRole::StaticClass();
}

TSubclassOf<UActorComponent> ULiveLinkLensController::GetDesiredComponentClass() const
{
	return UCineCameraComponent::StaticClass();
}

void ULiveLinkLensController::SetAttachedComponent(UActorComponent* ActorComponent)
{
	const bool bHasChangedComponent = (ActorComponent != AttachedComponent);
	if (bHasChangedComponent)
	{
		//Remove MID we could have added to the old component
		CleanupDistortion();

		// If the component is changing from one camera actor to another camera actor, update the undistorted focal length to the focal length of the new component
		if (AttachedComponent != nullptr)
		{
			if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(ActorComponent))
			{
				UndistortedFocalLength = CineCameraComponent->CurrentFocalLength;
			}
		}
	}

	Super::SetAttachedComponent(ActorComponent);
	
	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
	{
		// Initialize the most recent focal length to the current focal length of the camera to properly detect changes to this property
		LastFocalLength = CineCameraComponent->CurrentFocalLength;

		//When our component has changed, make sure we update our 
		//cached/original focal length to restore it correctly
		//PIE case is odd because the camera is duplicated from editor and its focal length will already be distorted
		if (bApplyDistortion == false
			|| (GetWorld() && GetWorld()->WorldType != EWorldType::PIE))
		{
			UpdateCachedFocalLength(CineCameraComponent);
		}

		UpdateDistortionHandler(CineCameraComponent);
	}
}

void ULiveLinkLensController::Cleanup()
{
	CleanupDistortion();
}

#if WITH_EDITOR
void ULiveLinkLensController::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkLensController, bApplyDistortion))
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
		{
			if (bApplyDistortion)
			{
				/**
				 * Cache focal length to be able to recover it once distortion is turned off
				 * not part of the distortion setup since entering PIE will duplicate actor / components
				 * and the duplicated one will already have modified focal length applied.
				 */
				UndistortedFocalLength = CineCameraComponent->CurrentFocalLength;
				UE_LOG(LogLiveLinkLensController, Verbose, TEXT("Enabling distortion. Cached Focal Length is %0.3fmm"), UndistortedFocalLength);

				UpdateDistortionHandler(CineCameraComponent);
			}
			else
			{
				// Static lens model data may not be available because the controller may not be ticking, so query the subsystem for any distortion handler
				UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
				ULensDistortionModelHandlerBase* Handler = SubSystem->GetDistortionModelHandler(CineCameraComponent);

				// Try to remove the post-process material from the cine camera's blendables. 
				CineCameraComponent->RemoveBlendable(Handler->GetDistortionMID());

				// Restore the original FOV of the 
				const float OriginalFOV = FMath::RadiansToDegrees(2.0f * FMath::Atan(CineCameraComponent->Filmback.SensorWidth / (2.0f * UndistortedFocalLength)));
				CineCameraComponent->SetFieldOfView(OriginalFOV);
			}
		}
	}
}
#endif

void ULiveLinkLensController::UpdateDistortionHandler(UCineCameraComponent* CineCameraComponent)
{
	if (LensDistortionHandler)
	{
		//Cache MID when changing handler to start with something valid. i.e. Cleaning MID from blendables if LL was never valid
		LastDistortionMID = LensDistortionHandler->GetDistortionMID();
	}
}

void ULiveLinkLensController::UpdateCachedFocalLength(UCineCameraComponent* CineCameraComponent)
{
	//Verify if focal length was changed by the user. If that's the case, take the current value and update the cached one
	if (CineCameraComponent->CurrentFocalLength != LastFocalLength)
	{
		UndistortedFocalLength = CineCameraComponent->CurrentFocalLength;

		UE_LOG(LogLiveLinkLensController, Verbose, TEXT("Updating cached focal length to %0.3fmm"), UndistortedFocalLength);
	}
}

void ULiveLinkLensController::CleanupDistortion()
{
	//Remove MID we could have added to the component
	if (bIsDistortionSetup)
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
		{
			CineCameraComponent->RemoveBlendable(LastDistortionMID);

			//Update cached focal length before resetting it. We could have stopped ticking because the LiveLink component was stopped
			//In the meantime, focal length could have been manually changed and our cache is out of date.
			UpdateCachedFocalLength(CineCameraComponent);
			
			const float OriginalFOV = FMath::RadiansToDegrees(2.0f * FMath::Atan(CineCameraComponent->Filmback.SensorWidth / (2.0f * UndistortedFocalLength)));
			CineCameraComponent->SetFieldOfView(OriginalFOV);
		}
	}

	bIsDistortionSetup = false;
}
