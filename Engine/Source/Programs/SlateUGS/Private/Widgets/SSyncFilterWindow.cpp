// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSyncFilterWindow.h"

#include "UGSTab.h"

#include "SPopupTextWindow.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "SPrimaryButton.h"
#include "Widgets/Colors/SSimpleGradient.h" // Todo: remove

#define LOCTEXT_NAMESPACE "SSyncFilterWindow"

void SSyncFilterWindow::Construct(const FArguments& InArgs)
{
	Tab = InArgs._Tab;

	SWindow::Construct(SWindow::FArguments()
	.Title(LOCTEXT("WindowTitle", "Sync Filters"))
	.SizingRule(ESizingRule::FixedSize)
	.ClientSize(FVector2D(1100, 800))
	[
		SNew(SVerticalBox)
		// Hint text
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 20.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SyncFilterHintText", "Files synced from Perforce may be filtered by a custom stream view, and list of predefined categories. Settings for the current workspace override defaults for all workspaces."))
		]
		// Filter checkbox list
		+SVerticalBox::Slot()
		.Padding(20.0f, 0.0f)
		[
			SNew(SSimpleGradient)
			.StartColor(FLinearColor(161.0f / 255.0f, 57.0f / 255.0f, 191.0f / 255.0f))
			.EndColor(FLinearColor(100.0f / 255.0f, 100.0f / 255.0f, 100.0f / 255.0f))
		]
		// Buttons
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 20.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(20.0f, 0.0f, 0.0f, 0.0f)
			.HAlign(HAlign_Left)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("ShowCombinedFilterButtonText", "Show Combined Filter"))
				.OnClicked(this, &SSyncFilterWindow::OnShowCombinedFilterClicked)
			]
			+SHorizontalBox::Slot()
			.Padding(0.0f, 0.0f, 10.0f, 0.0f)
			.HAlign(HAlign_Right)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(10.0f, 0.0f))
				+SUniformGridPanel::Slot(0, 0)
				[
					SNew(SPrimaryButton)
					.Text(LOCTEXT("OkButtonText", "Ok"))
					.OnClicked(this, &SSyncFilterWindow::OnOkClicked)
				]
				+SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("CancelButtonText", "Cancel"))
					.OnClicked(this, &SSyncFilterWindow::OnCancelClicked)
				]
			]
		]
	]);
}

FReply SSyncFilterWindow::OnShowCombinedFilterClicked()
{
	fprintf(stderr, "Sync filters:\n%sEnd of sync filters\n", TCHAR_TO_ANSI(*FString::Join(Tab->GetSyncFilters(), TEXT("\n"))));

	TSharedRef<SPopupTextWindow> CombinedFilterWindow = SNew(SPopupTextWindow)
		.TitleText(FText::FromString("Combined Sync Filter"))
		.BodyText(FText::FromString(FString::Join(Tab->GetCombinedSyncFilter(), TEXT("\n"))))
		.BodyTextJustification(ETextJustify::Left);
	FSlateApplication::Get().AddModalWindow(CombinedFilterWindow, SharedThis(this), false);
	return FReply::Handled();
}

FReply SSyncFilterWindow::OnOkClicked()
{
	// Todo: save sync filters

	FSlateApplication::Get().FindWidgetWindow(AsShared())->RequestDestroyWindow();
	return FReply::Handled();
}

FReply SSyncFilterWindow::OnCancelClicked()
{
	FSlateApplication::Get().FindWidgetWindow(AsShared())->RequestDestroyWindow();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
