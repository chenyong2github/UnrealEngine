// Copyright Epic Games, Inc. All Rights Reserved.

#include "NodalOffsetTool.h"

#include "CalibrationPointComponent.h"
#include "CameraCalibrationStepsController.h"
#include "CameraCalibrationSubsystem.h"
#include "CameraCalibrationTypes.h"
#include "LensDistortionTool.h"
#include "LiveLinkCameraController.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "SNodalOffsetToolPanel.h"

#define LOCTEXT_NAMESPACE "NodalOffsetTool"

void UNodalOffsetTool::Initialize(TWeakPtr<FCameraCalibrationStepsController> InCameraCalibrationStepController)
{
	CameraCalibrationStepsController = InCameraCalibrationStepController;
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

TSharedRef<SWidget> UNodalOffsetTool::BuildUI()
{
	return SNew(SNodalOffsetToolPanel, this);
}

bool UNodalOffsetTool::DependsOnStep(UCameraCalibrationStep* Step) const
{
	return !!Cast<ULensDistortionTool>(Step);
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
		MakeUniqueObjectName(GetTransientPackage(), AlgoClass));

	if (NodalOffsetAlgo)
	{
		NodalOffsetAlgo->Initialize(this);
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

#undef LOCTEXT_NAMESPACE
