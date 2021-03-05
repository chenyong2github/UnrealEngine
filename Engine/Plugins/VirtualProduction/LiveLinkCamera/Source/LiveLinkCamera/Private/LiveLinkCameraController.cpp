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
			bIsEncoderMappingNeeded = (StaticData->FIZDataMode == ECameraFIZMode::EncoderData);
			
			if (StaticData->bIsFieldOfViewSupported) { CameraComponent->SetFieldOfView(FrameData->FieldOfView); }
			if (StaticData->bIsAspectRatioSupported) { CameraComponent->SetAspectRatio(FrameData->AspectRatio); }
			if (StaticData->bIsProjectionModeSupported) { CameraComponent->SetProjectionMode(FrameData->ProjectionMode == ELiveLinkCameraProjectionMode::Perspective ? ECameraProjectionMode::Perspective : ECameraProjectionMode::Orthographic); }

			if (UCineCameraComponent* CineCameraComponent = Cast<UCineCameraComponent>(CameraComponent))
			{
				if (StaticData->FilmBackWidth > 0.0f) { CineCameraComponent->Filmback.SensorWidth = StaticData->FilmBackWidth; }
				if (StaticData->FilmBackHeight > 0.0f) { CineCameraComponent->Filmback.SensorHeight = StaticData->FilmBackHeight; }
				
				ULensFile* SelectedLensFile = LensFilePicker.GetLensFile();

				//When FIZ data comes from encoder, we need to map incoming values to actual FIZ
				if (bIsEncoderMappingNeeded)
				{
					if (SelectedLensFile)
					{
						if (StaticData->bIsFocusDistanceSupported)
						{
							float NewFocusDistance;
							if (SelectedLensFile->EvaluateNormalizedFocus(FrameData->FocusDistance, NewFocusDistance))
							{
								CineCameraComponent->FocusSettings.ManualFocusDistance = NewFocusDistance;
							}
							else
							{
								UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("'%s' could not evaluate raw focus value '%0.3f' using LensFile '%s'"), *GetName(), FrameData->FocusDistance, *SelectedLensFile->GetName())
							}
						}

						if (StaticData->bIsApertureSupported)
						{
							float NewAperture;
							if (SelectedLensFile->EvaluateNormalizedIris(FrameData->Aperture, NewAperture))
							{
								CineCameraComponent->CurrentAperture = NewAperture;
							}
							else
							{
								UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("'%s' could not evaluate raw iris value '%0.3f' using LensFile '%s'"), *GetName(), FrameData->FocusDistance, *SelectedLensFile->GetName())
							}
						}

						if (StaticData->bIsFocalLengthSupported)
						{
							float NewZoom;
							if (SelectedLensFile->EvaluateNormalizedZoom(FrameData->FocalLength, NewZoom))
							{
								CineCameraComponent->SetCurrentFocalLength(NewZoom);
							}
							else
							{
								UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("'%s' could not evaluate raw zoom value '%0.3f' using LensFile '%s'"), *GetName(), FrameData->FocalLength, *SelectedLensFile->GetName())
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

				// Compute and set the lens distortion material parameters
				UpdateDistortionSetup();

				if (LensDistortionSettings.bApplyDistortion)
				{
					// Evaluate the lens distortion parameters and camera intrinsics parameters from the selected Lens File
					if (SelectedLensFile != nullptr)
					{
						FDistortionParameters DistortionParams;
						FIntrinsicParameters IntrinsicParams;
					
						SelectedLensFile->EvaluateDistortionParameters(CineCameraComponent->CurrentFocusDistance, CineCameraComponent->CurrentFocalLength, DistortionParams);
						SelectedLensFile->EvaluateIntrinsicParameters(CineCameraComponent->CurrentFocusDistance, CineCameraComponent->CurrentFocalLength, IntrinsicParams);

						FLensDistortionCameraModel CameraModel;
						CameraModel.K1 = DistortionParams.K1;
						CameraModel.K2 = DistortionParams.K2;
						CameraModel.K3 = DistortionParams.K3;
						CameraModel.P1 = DistortionParams.P1;
						CameraModel.P2 = DistortionParams.P2;
						CameraModel.F.X = CineCameraComponent->CurrentFocalLength / OriginalCameraFilmback.SensorWidth;
						CameraModel.F.Y = CameraModel.F.X;
						CameraModel.C.X = 0.5f;
						CameraModel.C.Y = 0.5f;

						// Compute the HFOV based on the original filmback width
						// Note: The math for the HFOV is not technically correct, but works correctly with the soon-to-be-deprecated LensDistortion plugin methods
						//       When we rewrite the overscan calculation, we will update this math as well to be an accurate HFOV calculation.
						float OriginalHFOV = 2.0f * FMath::Atan(OriginalCameraFilmback.SensorWidth / (CineCameraComponent->CurrentFocalLength));

						if (LensDistortionSettings.bOverrideOverscanFactor == false)
						{
							LensDistortionSettings.OverscanFactor = CameraModel.GetUndistortOverscanFactor(OriginalHFOV, CineCameraComponent->AspectRatio);
							LensDistortionSettings.OverscanFactor += LensDistortionSettings.OverscanNudge;
						}

						// Modify the cinecamera filmback by the overscan factor to simulate a wider FOV image
						CineCameraComponent->Filmback.SensorWidth = OriginalCameraFilmback.SensorWidth * LensDistortionSettings.OverscanFactor;
						CineCameraComponent->Filmback.SensorHeight = OriginalCameraFilmback.SensorHeight * LensDistortionSettings.OverscanFactor;

						// Apply Distortion Parameters as Material Parameters
						LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("K1", DistortionParams.K1);
						LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("K2", DistortionParams.K2);
						LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("K3", DistortionParams.K3);
						LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("P1", DistortionParams.P1);
						LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("P2", DistortionParams.P2);

						LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("cx", IntrinsicParams.CenterShift.X);
						LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("cy", IntrinsicParams.CenterShift.Y);

						LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("OverscanFactor", LensDistortionSettings.OverscanFactor);
					}

					LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("ApplyDistortion", 1.0f);
					LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("sensor_w_mm", OriginalCameraFilmback.SensorWidth);
					LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("sensor_h_mm", OriginalCameraFilmback.SensorHeight);
					LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("fl_mm", CineCameraComponent->CurrentFocalLength);

					if (LensDistortionSettings.bEnableDistortionDebugView)
					{
						LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("DebugView", 1.0f);
					}
					else
					{
						LensDistortionSettings.LensDistortionMID->SetScalarParameterValue("DebugView", -1.0f);
					}
				}
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

#if WITH_EDITOR
void ULiveLinkCameraController::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FLensDistortionConfiguration, bApplyDistortion))
	{
		UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent);
		if (!CineCameraComponent)
		{
			return;
		}

		if (LensDistortionSettings.bApplyDistortion == true)
		{
			//Cache filmback to be able to recover it once distortion is turned off
			//Not part of the distortion setup since entering PIE will duplicate actor / components
			//and the duplicated one will already have modified filmback applied.
			OriginalCameraFilmback = CineCameraComponent->Filmback;
		}
		else
		{
			CineCameraComponent->Filmback = OriginalCameraFilmback;
		}
	}
}

