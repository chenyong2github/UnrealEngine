// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFilePathBlock.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "PluginStyle.h"

#define LOCTEXT_NAMESPACE "FilePathBlock"

void SFilePathBlock::Construct(const FArguments& InArgs)
{
	FText BrowseForFolderToolTipText;

	const bool bReadOnlyFolderPath = InArgs._ReadOnlyFolderPath;

	if (bReadOnlyFolderPath)
	{
		BrowseForFolderToolTipText = LOCTEXT("BrowseForFolderDisabled", "You cannot modify this location");
	}
	else
	{
		BrowseForFolderToolTipText = LOCTEXT("BrowseForFolder", "Browse for a folder");
	}

	ChildSlot
	[
		SNew(SHorizontalBox)
		// Folder input
		+SHorizontalBox::Slot()
		[
			SNew(SBox)
			.MinDesiredWidth(250.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(FolderPathTextBox, SEditableTextBox)
					.Text(InArgs._FolderPath)
					// Large right hand padding to make room for the browse button
					.Padding(FMargin(5.f, 3.f, 25.f, 3.f))
					.OnTextChanged(InArgs._OnFolderChanged)
					.OnTextCommitted(InArgs._OnFolderCommitted)
					.IsReadOnly(bReadOnlyFolderPath)
					.HintText(LOCTEXT("Folder", "Folder"))
					.ForegroundColor(FSlateColor::UseForeground())
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.Padding(1)
				[
					SNew(SButton)
					.ButtonStyle(FPluginStyle::Get(), "PluginPath.BrowseButton")
					.OnClicked(InArgs._OnBrowseForFolder)
					.ContentPadding(0)
					.ToolTipText(BrowseForFolderToolTipText)
					.Text(LOCTEXT("...", "..."))
					.IsEnabled(!bReadOnlyFolderPath)
				]
			]
		]
		// Name input
		+ SHorizontalBox::Slot()
		.Padding(FMargin(5.f, 0.f, 0.f, 0.f))
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.MinDesiredWidth(200.f)
			[
				SAssignNew(NameTextBox, SEditableTextBox)
				.Text(InArgs._Name)
				.Padding(FMargin(5.f, 3.f))
				.HintText(InArgs._NameHint)
				.OnTextChanged(InArgs._OnNameChanged)
				.OnTextCommitted(InArgs._OnNameCommitted)
			]
		]
	];
}

void SFilePathBlock::SetFolderPathError(const FText& ErrorText)
{
	if (FolderPathTextBox.IsValid())
	{
		FolderPathTextBox->SetError(ErrorText);
	}
}

void SFilePathBlock::SetNameError(const FText& ErrorText)
{
	if (NameTextBox.IsValid())
	{
		NameTextBox->SetError(ErrorText);
	}
}

#undef LOCTEXT_NAMESPACE
