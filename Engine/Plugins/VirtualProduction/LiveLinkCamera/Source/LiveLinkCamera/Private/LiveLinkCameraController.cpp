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

namespace LiveLinkCameraControllerUtils
{
	/** 
	 * Finds indices of neighbor focus points for a given focus value and verify if both have only one zoom point 
	 * Copied from private LensTablesUtils.
	 */
	template<typename Type>
	bool HasSingleZoomPointInvolved(float InFocus, TConstArrayView<Type> Container)
	{
		if (Container.Num() <= 0)
		{
			return false;
		}

		int32 FirstFocusPointIndex = INDEX_NONE;
		int32 SecondFocusPointIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Container.Num(); ++Index)
		{
			const Type& Point = Container[Index];
			if (Point.Focus > InFocus)
			{
				SecondFocusPointIndex = Index;
				FirstFocusPointIndex = FMath::Max(Index - 1, 0);
				break;
			}
			else if (FMath::IsNearlyEqual(Point.Focus, InFocus))
			{
				//We found a point exactly matching the desired one
				SecondFocusPointIndex = Index;
				FirstFocusPointIndex = Index;
				break;
			}
		}

		//We haven't found a point, default to last one
		if (FirstFocusPointIndex == INDEX_NONE || SecondFocusPointIndex == INDEX_NONE)
		{
			SecondFocusPointIndex = Container.Num() - 1;
			FirstFocusPointIndex = Container.Num() - 1;
		}

