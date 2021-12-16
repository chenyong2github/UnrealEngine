// Copyright Epic Games, Inc. All Rights Reserved.

#include "NodalOffsetTool.h"

#include "CalibrationPointComponent.h"
#include "CameraCalibrationStepsController.h"
#include "CameraCalibrationSubsystem.h"
#include "CameraCalibrationTypes.h"
#include "ImageCenterTool.h"
#include "LiveLinkCameraController.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "SNodalOffsetToolPanel.h"

#define LOCTEXT_NAMESPACE "NodalOffsetTool"

void UNodalOffsetTool::Initialize(TWeakPtr<FCameraCalibrationStepsController> InCameraCalibrationStepController)
{
	CameraCalibrationStepsController = InCameraCalibrationStepController;

	UCameraCalibrationSubsystem* Subsystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
	check(Subsystem);

	for (const FName& AlgoName : Subsystem->GetCameraNodalOffsetAlgos())
	{
		TSubclassOf<UCameraNodalOffsetAlgo> AlgoClass = Subsystem->GetCameraNodalOffsetAlgo(AlgoName);
		const UCameraNodalOffsetAlgo* Algo = CastChecked<UCameraNodalOffsetAlgo>(AlgoClass->GetDefaultObject());

		// If the algo uses an overlay material, create a new MID to use with that algo
		if (UMaterialInterface* OverlayMaterial = Algo->GetOverlayMaterial())
		{
			AlgoOverlayMIDs.Add(Algo->FriendlyName(), UMaterialInstanceDynamic::Create(OverlayMaterial, GetTransientPackage()));
		}
	}
}

void UNodalOffsetTool::Shutdown()
{
	if (NodalOffsetAlgo)
	{
		NodalOffsetAlgo->Shutdown();
		NodalOffsetAlgo = nullptr;
	}
}

void UNodalOffsetTool::Tick(float DeltaTime)
{
	if (NodalOffsetAlgo)
	{
		NodalOffsetAlgo->Tick(DeltaTime);
	}
}

bool UNodalOffsetTool::OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bIsActive)
	{
		return false;
	}

	if (!NodalOffsetAlgo)
	{
		return false;
	}

	return NodalOffsetAlgo->OnViewportClicked(MyGeometry, MouseEvent);
}

bool UNodalOffsetTool::OnViewportInputKey(const FKey& InKey, const EInputEvent& InEvent)
{
	if (!bIsActive)
	{
		return false;
	}

	if (!NodalOffsetAlgo)
	{
		return false;
	}

	return NodalOffsetAlgo->OnViewportInputKey(InKey, InEvent);
}

TSharedRef<SWidget> UNodalOffsetTool::BuildUI()
{
	return SNew(SNodalOffsetToolPanel, this);
}

bool UNodalOffsetTool::DependsOnStep(UCameraCalibrationStep* Step) const
{
	return !!Cast<UImageCenterTool>(Step);
}

void UNodalOffsetTool::Activate()
{
	// Nothing to do if it is already active.
	if (bIsActive)
	{
		return;
	}

	bIsActive = true;
}

void UNodalOffsetTool::Deactivate()
{
	bIsActive = false;
}

void UNodalOffsetTool::SetNodalOffsetAlgo(const FName& AlgoName)
{
	// Ask subsystem for the selected nodal offset algo class

	UCameraCalibrationSubsystem* Subsystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
	check(Subsystem);

	TSubclassOf<UCameraNodalOffsetAlgo> AlgoClass = Subsystem->GetCameraNodalOffsetAlgo(AlgoName);
		
	// If it is the same as the existing one, do nothing.
	if (!NodalOffsetAlgo && !AlgoClass)
	{
		return;
	}
	else if (NodalOffsetAlgo && (NodalOffsetAlgo->GetClass() == AlgoClass))
	{
		return;
	}

	// Remove old Algo
	if (NodalOffsetAlgo)
	{
		NodalOffsetAlgo->Shutdown();
		NodalOffsetAlgo = nullptr;
	}

	// If AlgoClass is none, we're done here.
	if (!AlgoClass)
	{
		return;
	}

	// Create new algo
	NodalOffsetAlgo = NewObject<UCameraNodalOffsetAlgo>(
		GetTransientPackage(),
		AlgoClass,
		MakeUniqueObjectName(GetTransientPackage(), AlgoClass),
		RF_Transactional);

	if (NodalOffsetAlgo)
	{
		NodalOffsetAlgo->Initialize(this);
	}

	// Set the tool overlay pass' material to the MID associate with the current algo
	if (CameraCalibrationStepsController.IsValid())
	{
		TSharedPtr<FCameraCalibrationStepsController> StepsController = CameraCalibrationStepsController.Pin();
		StepsController->SetOverlayEnabled(false);

		if (UMaterialInstanceDynamic* OverlayMID = GetOverlayMID())
		{
			StepsController->SetOverlayMaterial(OverlayMID);
		}
	}
}

