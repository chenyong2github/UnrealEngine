// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensComponent.h"

#include "CameraCalibrationSubsystem.h"
#include "CineCameraComponent.h"
#include "Controllers/LiveLinkTransformController.h"
#include "Engine/Engine.h"
#include "ILiveLinkComponentModule.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkTransformRole.h"
#include "UObject/UE5MainStreamObjectVersion.h"

DEFINE_LOG_CATEGORY_STATIC(LogLensComponent, Log, All);

ULensComponent::ULensComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// This component is designed to tick after the LiveLink component, which uses TG_PrePhysics
	// We also use a tick prerequisite on LiveLink components, so technically this could also use TG_PrePhysics
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_DuringPhysics;
	bTickInEditor = true;

	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULensComponent::OnRegister()
{
	Super::OnRegister();

	InitDefaultCamera();

	// Register a callback to let us know when a new LiveLinkComponent is added to this component's parent actor
	// This gives us the opportunity to track when the subject representation of that LiveLinkComponent changes
	ILiveLinkComponentsModule& LiveLinkComponentsModule = FModuleManager::GetModuleChecked<ILiveLinkComponentsModule>(TEXT("LiveLinkComponents"));
	if (!LiveLinkComponentsModule.OnLiveLinkComponentRegistered().IsBoundToObject(this))
	{
		LiveLinkComponentsModule.OnLiveLinkComponentRegistered().AddUObject(this, &ULensComponent::OnLiveLinkComponentRegistered);
	}

	// Look for any LiveLinkComponents that were previously added to this component's parent actor
	TInlineComponentArray<ULiveLinkComponentController*> LiveLinkComponents;
	GetOwner()->GetComponents(LiveLinkComponents);

	for (ULiveLinkComponentController* LiveLinkComponent : LiveLinkComponents)
	{	
		AddTickPrerequisiteComponent(LiveLinkComponent);

		if (!LiveLinkComponent->OnLiveLinkControllersTicked().IsBoundToObject(this))
		{
			LiveLinkComponent->OnLiveLinkControllersTicked().AddUObject(this, &ULensComponent::ProcessLiveLinkData);
		}
	}
}

void ULensComponent::OnUnregister()
{
	Super::OnUnregister();

	ILiveLinkComponentsModule& LiveLinkComponentsModule = FModuleManager::GetModuleChecked<ILiveLinkComponentsModule>(TEXT("LiveLinkComponents"));
	LiveLinkComponentsModule.OnLiveLinkComponentRegistered().RemoveAll(this);

	TInlineComponentArray<ULiveLinkComponentController*> LiveLinkComponents;
	GetOwner()->GetComponents(LiveLinkComponents);

	for (ULiveLinkComponentController* LiveLinkComponent : LiveLinkComponents)
	{
		LiveLinkComponent->OnLiveLinkControllersTicked().RemoveAll(this);
	}
}

void ULensComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Get the focus and zoom values needed to evaluate the LensFile this Tick
	UpdateLensFileEvaluationInputs();

	// Attempt to apply nodal offset, which will only succeed if there is a valid LensFile, evaluation inputs, and component to offset
	if (bApplyNodalOffsetOnTick)
	{
		ApplyNodalOffset();
	}

	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner())))
	{
		DistortionSource.TargetCameraComponent = CineCameraComponent;

		UCameraCalibrationSubsystem* const SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();

		// Evaluate the selected lens file for distortion data using the current settings of the target camera
		ULensFile* LensFile = LensFilePicker.GetLensFile();
		if (bEvaluateLensFileForDistortion && LensFile)
		{
			if (!ProducedLensDistortionHandler)
			{
				CreateDistortionHandlerForLensFile();
			}

			//Go through the lens file to get distortion data based on FIZ
			//Our handler's displacement map will get updated
			const FVector2D CurrentSensorDimensions = FVector2D(CineCameraComponent->Filmback.SensorWidth, CineCameraComponent->Filmback.SensorHeight);

			if (SubSystem)
			{
				SubSystem->GetOriginalFocalLength(CineCameraComponent, OriginalFocalLength);
			}

			LensFile->EvaluateDistortionData(CineCameraComponent->CurrentFocusDistance, OriginalFocalLength, CurrentSensorDimensions, ProducedLensDistortionHandler);

			// Adjust overscan by the overscan multiplier
			if (bScaleOverscan)
			{
				const float ScaledOverscanFactor = ((ProducedLensDistortionHandler->GetOverscanFactor() - 1.0f) * OverscanMultiplier) + 1.0f;
				ProducedLensDistortionHandler->SetOverscanFactor(ScaledOverscanFactor);
			}

			// Track changes to the cine camera's focal length for consumers of distortion data
			if (SubSystem)
			{
				SubSystem->UpdateOriginalFocalLength(CineCameraComponent, CineCameraComponent->CurrentFocalLength);
			}
		}

		if (bApplyDistortion)
		{
			// Get the lens distortion handler for the target camera and distortion source
			ULensDistortionModelHandlerBase* LensDistortionHandler = nullptr;
			if (SubSystem)
			{
				LensDistortionHandler = SubSystem->FindDistortionModelHandler(DistortionSource);
			}

			if (LensDistortionHandler)
			{
				// Get the current distortion MID from the lens distortion handler
				UMaterialInstanceDynamic* NewDistortionMID = LensDistortionHandler->GetDistortionMID();

				// If the MID has changed
				if (LastDistortionMID != NewDistortionMID)
				{
					CineCameraComponent->RemoveBlendable(LastDistortionMID);
					CineCameraComponent->AddOrUpdateBlendable(NewDistortionMID);
				}

				// Cache the latest distortion MID
				LastDistortionMID = NewDistortionMID;

				SubSystem->GetOriginalFocalLength(CineCameraComponent, OriginalFocalLength);

				// Get the overscan factor and use it to modify the target camera's FOV
				const float OverscanFactor = LensDistortionHandler->GetOverscanFactor();
				const float OverscanSensorWidth = CineCameraComponent->Filmback.SensorWidth * OverscanFactor;
				const float OverscanFOV = FMath::RadiansToDegrees(2.0f * FMath::Atan(OverscanSensorWidth / (2.0f * OriginalFocalLength)));
				CineCameraComponent->SetFieldOfView(OverscanFOV);

				// Update the minimum and maximum focal length of the camera (if needed)
				CineCameraComponent->LensSettings.MinFocalLength = FMath::Min(CineCameraComponent->LensSettings.MinFocalLength, CineCameraComponent->CurrentFocalLength);
				CineCameraComponent->LensSettings.MaxFocalLength = FMath::Max(CineCameraComponent->LensSettings.MaxFocalLength, CineCameraComponent->CurrentFocalLength);

				SubSystem->UpdateOverscanFocalLength(CineCameraComponent, CineCameraComponent->CurrentFocalLength);

				bIsDistortionSetup = true;
			}
			else
			{
				CleanupDistortion(CineCameraComponent);
			}
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void ULensComponent::DestroyComponent(bool bPromoteChildren /*= false*/)
{
	Super::DestroyComponent(bPromoteChildren);

	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner())))
	{
		CleanupDistortion(CineCameraComponent);

		if (UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>())
		{
			SubSystem->UnregisterDistortionModelHandler(CineCameraComponent, ProducedLensDistortionHandler);
		}
	}
}

void ULensComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	InitDefaultCamera();

	// When this component is duplicated (e.g. for PIE), the duplicated component needs a new unique ID
	if (!DistortionProducerID.IsValid())
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULensComponent::PostEditImport()
{
	Super::PostEditImport();

	InitDefaultCamera();

	// When this component is duplicated (e.g. copy-paste), the duplicated component needs a new unique ID
	if (!DistortionProducerID.IsValid())
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULensComponent::PostLoad()
{
	Super::PostLoad();

	if (bIsDistortionSetup)
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner())))
		{
			CleanupDistortion(CineCameraComponent);
		}
		bIsDistortionSetup = false;
	}

#if WITH_EDITOR
	const int32 UE5MainVersion = GetLinkerCustomVersion(FUE5MainStreamObjectVersion::GUID);
	if (UE5MainVersion < FUE5MainStreamObjectVersion::LensComponentNodalOffset)
	{
		// If this component was previously applying nodal offset, reset the camera component's transform to its original pose
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner())))
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			if (bApplyNodalOffsetOnTick)
			{
				CineCameraComponent->SetRelativeLocation(OriginalCameraLocation_DEPRECATED);
				CineCameraComponent->SetRelativeRotation(OriginalCameraRotation_DEPRECATED.Quaternion());
			}
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
	}
