// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleEditorPresetWidget.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleEditorManager.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsolePreset.h"

#include "AssetRegistry/AssetData.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "Widgets/SAssetPickerButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleEditorPresetWidget"

void SDMXControlConsoleEditorPresetWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SHorizontalBox)

			// 'Create New Preset' button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.f, 0.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.ToolTipText(LOCTEXT("ControlConsoleCreateNewButton", "Create New Preset"))
				.ContentPadding(2.f)
				.OnClicked(this, &SDMXControlConsoleEditorPresetWidget::OnCreateNewClicked)
				.ForegroundColor(FSlateColor::UseForeground())
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(8.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CreateNewPresetText", "Create New Preset"))
					]
				]
			]

			// Asset Picker
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.f, 0.f)
			[
				SNew(SAssetPickerButton)
				.AssetClass(UDMXControlConsolePreset::StaticClass())
				.CurrentAssetValue(this, &SDMXControlConsoleEditorPresetWidget::GetSelectedPreset)
				.OnAssetSelected(this, &SDMXControlConsoleEditorPresetWidget::OnPresetSelected)
			]
		];

	SelectedControlConsolePreset = FDMXControlConsoleEditorManager::Get().GetDefaultControlConsolePreset();
	FDMXControlConsoleEditorManager::Get().LoadFromPreset(SelectedControlConsolePreset.Get());
}

TWeakObjectPtr<UObject> SDMXControlConsoleEditorPresetWidget::GetSelectedPreset() const
{
	return SelectedControlConsolePreset.IsValid() ? SelectedControlConsolePreset.Get() : nullptr;
}

FReply SDMXControlConsoleEditorPresetWidget::OnCreateNewClicked()
{
	const TSharedRef<SDlgPickAssetPath> PickAssetDialog =	
		SNew(SDlgPickAssetPath)
		.Title(LOCTEXT("ControlConsolPresetDialog", "Create New Preset"));

	const EAppReturnType::Type Result = PickAssetDialog->ShowModal();
	if (Result == EAppReturnType::Ok)
	{
		const FText AssetPath = PickAssetDialog->GetAssetPath();
		const FText AssetName = PickAssetDialog->GetAssetName();

		FDMXControlConsoleEditorManager& ControlConsoleManager = FDMXControlConsoleEditorManager::Get();
		SelectedControlConsolePreset = ControlConsoleManager.CreateNewPreset(AssetPath.ToString(), AssetName.ToString());
		if (SelectedControlConsolePreset.IsValid())
		{
			SelectedControlConsolePreset->GetOnControlConsolePresetSaved().AddSP(this, &SDMXControlConsoleEditorPresetWidget::OnPresetSaved);
			ControlConsoleManager.LoadFromPreset(SelectedControlConsolePreset.Get());
		}
	}

	return FReply::Handled();
}

void SDMXControlConsoleEditorPresetWidget::OnPresetSelected(const FAssetData& AssetData)
{
	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
	SelectionHandler->ClearSelection();

	FDMXControlConsoleEditorManager& ControlConsoleEditorManager = FDMXControlConsoleEditorManager::Get();

	UDMXControlConsolePreset* ControlConsolePreset = Cast<UDMXControlConsolePreset>(AssetData.GetAsset());
	SelectedControlConsolePreset = ControlConsolePreset;
	if (!SelectedControlConsolePreset.IsValid())
	{
		ControlConsoleEditorManager.CreateNewTransientConsole();

		return;
	}

	ControlConsoleEditorManager.LoadFromPreset(ControlConsolePreset);

	// If selected asset's package is not dirty, the asset's path gets registered in DMXControlConsole configuration settings
	UPackage* Package = SelectedControlConsolePreset->GetPackage();
	if (Package && !Package->IsDirty())
	{
		ControlConsoleEditorManager.SetDefaultControlConsolePreset(AssetData.GetSoftObjectPath());
	}
}

void SDMXControlConsoleEditorPresetWidget::OnPresetSaved(const UDMXControlConsolePreset* Preset)
{
	if (!Preset || SelectedControlConsolePreset != Preset)
	{
		return;
	}

	FDMXControlConsoleEditorManager::Get().SetDefaultControlConsolePreset(Preset->GetPathName());
}

#undef LOCTEXT_NAMESPACE
