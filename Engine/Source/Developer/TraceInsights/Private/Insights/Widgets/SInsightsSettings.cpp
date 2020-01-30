// Copyright Epic Games, Inc. All Rights Reserved.

#include "SInsightsSettings.h"

#include "EditorStyleSet.h"
#include "Fonts/SlateFontInfo.h"
#include "Misc/Paths.h"
#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/InsightsSettings.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SInsightsSettings"

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SInsightsSettings::Construct(const FArguments& InArgs)
{
	OnClose = InArgs._OnClose;
	SettingPtr = InArgs._SettingPtr;

	const TSharedRef<SGridPanel> SettingsGrid = SNew(SGridPanel);
	int32 CurrentRowPos = 0;

	AddTitle(LOCTEXT("SettingsTitle","Unreal Insights - Settings"), SettingsGrid, CurrentRowPos);
	AddSeparator(SettingsGrid, CurrentRowPos);
	AddHeader(LOCTEXT("TimingProfilerTitle","TimingProfiler"), SettingsGrid, CurrentRowPos);
	AddOption
	(
		LOCTEXT("bShowEmptyTracksByDefault_T","Show empty CPU/GPU tracks in Timing View, by default"),
		LOCTEXT("bShowEmptyTracksByDefault_TT","If True, empty CPU/GPU tracks will be visible in Timing View. Applies when session starts."),
		SettingPtr->bShowEmptyTracksByDefault,
		SettingPtr->GetDefaults().bShowEmptyTracksByDefault,
		SettingsGrid,
		CurrentRowPos
	);
	AddSeparator(SettingsGrid, CurrentRowPos);
	AddFooter(SettingsGrid, CurrentRowPos);

	ChildSlot
	[
		SettingsGrid
	];

	SettingPtr->EnterEditMode();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SInsightsSettings::AddTitle(const FText& TitleText, const TSharedRef<SGridPanel>& Grid, int32& RowPos)
{
	Grid->AddSlot(0, RowPos++)
	.Padding(2.0f)
	[
		SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
		.Text(TitleText)
	];
	RowPos++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SInsightsSettings::AddSeparator(const TSharedRef<SGridPanel>& Grid, int32& RowPos)
{
	Grid->AddSlot(0, RowPos++)
	.Padding(2.0f)
	.ColumnSpan(2)
	[
		SNew(SSeparator)
		.Orientation(Orient_Horizontal)
	];
	RowPos++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SInsightsSettings::AddHeader(const FText& HeaderText, const TSharedRef<SGridPanel>& Grid, int32& RowPos)
{
	Grid->AddSlot(0, RowPos++)
	.Padding(2.0f)
	[
		SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
		.Text(HeaderText)
	];
	RowPos++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SInsightsSettings::AddOption(const FText& OptionName, const FText& OptionDesc, bool& Value, const bool& Default, const TSharedRef<SGridPanel>& Grid, int32& RowPos)
{
	Grid->AddSlot(0, RowPos)
	.Padding(2.0f)
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(OptionName)
		.ToolTipText(OptionDesc)
	];

	Grid->AddSlot(1, RowPos)
	.Padding(2.0f)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Fill)
	[
		SNew(SHorizontalBox)

		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SInsightsSettings::OptionValue_IsChecked, (const bool*)&Value)
			.OnCheckStateChanged(this, &SInsightsSettings::OptionValue_OnCheckStateChanged, (bool*)&Value)
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("ResetToDefaultToolTip", "Reset to default"))
			.ButtonStyle(FEditorStyle::Get(), TEXT("NoBorder"))
			.ContentPadding(0.0f)
			.Visibility(this, &SInsightsSettings::OptionDefault_GetDiffersFromDefaultAsVisibility, (const bool*)&Value, (const bool*)&Default)
			.OnClicked(this, &SInsightsSettings::OptionDefault_OnClicked, (bool*)&Value, (const bool*)&Default)
			.Content()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
			]
		]
	];

	RowPos++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SInsightsSettings::AddFooter(const TSharedRef<SGridPanel>& Grid, int32& RowPos)
{
	Grid->AddSlot(0, RowPos)
	.Padding(2.0f)
	.ColumnSpan(2)
	.HAlign(HAlign_Right)
	[
		SNew(SHorizontalBox)

		+SHorizontalBox::Slot()
		.AutoWidth()
		.MaxWidth(132)
		.HAlign(HAlign_Center)
		[
			SNew(SButton)
			.OnClicked(this, &SInsightsSettings::SaveAndClose_OnClicked)
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Profiler.Misc.Save16"))
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SaveAndCloseTitle","Save and close"))
				]
			]
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.MaxWidth(132)
		.HAlign(HAlign_Center)
		[
			SNew(SButton)
			.OnClicked(this, &SInsightsSettings::ResetToDefaults_OnClicked)
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Profiler.Misc.Reset16"))
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ResetToDefaultsTitle","Reset to defaults"))
				]
			]
		]
	];

	RowPos++;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SInsightsSettings::SaveAndClose_OnClicked()
{
	OnClose.ExecuteIfBound();
	SettingPtr->ExitEditMode();
	SettingPtr->SaveToConfig();

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SInsightsSettings::ResetToDefaults_OnClicked()
{
	SettingPtr->ResetToDefaults();

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SInsightsSettings::OptionValue_OnCheckStateChanged(ECheckBoxState CheckBoxState, bool* ValuePtr)
{
	*ValuePtr = CheckBoxState == ECheckBoxState::Checked ? true : false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SInsightsSettings::OptionValue_IsChecked(const bool* ValuePtr) const
{
	return *ValuePtr ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SInsightsSettings::OptionDefault_GetDiffersFromDefaultAsVisibility(const bool* ValuePtr, const bool* DefaultPtr) const
{
	return *ValuePtr != *DefaultPtr ? EVisibility::Visible : EVisibility::Hidden;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SInsightsSettings::OptionDefault_OnClicked(bool* ValuePtr, const bool* DefaultPtr)
{
	*ValuePtr = *DefaultPtr;

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