		return Container[FirstFocusPointIndex].GetNumPoints() <= 1 && Container[SecondFocusPointIndex].GetNumPoints() <= 1;
	}
}

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
			//Stamp previous values that have an impact on frustum visual representation
			const float PreviousFOV = CameraComponent->FieldOfView;
			const float PreviousAspectRatio = CameraComponent->AspectRatio;
			const ECameraProjectionMode::Type PreviousProjectionMode = CameraComponent->ProjectionMode;

			if (StaticData->bIsFieldOfViewSupported && UpdateFlags.bApplyFieldOfView) { CameraComponent->SetFieldOfView(FrameData->FieldOfView); }
			if (StaticData->bIsAspectRatioSupported && UpdateFlags.bApplyAspectRatio) { CameraComponent->SetAspectRatio(FrameData->AspectRatio); }
			if (StaticData->bIsProjectionModeSupported && UpdateFlags.bApplyProjectionMode) { CameraComponent->SetProjectionMode(FrameData->ProjectionMode == ELiveLinkCameraProjectionMode::Perspective ? ECameraProjectionMode::Perspective : ECameraProjectionMode::Orthographic); }

			if (UCineCameraComponent* CineCameraComponent = Cast<UCineCameraComponent>(CameraComponent))
			{
				ULensFile* SelectedLensFile = LensFilePicker.GetLensFile();
				LensFileEvalData.LensFile = SelectedLensFile;

				// If we're using a lens file, use its sensor dimensions unless Live Link is streaming different values
				if (SelectedLensFile)
				{
					if (bUseCroppedFilmback)
					{
						LensFileEvalData.Distortion.Filmback.X = CroppedFilmback.SensorWidth;
					}
					else if ((StaticData->FilmBackWidth > 0.0f) && UpdateFlags.bApplyFilmBack) 
					{ 
						LensFileEvalData.Distortion.Filmback.X = StaticData->FilmBackWidth;
					}
					else
					{
						LensFileEvalData.Distortion.Filmback.X = SelectedLensFile->LensInfo.SensorDimensions.X;
					}

					if (bUseCroppedFilmback)
					{
						LensFileEvalData.Distortion.Filmback.Y = CroppedFilmback.SensorHeight;
					}
					else if ((StaticData->FilmBackHeight > 0.0f) && UpdateFlags.bApplyFilmBack)
					{
						LensFileEvalData.Distortion.Filmback.Y = StaticData->FilmBackHeight;
					}
					else
					{
						LensFileEvalData.Distortion.Filmback.Y = SelectedLensFile->LensInfo.SensorDimensions.Y;
					}

					CineCameraComponent->Filmback.SensorWidth = LensFileEvalData.Distortion.Filmback.X;
					CineCameraComponent->Filmback.SensorHeight = LensFileEvalData.Distortion.Filmback.Y;
				}
				else
				{
					if (StaticData->FilmBackWidth > 0.0f && UpdateFlags.bApplyFilmBack) 
					{ 
						CineCameraComponent->Filmback.SensorWidth = StaticData->FilmBackWidth; 
					}

					if (StaticData->FilmBackHeight > 0.0f && UpdateFlags.bApplyFilmBack) 
					{ 
						CineCameraComponent->Filmback.SensorHeight = StaticData->FilmBackHeight; 
					}
				}

				// If the filmback changed and there is a lens file selected, warn the user if it is incompatible with it
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

				//Verify different Lens Tables based on streamed FIZ at some intervals
				static const double TableVerificationInterval = 10;
				if ((FPlatformTime::Seconds() - LastLensTableVerificationTimestamp) >= TableVerificationInterval)
				{
					LastLensTableVerificationTimestamp = FPlatformTime::Seconds();
					VerifyFIZWithLensFileTables(SelectedLensFile, StaticData);
				}

				ApplyFIZ(SelectedLensFile, CineCameraComponent, StaticData, FrameData);
				ApplyDistortion(SelectedLensFile, CineCameraComponent, StaticData, FrameData);

				LastFilmback = CineCameraComponent->Filmback;

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
		// Initialize the most recent filmback and component transform to the current values of the camera to properly detect changes to this property
		LastFilmback = CineCameraComponent->Filmback;
		LastRotation = CineCameraComponent->GetRelativeRotation();
		LastLocation = CineCameraComponent->GetRelativeLocation();

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
bool ULiveLinkCameraController::CanEditChange(const FProperty* InProperty) const
{
	const FName PropertyName = InProperty->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkCameraController, bUseCroppedFilmback))
	{
		if (LensFilePicker.GetLensFile())
		{
			return true;
		}
		return false;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkCameraController, CroppedFilmback))
	{
		if (LensFilePicker.GetLensFile() && bUseCroppedFilmback)
		{
			return true;
		}
		return false;
	}

	return Super::CanEditChange(InProperty);
}

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
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FLensFilePicker, LensFile)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(FLensFilePicker, bUseDefaultLensFile))
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
		{
			ULensFile* SelectedLensFile = LensFilePicker.GetLensFile();
			if (SelectedLensFile)
			{
				if (SelectedLensFile->IsCineCameraCompatible(CineCameraComponent) == false)
				{
					UE_LOG(LogLiveLinkCameraController, Warning, TEXT("LensFile '%s' has a smaller sensor size than the CameraComponent of '%s' (driven by LiveLinkCameraController '%s')")
						, *SelectedLensFile->GetName()
						, *CineCameraComponent->GetName()
						, *this->GetName());
				}
			}
		}

		//When LensFile is changed, force update Table verification
		LastLensTableVerificationTimestamp = 0;
	}
}
#endif