UCameraNodalOffsetAlgo* UNodalOffsetTool::GetNodalOffsetAlgo() const
{
	return NodalOffsetAlgo;
}

void UNodalOffsetTool::OnSaveCurrentNodalOffset()
{
	if (!CameraCalibrationStepsController.IsValid())
	{
		return;
	}

	const FText TitleInfo = LOCTEXT("NodalOffsetInfo", "Nodal Offset Calibration Info");
	const FText TitleError = LOCTEXT("NodalOffsetError", "Nodal Offset Calibration Error");

	UCameraNodalOffsetAlgo* Algo = GetNodalOffsetAlgo();

	if (!Algo)
	{
		FText ErrorMessage = LOCTEXT("NoAlgoFound", "No algo found");
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage, &TitleError);
		return;
	}

	FText ErrorMessage;

	float Focus = 0.0f;
	float Zoom = 0.0f;
	FNodalPointOffset NodalOffset;
	float ReprojectionError;

	if (!Algo->GetNodalOffset(NodalOffset, Focus, Zoom, ReprojectionError, ErrorMessage))
	{
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage, &TitleError);
		return;
	}

	// Show reprojection error
	{
		FFormatOrderedArguments Arguments;
		Arguments.Add(FText::FromString(FString::Printf(TEXT("%.2f"), ReprojectionError)));

		const FText Message = FText::Format(LOCTEXT("ReprojectionError", "RMS Reprojection Error: {0} pixels"), Arguments);

		// Allow the user to cancel adding to the LUT if the reprojection error is unacceptable.
		if (FMessageDialog::Open(EAppMsgType::OkCancel, Message, &TitleInfo) != EAppReturnType::Ok)
		{
			return;
		}
	}

	ULensFile* LensFile = CameraCalibrationStepsController.Pin()->GetLensFile();

	if (!LensFile)
	{
		ErrorMessage = LOCTEXT("NoLensFile", "No Lens File");
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage, &TitleError);
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SaveCurrentNodalOffset", "Save Current Nodal Offset"));
	LensFile->Modify();
	LensFile->AddNodalOffsetPoint(Focus, Zoom, NodalOffset);

	// Force bApplyNodalOffset in the LiveLinkCameraController so that we can see the effect right away
	if (ULiveLinkCameraController* LiveLinkCameraController = CameraCalibrationStepsController.Pin()->FindLiveLinkCameraController())
	{
		LiveLinkCameraController->SetApplyNodalOffset(true);
	}

	Algo->OnSavedNodalOffset();
}

FCameraCalibrationStepsController* UNodalOffsetTool::GetCameraCalibrationStepsController() const
{
	if (!CameraCalibrationStepsController.IsValid())
	{
		return nullptr;
	}

	return CameraCalibrationStepsController.Pin().Get();
}

bool UNodalOffsetTool::IsActive() const
{
	return bIsActive;
}

UMaterialInstanceDynamic* UNodalOffsetTool::GetOverlayMID() const
{
	return AlgoOverlayMIDs.FindRef(NodalOffsetAlgo->FriendlyName()).Get();
}

bool UNodalOffsetTool::IsOverlayEnabled() const
{
	return NodalOffsetAlgo->IsOverlayEnabled();
}

#undef LOCTEXT_NAMESPACE
