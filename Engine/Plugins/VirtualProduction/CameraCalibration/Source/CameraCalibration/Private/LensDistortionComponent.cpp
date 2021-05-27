// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionComponent.h"

#include "CameraCalibrationSubsystem.h"
#include "CineCameraComponent.h"
#include "Engine/Engine.h"
#include "LiveLinkComponentController.h"

ULensDistortionComponent::ULensDistortionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_LastDemotable;
	bTickInEditor = true;

	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULensDistortionComponent::OnRegister()
{
	Super::OnRegister();

	InitDefaultCamera();
}

void ULensDistortionComponent::DestroyComponent(bool bPromoteChildren /*= false*/)
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

void ULensDistortionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner())))
	{
		// Add a tick prerequisite on any live link components also on this camera
		// LiveLink might set the focal length of the target camera, so it is important that this component ticks after that
		TInlineComponentArray<ULiveLinkComponentController*> LiveLinkComponents;
		CineCameraComponent->GetOwner()->GetComponents(LiveLinkComponents);

		for (UActorComponent* LiveLinkComponent : LiveLinkComponents)
		{
			AddTickPrerequisiteComponent(LiveLinkComponent);
		}

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
			FDistortionData DistortionData;
			const FVector2D CurrentSensorDimensions = FVector2D(CineCameraComponent->Filmback.SensorWidth, CineCameraComponent->Filmback.SensorHeight);

			LensFile->EvaluateDistortionData(CineCameraComponent->CurrentFocusDistance, OriginalFocalLength, CurrentSensorDimensions, ProducedLensDistortionHandler, DistortionData);

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

#if WITH_EDITOR
void ULensDistortionComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
 	const FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
 
 	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULensDistortionComponent, bApplyDistortion))
 	{
 		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(TargetCameraComponent.GetComponent(GetOwner())))
 		{
 			if (!bApplyDistortion)
 			{
				CleanupDistortion(CineCameraComponent);
 			}
 		}
 	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULensDistortionComponent, bEvaluateLensFileForDistortion))
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
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULensDistortionComponent, TargetCameraComponent))
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

void ULensDistortionComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	InitDefaultCamera();

	// When this component is duplicated (e.g. for PIE), the duplicated component needs a new unique ID
	if (!DistortionProducerID.IsValid())
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULensDistortionComponent::PostEditImport()
{
	Super::PostEditImport();

	InitDefaultCamera();

	// When this component is duplicated (e.g. copy-paste), the duplicated component needs a new unique ID
	if (!DistortionProducerID.IsValid())
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULensDistortionComponent::PostLoad()
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
}

void ULensDistortionComponent::CleanupDistortion(UCineCameraComponent* const CineCameraComponent)
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

		UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
		if (SubSystem)
		{
			SubSystem->UpdateOverscanFocalLength(CineCameraComponent, 0.0f);
		}
	}

	bIsDistortionSetup = false;
}

void ULensDistortionComponent::InitDefaultCamera()
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

void ULensDistortionComponent::CreateDistortionHandlerForLensFile()
{
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