void ULiveLinkCameraController::ApplyFIZ(ULensFile* LensFile, UCineCameraComponent* CineCameraComponent, const FLiveLinkCameraStaticData* StaticData, const FLiveLinkCameraFrameData* FrameData)
{
	/**
	 * The logic to apply fiz is :
	 * If there is a LensFile, give it LiveLink's Focus and Zoom to evaluate calibrated / mapped FIZ
	 * It is assumed that the LiveLink feed matches what what used to produce the lens file
	 * If no lens file is present, two choices :
	 * Use LiveLink data directly as usable FIZ. This could be coming from a tracking vendor for example
	 * Use cinecamera's min and max value range to denormalize inputs, assuming it is normalized.
	 */
	if (LensFile)
	{
		if (UpdateFlags.bApplyFocusDistance)
		{
			//If Focus encoder mapping is present, use it. If not, use incoming value directly if streamed in
			if (LensFile->HasFocusEncoderMapping())
			{
				CineCameraComponent->FocusSettings.ManualFocusDistance = LensFile->EvaluateNormalizedFocus(LensFileEvalData.Input.Focus);
			}
			else if (StaticData->bIsFocusDistanceSupported)
			{
				//If focus is streamed in, query the mapping if there is one. Otherwise, assume focus is usable as is
				CineCameraComponent->FocusSettings.ManualFocusDistance = LensFileEvalData.Input.Focus;
			}

			// Update the minimum focus of the camera (if needed)
			CineCameraComponent->LensSettings.MinimumFocusDistance = FMath::Min(CineCameraComponent->LensSettings.MinimumFocusDistance, CineCameraComponent->FocusSettings.ManualFocusDistance);
		}

		if (UpdateFlags.bApplyAperture)
		{
			//If Iris encoder mapping is present, use it. If not, use incoming value directly if streamed in
			if (LensFile->HasIrisEncoderMapping())
			{
				CineCameraComponent->CurrentAperture = LensFile->EvaluateNormalizedIris(LensFileEvalData.Input.Iris);
			}
			else if (StaticData->bIsApertureSupported)
			{
				CineCameraComponent->CurrentAperture = LensFileEvalData.Input.Iris;
			}

			// Update the minimum and maximum aperture of the camera (if needed)
			CineCameraComponent->LensSettings.MinFStop = FMath::Min(CineCameraComponent->LensSettings.MinFStop, CineCameraComponent->CurrentAperture);
			CineCameraComponent->LensSettings.MaxFStop = FMath::Max(CineCameraComponent->LensSettings.MaxFStop, CineCameraComponent->CurrentAperture);
		}
		
		if (UpdateFlags.bApplyFocalLength)
		{
			//To evaluate focal length, we need F/Z pair. If F or Z is not streamed, we use the default value (0)
			
			const float FocusValue = LensFileEvalData.Input.Focus;
			bool bHasValidFocalLength = false;
			const float ZoomValue = LensFileEvalData.Input.Zoom;
			FFocalLengthInfo FocalLengthInfo;
			if (LensFile->EvaluateFocalLength(FocusValue, ZoomValue, FocalLengthInfo))
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

					// Update the minimum and maximum focal length of the camera (if needed)
					CineCameraComponent->LensSettings.MinFocalLength = FMath::Min(CineCameraComponent->LensSettings.MinFocalLength, NewFocalLength);
					CineCameraComponent->LensSettings.MaxFocalLength = FMath::Max(CineCameraComponent->LensSettings.MaxFocalLength, NewFocalLength);

					CineCameraComponent->SetCurrentFocalLength(NewFocalLength);
					bHasValidFocalLength = true;
				}
			}

			//If FocalLength could not be applied and it's streamed in, use LiveLink input directly without affecting filmback
			if (StaticData->bIsFocalLengthSupported && bHasValidFocalLength == false)
			{
				// Update the minimum and maximum focal length of the camera (if needed)
				CineCameraComponent->LensSettings.MinFocalLength = FMath::Min(CineCameraComponent->LensSettings.MinFocalLength, ZoomValue);
				CineCameraComponent->LensSettings.MaxFocalLength = FMath::Max(CineCameraComponent->LensSettings.MaxFocalLength, ZoomValue);

				CineCameraComponent->SetCurrentFocalLength(ZoomValue);
			}
		}
	}
	else
	{
		//Use Min/Max values of each component to remap normalized incoming values
		if (bUseCameraRange)
		{
			if (StaticData->bIsFocusDistanceSupported && UpdateFlags.bApplyFocusDistance)
			{
				const float MinFocusDistanceInWorldUnits = CineCameraComponent->LensSettings.MinimumFocusDistance * (CineCameraComponent->GetWorldToMetersScale() / 1000.f);	// convert mm to uu
				const float NewFocusDistance = FMath::Lerp(MinFocusDistanceInWorldUnits, 100000.0f, FrameData->FocusDistance);
				CineCameraComponent->FocusSettings.ManualFocusDistance = NewFocusDistance;
			}
			if (StaticData->bIsApertureSupported && UpdateFlags.bApplyAperture)
			{
				const float NewAperture = FMath::Lerp(CineCameraComponent->LensSettings.MinFStop, CineCameraComponent->LensSettings.MaxFStop, FrameData->Aperture);
				CineCameraComponent->CurrentAperture = NewAperture;
			}
			if (StaticData->bIsFocalLengthSupported && UpdateFlags.bApplyFocalLength)
			{
				const float NewZoom = FMath::Lerp(CineCameraComponent->LensSettings.MinFocalLength, CineCameraComponent->LensSettings.MaxFocalLength, FrameData->FocalLength);
				CineCameraComponent->SetCurrentFocalLength(NewZoom);
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

			SelectedLensFile->EvaluateNodalPointOffset(LensFileEvalData.Input.Focus, LensFileEvalData.Input.Zoom, Offset);

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
	//Even if no FIZ was streamed in, we evaluate LensFile for distortion using default values (0.0f) like we do when applying FIZ
	if (LensFile)
	{
		UCameraCalibrationSubsystem* const SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
		const FString HandlerDisplayName = FString::Format(TEXT("{0} (Lens File)"), { LensFile->GetFName().ToString() });

		FDistortionHandlerPicker DistortionHandlerPicker = { CineCameraComponent, DistortionProducerID, HandlerDisplayName };
		LensDistortionHandler = SubSystem->FindOrCreateDistortionModelHandler(DistortionHandlerPicker, LensFile->LensInfo.LensModel);

		if (LensDistortionHandler)
		{
			//Go through the lens file to get distortion data based on FIZ
			//Our handler's displacement map will get updated
			LensFileEvalData.Distortion.bWasEvaluated = true;

			//Evaluate distortion using filmback not including SensorHeight modification based on Fy
			LensFile->EvaluateDistortionData(LensFileEvalData.Input.Focus
											, LensFileEvalData.Input.Zoom
											, LensFileEvalData.Distortion.Filmback
											, LensDistortionHandler);

			// Adjust overscan by the overscan multiplier
			if (bScaleOverscan)
			{
				const float ScaledOverscanFactor = ((LensDistortionHandler->GetOverscanFactor() - 1.0f) * OverscanMultiplier) + 1.0f;
				LensDistortionHandler->SetOverscanFactor(ScaledOverscanFactor);
			}
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

void ULiveLinkCameraController::VerifyFIZWithLensFileTables(ULensFile* LensFile, const FLiveLinkCameraStaticData* StaticData) const
{
	if (LensFile && StaticData)
	{
		const FLiveLinkSubjectKey FakedSubjectKey = { FGuid(), SelectedSubject.Subject };
		
		if (UpdateFlags.bApplyFocusDistance && StaticData->bIsFocusDistanceSupported == false)
		{
			// If the LiveLink source is not streaming focus, we will default to evaluate at 0.0f.
			// In this case, it is expected that there is only one mapping, so warn the user if this is not the case.
			if (LensFile->EncodersTable.GetNumFocusPoints() > 1)
			{
				static const FName NAME_InvalidFocusMappingWhenNotStreamed = "LiveLinkCamera_FocusNotStreamedInvalidMapping";
				FLiveLinkLog::WarningOnce(NAME_InvalidFocusMappingWhenNotStreamed, FakedSubjectKey, TEXT("Problem applying Focus for subject '%s' using LensFile '%s'. Focus wasn't streamed in and more than one focus mapping was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
			}
		}

		if (UpdateFlags.bApplyAperture && StaticData->bIsApertureSupported == false)
		{
			// If the LiveLink source is not streaming iris, we will default to evaluate at 0.0f.
			// In this case, it is expected that there is only one mapping, so warn the user if this is not the case.
			if (LensFile->EncodersTable.GetNumIrisPoints() > 1)
			{
				static const FName NAME_InvalidIrisMappingWhenNotStreamed = "LiveLinkCamera_IrisNotStreamedInvalidMapping";
				FLiveLinkLog::WarningOnce(NAME_InvalidIrisMappingWhenNotStreamed, FakedSubjectKey, TEXT("Problem applying Iris for subject '%s' using LensFile '%s'. Iris wasn't streamed in and more than one iris mapping was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
			}
		}

		if (UpdateFlags.bApplyFocalLength)
		{
			if (StaticData->bIsFocusDistanceSupported == false)
			{
				// If the LiveLink source is not streaming focus, we will default to evaluate at 0.0f.
				// In this case, it is expected that there is only one mapping, so warn the user if this is not the case.
				const int32 FocalLengthFocusPointCount = LensFile->FocalLengthTable.GetFocusPointNum();
				if (FocalLengthFocusPointCount > 0 && FocalLengthFocusPointCount != 1)
				{
					static const FName NAME_InvalidFocalLengthMappingWhenNoFocusStreamed = "LiveLinkCamera_FocusNotStreamedInvalidFocalLengthMapping";
					FLiveLinkLog::WarningOnce(NAME_InvalidFocalLengthMappingWhenNoFocusStreamed, FakedSubjectKey, TEXT("Problem applying FocalLength using subject '%s' and LensFile '%s'. Focus wasn't streamed in and more than one focal length focus point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
				}
			}

			if (StaticData->bIsFocalLengthSupported == false)
			{
				// If the LiveLink source is not streaming zoom, but the lens file has a valid mapping,
				// evaluate the mapping at 0.0f. In this case, it is expected that the mapping will have 
				// exactly one value in it, so warn the user if this is not the case.
				if (LensFile->FocalLengthTable.GetFocusPointNum() > 0)
				{
					//Find FocusPoints involved for streamed Focus value. If there are, make sure there is only one zoom point in both of them or warn the user
					if (LiveLinkCameraControllerUtils::HasSingleZoomPointInvolved(LensFileEvalData.Input.Focus, LensFile->FocalLengthTable.GetFocusPoints()) == false)
					{
						static const FName NAME_InvalidFocalLengthMappingWhenNotStreamed = "LiveLinkCamera_ZoomNotStreamedInvalidFocalLengthMapping";
						FLiveLinkLog::WarningOnce(NAME_InvalidFocalLengthMappingWhenNotStreamed, FakedSubjectKey, TEXT("Problem applying FocalLength using subject '%s' and LensFile '%s'. Zoom wasn't streamed in and more than one focal length zoom point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
					}
				}
			}
		}

		//Verify distortion, image center and nodal offset tables 
		{
			if (StaticData->bIsFocusDistanceSupported == false)
			{
				//No focus : Look for single focus point in tables
				if (LensFile->DataMode == ELensDataMode::Parameters && LensFile->DistortionTable.GetFocusPointNum() > 0 && LensFile->DistortionTable.GetFocusPointNum() != 1)
				{
					static const FName NAME_InvalidDistortionMappingWhenNotStreamed = "LiveLinkCamera_FocusNotStreamedInvalidDistortionMapping";
					FLiveLinkLog::WarningOnce(NAME_InvalidDistortionMappingWhenNotStreamed, FakedSubjectKey, TEXT("Potential problem evaluating Distortion data using subject '%s' and LensFile '%s'. Focus wasn't streamed in and more than one Distortion focus point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
				}

				if (LensFile->ImageCenterTable.GetFocusPointNum() > 0 && LensFile->ImageCenterTable.GetFocusPointNum() != 1)
				{
					static const FName NAME_InvalidImageCenterMappingWhenNotStreamed = "LiveLinkCamera_FocusNotStreamedInvalidImageCenterMapping";
					FLiveLinkLog::WarningOnce(NAME_InvalidImageCenterMappingWhenNotStreamed, FakedSubjectKey, TEXT("Potential problem evaluating ImageCenter data using subject '%s' and LensFile '%s'. Focus wasn't streamed in and more than one ImageCenter focus point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
				}

				if (bApplyNodalOffset && LensFile->NodalOffsetTable.GetFocusPointNum() > 0 && LensFile->NodalOffsetTable.GetFocusPointNum() != 1)
				{
					static const FName NAME_InvalidNodalOffsetMappingWhenNotStreamed = "LiveLinkCamera_FocusNotStreamedInvalidNodalOffsetMapping";
					FLiveLinkLog::WarningOnce(NAME_InvalidNodalOffsetMappingWhenNotStreamed, FakedSubjectKey, TEXT("Potential problem evaluating NodalOffset data using subject '%s' and LensFile '%s'. Focus wasn't streamed in and more than one NodalOffset focus point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
				}

				if (LensFile->DataMode == ELensDataMode::STMap && LensFile->STMapTable.GetFocusPointNum() > 0 && LensFile->STMapTable.GetFocusPointNum() != 1)
				{
					static const FName NAME_InvalidSTMapMappingWhenNotStreamed = "LiveLinkCamera_FocusNotStreamedInvalidSTMapMapping";
					FLiveLinkLog::WarningOnce(NAME_InvalidSTMapMappingWhenNotStreamed, FakedSubjectKey, TEXT("Potential problem evaluating STMap data using subject '%s' and LensFile '%s'. Focus wasn't streamed in and more than one STMap focus point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
				}
			}

			if(StaticData->bIsFocalLengthSupported == false)
			{
				//Zoom not supported -> Look for one zoom point in involved focus points
				{
					//Find FocusPoints involved for streamed Focus value. If there are, make sure there is only one zoom point in both of them or warn the user
					if (LensFile->DataMode == ELensDataMode::Parameters
						&& LensFile->DistortionTable.GetFocusPointNum() > 0 
						&& LiveLinkCameraControllerUtils::HasSingleZoomPointInvolved<FDistortionFocusPoint>(LensFileEvalData.Input.Focus, LensFile->DistortionTable.GetFocusPoints()) == false)
					{
						static const FName NAME_InvalidDistortionMappingWhenNotStreamed = "LiveLinkCamera_ZoomNotStreamedInvalidDistortionMapping";
						FLiveLinkLog::WarningOnce(NAME_InvalidDistortionMappingWhenNotStreamed, FakedSubjectKey, TEXT("Potential problem evaluating Distortion data using subject '%s' and LensFile '%s'. Zoom wasn't streamed in and more than one Distortion zoom point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
					}

					if (LensFile->DataMode == ELensDataMode::STMap
						&& LensFile->STMapTable.GetFocusPointNum() > 0
						&& LiveLinkCameraControllerUtils::HasSingleZoomPointInvolved<FSTMapFocusPoint>(LensFileEvalData.Input.Focus, LensFile->STMapTable.GetFocusPoints()) == false)
					{
						static const FName NAME_InvalidSTMapMappingWhenNotStreamed = "LiveLinkCamera_ZoomNotStreamedInvalidSTMapMapping";
						FLiveLinkLog::WarningOnce(NAME_InvalidSTMapMappingWhenNotStreamed, FakedSubjectKey, TEXT("Potential problem evaluating STMap data using subject '%s' and LensFile '%s'. Zoom wasn't streamed in and more than one STMap zoom point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
					}

					if (LensFile->ImageCenterTable.GetFocusPointNum() > 0
						&& LiveLinkCameraControllerUtils::HasSingleZoomPointInvolved<FImageCenterFocusPoint>(LensFileEvalData.Input.Focus, LensFile->ImageCenterTable.GetFocusPoints()) == false)
					{
						static const FName NAME_InvalidImageCenterMappingWhenNotStreamed = "LiveLinkCamera_ZoomNotStreamedInvalidImageCenterMapping";
						FLiveLinkLog::WarningOnce(NAME_InvalidImageCenterMappingWhenNotStreamed, FakedSubjectKey, TEXT("Potential problem evaluating ImageCenter data using subject '%s' and LensFile '%s'. Zoom wasn't streamed in and more than one ImageCenter zoom point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
					}

					if (bApplyNodalOffset && LensFile->NodalOffsetTable.GetFocusPointNum() > 0)
					{
						if (LiveLinkCameraControllerUtils::HasSingleZoomPointInvolved<FNodalOffsetFocusPoint>(LensFileEvalData.Input.Focus, LensFile->NodalOffsetTable.GetFocusPoints()) == false)
						{
							static const FName NAME_InvalidNodalOffsetMappingWhenNotStreamed = "LiveLinkCamera_ZoomNotStreamedInvalidNodalOffsetMapping";
							FLiveLinkLog::WarningOnce(NAME_InvalidNodalOffsetMappingWhenNotStreamed, FakedSubjectKey, TEXT("Potential problem evaluating NodalOffset data using subject '%s' and LensFile '%s'. Zoom wasn't streamed in and more than one NodalOffset zoom point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
						}
					}
				}
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
