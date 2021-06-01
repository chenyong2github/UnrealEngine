// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkCameraController.h"

#include "Camera/CameraComponent.h"
#include "CameraCalibrationSubsystem.h"
#include "CineCameraComponent.h"
#include "Controllers/LiveLinkTransformController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"
#include "ILiveLinkClient.h"
#include "LensFile.h"
#include "LiveLinkComponentController.h"
#include "LiveLinkLog.h"
#include "Logging/LogMacros.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "UObject/EnterpriseObjectVersion.h"

#if WITH_EDITOR
#include "Kismet2/ComponentEditorUtils.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogLiveLinkCameraController, Log, All);

ULiveLinkCameraController::ULiveLinkCameraController()
{
	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
	{
		//Hook up to PostActorTick to handle nodal offset
		FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ULiveLinkCameraController::OnPostActorTick);

		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULiveLinkCameraController::Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData)
{
	// Invalidate the lens file evaluation data
	LensFileEvalData.Invalidate();

	if (UCameraComponent* CameraComponent = Cast<UCameraComponent>(AttachedComponent))
	{
		if (AActor* Camera = Cast<AActor>(AttachedComponent->GetOwner()))
		{
			LensFileEvalData.Camera.UniqueId = Camera->GetUniqueID();
		}
	}

	const FLiveLinkCameraStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkCameraStaticData>();
	const FLiveLinkCameraFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkCameraFrameData>();

	if (StaticData && FrameData)
	{
		if (StaticData->bIsFocusDistanceSupported)
		{
			LensFileEvalData.Input.Focus = FrameData->FocusDistance;
		}

		if (StaticData->bIsApertureSupported)
		{
			LensFileEvalData.Input.Iris = FrameData->Aperture;
		}

		if (StaticData->bIsFocalLengthSupported)
		{
			LensFileEvalData.Input.Zoom = FrameData->FocalLength;
		}

		if (UCameraComponent* CameraComponent = Cast<UCameraComponent>(AttachedComponent))
		{
			bIsEncoderMappingNeeded = (StaticData->FIZDataMode == ECameraFIZMode::EncoderData);
			
			//Stamp previous values that have an impact on frustum visual representation
			const float PreviousFOV = CameraComponent->FieldOfView;
			const float PreviousAspectRatio = CameraComponent->AspectRatio;
			const ECameraProjectionMode::Type PreviousProjectionMode = CameraComponent->ProjectionMode;

			if (StaticData->bIsFieldOfViewSupported && UpdateFlags.bApplyFieldOfView) { CameraComponent->SetFieldOfView(FrameData->FieldOfView); }
			if (StaticData->bIsAspectRatioSupported && UpdateFlags.bApplyAspectRatio) { CameraComponent->SetAspectRatio(FrameData->AspectRatio); }
			if (StaticData->bIsProjectionModeSupported && UpdateFlags.bApplyProjectionMode) { CameraComponent->SetProjectionMode(FrameData->ProjectionMode == ELiveLinkCameraProjectionMode::Perspective ? ECameraProjectionMode::Perspective : ECameraProjectionMode::Orthographic); }

			if (UCineCameraComponent* CineCameraComponent = Cast<UCineCameraComponent>(CameraComponent))
			{
				if (StaticData->FilmBackWidth > 0.0f && UpdateFlags.bApplyFilmBack) { CineCameraComponent->Filmback.SensorWidth = StaticData->FilmBackWidth; }
				if (StaticData->FilmBackHeight > 0.0f && UpdateFlags.bApplyFilmBack) { CineCameraComponent->Filmback.SensorHeight = StaticData->FilmBackHeight; }
				
				ULensFile* SelectedLensFile = LensFilePicker.GetLensFile();
				LensFileEvalData.LensFile = SelectedLensFile;

				if (LastFilmback != CineCameraComponent->Filmback)
				{
					if (SelectedLensFile && SelectedLensFile->IsCineCameraCompatible(CineCameraComponent) == false)
					{
						UE_LOG(LogLiveLinkCameraController, Warning, TEXT("LensFile '%s' has a smaller sensor size than the CameraComponent of '%s' (driven by LiveLinkCameraController '%s')")
							, *SelectedLensFile->GetName()
							, *CineCameraComponent->GetOwner()->GetName()
							, *this->GetName());
					}
				}
				LastFilmback = CineCameraComponent->Filmback;

				ApplyFIZ(SelectedLensFile, CineCameraComponent, StaticData, FrameData);
				ApplyDistortion(SelectedLensFile, CineCameraComponent, StaticData, FrameData);

				UCameraCalibrationSubsystem* const SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
				if (SubSystem)
				{
					SubSystem->UpdateOriginalFocalLength(CineCameraComponent, CineCameraComponent->CurrentFocalLength);
				}
			}

#if WITH_EDITORONLY_DATA

			const bool bIsVisualImpacted = (FMath::IsNearlyEqual(PreviousFOV, CameraComponent->FieldOfView) == false)
			|| (FMath::IsNearlyEqual(PreviousAspectRatio, CameraComponent->AspectRatio) == false)
			|| (PreviousProjectionMode != CameraComponent->ProjectionMode);

			if (bShouldUpdateVisualComponentOnChange && bIsVisualImpacted)
			{
				CameraComponent->RefreshVisualRepresentation();
			}
#endif
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

void ULiveLinkCameraController::SetAttachedComponent(UActorComponent* ActorComponent)
{
	Super::SetAttachedComponent(ActorComponent);

	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
	{	
		// Initialize the most recent filmback to the current filmback of the camera to properly detect changes to this property
		LastFilmback = CineCameraComponent->Filmback;

		ULensFile* SelectedLensFile = LensFilePicker.GetLensFile();
		if (SelectedLensFile && SelectedLensFile->IsCineCameraCompatible(CineCameraComponent) == false)
		{
			UE_LOG(LogLiveLinkCameraController, Warning, TEXT("LensFile '%s' has a smaller sensor size than the CameraComponent of '%s' (driven by LiveLinkCameraController '%s')")
				, *SelectedLensFile->GetName()
				, *CineCameraComponent->GetOwner()->GetName()
				, *this->GetName());
		}
	}
}

void ULiveLinkCameraController::Cleanup()
{
	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
	{
		if (UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>())
		{
			SubSystem->UnregisterDistortionModelHandler(CineCameraComponent, LensDistortionHandler);
		}
	}

	FWorldDelegates::OnWorldPostActorTick.RemoveAll(this);
}

void ULiveLinkCameraController::OnEvaluateRegistered()
{
	//Reset flag until the next tick with actual data
	bIsEncoderMappingNeeded = false;
}

void ULiveLinkCameraController::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!DistortionProducerID.IsValid())
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULiveLinkCameraController::PostEditImport()
{
	Super::PostEditImport();

	if (!DistortionProducerID.IsValid())
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULiveLinkCameraController::SetApplyNodalOffset(bool bInApplyNodalOffset)
{
	if (bApplyNodalOffset == bInApplyNodalOffset)
	{
		return;
	}

	bApplyNodalOffset = bInApplyNodalOffset;

	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
	{
		if (bApplyNodalOffset)
		{
			OriginalCameraRotation = CineCameraComponent->GetRelativeRotation();
			OriginalCameraLocation = CineCameraComponent->GetRelativeLocation();
		}
		else
		{
			CineCameraComponent->SetRelativeLocation(OriginalCameraLocation);
			CineCameraComponent->SetRelativeRotation(OriginalCameraRotation.Quaternion());
		}
	}
}

#if WITH_EDITOR
void ULiveLinkCameraController::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkCameraController, bApplyNodalOffset))
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
		{
			if (bApplyNodalOffset)
			{
				OriginalCameraRotation = CineCameraComponent->GetRelativeRotation();
				OriginalCameraLocation = CineCameraComponent->GetRelativeLocation();
				UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("Enabling Nodal offset. Cached Location is '%s' and cached rotation is '%s'")
						, *OriginalCameraLocation.ToString()
						, *OriginalCameraRotation.ToString());
			}
			else
			{
				CineCameraComponent->SetRelativeLocation(OriginalCameraLocation);
				CineCameraComponent->SetRelativeRotation(OriginalCameraRotation.Quaternion());
			}
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FLensFilePicker, LensFile))
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
		{
			ULensFile* SelectedLensFile = LensFilePicker.GetLensFile();
			if (SelectedLensFile && SelectedLensFile->IsCineCameraCompatible(CineCameraComponent) == false)
			{
				UE_LOG(LogLiveLinkCameraController, Warning, TEXT("LensFile '%s' has a smaller sensor size than the CameraComponent of '%s' (driven by LiveLinkCameraController '%s')")
					, *SelectedLensFile->GetName()
					, *CineCameraComponent->GetName()
					, *this->GetName());
			}
		}
	}
}

