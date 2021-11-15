// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSkeinSourceControlSettings.h"
#include "SkeinSourceControlModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SSkeinSourceControlSettings"

void SSkeinSourceControlSettings::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		// Explanation text for missing Skein CLI
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(2.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SSkeinSourceControlSettings::CanUseSkeinCLI)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SkeinCLINotFound", "The Skein Command Line application could not be found."))
				.ColorAndOpacity(FLinearColor::Red)
				.WrapTextAt(450.0f)
				.ToolTipText(LOCTEXT("SkeinCLINotFound_Tooltip", "Without the Skein Command Line application the Unreal Editor cannot communicate to the Skein cloud server. Please make sure it's installed at the correct location."))
			]
		]
		// Explanation text for missing Skein project
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(2.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SSkeinSourceControlSettings::CanUseSkeinProject)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SkeinProjectNotFound", "There is no Skein project initialized for this location."))
				.ColorAndOpacity(FLinearColor::Red)
				.WrapTextAt(450.0f)
				.ToolTipText(LOCTEXT("SkeinProjectNotFound_Tooltip", "Skein projects are created in the Skein Web UI and then cloned to your local machine using the Skein Command Line application. Once complete, you can use the Skein Source Control plugin to manage the project assets from within Unreal Editor."))
			]
		]
	];
}

EVisibility SSkeinSourceControlSettings::CanUseSkeinCLI() const
{
	FSkeinSourceControlModule& SkeinSourceControl = FModuleManager::LoadModuleChecked<FSkeinSourceControlModule>("SkeinSourceControl");
	const bool bSkeinAvailable = SkeinSourceControl.GetProvider().IsAvailable();
	return (!bSkeinAvailable) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SSkeinSourceControlSettings::CanUseSkeinProject() const
{
	FSkeinSourceControlModule& SkeinSourceControl = FModuleManager::LoadModuleChecked<FSkeinSourceControlModule>("SkeinSourceControl");
	const bool bSkeinAvailable = SkeinSourceControl.GetProvider().IsAvailable();
	const bool bSkeinEnabled = SkeinSourceControl.GetProvider().IsEnabled();
	return (bSkeinAvailable && !bSkeinEnabled) ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
