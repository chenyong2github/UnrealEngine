// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionTool.h"

#include "CameraCalibrationStepsController.h"
#include "CameraCalibrationTypes.h"
#include "CameraLensDistortionAlgo.h"
#include "LensFile.h"
#include "LensInfoStep.h"
#include "Misc/MessageDialog.h"
#include "Models/SphericalLensModel.h"
#include "ScopedTransaction.h"
#include "SLensDistortionToolPanel.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "LensDistortionTool"

void ULensDistortionTool::Initialize(TWeakPtr<FCameraCalibrationStepsController> InCameraCalibrationStepController)
{
	CameraCalibrationStepsController = InCameraCalibrationStepController;

	// Find available algos

	TArray<TSubclassOf<UCameraLensDistortionAlgo>> Algos;

	for (TObjectIterator<UClass> AlgoIt; AlgoIt; ++AlgoIt)
	{
		if (AlgoIt->IsChildOf(UCameraLensDistortionAlgo::StaticClass()) && !AlgoIt->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			const UCameraLensDistortionAlgo* Algo = CastChecked<UCameraLensDistortionAlgo>(AlgoIt->GetDefaultObject());
			AlgosMap.Add(Algo->FriendlyName(), TSubclassOf<UCameraLensDistortionAlgo>(*AlgoIt));
		}
	}
}

void ULensDistortionTool::Shutdown()
{
	if (CurrentAlgo)
	{
		CurrentAlgo->Shutdown();
		CurrentAlgo = nullptr;
	}
}

void ULensDistortionTool::Tick(float DeltaTime)
{
	if (CurrentAlgo)
	{
		CurrentAlgo->Tick(DeltaTime);
	}
}

bool ULensDistortionTool::OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bIsActive)
	{
		return false;
	}

	if (!CurrentAlgo)
	{
		return false;
	}

	return CurrentAlgo->OnViewportClicked(MyGeometry, MouseEvent);
}

TSharedRef<SWidget> ULensDistortionTool::BuildUI()
{
	return SNew(SLensDistortionToolPanel, this);
}

bool ULensDistortionTool::DependsOnStep(UCameraCalibrationStep* Step) const
{
	return Cast<ULensInfoStep>(Step) != nullptr;
}

void ULensDistortionTool::Activate()
{
	// Nothing to do if it is already active.
	if (bIsActive)
	{
		return;
	}

	bIsActive = true;
}

void ULensDistortionTool::Deactivate()
{
	bIsActive = false;
}

void ULensDistortionTool::SetAlgo(const FName& AlgoName)
{
	// Find the algo class

	//@todo replace with Find to avoid double search

	if (!AlgosMap.Contains(AlgoName))
	{
		return;
	}

	TSubclassOf<UCameraLensDistortionAlgo>& AlgoClass = AlgosMap[AlgoName];
		
	// If it is the same as the existing one, do nothing.
	if (!CurrentAlgo && !AlgoClass)
	{
		return;
	}
	else if (CurrentAlgo && (CurrentAlgo->GetClass() == AlgoClass))
	{
		return;
	}

	// Remove old Algo
	if (CurrentAlgo)
	{
		CurrentAlgo->Shutdown();
		CurrentAlgo = nullptr;
	}

	// If AlgoClass is none, we're done here.
	if (!AlgoClass)
	{
		return;
	}

	// Create new algo
	CurrentAlgo = NewObject<UCameraLensDistortionAlgo>(
		GetTransientPackage(),
		AlgoClass,
		MakeUniqueObjectName(GetTransientPackage(), AlgoClass));

	if (CurrentAlgo)
	{
		CurrentAlgo->Initialize(this);
	}
}

UCameraLensDistortionAlgo* ULensDistortionTool::GetAlgo() const
{
	return CurrentAlgo;
}

void ULensDistortionTool::OnSaveCurrentCalibrationData()
{
	if (!CameraCalibrationStepsController.IsValid())
	{
		return;
	}

	UCameraLensDistortionAlgo* Algo = GetAlgo();
	
	if (!Algo)
	{
		FText ErrorMessage = LOCTEXT("NoAlgoFound", "No algo found");
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
		return;
	}
	
	ULensFile* LensFile = CameraCalibrationStepsController.Pin()->GetLensFile();

	if (!LensFile)
	{
		FText ErrorMessage = LOCTEXT("NoLensFile", "No Lens File");
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
		return;
	}

	const FText TitleError = LOCTEXT("LensCalibrationError", "Lens Calibration Error");
	const FText TitleInfo = LOCTEXT("LensCalibrationInfo", "Lens Calibration Info");

	float Focus;
	float Zoom;
	FDistortionInfo DistortionInfo;
	FFocalLengthInfo FocalLengthInfo;
	FImageCenterInfo ImageCenterInfo;
	TSubclassOf<ULensModel> LensModel;
	double Error;

	// Get distortion value, and if errors, inform the user.
	{
		FText ErrorMessage;

		if (!Algo->GetLensDistortion(Focus, Zoom, DistortionInfo, FocalLengthInfo, ImageCenterInfo, LensModel, Error, ErrorMessage))
		{
			FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage, &TitleError);
			return;
		}
	}

	// Show reprojection error
	{
		FFormatOrderedArguments Arguments;
		Arguments.Add(FText::FromString(FString::Printf(TEXT("%.1f"), Error)));

		const FText Message = FText::Format(LOCTEXT("ReprojectionError", "RMS Reprojection Error: {0} pixels"), Arguments);

		// Allow the user to cancel adding to the LUT if the reprojection error is unacceptable.
		if (FMessageDialog::Open(EAppMsgType::OkCancel, Message, &TitleInfo) != EAppReturnType::Ok)
		{
			return;
		}
	}

	if (LensFile->HasSamples(ELensDataCategory::Distortion) && LensFile->LensInfo.LensModel != LensModel)
	{
		const FText ErrorMessage = LOCTEXT("LensDistortionModelMismatch", "There is a distortion model mismatch between the new and existing samples");
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage, &TitleError);
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SaveCurrentDistortionData", "Save Current Distortion Data"));

 	LensFile->Modify();

	LensFile->AddDistortionPoint(Focus, Zoom, DistortionInfo, FocalLengthInfo);
	LensFile->AddImageCenterPoint(Focus, Zoom, ImageCenterInfo);

	Algo->OnDistortionSavedToLens();
}

FCameraCalibrationStepsController* ULensDistortionTool::GetCameraCalibrationStepsController() const
{
	if (!CameraCalibrationStepsController.IsValid())
	{
		return nullptr;
	}

	return CameraCalibrationStepsController.Pin().Get();
}

TArray<FName> ULensDistortionTool::GetAlgos() const
{
	TArray<FName> OutKeys;
	AlgosMap.GetKeys(OutKeys);
	return OutKeys;
}

bool ULensDistortionTool::IsActive() const
{
	return bIsActive;
}


#undef LOCTEXT_NAMESPACE
