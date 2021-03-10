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

ULiveLinkCameraController::ULiveLinkCameraController()
{
	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
	{
		//Hook up to PostActorTick to handle nodal offset
		FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ULiveLinkCameraController::OnPostActorTick);
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
			
			if (StaticData->bIsFieldOfViewSupported) { CameraComponent->SetFieldOfView(FrameData->FieldOfView); }
			if (StaticData->bIsAspectRatioSupported) { CameraComponent->SetAspectRatio(FrameData->AspectRatio); }
			if (StaticData->bIsProjectionModeSupported) { CameraComponent->SetProjectionMode(FrameData->ProjectionMode == ELiveLinkCameraProjectionMode::Perspective ? ECameraProjectionMode::Perspective : ECameraProjectionMode::Orthographic); }

			if (UCineCameraComponent* CineCameraComponent = Cast<UCineCameraComponent>(CameraComponent))
			{
				if (StaticData->FilmBackWidth > 0.0f) { CineCameraComponent->Filmback.SensorWidth = StaticData->FilmBackWidth; }
				if (StaticData->FilmBackHeight > 0.0f) { CineCameraComponent->Filmback.SensorHeight = StaticData->FilmBackHeight; }
				
				ULensFile* SelectedLensFile = LensFilePicker.GetLensFile();

				ApplyFIZ(SelectedLensFile, CineCameraComponent, StaticData, FrameData);
				ApplyDistortion(SelectedLensFile, CineCameraComponent);
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

void ULiveLinkCameraController::SetAttachedComponent(UActorComponent* ActorComponent)
{
	const bool bHasChangedComponent = ActorComponent != AttachedComponent;
	if (bHasChangedComponent)
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

void ULiveLinkCameraController::Cleanup()
{
	CleanupDistortion();

	FWorldDelegates::OnWorldPostActorTick.RemoveAll(this);
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
				OriginalCameraFilmback = CineCameraComponent->Filmback;
				UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("Enabling distortion. Cached Filmback is %0.3fmm x %0.3fmm"), OriginalCameraFilmback.SensorWidth, OriginalCameraFilmback.SensorHeight);

				UpdateDistortionHandler(CineCameraComponent);
			}
			else
			{
				CineCameraComponent->Filmback = OriginalCameraFilmback;
				LensDistortionHandler = nullptr;
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
}

#endif

void ULiveLinkCameraController::ApplyFIZ(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent, const FLiveLinkCameraStaticData* StaticData, const FLiveLinkCameraFrameData* FrameData)
{
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

	UpdateCachedFilmback(CineCameraComponent);

	// Update DistortionHandler's state based on the lens file data
	if (LensDistortionHandler)
	{
		if (LensFile != nullptr)
		{
			/** Update the lens distortion handler with the evaluated data from the lens file */
			FLensDistortionState DistortionState;
			DistortionState.LensModel = LensFile->LensInfo.LensModel;

			FDistortionParameters DistortionParams;
			FIntrinsicParameters IntrinsicParams;
			LensFile->EvaluateDistortionParameters(CineCameraComponent->CurrentFocusDistance, CineCameraComponent->CurrentFocalLength, DistortionParams);
			LensFile->EvaluateIntrinsicParameters(CineCameraComponent->CurrentFocusDistance, CineCameraComponent->CurrentFocalLength, IntrinsicParams);

			DistortionState.DistortionParameters = MoveTemp(DistortionParams);
			DistortionState.PrincipalPoint = MoveTemp(IntrinsicParams.CenterShift);

			/** The sensor dimensions must be the original dimensions of the source camera (with no overscan applied) */
			DistortionState.SensorDimensions = FVector2D(OriginalCameraFilmback.SensorWidth, OriginalCameraFilmback.SensorHeight);
			DistortionState.FocalLength = CineCameraComponent->CurrentFocalLength;

			LensDistortionHandler->Update(DistortionState);
		}
		else
		{
			const FVector2D SensorDimensions(OriginalCameraFilmback.SensorWidth, OriginalCameraFilmback.SensorHeight);
			LensDistortionHandler->UpdateCameraSettings(SensorDimensions, CineCameraComponent->CurrentFocalLength);
		}

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

void ULiveLinkCameraController::UpdateDistortionHandler(UCineCameraComponent* CineCameraComponent)
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

void ULiveLinkCameraController::UpdateCachedFilmback(UCineCameraComponent* CineCameraComponent)
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

		UE_LOG(LogLiveLinkCameraController, Verbose, TEXT("Updating cached Filmback to %0.3fmm x %0.3fmm"), OriginalCameraFilmback.SensorWidth, OriginalCameraFilmback.SensorHeight);
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
			UpdateCachedFilmback(CineCameraComponent);
			CineCameraComponent->Filmback = OriginalCameraFilmback;
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

