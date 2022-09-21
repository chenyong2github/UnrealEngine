// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNewWorkspaceWindow.h"

#include "SlateUGSStyle.h"
#include "Framework/Application/SlateApplication.h"

#include "UGSTab.h"
#include "SGameSyncTab.h"
#include "SPrimaryButton.h"
#include "Widgets/Layout/SHeader.h"
#include "Widgets/SSelectStreamWindow.h"

#define LOCTEXT_NAMESPACE "UGSNewWorkspaceWindow"

void SNewWorkspaceWindow::Construct(const FArguments& InArgs, UGSTab* InTab)
{
	Tab = InTab;

	SWindow::Construct(SWindow::FArguments()
	.Title(LOCTEXT("WindowTitle", "New Workspace"))
	.SizingRule(ESizingRule::FixedSize)
	.ClientSize(FVector2D(800, 200))
	[
		SNew(SBox)
		.Padding(30.0f, 15.0f, 30.0f, 0.0f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHeader)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CustomView", "Settings"))
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Bottom)
			.Padding(40.0f, 20.0f)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(0.25f)
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("StreamText", "Stream:"))
					]
					+SVerticalBox::Slot()
					.Padding(0.0f, 7.5f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RootDirectoryText", "Root Directory:"))
					]
					+SVerticalBox::Slot()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NameText", "Name:"))
					]
				]
				+SHorizontalBox::Slot()
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						[
							SNew(SEditableTextBox)
						]
						+SHorizontalBox::Slot()
						.FillWidth(0.225f)
						.HAlign(HAlign_Right)
						[
							SNew(SButton)
							.Text(LOCTEXT("BrowseStreamButtonText", "Browse..."))
							.OnClicked(this, &SNewWorkspaceWindow::OnBrowseStreamClicked)
						]
					]
					+SVerticalBox::Slot()
					.Padding(0.0f, 7.5f)
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						[
							SNew(SEditableTextBox)
						]
						+SHorizontalBox::Slot()
						.FillWidth(0.225f)
						.HAlign(HAlign_Right)
						[
							SNew(SButton)
							.Text(LOCTEXT("BrowseRootDirectoryButtonText", "Browse..."))
							.OnClicked(this, &SNewWorkspaceWindow::OnBrowseRootDirectoryClicked)
						]
					]
					+SVerticalBox::Slot()
					[
						SNew(SEditableTextBox)
					]
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Bottom)
			[
				SNew(SBox)
				.HAlign(HAlign_Right)
				.Padding(10.0f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(SPrimaryButton)
						.Text(LOCTEXT("CreateButtonText", "Create"))
						.OnClicked(this, &SNewWorkspaceWindow::OnCreateClicked)
					]
					+SHorizontalBox::Slot()
					[
						SNew(SButton)
						.Text(LOCTEXT("CancelButtonText", "Cancel"))
						.OnClicked(this, &SNewWorkspaceWindow::OnCancelClicked)
					]
				]
			]
		]
	]);
}

FReply SNewWorkspaceWindow::OnBrowseStreamClicked()
{
	FSlateApplication::Get().AddModalWindow(SNew(SSelectStreamWindow, Tab), SharedThis(this), false);

	return FReply::Handled();
}

FReply SNewWorkspaceWindow::OnBrowseRootDirectoryClicked()
{
	return FReply::Handled();
}

FReply SNewWorkspaceWindow::OnCreateClicked()
{
	return FReply::Handled();
}

FReply SNewWorkspaceWindow::OnCancelClicked()
{
	FSlateApplication::Get().FindWidgetWindow(AsShared())->RequestDestroyWindow();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