#endif

void ULiveLinkCameraController::ApplyFIZ(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent, const FLiveLinkCameraStaticData* StaticData, const FLiveLinkCameraFrameData* FrameData)
{
	//When FIZ data comes from encoder, we need to map incoming values to actual FIZ
	if (bIsEncoderMappingNeeded)
	{
		bool bUseRange = !bUseLensFileForEncoderMapping;
		if (LensFile)
		{
			if (LensFileEvalData.Input.Focus.IsSet() && UpdateFlags.bApplyFocusDistance)
			{
				if (LensFile->HasFocusEncoderMapping())
				{
					CineCameraComponent->FocusSettings.ManualFocusDistance = LensFile->EvaluateNormalizedFocus(*LensFileEvalData.Input.Focus);
				}
				else
				{
					UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("'%s' could not evaluate raw focus value '%0.3f' using LensFile '%s'"), *GetName(), FrameData->FocusDistance, *LensFile->GetName())
				}
			}

			if (LensFileEvalData.Input.Iris.IsSet() && UpdateFlags.bApplyAperture)
			{
				if (LensFile->HasIrisEncoderMapping())
				{
					CineCameraComponent->CurrentAperture = LensFile->EvaluateNormalizedIris(*LensFileEvalData.Input.Iris);
				}
				else
				{
					UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("'%s' could not evaluate raw iris value '%0.3f' using LensFile '%s'"), *GetName(), FrameData->Aperture, *LensFile->GetName())
				}
			}

			if (LensFileEvalData.Input.Zoom.IsSet() && UpdateFlags.bApplyFocalLength)
			{
				//To evaluate focal length, we need F/Z pair. If focus is not available default to 0
				bool bHasValidFxFy = false;
				const float FocusValue = LensFileEvalData.Input.Focus.IsSet() ? LensFileEvalData.Input.Focus.GetValue() : 0.0f;
				FFocalLengthInfo FocalLengthInfo;
				if (LensFile->EvaluateFocalLength(FocusValue, LensFileEvalData.Input.Zoom.GetValue(), FocalLengthInfo))
				{
					if ((FocalLengthInfo.FxFy[0] > KINDA_SMALL_NUMBER) && (FocalLengthInfo.FxFy[1] > KINDA_SMALL_NUMBER))
					{
						// This is how field of view, filmback, and focal length are related:
						//
						// FOVx = 2*atan(1/(2*Fx)) = 2*atan(FilmbackX / (2*FocalLength))
						// => FocalLength = Fx*FilmbackX
						// 
						// FOVy = 2*atan(1/(2*Fy)) = 2*atan(FilmbackY / (2*FocalLength))
						// => FilmbackY = FocalLength / Fy

						// Adjust FocalLength and Filmback to match FxFy (which has already been divided by resolution in pixels)
						const float NewFocalLength = CineCameraComponent->CurrentFocalLength = FocalLengthInfo.FxFy[0] * CineCameraComponent->Filmback.SensorWidth;
						CineCameraComponent->Filmback.SensorHeight = CineCameraComponent->CurrentFocalLength / FocalLengthInfo.FxFy[1];
						CineCameraComponent->SetCurrentFocalLength(NewFocalLength);
						bHasValidFxFy = true;
					}
				}
				
				if(bHasValidFxFy == false)
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
				FLiveLinkLog::Warning(TEXT("'%s' needs encoder mapping and wants to use LensFile to remap but it's invalid. Falling back to default CineCamera parameters?"), *GetName());
			}
			
			//Fallback to camera component range
			bUseRange = true;
		}

		//Use Min/Max values of each component to remap normalized incoming values
		if(bUseRange)
		{
			if (StaticData->bIsFocusDistanceSupported && UpdateFlags.bApplyFocusDistance)
			{
				const float MinFocusDistanceInWorldUnits = CineCameraComponent->LensSettings.MinimumFocusDistance * (CineCameraComponent->GetWorldToMetersScale() / 1000.f);	// convert mm to uu
				float NewFocusDistance = FMath::Lerp(MinFocusDistanceInWorldUnits, 100000.0f, FrameData->FocusDistance);
				CineCameraComponent->FocusSettings.ManualFocusDistance = NewFocusDistance;
			}
			if (StaticData->bIsApertureSupported && UpdateFlags.bApplyAperture)
			{
				float NewAperture = FMath::Lerp(CineCameraComponent->LensSettings.MinFStop, CineCameraComponent->LensSettings.MaxFStop, FrameData->Aperture);
				CineCameraComponent->CurrentAperture = NewAperture;
			}
			if (StaticData->bIsFocalLengthSupported && UpdateFlags.bApplyFocalLength)
			{
				float NewZoom = FMath::Lerp(CineCameraComponent->LensSettings.MinFocalLength, CineCameraComponent->LensSettings.MaxFocalLength, FrameData->FocalLength);
				CineCameraComponent->SetCurrentFocalLength(NewZoom);
			}
		}
	}
	else
	{
		if (StaticData->bIsFocusDistanceSupported && UpdateFlags.bApplyFocusDistance) 
		{ 
			CineCameraComponent->FocusSettings.ManualFocusDistance = FrameData->FocusDistance; 
		}

		if (StaticData->bIsApertureSupported && UpdateFlags.bApplyAperture) 
		{ 
			CineCameraComponent->CurrentAperture = FrameData->Aperture; 
		}

		if (StaticData->bIsFocalLengthSupported && UpdateFlags.bApplyFocalLength)
		{ 
			CineCameraComponent->SetCurrentFocalLength(FrameData->FocalLength); 
		}
	}
}

