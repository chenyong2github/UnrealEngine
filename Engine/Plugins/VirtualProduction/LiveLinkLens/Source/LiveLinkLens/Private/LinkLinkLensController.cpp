// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkLensController.h"

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
			//To keep track of an updated MID from the handler
			UMaterialInstanceDynamic* NewDistortionMID = nullptr;

			UpdateCachedFilmback(CineCameraComponent);

			if (LensDistortionHandler)
			{
				/** Update the lens distortion handler with the latest frame of data from the LiveLink source */
				FLensDistortionState DistortionState;

				DistortionState.LensModel = StaticData->LensModel;
				DistortionState.DistortionParameters = FrameData->DistortionParameters;
				DistortionState.PrincipalPoint = FrameData->PrincipalPoint;

				/** The sensor dimensions must be the original dimensions of the source camera (with no overscan applied) */
				DistortionState.SensorDimensions = FVector2D(OriginalCameraFilmback.SensorWidth, OriginalCameraFilmback.SensorHeight);
				DistortionState.FocalLength = FrameData->FocalLength;

				LensDistortionHandler->Update(DistortionState);

				NewDistortionMID = LensDistortionHandler->GetDistortionMID();

				/** Get the computed overscan factor and scale the camera's sensor dimensions to simulate a wider FOV */
				if (bApplyDistortion)
				{
					const float OverscanFactor = LensDistortionHandler->GetOverscanFactor();
					CineCameraComponent->Filmback.SensorWidth = OriginalCameraFilmback.SensorWidth * OverscanFactor;
					CineCameraComponent->Filmback.SensorHeight = OriginalCameraFilmback.SensorHeight * OverscanFactor;
				}
			}

			//Cleanup distortion MIDs we could have setup
			if (NewDistortionMID != LastDistortionMID)
			{
				CleanupDistortion();
			}

			//Stamp last MID used for distortion
			LastDistortionMID = NewDistortionMID;

			/** If distortion should be applied to the attached cinecamera, fetch the distortion MID from the Lens Distortion Handler and add it to the camera's post-process materials */
			if (bApplyDistortion && (bIsDistortionSetup == false))
			{
				CineCameraComponent->AddOrUpdateBlendable(LastDistortionMID);
				bIsDistortionSetup = true;
			}
			/** If distortion should not be applied, remove the distortion MID from the camera's post process materials */
			else if (bIsDistortionSetup && (bApplyDistortion == false))
			{
				CleanupDistortion();
			}

			//Stamp last applied filmback to detect if user has changed it
			LastCameraFilmback = CineCameraComponent->Filmback;
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
	if (ActorComponent != AttachedComponent)
	{
		//Remove MID we could have added to the old component
		CleanupDistortion();
	}

	Super::SetAttachedComponent(ActorComponent);
	
	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
	{
		//When our component has changed, make sure we update our 
		//cached/original filmback to restore it correctly
		//PIE case is odd because the camera is duplicated from editor and its filmback will already be distorted
		if (bApplyDistortion == false
			|| (GetWorld() && GetWorld()->WorldType != EWorldType::PIE))
		{
			UpdateCachedFilmback(CineCameraComponent);
		}

		UpdateDistortionHandler(CineCameraComponent);
		LastCameraFilmback = CineCameraComponent->Filmback;
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
				 * Cache filmback to be able to recover it once distortion is turned off
				 * not part of the distortion setup since entering PIE will duplicate actor / components
				 * andthe duplicated one will already have modified filmback applied.
				 */
				OriginalCameraFilmback = CineCameraComponent->Filmback;
				UE_LOG(LogLiveLinkLensController, Verbose, TEXT("Enabling distortion. Cached Filmback is %0.3fmm x %0.3fmm"), OriginalCameraFilmback.SensorWidth, OriginalCameraFilmback.SensorHeight);

				UpdateDistortionHandler(CineCameraComponent);
			}
			else
			{
				CineCameraComponent->Filmback = OriginalCameraFilmback;
				LensDistortionHandler = nullptr;
			}
		}
	}
}
#endif

void ULiveLinkLensController::UpdateDistortionHandler(UCineCameraComponent* CineCameraComponent)
{
	check(CineCameraComponent);
	LensDistortionHandler = ULensDistortionDataHandler::GetLensDistortionDataHandler(CineCameraComponent);
	if (LensDistortionHandler == nullptr)
	{
		LensDistortionHandler = NewObject<ULensDistortionDataHandler>(CineCameraComponent);
		CineCameraComponent->AddAssetUserData(LensDistortionHandler);
	}

	//Cache MID when changing handler to start with something valid. i.e. Cleaning MID from blendables if LL was never valid
	LastDistortionMID = LensDistortionHandler->GetDistortionMID();
}

void ULiveLinkLensController::UpdateCachedFilmback(UCineCameraComponent* CineCameraComponent)
{
	//Verify if filmback was changed by the user. If that's the case, take the current value and update the cached one
	if (CineCameraComponent->Filmback != LastCameraFilmback)
	{
		if (CineCameraComponent->Filmback.SensorWidth != LastCameraFilmback.SensorWidth)
		{
			OriginalCameraFilmback.SensorWidth = CineCameraComponent->Filmback.SensorWidth;
		}

		if (CineCameraComponent->Filmback.SensorHeight != LastCameraFilmback.SensorHeight)
		{
			OriginalCameraFilmback.SensorHeight = CineCameraComponent->Filmback.SensorHeight;
		}

		UE_LOG(LogLiveLinkLensController, Verbose, TEXT("Updating cached Filmback to %0.3fmm x %0.3fmm"), OriginalCameraFilmback.SensorWidth, OriginalCameraFilmback.SensorHeight);
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

			//Update cached filmback before resetting it. We could have stopped ticking because the LiveLink component was stopped
			//In the meantime, filmback could have been manually changed and our cache is out of date.
			UpdateCachedFilmback(CineCameraComponent);
			CineCameraComponent->Filmback = OriginalCameraFilmback;
		}
	}

	bIsDistortionSetup = false;
}