#endif //WITH_EDITOR
}

#if WITH_EDITOR
void ULensComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULensComponent, bApplyDistortion))
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner())))
		{
			if (!bApplyDistortion)
			{
				CleanupDistortion(CineCameraComponent);
			}
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULensComponent, bEvaluateLensFileForDistortion))
	{
		if (bEvaluateLensFileForDistortion)
		{
			// Create a new distortion handler if one does not yet exist for the selected lens file
			if (!ProducedLensDistortionHandler)
			{
				CreateDistortionHandlerForLensFile();
			}
		}
		else
		{
			// Reset the distortion source settings
			if (DistortionSource.DistortionProducerID == DistortionProducerID)
			{
				DistortionSource.HandlerDisplayName.Empty();
				DistortionSource.DistortionProducerID.Invalidate();
			}

			// Unregister the produced handler from the subsystem
			if (UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>())
			{
				if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner())))
				{
					SubSystem->UnregisterDistortionModelHandler(CineCameraComponent, ProducedLensDistortionHandler);
				}
			}

			ProducedLensDistortionHandler = nullptr;
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULensComponent, TargetCameraComponent))
	{
		// Clean up distortion on the last target camera
		if (LastCameraComponent)
		{
			CleanupDistortion(LastCameraComponent);

			// Unregister the produced handler (if one exists) from the subsystem for the last camera component
			if (ProducedLensDistortionHandler)
			{
				if (UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>())
				{
					SubSystem->UnregisterDistortionModelHandler(LastCameraComponent, ProducedLensDistortionHandler);
				}

				ProducedLensDistortionHandler = nullptr;
			}
		}

		LastCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner()));
		DistortionSource.TargetCameraComponent = LastCameraComponent;

		// Create a new handler for the new target camera component
		if (bEvaluateLensFileForDistortion)
		{
			CreateDistortionHandlerForLensFile();
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FLensFilePicker, LensFile))
	{
		if (bEvaluateLensFileForDistortion)
		{
			CreateDistortionHandlerForLensFile();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif //WITH_EDITOR

void ULensComponent::OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	// The use of this callback by this class has been deprecated
}

void ULensComponent::ApplyNodalOffset()
{
	// Verify that we detected a tracked component for us to offset this tick. If there is none, it is likely because no LiveLink transform controller executed this tick.
	if (!TrackedComponent.IsValid())
	{
		return;
	}

	// Verify that the evaluation inputs are valid
	if (EvaluationMode == EFIZEvaluationMode::DoNotEvaluate)
	{
		return;
	}

	// Verify that there is a valid LensFile to evaluate
	ULensFile* LensFile = LensFilePicker.GetLensFile();
	if (!LensFile)
	{
		return;
	}

	FNodalPointOffset Offset;
	LensFile->EvaluateNodalPointOffset(EvalFocus, EvalZoom, Offset);

	// TODO: This would be a good time to cache the raw tracking pose before applying the nodal offset

	TrackedComponent.Get()->AddLocalOffset(Offset.LocationOffset);
	TrackedComponent.Get()->AddLocalRotation(Offset.RotationOffset);

	// Reset so that nodal offset will only be applied again next tick if new tracking data was applied between now and then
	TrackedComponent.Reset();
}

void ULensComponent::ApplyNodalOffset(USceneComponent* ComponentToOffset, bool bUseManualInputs, float ManualFocusInput, float ManualZoomInput)
{
	bApplyNodalOffsetOnTick = false;

	// Verify that the input component was not null
	if (!ComponentToOffset)
	{
		return;
	}

	// Verify that there is a valid LensFile to evaluate
	ULensFile* LensFile = LensFilePicker.GetLensFile();
	if (!LensFile)
	{
		return;
	}

	FNodalPointOffset Offset;
	if (bUseManualInputs)
	{
		LensFile->EvaluateNodalPointOffset(ManualFocusInput, ManualZoomInput, Offset);
	}
	else
	{
		LensFile->EvaluateNodalPointOffset(EvalFocus, EvalZoom, Offset);
	}

	ComponentToOffset->AddLocalOffset(Offset.LocationOffset);
	ComponentToOffset->AddLocalRotation(Offset.RotationOffset);
}

ULensFile* ULensComponent::GetLensFile() const
{
	return LensFilePicker.GetLensFile();
}

void ULensComponent::SetLensFile(FLensFilePicker LensFile)
{
	LensFilePicker = LensFile;
}

EFIZEvaluationMode ULensComponent::GetFIZEvaluationMode() const
{
	return EvaluationMode;
}

void ULensComponent::SetFIZEvaluationMode(EFIZEvaluationMode Mode)
{
	EvaluationMode = Mode;
}

bool ULensComponent::ShouldApplyNodalOffsetOnTick() const
{
	return bApplyNodalOffsetOnTick;
}

void ULensComponent::SetApplyNodalOffsetOnTick(bool bApplyNodalOffset)
{
	bApplyNodalOffsetOnTick = bApplyNodalOffset;
}

void ULensComponent::InitDefaultCamera()
{
	UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner()));
	if (!CineCameraComponent)
	{
		// Find all CineCameraComponents on the same actor as this component and set the first one to be the target
		TInlineComponentArray<UCineCameraComponent*> CineCameraComponents;
		GetOwner()->GetComponents(CineCameraComponents);
		if (CineCameraComponents.Num() > 0)
		{
			TargetCameraComponent.ComponentProperty = CineCameraComponents[0]->GetFName();
			LastCameraComponent = CineCameraComponents[0];
		}
	}
}

