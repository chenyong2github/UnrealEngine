// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNodalOffsetToolPanel.h"

#include "AssetRegistry/AssetData.h"
#include "Camera/CameraActor.h"
#include "CameraCalibrationSubsystem.h"
#include "CameraNodalOffsetAlgo.h"
#include "EditorStyleSet.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IContentBrowserSingleton.h"
#include "LensFile.h"
#include "NodalOffsetTool.h"
#include "PropertyCustomizationHelpers.h"
#include "SSimulcamViewport.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "UI/CameraCalibrationWidgetHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"


#define LOCTEXT_NAMESPACE "NodalOffsetTool"


void SNodalOffsetToolPanel::Construct(const FArguments& InArgs, ULensFile* InLensFile)
{
	LensFile = TStrongObjectPtr<ULensFile>(InLensFile);
	NodalOffsetTool = MakeShared<FNodalOffsetTool>(InLensFile);

	// This will be the widget wrapper of the custom algo UI.
	NodalOffsetUI = SNew(SVerticalBox);

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot() // Simulcam Viewport
		.FillWidth(0.75f) 
		[ 
			SNew(SSimulcamViewport, NodalOffsetTool->GetRenderTarget())
			.OnSimulcamViewportClicked_Raw(NodalOffsetTool.Get(), &FNodalOffsetTool::OnSimulcamViewportClicked)
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

			+ SVerticalBox::Slot() // Algo picker
			.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
			[ FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("NodalOffsetAlgo", "Nodal Offset Algo"), BuildNodalOffsetAlgoPickerWidget())]

			+ SVerticalBox::Slot() // Algo UI
			.AutoHeight()
			[ BuildNodalOffsetUIWrapper() ]

			+ SVerticalBox::Slot() // Save Offset
			.AutoHeight()
			.Padding(0, 20)
			[
				SNew(SButton).Text(LOCTEXT("AddToNodalOffsetLUT", "Add To Nodal Offset LUT"))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([&]() -> FReply
				{
					if (NodalOffsetTool.IsValid())
					{
						NodalOffsetTool->OnSaveCurrentNodalOffset();
					}
					return FReply::Handled();
				})
			]
		]
	];
}

TSharedRef<SWidget> SNodalOffsetToolPanel::BuildNodalOffsetUIWrapper()
{
	const UCameraNodalOffsetAlgo* Algo = NodalOffsetTool->GetNodalOffsetAlgo();

	NodalOffsetUITitle = SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
		.Text(Algo ? FText::FromName(Algo->FriendlyName()) : LOCTEXT("None", "None"))
		.Justification(ETextJustify::Center);

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot() // Title
		.AutoHeight()
		.Padding(0,10)
		[ NodalOffsetUITitle.ToSharedRef() ]

		+ SVerticalBox::Slot() // Algo's Widget
		.AutoHeight()
		[ NodalOffsetUI.ToSharedRef() ];
}

void SNodalOffsetToolPanel::UpdateNodalOffsetUI()
{
	check(AlgosComboBox.IsValid());

	// Get current algo to later compare with new one
	const UCameraNodalOffsetAlgo* OldAlgo = NodalOffsetTool->GetNodalOffsetAlgo();

	// Set new algo by name
	const FName AlgoName(*AlgosComboBox->GetSelectedItem());
	NodalOffsetTool->SetNodalOffsetAlgo(AlgoName);

	// Get the new algo
	UCameraNodalOffsetAlgo* Algo = NodalOffsetTool->GetNodalOffsetAlgo();

	// nullptr may indicate that it was unregistered, so refresh combobox options.
	if (!Algo)
	{
		UpdateAlgosOptions();
		return;
	}

	// If we didn't change the algo, we're done here.
	if (Algo == OldAlgo)
	{
		return;
	}

	// Remove old UI
	check(NodalOffsetUI.IsValid());
	NodalOffsetUI->ClearChildren();

	// Assign GUI
	NodalOffsetUI->AddSlot() [Algo->BuildUI()];

	// Update Title
	if (NodalOffsetUITitle.IsValid())
	{
		NodalOffsetUITitle->SetText(FText::FromName(Algo->FriendlyName()));
	}
}

