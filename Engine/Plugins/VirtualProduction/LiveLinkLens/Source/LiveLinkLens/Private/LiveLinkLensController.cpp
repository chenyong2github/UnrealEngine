// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkLensController.h"

#include "CameraCalibrationSubsystem.h"
#include "CineCameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "LiveLinkLensRole.h"
#include "LiveLinkLensTypes.h"


ULiveLinkLensController::ULiveLinkLensController() 
{
	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULiveLinkLensController::Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData)
{
	const FLiveLinkLensStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkLensStaticData>();
	const FLiveLinkLensFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkLensFrameData>();

	if (StaticData && FrameData)
	{
		if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
		{
			UCameraCalibrationSubsystem* const SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
			const TSubclassOf<ULensModel> LensModel = SubSystem->GetRegisteredLensModel(StaticData->LensModel);

			const FName SubjectName = SelectedSubject.Subject.Name;
			const FText RoleName = SelectedSubject.Role->GetDefaultObject<ULiveLinkRole>()->GetDisplayName();
			const FString HandlerDisplayName = FString::Format(TEXT("'{0}' LiveLink Subject ({1} Role)"), { SubjectName.ToString(), RoleName.ToString() });

			FDistortionHandlerPicker DistortionHandlerPicker = { CineCameraComponent, DistortionProducerID, HandlerDisplayName };
			LensDistortionHandler = SubSystem->FindOrCreateDistortionModelHandler(DistortionHandlerPicker, LensModel);
			
			if (LensDistortionHandler)
			{
				// Update the lens distortion handler with the latest frame of data from the LiveLink source
				FLensDistortionState DistortionState;

				DistortionState.DistortionInfo.Parameters = FrameData->DistortionParameters;
				DistortionState.FocalLengthInfo.FxFy = FrameData->FxFy;
				DistortionState.ImageCenter.PrincipalPoint = FrameData->PrincipalPoint;

				//Update the distortion state based on incoming LL data.
				LensDistortionHandler->SetDistortionState(DistortionState);

				//Recompute overscan factor for the distortion state
				float OverscanFactor = LensDistortionHandler->ComputeOverscanFactor();

				if (bScaleOverscan)
				{
					OverscanFactor = ((OverscanFactor - 1.0f) * OverscanMultiplier) + 1.0f;
				}
				LensDistortionHandler->SetOverscanFactor(OverscanFactor);

				//Make sure the displacement map is up to date
				LensDistortionHandler->ProcessCurrentDistortion();
			}

			// Track changes to the cine camera's focal length for consumers of distortion data
			SubSystem->UpdateOriginalFocalLength(CineCameraComponent, CineCameraComponent->CurrentFocalLength);
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

void ULiveLinkLensController::Cleanup()
{
	if (UCineCameraComponent* const CineCameraComponent = Cast<UCineCameraComponent>(AttachedComponent))
	{
		if (UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>())
		{
			SubSystem->UnregisterDistortionModelHandler(CineCameraComponent, LensDistortionHandler);
		}
	}
}

void ULiveLinkLensController::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// When this controller is duplicated (e.g. for PIE), the duplicated controller needs a new unique ID
	if (!DistortionProducerID.IsValid())
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}

void ULiveLinkLensController::PostEditImport()
{
	Super::PostEditImport();

	// PostDuplicate is not called on components during actor duplication, such as alt-drag and copy-paste, so PostEditImport covers those duplication cases
	// When this controller is duplicated in those cases, the duplicated controller needs a new unique ID
	if (!DistortionProducerID.IsValid())
	{
		DistortionProducerID = FGuid::NewGuid();
	}
}