void ULiveLinkCameraController::ApplyNodalOffset(ULensFile* SelectedLensFile, UCineCameraComponent* CineCameraComponent)
{
	if (CineCameraComponent)
	{
		const FRotator CurrentRotator = CineCameraComponent->GetRelativeRotation();
		const FVector CurrentTranslation = CineCameraComponent->GetRelativeLocation();
		
		//Verify if something was set by user / programmatically and update original values
		if (CurrentRotator != LastRotation)
		{
			if (CurrentRotator.Pitch != LastRotation.Pitch)
			{
				OriginalCameraRotation.Pitch = CurrentRotator.Pitch;
			}

			if (CurrentRotator.Yaw != LastRotation.Yaw)
			{
				OriginalCameraRotation.Yaw = CurrentRotator.Yaw;
			}

			if (CurrentRotator.Roll != LastRotation.Roll)
			{
				OriginalCameraRotation.Roll = CurrentRotator.Roll;
			}
		}

		if (CurrentTranslation != LastLocation)
		{
			if (CurrentTranslation.X != LastLocation.X)
			{
				OriginalCameraLocation.X = CurrentTranslation.X;
			}
			if (CurrentTranslation.Y != LastLocation.Y)
			{
				OriginalCameraLocation.Y = CurrentTranslation.Y;
			}
			if (CurrentTranslation.Z != LastLocation.Z)
			{
				OriginalCameraLocation.Z = CurrentTranslation.Z;
			}
		}

		if (bApplyNodalOffset && SelectedLensFile)
		{
			FNodalPointOffset Offset;

			SelectedLensFile->EvaluateNodalPointOffset(
				LensFileEvalData.Input.Focus.IsSet() ? *LensFileEvalData.Input.Focus : CineCameraComponent->CurrentFocusDistance,
				LensFileEvalData.Input.Zoom.IsSet() ? *LensFileEvalData.Input.Zoom : CineCameraComponent->CurrentFocalLength,
				Offset);

			LensFileEvalData.NodalOffset.bWasApplied = true;

			CineCameraComponent->SetRelativeLocation(OriginalCameraLocation);
			CineCameraComponent->SetRelativeRotation(OriginalCameraRotation.Quaternion());
			CineCameraComponent->AddLocalOffset(Offset.LocationOffset);
			CineCameraComponent->AddLocalRotation(Offset.RotationOffset);
		}

		LastLocation = CineCameraComponent->GetRelativeLocation();
		LastRotation = CineCameraComponent->GetRelativeRotation();
	}
	else 
	{
		LastLocation = FVector::OneVector;
		LastRotation = FRotator::ZeroRotator;
	}
}

