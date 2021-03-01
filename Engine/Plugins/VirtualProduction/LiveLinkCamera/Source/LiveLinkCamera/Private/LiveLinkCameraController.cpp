// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkCameraController.h"


#include "Camera/CameraComponent.h"
#include "CineCameraComponent.h"
#include "Controllers/LiveLinkTransformController.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"
#include "ILiveLinkClient.h"
#include "LensFile.h"
#include "LiveLinkComponentController.h"
#include "Logging/LogMacros.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "UObject/EnterpriseObjectVersion.h"

#if WITH_EDITOR
#include "Kismet2/ComponentEditorUtils.h"
#endif


DEFINE_LOG_CATEGORY_STATIC(LogLiveLinkCameraController, Log, All);


void ULiveLinkCameraController::Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData)
{
	const FLiveLinkCameraStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkCameraStaticData>();
	const FLiveLinkCameraFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkCameraFrameData>();

	if (StaticData && FrameData)
	{
		if (UCameraComponent* CameraComponent = Cast<UCameraComponent>(AttachedComponent))
		{
			bIsEncoderMappingNeeded = (StaticData->FIZDataMode == ELiveLinkCameraFIZMode::EncoderData);
			
			if (StaticData->bIsFieldOfViewSupported) { CameraComponent->SetFieldOfView(FrameData->FieldOfView); }
			if (StaticData->bIsAspectRatioSupported) { CameraComponent->SetAspectRatio(FrameData->AspectRatio); }
			if (StaticData->bIsProjectionModeSupported) { CameraComponent->SetProjectionMode(FrameData->ProjectionMode == ELiveLinkCameraProjectionMode::Perspective ? ECameraProjectionMode::Perspective : ECameraProjectionMode::Orthographic); }

			if (UCineCameraComponent* CineCameraComponent = Cast<UCineCameraComponent>(CameraComponent))
			{
				if (StaticData->FilmBackWidth > 0.0f) { CineCameraComponent->Filmback.SensorWidth = StaticData->FilmBackWidth; }
				if (StaticData->FilmBackHeight > 0.0f) { CineCameraComponent->Filmback.SensorHeight = StaticData->FilmBackHeight; }
				
				//When FIZ data comes from encoder, we need to map incoming values to actual FIZ
				if (bIsEncoderMappingNeeded)
				{
					if (LensFile)
					{
						if (StaticData->bIsFocusDistanceSupported)
						{
							float NewFocusDistance;
							if (LensFile->EvaluateNormalizedFocus(FrameData->FocusDistance, NewFocusDistance))
							{
								CineCameraComponent->FocusSettings.ManualFocusDistance = NewFocusDistance;
							}
							else
							{
								UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("'%s' could not evaluate raw focus value '%0.3f' using LensFile '%s'"), *GetName(), FrameData->FocusDistance, *LensFile->GetName())
							}
						}

						if (StaticData->bIsApertureSupported)
						{
							float NewAperture;
							if (LensFile->EvaluateNormalizedIris(FrameData->Aperture, NewAperture))
							{
								CineCameraComponent->CurrentAperture = NewAperture;
							}
							else
							{
								UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("'%s' could not evaluate raw iris value '%0.3f' using LensFile '%s'"), *GetName(), FrameData->FocusDistance, *LensFile->GetName())
							}
						}

						if (StaticData->bIsFocalLengthSupported)
						{
							float NewZoom;
							if (LensFile->EvaluateNormalizedZoom(FrameData->FocalLength, NewZoom))
							{
								CineCameraComponent->SetCurrentFocalLength(NewZoom);
							}
							else
							{
								UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("'%s' could not evaluate raw zoom value '%0.3f' using LensFile '%s'"), *GetName(), FrameData->FocalLength, *LensFile->GetName())
							}
						}
					}
					else
					{
						const double CurrentTime = FPlatformTime::Seconds();
						if ((CurrentTime - LastInvalidLoggingLoggedTimestamp) > TimeBetweenLoggingSeconds)
						{
							LastInvalidLoggingLoggedTimestamp = CurrentTime;
							UE_LOG(LogLiveLinkCameraController, Warning, TEXT("'%s' needs encoder mapping but lens file is invalid"), *GetName())
						}
						
					}
				}
				else
				{
					if (StaticData->bIsFocusDistanceSupported) { CineCameraComponent->FocusSettings.ManualFocusDistance = FrameData->FocusDistance; }
					if (StaticData->bIsApertureSupported) { CineCameraComponent->CurrentAperture = FrameData->Aperture; }
					if (StaticData->bIsFocalLengthSupported) { CineCameraComponent->SetCurrentFocalLength(FrameData->FocalLength); }
				}
			}

			if (bApplyLensDistortion && LensFile)
			{
				//todo
			}
		}
	}
}

bool ULiveLinkCameraController::IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport)
{
	return RoleToSupport == ULiveLinkCameraRole::StaticClass();
}

TSubclassOf<UActorComponent> ULiveLinkCameraController::GetDesiredComponentClass() const
{
	return UCameraComponent::StaticClass();
}

void ULiveLinkCameraController::OnEvaluateRegistered()
{
	//Reset flag until the next tick with actual data
	bIsEncoderMappingNeeded = false;
}

void ULiveLinkCameraController::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	const int32 Version = GetLinkerCustomVersion(FEnterpriseObjectVersion::GUID);
	if (Version < FEnterpriseObjectVersion::LiveLinkControllerSplitPerRole)
	{
		AActor* MyActor = GetOuterActor();
		if (MyActor)
		{
			//Make sure all UObjects we use in our post load have been postloaded
			MyActor->ConditionalPostLoad();

			ULiveLinkComponentController* LiveLinkComponent = Cast<ULiveLinkComponentController>(MyActor->GetComponentByClass(ULiveLinkComponentController::StaticClass()));
			if (LiveLinkComponent)
			{
				LiveLinkComponent->ConditionalPostLoad();

				//If the transform controller that was created to drive the TransformRole is the built in one, set its data structure with the one that we had internally
				if (LiveLinkComponent->ControllerMap.Contains(ULiveLinkTransformRole::StaticClass()))
				{
					ULiveLinkTransformController* TransformController = Cast<ULiveLinkTransformController>(LiveLinkComponent->ControllerMap[ULiveLinkTransformRole::StaticClass()]);
					if (TransformController)
					{
						TransformController->ConditionalPostLoad();
						TransformController->TransformData = TransformData_DEPRECATED;
					}
				}

				//if Subjects role direct controller is us, set the component to control to what we had
				if (LiveLinkComponent->SubjectRepresentation.Role == ULiveLinkCameraRole::StaticClass())
				{
					LiveLinkComponent->ComponentToControl = ComponentToControl_DEPRECATED;
				}
			}
		}
	}
#endif //WITH_EDITOR
}
