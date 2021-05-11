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
#include "Logging/LogMacros.h"
#include "LiveLinkLog.h"
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
	const FLiveLinkCameraStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkCameraStaticData>();
	const FLiveLinkCameraFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkCameraFrameData>();

	if (StaticData && FrameData)
	{
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
				ApplyDistortion(SelectedLensFile, CineCameraComponent);
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

		// Initialize the most recent filmback to the current filmback of the camera to properly detect changes to this property
		LastFilmback = CineCameraComponent->Filmback;

		//When our component has changed, make sure we update our 
		//cached/original filmback to restore it correctly
		//PIE case is odd because the camera is duplicated from editor and its filmback will already be distorted
		if (bApplyDistortion == false 
			|| (GetWorld() && GetWorld()->WorldType != EWorldType::PIE))
		{
			UpdateCachedFocalLength(CineCameraComponent);
		}

		UpdateDistortionHandler(CineCameraComponent);

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
	CleanupDistortion();

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

#if WITH_EDITOR
void ULiveLinkCameraController::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkCameraController, bApplyDistortion))
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
		{
			if (bApplyDistortion)
			{
				/** 
				 * Cache filmback to be able to recover it once distortion is turned off
				 * not part of the distortion setup since entering PIE will duplicate actor / components
				 * and the duplicated one will already have modified filmback applied.
				 */
				UndistortedFocalLength = CineCameraComponent->CurrentFocalLength;
				UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("Enabling distortion. Cached focal length is %0.3fmm"), UndistortedFocalLength);

				UpdateDistortionHandler(CineCameraComponent);
			}
			else
			{
				// Static lens model data may not be available because the controller may not be ticking, so query the subsystem for any distortion handler
				UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
				FDistortionHandlerPicker DistortionHandlerPicker;
				DistortionHandlerPicker.TargetCameraComponent = CineCameraComponent;
				DistortionHandlerPicker.DistortionProducerID = DistortionProducerID;
				ULensDistortionModelHandlerBase* Handler = SubSystem->FindDistortionModelHandler(DistortionHandlerPicker);

				// Try to remove the post-process material from the cine camera's blendables. 
				if (Handler)
				{
					CineCameraComponent->RemoveBlendable(Handler->GetDistortionMID());
				}

				const float OriginalFOV = FMath::RadiansToDegrees(2.0f * FMath::Atan(CineCameraComponent->Filmback.SensorWidth / (2.0f * UndistortedFocalLength)));
				CineCameraComponent->SetFieldOfView(OriginalFOV);
			}
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkCameraController, bApplyNodalOffset))
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
			if (StaticData->bIsFocusDistanceSupported && UpdateFlags.bApplyFocusDistance)
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

			if (StaticData->bIsApertureSupported && UpdateFlags.bApplyAperture)
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

			if (StaticData->bIsFocalLengthSupported && UpdateFlags.bApplyFocalLength)
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
		if (StaticData->bIsFocusDistanceSupported && UpdateFlags.bApplyFocusDistance) { CineCameraComponent->FocusSettings.ManualFocusDistance = FrameData->FocusDistance; }
		if (StaticData->bIsApertureSupported && UpdateFlags.bApplyAperture) { CineCameraComponent->CurrentAperture = FrameData->Aperture; }
		if (StaticData->bIsFocalLengthSupported && UpdateFlags.bApplyFocalLength) { CineCameraComponent->SetCurrentFocalLength(FrameData->FocalLength); }
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
			if (SelectedLensFile->EvaluateNodalPointOffset(CineCameraComponent->CurrentFocusDistance, CineCameraComponent->CurrentFocalLength, Offset))
			{
				CineCameraComponent->SetRelativeLocation(OriginalCameraLocation);
				CineCameraComponent->SetRelativeRotation(OriginalCameraRotation.Quaternion());
				CineCameraComponent->AddLocalOffset(Offset.LocationOffset);
				CineCameraComponent->AddLocalRotation(Offset.RotationOffset);
			}
		}

		LastLocation= CineCameraComponent->GetRelativeLocation();
		LastRotation = CineCameraComponent->GetRelativeRotation();
	}
	else 
	{
		LastLocation = FVector::OneVector;
		LastRotation = FRotator::ZeroRotator;
	}
}

void ULiveLinkCameraController::ApplyDistortion(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent)
{
	UMaterialInstanceDynamic* NewDistortionMID = nullptr;

	UpdateCachedFocalLength(CineCameraComponent);

	if (LensFile)
	{
		UCameraCalibrationSubsystem* const SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
		const FString HandlerDisplayName = FString::Format(TEXT("{0} (Lens File)"), { LensFile->GetFName().ToString() });

		FDistortionHandlerPicker DistortionHandlerPicker = { CineCameraComponent, DistortionProducerID, HandlerDisplayName };
		LensDistortionHandler = SubSystem->FindOrCreateDistortionModelHandler(DistortionHandlerPicker, LensFile->LensInfo.LensModel);
	}

	if (LensDistortionHandler)
	{
		if (LensFile != nullptr)
		{
			//Go through the lens file to get distortion data based on FIZ
			//Our handler's displacement map will get updated
			FDistortionData DistortionData;
			const FVector2D CurrentSensorDimensions = FVector2D(CineCameraComponent->Filmback.SensorWidth, CineCameraComponent->Filmback.SensorHeight);
			LensFile->EvaluateDistortionData(CineCameraComponent->CurrentFocusDistance, UndistortedFocalLength, CurrentSensorDimensions, LensDistortionHandler, DistortionData);
		}

		NewDistortionMID = LensDistortionHandler->GetDistortionMID();

		if (bApplyDistortion)
		{
			// Scale the camera's FOV by an overscan factor
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

void ULiveLinkCameraController::UpdateDistortionHandler(UCineCameraComponent* CineCameraComponent)
{
 	if (LensDistortionHandler)
 	{
 		//Cache MID when changing handler to start with something valid. i.e. Cleaning MID from blendables if LL was never valid
 		LastDistortionMID = LensDistortionHandler->GetDistortionMID();
 	}
}

void ULiveLinkCameraController::UpdateCachedFocalLength(UCineCameraComponent* CineCameraComponent)
{
	//Verify if focal length was changed by the user. If that's the case, take the current value and update the cached one
	if (CineCameraComponent->CurrentFocalLength != LastFocalLength)
	{
		UndistortedFocalLength = CineCameraComponent->CurrentFocalLength;

		UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("Updating cached focal length to %0.3fmm"), UndistortedFocalLength);
	}
}

void ULiveLinkCameraController::CleanupDistortion()
{
	//Remove MID we could have added to the component
	if (bIsDistortionSetup)
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
		{
			CineCameraComponent->RemoveBlendable(LastDistortionMID);
			
			//Update cached filmback before resetting it. We could have stopped ticking because the LiveLink component was stopped
			//In the meantime, filmback could have been manually changed and our cache is out of date.
			UpdateCachedFocalLength(CineCameraComponent);

			const float OriginalFOV = FMath::RadiansToDegrees(2.0f * FMath::Atan(CineCameraComponent->Filmback.SensorWidth / (2.0f * UndistortedFocalLength)));
			CineCameraComponent->SetFieldOfView(OriginalFOV);
		}
	}

	bIsDistortionSetup = false;
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
