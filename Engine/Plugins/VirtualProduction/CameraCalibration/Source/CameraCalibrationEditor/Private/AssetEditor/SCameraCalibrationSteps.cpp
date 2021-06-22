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
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
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

			+ SVerticalBox::Slot() // Viewport Title
			.Padding(0, 5)
			.AutoHeight()
			.VAlign(EVerticalAlignment::VAlign_Center)
			[
				SNew(SBox)
				.MinDesiredHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
				.MaxDesiredHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
				[
					SNew(SBorder) // Background color for title
					.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
					.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
					.VAlign(EVerticalAlignment::VAlign_Center)
					[
						SNew(SOverlay) 
						+ SOverlay::Slot() // Used to add left padding to the title
						.Padding(5,0,0,0)
						[
							SNew(STextBlock) // Title text
							.Text(LOCTEXT("ViewportSettings", "Viewport Settings"))
							.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ShadowOffset(FVector2D(1.0f, 1.0f))
						]
					]
				]
			]

			+ SVerticalBox::Slot() // Wiper
			.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
			[ FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("Transparency", "Transparency"), BuildSimulcamWiperWidget())]
				
			+ SVerticalBox::Slot() // Camera picker
			.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
			[ FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("Camera", "Camera"), BuildCameraPickerWidget())]
				
			+ SVerticalBox::Slot() // Media Source picker
			.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
			[FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("MediaSource", "Media Source"), BuildMediaSourceWidget())]

			+ SVerticalBox::Slot() // Step Title
			.Padding(0, 5)
			.AutoHeight()
			.VAlign(EVerticalAlignment::VAlign_Center)
			[
				SNew(SBox) // Constrain the height
				.MinDesiredHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
				.MaxDesiredHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
				[
					SNew(SBorder) // Background color of title
					.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
					.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
					.VAlign(EVerticalAlignment::VAlign_Center)
					[
						SNew(SOverlay) 
						+ SOverlay::Slot() // Used to add left padding to the title
						.Padding(5, 0, 0, 0)
						[
							SNew(STextBlock) // Title text
							.Text_Lambda([&]() -> FText
							{
								for (const TStrongObjectPtr<UCameraCalibrationStep>& Step : CalibrationStepsController.Pin()->GetCalibrationSteps())
								{
									if (!Step.IsValid() || !Step->IsActive())
									{
										continue;
									}

									return FText::FromName(Step->FriendlyName());
								}

								return LOCTEXT("StepSettings", "Step");
							})
							.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ShadowOffset(FVector2D(1.0f, 1.0f))
						]
					]
				]
			]

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

	StepToggles.Empty();

	TSharedPtr<SHorizontalBox> ToggleButtonsBox = SNew(SHorizontalBox);

	for (const TStrongObjectPtr<UCameraCalibrationStep>& Step : CalibrationStepsController.Pin()->GetCalibrationSteps())
	{
		if (!Step.IsValid())
		{
			continue;
		}
	
		const FName StepName = Step->FriendlyName();

		TSharedPtr<SCheckBox> ToggleButton = SNew(SCheckBox) // Toggle buttons are implemented as checkboxes
			.Style(FEditorStyle::Get(), "PlacementBrowser.Tab")
			.OnCheckStateChanged_Lambda([&, StepName](ECheckBoxState CheckState)->void
			{
				SelectStep(StepName);
			})
			.IsChecked_Lambda([&, StepName]() -> ECheckBoxState
			{
				// Note: This will be called every tick

				if (!CalibrationStepsController.IsValid())
				{
					return ECheckBoxState::Unchecked;
				}

				// Return checked state only for the active step
				for (const TStrongObjectPtr<UCameraCalibrationStep>& Step : CalibrationStepsController.Pin()->GetCalibrationSteps())
				{
					if (!Step.IsValid())
					{
						continue;
					}

					if (Step->FriendlyName() == StepName)
					{
						return Step->IsActive() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}
				}

				return ECheckBoxState::Unchecked;
			})
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				.Padding(FMargin(6, 0, 0, 0))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), TEXT("PlacementBrowser.Tab.Text"))
					.Text(FText::FromName(Step->FriendlyName()))
				]

				+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				.HAlign(HAlign_Fill)
				.Padding(0,0,0,5) // This separates the line from the bottom and makes it more discernible against unpredictable media plate colors.
				[
					SNew(SImage) // Draws line that enforces the indication of the selected step
					.Image_Lambda([&, StepName]() -> const FSlateBrush*
					{
						// Note: This will be called every tick

						if (!CalibrationStepsController.IsValid())
						{
								return nullptr;
						}

						for (const TStrongObjectPtr<UCameraCalibrationStep>& Step : CalibrationStepsController.Pin()->GetCalibrationSteps())
						{
							if (!Step.IsValid())
							{
								continue;
							}

							if (Step->FriendlyName() == StepName)
							{
								return Step->IsActive() ? FEditorStyle::GetBrush(TEXT("PlacementBrowser.ActiveTabBar")) : nullptr;
							}
						}

						return nullptr;
					})
				]
			];

		StepToggles.Add(StepName, ToggleButton);

		ToggleButtonsBox->AddSlot()
		[ToggleButton.ToSharedRef() ];
	}

	return SNew(SBox)
		.MinDesiredHeight(1.5 * FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		.MaxDesiredHeight(1.5 * FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		[ToggleButtonsBox.ToSharedRef()];
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
}


#undef LOCTEXT_NAMESPACE