bool ULiveLinkCameraController::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FLensDistortionConfiguration, OverscanFactor))
	{
		return (bIsEditable && LensDistortionSettings.bApplyDistortion && LensDistortionSettings.bOverrideOverscanFactor);
	}
	else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FLensDistortionConfiguration, OverscanNudge))
	{
		return (bIsEditable && LensDistortionSettings.bApplyDistortion && !LensDistortionSettings.bOverrideOverscanFactor);
	}
	return bIsEditable;
}

#endif

void ULiveLinkCameraController::UpdateDistortionSetup()
{
	UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent);
	if (CineCameraComponent)
	{
		if(LensDistortionSettings.bApplyDistortion && bIsDistortionSetup == false)
		{
			if (LensDistortionSettings.LensDistortionMID == nullptr)
			{
				//Will need to update material instance once we support user selected materials
				UMaterialInterface* DistortionMaterialParent = LoadObject<UMaterialInterface>(NULL, TEXT("/LensDistortion/Materials/MAT_brown3t2_overscan.MAT_brown3t2_overscan"), NULL, LOAD_None, NULL);
				LensDistortionSettings.LensDistortionMID = UMaterialInstanceDynamic::Create(DistortionMaterialParent, this);
			}

			CineCameraComponent->AddOrUpdateBlendable(LensDistortionSettings.LensDistortionMID);

			bIsDistortionSetup = true;
		}
		else if(bIsDistortionSetup && LensDistortionSettings.bApplyDistortion == false)
		{
			CineCameraComponent->RemoveBlendable(LensDistortionSettings.LensDistortionMID);
			bIsDistortionSetup = false;
		}
	}
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
