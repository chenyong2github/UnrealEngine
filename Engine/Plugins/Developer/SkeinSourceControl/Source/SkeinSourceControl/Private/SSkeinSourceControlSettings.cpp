// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSkeinSourceControlSettings.h"
#include "SkeinSourceControlModule.h"
#include "SkeinSourceControlUtils.h"
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
				.Text(LOCTEXT("SkeinCLINotFound", "The Skein environment is not available."))
				.ColorAndOpacity(FLinearColor::Red)
				.WrapTextAt(450.0f)
				.ToolTipText(LOCTEXT("SkeinCLINotFound_Tooltip", "Without the Skein environment the Unreal Editor cannot communicate to the Skein cloud server. Please make sure it's installed at the correct location and that no other instances of the application are running."))
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
	const bool bSkeinBinaryFound = SkeinSourceControlUtils::IsSkeinBinaryFound();
	return (!bSkeinBinaryFound) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SSkeinSourceControlSettings::CanUseSkeinProject() const
{
	FSkeinSourceControlModule& SkeinSourceControl = FModuleManager::LoadModuleChecked<FSkeinSourceControlModule>("SkeinSourceControl");
	const bool bSkeinBinaryFound = SkeinSourceControlUtils::IsSkeinBinaryFound();
	const bool bSkeinProjectFound = SkeinSourceControlUtils::IsSkeinProjectFound(FPaths::ProjectDir());
	return (bSkeinBinaryFound && !bSkeinProjectFound) ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
