// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkLensController.h"

#include "LiveLinkLensRole.h"
#include "LiveLinkLensTypes.h"

void ULiveLinkLensController::Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData)
{
	const FLiveLinkLensStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkLensStaticData>();
	const FLiveLinkLensFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkLensFrameData>();

	if (StaticData && FrameData)
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
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

			/** If distortion should be applied to the attached cinecamera, fetch the distortion MID from the Lens Distortion Handler and add it to the camera's post-process materials */
			if (bApplyDistortion && bIsDistortionSetup == false)
			{
				CineCameraComponent->AddOrUpdateBlendable(LensDistortionHandler->GetDistortionMID());

				bIsDistortionSetup = true;
			}
			/** If distortion should not be applied, remove the distortion MID from the camera's post process materials */
			else if (bIsDistortionSetup && bApplyDistortion == false)
			{
				CineCameraComponent->RemoveBlendable(LensDistortionHandler->GetDistortionMID());
				bIsDistortionSetup = false;
			}

			/** Get the computed overscan factor and scale the camera's sensor dimensions to simulate a wider FOV */
			if (bApplyDistortion)
			{
				const float OverscanFactor = LensDistortionHandler->GetOverscanFactor();
				CineCameraComponent->Filmback.SensorWidth = OriginalCameraFilmback.SensorWidth * OverscanFactor;
				CineCameraComponent->Filmback.SensorHeight = OriginalCameraFilmback.SensorHeight * OverscanFactor;
			}
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
	Super::SetAttachedComponent(ActorComponent);
	
	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
	{
		LensDistortionHandler = ULensDistortionDataHandler::GetLensDistortionDataHandler(CineCameraComponent);
		if (LensDistortionHandler == nullptr)
		{
			LensDistortionHandler = NewObject<ULensDistortionDataHandler>(CineCameraComponent);
			CineCameraComponent->AddAssetUserData(LensDistortionHandler);
		}

		OriginalCameraFilmback = CineCameraComponent->Filmback;
	}
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
				/** Cache filmback to be able to recover it once distortion is turned off
				    Not part of the distortion setup since entering PIE will duplicate actor / components
				    and the duplicated one will already have modified filmback applied. */
				OriginalCameraFilmback = CineCameraComponent->Filmback;
			}
			else
			{
				CineCameraComponent->Filmback = OriginalCameraFilmback;
			}
		}
	}
}
#endif
