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
	/**
	 * The logic to apply fiz is :
	 * If there is a LensFile, give it LiveLink's Focus and Zoom to evaluate calibrated / mapped FIZ
	 * It is assumed that the LiveLink feed matches what what used to produce the lens file
	 * If no lens file is present, two choices :
	 * Use LiveLink data directly as usable FIZ. This could be coming from a tracking vendor for example
	 * Use cinecamera's min and max value range to denormalize inputs, assuming it is noramlized.
	 */
	if (LensFile)
	{
		if (UpdateFlags.bApplyFocusDistance)
		{
			if(LensFileEvalData.Input.Focus.IsSet())
			{
				//If focus is streamed in, query the mapping if there is one. Otherwise, use focus as is
				float FocusDistance = LensFileEvalData.Input.Focus.GetValue(); 
				if (LensFile->HasFocusEncoderMapping())
				{
					FocusDistance = LensFile->EvaluateNormalizedFocus(*LensFileEvalData.Input.Focus);
				}

				CineCameraComponent->FocusSettings.ManualFocusDistance = FocusDistance;
			}
			else
			{
				// If the LiveLink source is not streaming focus, but the lens file has a valid mapping for focus,
				// evaluate the mapping at 0.0f. In this case, it is expected that the mapping will have 
				// exactly one value in it, so warn the user if this is not the case.
				if(LensFile->EncodersTable.GetNumFocusPoints() > 1)
				{
					static const FName NAME_InvalidFocusMappingWhenNotStreamed = "LiveLinkCamera_FocusNotStreamedInvalidMapping";
					const FLiveLinkSubjectKey FakedSubjectKey = {FGuid(), SelectedSubject.Subject};
					FLiveLinkLog::WarningOnce(NAME_InvalidFocusMappingWhenNotStreamed, FakedSubjectKey, TEXT("Problem applying Focus for subject '%s' using LensFile '%s'. Focus wasn't streamed in and more than one focus mapping was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
				}

				//If focus wasn't streamed, only set focus distance if there is a mapping
				if (LensFile->HasFocusEncoderMapping())
				{
					CineCameraComponent->FocusSettings.ManualFocusDistance = LensFile->EvaluateNormalizedFocus(0.0f);
				}
			}
		}

		if(LensFileEvalData.Input.Iris.IsSet())
		{
			//If iris is streamed in, query the mapping if there is one. Otherwise, use iris as is
			float Aperture = LensFileEvalData.Input.Iris.GetValue(); 
			if (LensFile->HasIrisEncoderMapping())
			{
				Aperture = LensFile->EvaluateNormalizedIris(*LensFileEvalData.Input.Iris);
			}

			CineCameraComponent->CurrentAperture = Aperture;
		}
		else
		{
			// If the LiveLink source is not streaming iris, but the lens file has a valid mapping for iris,
			// evaluate the mapping at 0.0f. In this case, it is expected that the mapping will have 
			// exactly one value in it, so warn the user if this is not the case.
			if(LensFile->EncodersTable.GetNumIrisPoints() > 1)
			{
				static const FName NAME_InvalidIrisMappingWhenNotStreamed = "LiveLinkCamera_IrisNotStreamedInvalidMapping";
				const FLiveLinkSubjectKey FakedSubjectKey = {FGuid(), SelectedSubject.Subject};
				FLiveLinkLog::WarningOnce(NAME_InvalidIrisMappingWhenNotStreamed, FakedSubjectKey, TEXT("Problem applying Iris for subject '%s' using LensFile '%s'. Iris wasn't streamed in and more than one iris mapping was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
			}

			//If iris wasn't streamed, only set aperture if there is a mapping
			if (LensFile->HasIrisEncoderMapping())
			{
				CineCameraComponent->CurrentAperture = LensFile->EvaluateNormalizedIris(0.0f);
			}
		}
		
		if (UpdateFlags.bApplyFocalLength)
		{
			//To evaluate focal length, we need F/Z pair. If focus is not available default to 0, same for zoom if it's not available
			
			float FocusValue = 0.0f;
			if(LensFileEvalData.Input.Focus.IsSet())
			{
				FocusValue = LensFileEvalData.Input.Focus.GetValue();
			}
			else
			{
				// If the LiveLink source is not streaming focus, but the lens file has a valid mapping for FocalLength,
				// evaluate the mapping at 0.0f. In this case, it is expected that the mapping will have 
				// exactly one value in it, so warn the user if this is not the case.
				const int32 FocalLengthFocusPointCount = LensFile->FocalLengthTable.GetFocusPointNum();
				if(FocalLengthFocusPointCount > 0 && FocalLengthFocusPointCount != 1)
				{
					static const FName NAME_InvalidFocalLengthMappingWhenNoFocusStreamed = "LiveLinkCamera_FocusNotStreamedInvalidFocusMapping";
					const FLiveLinkSubjectKey FakedSubjectKey = {FGuid(), SelectedSubject.Subject};
					FLiveLinkLog::WarningOnce(NAME_InvalidFocalLengthMappingWhenNoFocusStreamed, FakedSubjectKey, TEXT("Problem applying FocalLength using subject '%s' and LensFile '%s'. Focus wasn't streamed in and more than one focal length focus point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
				}
			}
			
			if(LensFileEvalData.Input.Zoom.IsSet())
			{
				bool bHasValidFocalLength = false;
				const float ZoomValue = LensFileEvalData.Input.Zoom.GetValue();
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
						CineCameraComponent->SetCurrentFocalLength(NewFocalLength);
						bHasValidFocalLength = true;
					}
				}

				//If FocalLength could not be applied, use LiveLink input directly without affecting filmback
				if (bHasValidFocalLength == false)
				{
					CineCameraComponent->SetCurrentFocalLength(ZoomValue);
				}
			}
			else
			{
				// If the LiveLink source is not streaming zoom, but the lens file has a valid mapping,
				// evaluate the mapping at 0.0f. In this case, it is expected that the mapping will have 
				// exactly one value in it, so warn the user if this is not the case.
				const FFocalLengthFocusPoint* FocusPoint = LensFileEvalData.Input.Focus.IsSet() ? LensFile->FocalLengthTable.GetFocusPoint(LensFileEvalData.Input.Focus.GetValue()) : LensFile->FocalLengthTable.GetFocusPointNum() > 0 ? &LensFile->FocalLengthTable.GetFocusPoints()[0] : nullptr;
				if(FocusPoint && FocusPoint->GetNumPoints() != 1)
				{
					static const FName NAME_InvalidFocalLengthMappingWhenNotStreamed = "LiveLinkCamera_ZoomNotStreamedInvalidMapping";
					const FLiveLinkSubjectKey FakedSubjectKey = {FGuid(), SelectedSubject.Subject};
					FLiveLinkLog::WarningOnce(NAME_InvalidFocalLengthMappingWhenNotStreamed, FakedSubjectKey, TEXT("Problem applying FocalLength using subject '%s' and LensFile '%s'. Zoom wasn't streamed in and more than one focal length zoom point was found."), *FakedSubjectKey.SubjectName.ToString(), *LensFile->GetName());
				}

				//If evaluating FocalLength with default FZ pair fails, don't update it
				constexpr float ZoomValue = 0.0f;
				FFocalLengthInfo FocalLengthInfo;
				if (LensFile->EvaluateFocalLength(FocusValue, ZoomValue, FocalLengthInfo))
				{
					if ((FocalLengthInfo.FxFy[0] > KINDA_SMALL_NUMBER) && (FocalLengthInfo.FxFy[1] > KINDA_SMALL_NUMBER))
					{
						// See above for explanation how focal length is applied
						// Adjust FocalLength and Filmback to match FxFy (which has already been divided by resolution in pixels)
						const float NewFocalLength = CineCameraComponent->CurrentFocalLength = FocalLengthInfo.FxFy[0] * CineCameraComponent->Filmback.SensorWidth;
						CineCameraComponent->Filmback.SensorHeight = CineCameraComponent->CurrentFocalLength / FocalLengthInfo.FxFy[1];
						CineCameraComponent->SetCurrentFocalLength(NewFocalLength);
					}
				}
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
				LensDistortionHandler
			);

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