void ULiveLinkCameraController::ApplyDistortion(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent, const FLiveLinkCameraStaticData* StaticData, const FLiveLinkCameraFrameData* FrameData)
{
	const bool bCanUpdateDistortion = (StaticData->bIsFocusDistanceSupported || StaticData->bIsFocalLengthSupported || StaticData->bIsFieldOfViewSupported);

	if (LensFile && bCanUpdateDistortion)
	{
		UCameraCalibrationSubsystem* const SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
		const FString HandlerDisplayName = FString::Format(TEXT("{0} (Lens File)"), { LensFile->GetFName().ToString() });

		FDistortionHandlerPicker DistortionHandlerPicker = { CineCameraComponent, DistortionProducerID, HandlerDisplayName };
		LensDistortionHandler = SubSystem->FindOrCreateDistortionModelHandler(DistortionHandlerPicker, LensFile->LensInfo.LensModel);

		if (LensDistortionHandler)
		{
			//Go through the lens file to get distortion data based on FIZ
			//Our handler's displacement map will get updated
			FDistortionData DistortionData;

			const FVector2D CurrentSensorDimensions = FVector2D(
				CineCameraComponent->Filmback.SensorWidth, 
				CineCameraComponent->Filmback.SensorHeight
			);
			
			// Cache Lens evaluation data
			LensFileEvalData.Distortion.bWasEvaluated = true;

			LensFile->EvaluateDistortionData(
				LensFileEvalData.Input.Focus.IsSet() ? *LensFileEvalData.Input.Focus : CineCameraComponent->CurrentFocusDistance,
				LensFileEvalData.Input.Zoom.IsSet() ? *LensFileEvalData.Input.Zoom : CineCameraComponent->CurrentFocalLength,
				CurrentSensorDimensions, 
				LensDistortionHandler, 
				DistortionData
			);
		}
	}
}

void ULiveLinkCameraController::OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World == GetWorld())
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
		{
			if (ULensFile* CurrentLensFile = LensFilePicker.GetLensFile())
			{
				ApplyNodalOffset(CurrentLensFile, CineCameraComponent);
			}
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

const FLensFileEvalData& ULiveLinkCameraController::GetLensFileEvalDataRef() const
{
	return LensFileEvalData;
}
