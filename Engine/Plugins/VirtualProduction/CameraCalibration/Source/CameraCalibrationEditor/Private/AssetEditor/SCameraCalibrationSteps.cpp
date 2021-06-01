// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCameraCalibrationSteps.h"

#include "AssetRegistry/AssetData.h"
#include "Camera/CameraActor.h"
#include "CameraCalibrationEditorLog.h"
#include "CameraCalibrationStepsController.h"
#include "CameraCalibrationSubsystem.h"
#include "EditorStyleSet.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IContentBrowserSingleton.h"
#include "LensFile.h"
#include "PropertyCustomizationHelpers.h"
#include "SSimulcamViewport.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "UI/CameraCalibrationWidgetHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "CameraCalibrationSteps"


void SCameraCalibrationSteps::Construct(const FArguments& InArgs, TWeakPtr<FCameraCalibrationStepsController> InCalibrationStepsController)
{
	CalibrationStepsController = InCalibrationStepsController;
	check(CalibrationStepsController.IsValid());

	// Create and populate the step switcher with the UI for all the calibration steps
	{
		StepWidgetSwitcher = SNew(SWidgetSwitcher);

		for (const TStrongObjectPtr<UCameraCalibrationStep>& Step: CalibrationStepsController.Pin()->GetCalibrationSteps())
		{
			StepWidgetSwitcher->AddSlot()
				[Step->BuildUI()];
		}
	}

	ChildSlot
	[		
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(0.75f) 
		[ 
			SNew(SVerticalBox)

			+ SVerticalBox::Slot() // Steps selection
			.VAlign(EVerticalAlignment::VAlign_Fill)
			.AutoHeight()
			[ BuildStepSelectionWidget() ]

			+ SVerticalBox::Slot() // Simulcam Viewport
			.VAlign(EVerticalAlignment::VAlign_Fill)
			[
				SNew(SSimulcamViewport, CalibrationStepsController.Pin()->GetRenderTarget())
				.OnSimulcamViewportClicked_Raw(CalibrationStepsController.Pin().Get(), &FCameraCalibrationStepsController::OnSimulcamViewportClicked)
			]
		]

		+ SHorizontalBox::Slot() // Right toolbar
		.FillWidth(0.25f)
		[ 
			SNew(SVerticalBox)

			+ SVerticalBox::Slot() // Wiper
			.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
			[ FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("Transparency", "Transparency"), BuildSimulcamWiperWidget())]

			+ SVerticalBox::Slot() // Camera picker
			.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
			[ FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("Camera", "Camera"), BuildCameraPickerWidget())]

			+ SVerticalBox::Slot() // Media Source picker
			.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
			[FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("MediaSource", "Media Source"), BuildMediaSourceWidget())]

			+ SVerticalBox::Slot() // Step UI
			.AutoHeight()
			[StepWidgetSwitcher.ToSharedRef()]
		]
	];

	// Select the first step
	for (const TStrongObjectPtr<UCameraCalibrationStep>& Step : CalibrationStepsController.Pin()->GetCalibrationSteps())
	{
		if (!Step.IsValid())
		{
			continue;
		}

		SelectStep(Step->FriendlyName());
		break;
	}
}

TSharedRef<SWidget> SCameraCalibrationSteps::BuildCameraPickerWidget()
{
	return SNew(SObjectPropertyEntryBox)
		.AllowedClass(ACameraActor::StaticClass())
		.OnObjectChanged_Lambda([&](const FAssetData& AssetData)
		{
			if (AssetData.IsValid())
			{
				CalibrationStepsController.Pin()->SetCamera(Cast<ACameraActor>(AssetData.GetAsset()));
			}
		})
		.ObjectPath_Lambda([&]() -> FString
		{
			if (ACameraActor* Camera = CalibrationStepsController.Pin()->GetCamera())
			{
				FAssetData AssetData(Camera, true);
				return AssetData.ObjectPath.ToString();
			}

			return TEXT("");
		});
}

TSharedRef<SWidget> SCameraCalibrationSteps::BuildSimulcamWiperWidget()
{
	return SNew(SSpinBox<float>)
		.Value_Lambda([&]() { return CalibrationStepsController.Pin()->GetWiperWeight(); })
		.ToolTipText(LOCTEXT("CGWiper", "CG/Media Wiper"))
		.OnValueChanged_Lambda([&](double InValue)
		{
			CalibrationStepsController.Pin()->SetWiperWeight(float(InValue));
		})
		.MinValue(0.0f)
		.MaxValue(1.0f)
		.MinSliderValue(0.0f)
		.MaxSliderValue(1.0f)
		.ClearKeyboardFocusOnCommit(true)
		.Delta(0.1f);
}

