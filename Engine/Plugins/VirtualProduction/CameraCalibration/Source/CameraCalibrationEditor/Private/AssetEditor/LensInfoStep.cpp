// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensInfoStep.h"

#include "CameraCalibrationStepsController.h"
#include "IStructureDetailsView.h"
#include "LensFile.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "SResetToDefaultMenu.h"
#include "Widgets/Input/SButton.h"


#define LOCTEXT_NAMESPACE "LensInfoStep"

void ULensInfoStep::Initialize(TWeakPtr<FCameraCalibrationStepsController> InCameraCalibrationStepController)
{
	CameraCalibrationStepsController = InCameraCalibrationStepController;
	
	EditedLensInfo = MakeShared<TStructOnScope<FLensInfo>>();
	EditedLensInfo->InitializeAs<FLensInfo>();
}

TSharedRef<SWidget> ULensInfoStep::BuildUI()
{
	FStructureDetailsViewArgs LensInfoStructDetailsView;
	FDetailsViewArgs DetailArgs;
	DetailArgs.bAllowSearch = false;
	DetailArgs.bShowScrollBar = true;

	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	TSharedPtr<IStructureDetailsView> LensInfoStructureDetailsView = PropertyEditor.CreateStructureDetailView(DetailArgs, LensInfoStructDetailsView, EditedLensInfo);

	TSharedPtr<SWidget> StepWidget = 
		SNew(SVerticalBox)
		+ SVerticalBox::Slot() // Lens information structure
		.AutoHeight()
		[ 
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[ LensInfoStructureDetailsView->GetWidget().ToSharedRef() ]
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SResetToDefaultMenu)
				.OnResetToDefault(FSimpleDelegate::CreateUObject(this, &ULensInfoStep::ResetToDefault))
				.DiffersFromDefault(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(this, &ULensInfoStep::DiffersFromDefault)))
			]
		]
		+ SVerticalBox::Slot() // Save 
		.AutoHeight()
		.Padding(0, 20)
		[
			SNew(SButton).Text(LOCTEXT("SaveToLensFile", "Save Lens Information"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]() -> FReply
			{
				OnSaveLensInformation();
				return FReply::Handled();
			})
		];
	

	return StepWidget.ToSharedRef();
}

bool ULensInfoStep::DependsOnStep(UCameraCalibrationStep* Step) const
{
	return false;
}

void ULensInfoStep::Activate()
{
	// Nothing to do if it is already active.
	if (bIsActive)
	{
		return;
	}

	ULensFile* LensFile = CameraCalibrationStepsController.Pin()->GetLensFile();
	if(LensFile)
	{
		//Initialize lens info to current value of the LensFile
		*EditedLensInfo->Get() = LensFile->LensInfo;
	}

	bIsActive = true;
}

void ULensInfoStep::Deactivate()
{
	bIsActive = false;
}

void ULensInfoStep::OnSaveLensInformation() const
{
	if (!CameraCalibrationStepsController.IsValid())
	{
		return;
	}

	ULensFile* LensFile = CameraCalibrationStepsController.Pin()->GetLensFile();
	const FLensInfo& EditedData = *EditedLensInfo->Get();
	if(EditedData == LensFile->LensInfo)
	{
		return;
	}

	//Validate sensor dimensions
	constexpr float MinimumSize = 1.0f; //Limit sensor dimension to 1mm
	if(EditedData.SensorDimensions.X < MinimumSize || EditedData.SensorDimensions.Y < MinimumSize)
	{
		const FText ErrorMessage = LOCTEXT("InvalidSensorDimensions", "Invalid sensor dimensions. Can't have dimensions smaller than 1mm");
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
		return;	
	}

	//Initiate transaction in case we go through the path clearing tables
	FScopedTransaction Transaction(LOCTEXT("ModifyingLensInfo", "Modifying LensFile information"));
	LensFile->Modify();

	//If lens model has changed and distortion table has data, notify the user he will lose calibration data if he continues
	const bool bHasModelChanged = EditedData.LensModel != LensFile->LensInfo.LensModel;
	const bool bHasSensorChanged = EditedData.SensorDimensions != LensFile->LensInfo.SensorDimensions;
	if(bHasModelChanged || bHasSensorChanged)
	{
		if(LensFile->HasSamples(ELensDataCategory::Distortion)
			|| LensFile->HasSamples(ELensDataCategory::ImageCenter)
			|| LensFile->HasSamples(ELensDataCategory::Zoom))
		{
			const FText DataChangeText = (bHasModelChanged && bHasSensorChanged) ? LOCTEXT("ModelAndSensorChange", "Lens model and sensor dimensions") : bHasModelChanged ? LOCTEXT("LensModelChange", "Lens model") : LOCTEXT("SensorDimensionsChange", "Sensor dimensions");
			const FText Message = FText::Format(LOCTEXT("DataChangeDataLoss", "{0} change detected. Distortion, ImageCenter and FocalLength data samples will be lost. Do you wish to continue?")
										, DataChangeText);
			if (FMessageDialog::Open(EAppMsgType::OkCancel, Message) != EAppReturnType::Ok)
			{
				Transaction.Cancel();
				return;
			}
		}

		//Clear table when disruptive change is detected and user proceeds
		LensFile->ClearData(ELensDataCategory::Distortion);
		LensFile->ClearData(ELensDataCategory::ImageCenter);
		LensFile->ClearData(ELensDataCategory::Zoom);
	}

	//Apply new data to LensFile
	LensFile->LensInfo = EditedData;
}

bool ULensInfoStep::IsActive() const
{
	return bIsActive;
}

void ULensInfoStep::ResetToDefault() const
{
	const ULensFile* LensFile = CameraCalibrationStepsController.Pin()->GetLensFile();
	*EditedLensInfo->Get() = LensFile->LensInfo;
}

bool ULensInfoStep::DiffersFromDefault() const
{
	const ULensFile* LensFile = CameraCalibrationStepsController.Pin()->GetLensFile();
	return !(*EditedLensInfo->Get() == LensFile->LensInfo);
}



#undef LOCTEXT_NAMESPACE