void ULensComponent::CleanupDistortion(UCineCameraComponent* const CineCameraComponent)
{
	if (bIsDistortionSetup)
	{
		// Remove the last distortion MID that was applied to the target camera component
		if (LastDistortionMID)
		{
			CineCameraComponent->RemoveBlendable(LastDistortionMID);
			LastDistortionMID = nullptr;
		}

		// Restore the original FOV of the target camera
		const float UndistortedFOV = FMath::RadiansToDegrees(2.0f * FMath::Atan(CineCameraComponent->Filmback.SensorWidth / (2.0f * OriginalFocalLength)));
		CineCameraComponent->SetFieldOfView(UndistortedFOV);

		// Update the minimum and maximum focal length of the camera (if needed)
		CineCameraComponent->LensSettings.MinFocalLength = FMath::Min(CineCameraComponent->LensSettings.MinFocalLength, CineCameraComponent->CurrentFocalLength);
		CineCameraComponent->LensSettings.MaxFocalLength = FMath::Max(CineCameraComponent->LensSettings.MaxFocalLength, CineCameraComponent->CurrentFocalLength);

		UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
		if (SubSystem)
		{
			SubSystem->UpdateOverscanFocalLength(CineCameraComponent, 0.0f);
		}
	}

	bIsDistortionSetup = false;
}

void ULensComponent::CreateDistortionHandlerForLensFile()
{
	if (DistortionSource.TargetCameraComponent == nullptr)
	{
		return;
	}

	ULensFile* const LensFile = LensFilePicker.GetLensFile();

	// Set the distortion source settings to match this component and the selected lens file
	if (LensFile)
	{
		DistortionSource.HandlerDisplayName = FString::Format(TEXT("{0} (Lens File) (Lens Distortion Component)"), { LensFile->GetFName().ToString() });
		DistortionSource.DistortionProducerID = DistortionProducerID;

		UCameraCalibrationSubsystem* const SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
		if (SubSystem)
		{
			ProducedLensDistortionHandler = SubSystem->FindOrCreateDistortionModelHandler(DistortionSource, LensFile->LensInfo.LensModel);
		}
	}
}

void ULensComponent::OnLiveLinkComponentRegistered(ULiveLinkComponentController* LiveLinkComponent)
{
	// Check that the new LiveLinkComponent that was just registered was added to the same actor that this component belongs to
	if (LiveLinkComponent->GetOwner() == GetOwner())
	{
		AddTickPrerequisiteComponent(LiveLinkComponent);

		if (!LiveLinkComponent->OnLiveLinkControllersTicked().IsBoundToObject(this))
		{
			LiveLinkComponent->OnLiveLinkControllersTicked().AddUObject(this, &ULensComponent::ProcessLiveLinkData);
		}
	}
}