void SCameraCalibrationSteps::UpdateMediaSourcesOptions()
{
	CurrentMediaSources.Empty();

	if (CalibrationStepsController.IsValid())
	{
		CalibrationStepsController.Pin()->FindMediaSourceUrls(CurrentMediaSources);
	}

	// Add a "None" option
	CurrentMediaSources.Add(MakeShareable(new FString(TEXT("None"))));

	check(MediaSourcesComboBox.IsValid());

	// Ask the ComboBox to refresh its options from its source (that we just updated)
	MediaSourcesComboBox->RefreshOptions();

	// Make sure we show the item that is selected
	const FString MediaSourceUrl = CalibrationStepsController.Pin()->GetMediaSourceUrl();

	for (const TSharedPtr<FString>& MediaSourceUrlItem: CurrentMediaSources)
	{
		check(MediaSourceUrlItem.IsValid());

		if (*MediaSourceUrlItem == MediaSourceUrl)
		{
			MediaSourcesComboBox->SetSelectedItem(MediaSourceUrlItem);
			return;
		}
	}

	// If we arrived here, we fall back to "None"
	MediaSourcesComboBox->SetSelectedItem(CurrentMediaSources[CurrentMediaSources.Num() - 1]);

	return;
}

TSharedRef<SWidget> SCameraCalibrationSteps::BuildMediaSourceWidget()
{
	MediaSourcesComboBox = SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&CurrentMediaSources)
		.OnSelectionChanged_Lambda([&](TSharedPtr<FString> NewValue, ESelectInfo::Type Type) -> void
		{
			if (!CalibrationStepsController.IsValid() || !NewValue.IsValid())
			{
				return;
			}

			CalibrationStepsController.Pin()->SetMediaSourceUrl(*NewValue);
		})
		.OnGenerateWidget_Lambda([&](TSharedPtr<FString> InOption) -> TSharedRef<SWidget>
		{
			return SNew(STextBlock).Text(FText::FromString(*InOption));
		})
		.InitiallySelectedItem(nullptr)
		[
			SNew(STextBlock)
			.Text_Lambda([&]() -> FText
			{
				if (MediaSourcesComboBox.IsValid() && MediaSourcesComboBox->GetSelectedItem().IsValid())
				{
					return FText::FromString(*MediaSourcesComboBox->GetSelectedItem());
				}

				return LOCTEXT("InvalidComboOption", "Invalid");
			})
		];

	UpdateMediaSourcesOptions();

	return MediaSourcesComboBox.ToSharedRef();
}

TSharedRef<SWidget> SCameraCalibrationSteps::BuildStepSelectionWidget()
{
	if (!CalibrationStepsController.IsValid())
	{
		return SNew(SHorizontalBox);
	}

	StepButtons.Empty();

	TSharedPtr<SHorizontalBox> ButtonsBox = SNew(SHorizontalBox);

	for (const TStrongObjectPtr<UCameraCalibrationStep>& Step : CalibrationStepsController.Pin()->GetCalibrationSteps())
	{
		if (!Step.IsValid())
		{
			continue;
		}
	
		const FName StepName = Step->FriendlyName();

		TSharedPtr<SButton> Button = SNew(SButton)
			.Text(FText::FromName(Step->FriendlyName()))
			.HAlign(EHorizontalAlignment::HAlign_Center)
			.VAlign(EVerticalAlignment::VAlign_Center)
			.OnClicked_Lambda([&, StepName]()->FReply
			{
				SelectStep(StepName);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromName(Step->FriendlyName()))
				.Font(FCameraCalibrationWidgetHelpers::TitleFontInfo)
			];

		StepButtons.Add(StepName, Button);

		ButtonsBox->AddSlot()
		[ Button.ToSharedRef() ];
	}

	return SNew(SBox)
		.MinDesiredHeight(2 * FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		.MaxDesiredHeight(2 * FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		[ButtonsBox.ToSharedRef()];
}

void SCameraCalibrationSteps::SelectStep(const FName& StepName)
{
	if (!CalibrationStepsController.IsValid() || !StepWidgetSwitcher.IsValid())
	{
		UE_LOG(LogCameraCalibrationEditor, Error, TEXT("CalibrationStepsController and/or StepWidgetSwitcher were unexpectedly invalid"));
		return;
	}

	// Tell the steps controller that the user has selected a different step.
	CalibrationStepsController.Pin()->SelectStep(StepName);

	// Switch the UI to the selected step

	int32 StepIdx = 0;

	for (const TStrongObjectPtr<UCameraCalibrationStep>& Step: CalibrationStepsController.Pin()->GetCalibrationSteps())
	{
		if (!Step.IsValid())
		{
			continue;
		}

		if (Step->FriendlyName() == StepName)
		{
			StepWidgetSwitcher->SetActiveWidgetIndex(StepIdx);
			break;
		}

		StepIdx++;
	}

	// Update button appearances to indicate selection
	for (const TPair<FName, TSharedPtr<SButton>>& NameButtonPair : StepButtons)
	{
		if (!NameButtonPair.Value.IsValid())
		{
			continue;
		}

		if (NameButtonPair.Key == StepName)
		{
			NameButtonPair.Value->SetBorderBackgroundColor(FCameraCalibrationWidgetHelpers::SelectedBoxBackgroundColor);
			NameButtonPair.Value->SetForegroundColor(FCameraCalibrationWidgetHelpers::SelectedBoxForegroundColor);
		}
		else
		{
			NameButtonPair.Value->SetBorderBackgroundColor(FCameraCalibrationWidgetHelpers::UnselectedBoxBackgroundColor);
			NameButtonPair.Value->SetForegroundColor(FCameraCalibrationWidgetHelpers::UnselectedBoxForegroundColor);
		}
	}
}


#undef LOCTEXT_NAMESPACE
