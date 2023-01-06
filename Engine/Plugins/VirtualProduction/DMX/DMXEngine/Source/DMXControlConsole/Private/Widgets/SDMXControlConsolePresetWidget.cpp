// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsolePresetWidget.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleManager.h"
#include "DMXControlConsolePreset.h"

#include "AssetRegistry/AssetData.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "Layout/Visibility.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "UObject/SavePackage.h"
#include "Widgets/SAssetPickerButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsolePresetWidget"

void SDMXControlConsolePresetWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SHorizontalBox)

			// 'Save Preset' button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f,0.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.ToolTipText(LOCTEXT("ControlConsoleSaveButton", "Save Preset"))
				.ContentPadding(2.0f)
				.OnClicked(this, &SDMXControlConsolePresetWidget::OnSaveClicked)
				.ForegroundColor(FSlateColor::UseForeground())
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Save"))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(8.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SaveButtonText", "Save"))
					]
				]
			]

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
				.OnClicked(this, &SDMXControlConsolePresetWidget::OnCreateNewClicked)
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
						.Text(LOCTEXT("CreateNewPresetText", "Create Preset"))
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
				.CurrentAssetValue(this, &SDMXControlConsolePresetWidget::GetSelectedPreset)
				.OnAssetSelected(this, &SDMXControlConsolePresetWidget::OnPresetSelected)
			]
		];
}

TWeakObjectPtr<UObject> SDMXControlConsolePresetWidget::GetSelectedPreset() const
{
	return SelectedControlConsolePreset.IsValid() ? SelectedControlConsolePreset.Get() : nullptr;
}

void SDMXControlConsolePresetWidget::SaveSelectedPreset()
{
	if (!SelectedControlConsolePreset.IsValid())
	{
		return;
	}

	UPackage* Package = SelectedControlConsolePreset->GetPackage();
	if (!Package || !Package->IsDirty())
	{
		return;
	}

	const FString PackageName = Package->GetName();
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone | RF_Public | RF_Transactional;
	SaveArgs.SaveFlags = SAVE_Async;
	UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
}

FReply SDMXControlConsolePresetWidget::OnSaveClicked()
{
	SaveSelectedPreset();

	return FReply::Handled();
}

FReply SDMXControlConsolePresetWidget::OnCreateNewClicked()
{
	const TSharedRef<SDlgPickAssetPath> PickAssetDialog =	
		SNew(SDlgPickAssetPath)
		.Title(LOCTEXT("ControlConsolPresetDialog", "Create New Preset"));

	const EAppReturnType::Type Result = PickAssetDialog->ShowModal();
	if (Result == EAppReturnType::Ok)
	{
		const FText AssetPath = PickAssetDialog->GetAssetPath();
		const FText AssetName = PickAssetDialog->GetAssetName();

		FDMXControlConsoleManager& ControlConsoleManager = FDMXControlConsoleManager::Get();
		SelectedControlConsolePreset = ControlConsoleManager.CreateNewPreset(AssetPath.ToString(), AssetName.ToString());
		if (SelectedControlConsolePreset.IsValid())
		{
			ControlConsoleManager.LoadFromPreset(SelectedControlConsolePreset.Get());
		}

		SaveSelectedPreset();
	}

	return FReply::Handled();
}

void SDMXControlConsolePresetWidget::OnPresetSelected(const FAssetData& AssetData)
{
	if (SelectedControlConsolePreset.IsValid() && 
		SelectedControlConsolePreset->GetPackage()->IsDirty())
	{
		const FText SaveMsg = LOCTEXT("PresetSaveMsg", "Do you want to save the current Preset?");
		if (FMessageDialog::Open(EAppMsgType::YesNo, SaveMsg) == EAppReturnType::Yes)
		{
			SaveSelectedPreset();
		}
	}

	UDMXControlConsolePreset* ControlConsolePreset = Cast<UDMXControlConsolePreset>(AssetData.GetAsset());
	SelectedControlConsolePreset = ControlConsolePreset;

	FDMXControlConsoleManager::Get().LoadFromPreset(ControlConsolePreset);
}

#undef LOCTEXT_NAMESPACE