void ULensComponent::ProcessLiveLinkData(const ULiveLinkComponentController* const LiveLinkComponent, const FLiveLinkSubjectFrameData& SubjectData)
{
	TSubclassOf<ULiveLinkRole> LiveLinkRole = LiveLinkComponent->GetSubjectRepresentation().Role;
	if (LiveLinkRole == ULiveLinkCameraRole::StaticClass())
	{
		UpdateTrackedComponent(LiveLinkComponent, SubjectData);
		UpdateLiveLinkFIZ(LiveLinkComponent, SubjectData);
	}
	else if (LiveLinkRole == ULiveLinkTransformRole::StaticClass())
	{
		UpdateTrackedComponent(LiveLinkComponent, SubjectData);
	}
}

void ULensComponent::UpdateTrackedComponent(const ULiveLinkComponentController* const LiveLinkComponent, const FLiveLinkSubjectFrameData& SubjectData)
{
	const FLiveLinkTransformStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkTransformStaticData>();
	const FLiveLinkTransformFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkTransformFrameData>();

	check(StaticData && FrameData);

	// Find the transform controller in the LiveLink component's controller map
	if (const TObjectPtr<ULiveLinkControllerBase>* TransformControllerPtr = LiveLinkComponent->ControllerMap.Find(ULiveLinkTransformRole::StaticClass()))
	{
		if (ULiveLinkTransformController* TransformController = Cast<ULiveLinkTransformController>(*TransformControllerPtr))
		{
			// Check LiveLink static data to ensure that location and rotation were supported by this subject
			bool bIsLocationRotationSupported = (StaticData->bIsLocationSupported && StaticData->bIsRotationSupported);

			// Check the transform controller usage flags to ensure that location and rotation were supported by the controller
			bIsLocationRotationSupported = bIsLocationRotationSupported && (TransformController->TransformData.bUseLocation && TransformController->TransformData.bUseRotation);

			if (bIsLocationRotationSupported)
			{
				TrackedComponent = Cast<USceneComponent>(TransformController->GetAttachedComponent());
			}
		}
	}
}

void ULensComponent::UpdateLiveLinkFIZ(const ULiveLinkComponentController* const LiveLinkComponent, const FLiveLinkSubjectFrameData& SubjectData)
{
	const FLiveLinkCameraStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkCameraStaticData>();
	const FLiveLinkCameraFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkCameraFrameData>();
	
	check(StaticData && FrameData);

	// Check that the camera component being controlled by the input LiveLink component matches our target camera
	if (const TObjectPtr<ULiveLinkControllerBase>* CameraControllerPtr = LiveLinkComponent->ControllerMap.Find(ULiveLinkCameraRole::StaticClass()))
	{
		if (ULiveLinkControllerBase* CameraController = *CameraControllerPtr)
		{
			if (CameraController->GetAttachedComponent() != TargetCameraComponent.GetComponent(GetOwner()))
			{
				return;
			}
		}
	}

	if (StaticData->bIsFocusDistanceSupported && StaticData->bIsFocalLengthSupported)
	{
		LiveLinkFIZ.Input.Focus = FrameData->FocusDistance;
		LiveLinkFIZ.Input.Zoom = FrameData->FocalLength;
	}
}

void ULensComponent::UpdateLensFileEvaluationInputs()
{
	// Query for the focus and zoom inputs to use to evaluate the LensFile. The source of these inputs will depend on the evaluation mode.
	if (EvaluationMode == EFIZEvaluationMode::UseLiveLink)
	{
		EvalFocus = LiveLinkFIZ.Input.Focus;
		EvalZoom = LiveLinkFIZ.Input.Zoom;
	}
	else if (EvaluationMode == EFIZEvaluationMode::UseCameraSettings)
	{
		if (const UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner())))
		{
			EvalFocus = CineCameraComponent->CurrentFocusDistance;
			EvalZoom = CineCameraComponent->CurrentFocalLength;
		}
	}
	else if (EvaluationMode == EFIZEvaluationMode::UseRecordedValues)
	{
		// Do nothing, the values for EvalFocus and EvalZoom are already loaded from the recorded sequence
	}
}