TSharedRef<SWidget> SNodalOffsetToolPanel::BuildCameraPickerWidget()
{
	return SNew(SObjectPropertyEntryBox)
		.AllowedClass(ACameraActor::StaticClass())
		.OnObjectChanged_Lambda([&](const FAssetData& AssetData)
		{
			if (AssetData.IsValid())
			{
				NodalOffsetTool->SetCamera(Cast<ACameraActor>(AssetData.GetAsset()));
			}
		})
		.ObjectPath_Lambda([&]() -> FString
		{
			if (ACameraActor* Camera = NodalOffsetTool->GetCamera())
			{
				FAssetData AssetData(Camera, true);
				return AssetData.ObjectPath.ToString();
			}

			return TEXT("");
		});
}

TSharedRef<SWidget> SNodalOffsetToolPanel::BuildSimulcamWiperWidget()
{
	return SNew(SSpinBox<float>)
		.Value_Lambda([&]() { return NodalOffsetTool->GetWiperWeight(); })
		.ToolTipText(LOCTEXT("CGWiper", "CG/Media Wiper"))
		.OnValueChanged_Lambda([&](double InValue)
		{
			NodalOffsetTool->SetWiperWeight(float(InValue));
		})
		.MinValue(0.0f)
		.MaxValue(1.0f)
		.MinSliderValue(0.0f)
		.MaxSliderValue(1.0f)
		.ClearKeyboardFocusOnCommit(true)
		.Delta(0.1f);
}

void SNodalOffsetToolPanel::UpdateAlgosOptions()
{
	CurrentAlgos.Empty();

	// Ask the subsystem for the list of registered Algos

	UCameraCalibrationSubsystem* Subsystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
	check(Subsystem);

	for (FName& Name : Subsystem->GetCameraNodalOffsetAlgos())
	{
		CurrentAlgos.Add(MakeShareable(new FString(Name.ToString())));
	}

	// Ask the ComboBox to refresh its options from its source (that we just updated)
	AlgosComboBox->RefreshOptions();
}

TSharedRef<SWidget> SNodalOffsetToolPanel::BuildNodalOffsetAlgoPickerWidget()
{
	// Create ComboBox widget

	AlgosComboBox = SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&CurrentAlgos)
		.OnSelectionChanged_Lambda([&](TSharedPtr<FString> NewValue, ESelectInfo::Type Type) -> void
		{
			// Replace the custom algo widget
			UpdateNodalOffsetUI();
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
				if (AlgosComboBox.IsValid() && AlgosComboBox->GetSelectedItem().IsValid())
				{
					return FText::FromString(*AlgosComboBox->GetSelectedItem());
				}

				return LOCTEXT("InvalidComboOption", "Invalid");
			})
		];

	// Update the object holding this combobox's options source
	UpdateAlgosOptions();

	// Pick the first option by default (if available)
	if (CurrentAlgos.Num())
	{
		AlgosComboBox->SetSelectedItem(CurrentAlgos[0]);
	}
	else
	{
		AlgosComboBox->SetSelectedItem(nullptr);
	}

	return AlgosComboBox.ToSharedRef();
}

void SNodalOffsetToolPanel::UpdateMediaSourcesOptions()
{
	CurrentMediaSources.Empty();

	if (NodalOffsetTool.IsValid())
	{
		NodalOffsetTool->FindMediaSourceUrls(CurrentMediaSources);
	}

	// Add a "None" option
	CurrentMediaSources.Add(MakeShareable(new FString(TEXT("None"))));

	check(MediaSourcesComboBox.IsValid());

	// Ask the ComboBox to refresh its options from its source (that we just updated)
	MediaSourcesComboBox->RefreshOptions();

	// Make sure we show the item that is selected
	const FString MediaSourceUrl = NodalOffsetTool->GetMediaSourceUrl();

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

TSharedRef<SWidget> SNodalOffsetToolPanel::BuildMediaSourceWidget()
{
	MediaSourcesComboBox = SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&CurrentMediaSources)
		.OnSelectionChanged_Lambda([&](TSharedPtr<FString> NewValue, ESelectInfo::Type Type) -> void
		{
			if (!NodalOffsetTool.IsValid() || !NewValue.IsValid())
			{
				return;
			}

			NodalOffsetTool->SetMediaSourceUrl(*NewValue);
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

#undef LOCTEXT_NAMESPACE
