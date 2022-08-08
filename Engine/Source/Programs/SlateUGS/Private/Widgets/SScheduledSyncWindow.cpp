// Copyright Epic Games, Inc. All Rights Reserved.

#include "SScheduledSyncWindow.h"

#include "UGSTab.h"

#include "Widgets/Layout/SUniformGridPanel.h"
#include "SPrimaryButton.h"
#include "SSimpleButton.h"
#include "SPopupTextWindow.h"

#define LOCTEXT_NAMESPACE "SScheduledSyncWindow"

void SScheduledSyncWindow::Construct(const FArguments& InArgs)
{
	Tab = InArgs._Tab;

	SWindow::Construct(SWindow::FArguments()
	.Title(LOCTEXT("WindowTitle", "Schedule Sync"))
	.SizingRule(ESizingRule::FixedSize)
	.ClientSize(FVector2D(400, 300))
	[
		SNew(SVerticalBox)
		// Hint text
		// TODO check box to enable ScheduleSync
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 20.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ScheduleSync", "Set a time for a sync to go off on all or some project."))
		]
		// TODO widget to enter a time of day
		// TODO check box to enable it for all projects
		// TODO check box to enable each project if the previous one is not enabled
		// Buttons
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 20.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(0.0f, 0.0f, 10.0f, 0.0f)
			.HAlign(HAlign_Right)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(10.0f, 0.0f))
				+SUniformGridPanel::Slot(0, 0)
				[
					SNew(SPrimaryButton)
					.Text(LOCTEXT("SaveButtonText", "Save"))
					.OnClicked(this, &SScheduledSyncWindow::OnSaveClicked)
				]
				+SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("CancelButtonText", "Cancel"))
					.OnClicked(this, &SScheduledSyncWindow::OnCancelClicked)
				]
			]
		]
	]);
}

FReply SScheduledSyncWindow::OnSaveClicked()
{
	FSlateApplication::Get().FindWidgetWindow(AsShared())->RequestDestroyWindow();

	return FReply::Handled();
}

FReply SScheduledSyncWindow::OnCancelClicked()
{
	FSlateApplication::Get().FindWidgetWindow(AsShared())->RequestDestroyWindow();

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
