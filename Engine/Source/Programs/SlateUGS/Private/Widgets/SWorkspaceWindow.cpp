// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWorkspaceWindow.h"

#include "Styling/AppStyle.h"
#include "DesktopPlatformModule.h"

#include "UGSTab.h"
#include "HordeBuildRowInfo.h"
#include "SGameSyncTab.h"

#define LOCTEXT_NAMESPACE "UGSWorkspaceWindow"

void SWorkspaceWindow::Construct(const FArguments& InArgs)
{
	Tab = InArgs._Tab;

	this->ChildSlot
	[
		SNew(SBox)
		.Padding(10.0f, 10.0f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(20.0f, 20.0f)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SCheckBox).Style(FAppStyle::Get(), "RadioButton")
						.IsChecked_Lambda([this] () { return bIsLocalFileSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda( [this] (ECheckBoxState InState) { bIsLocalFileSelected = (bIsLocalFileSelected || InState == ECheckBoxState::Checked); } )
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("LocalFileText", "Local File"))
					]
				]
				+SVerticalBox::Slot()
				.Padding(0.0f, 10.0f)
				[
					SNew(SHorizontalBox)
					.IsEnabled_Lambda([this]() { return bIsLocalFileSelected; })
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.FillWidth(1)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("FileText", "File:"))
					]
					+SHorizontalBox::Slot()
					.Padding(10.0f, 0.0f)
					.FillWidth(7)
					[
						SAssignNew(LocalFileText, SEditableTextBox)
						.HintText(LOCTEXT("FilePathHint", "Path/To/ProjectFile.uproject")) // Todo: Make hint text use backslash for Windows, forward slash for Unix
			 			.OnTextChanged_Lambda([this](const FText& InText)
						{
							WorkspacePathText = InText.ToString();
							fprintf(stderr, "Workspace path text changed to: %s\n", TCHAR_TO_ANSI(*WorkspacePathText));
						})
					]
					+SHorizontalBox::Slot()
					.FillWidth(2)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("BrowseText", "Browse..."))
						.OnClicked(this, &SWorkspaceWindow::OnBrowseClicked)
					]
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(20.0f, 10.0f)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SCheckBox).Style(FAppStyle::Get(), "RadioButton")
						.IsChecked_Lambda([this] () { return bIsLocalFileSelected ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
						.OnCheckStateChanged_Lambda( [this] (ECheckBoxState InState) { bIsLocalFileSelected = (!bIsLocalFileSelected && InState == ECheckBoxState::Checked); } )
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("WorkspaceText", "Workspace"))
					]
				]
				+SVerticalBox::Slot()
				.Padding(0.0f, 10.0f)
				[
					SNew(SVerticalBox)
					.IsEnabled_Lambda([this]() { return !bIsLocalFileSelected; })
					+SVerticalBox::Slot()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						.FillWidth(1)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NameText", "Name:"))
						]
						+SHorizontalBox::Slot()
						.Padding(10.0f, 0.0f)
						.FillWidth(5)
						[
							SNew(SEditableTextBox)
							.HintText(LOCTEXT("NameHint", "WorkspaceName"))
						]
						+SHorizontalBox::Slot()
						.FillWidth(2)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.Text(LOCTEXT("NewText", "New..."))
						]
						+SHorizontalBox::Slot()
						.Padding(10.0f, 0.0f, 0.0f, 0.0f)
						.FillWidth(2)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.Text(LOCTEXT("BrowseText", "Browse..."))
						]
					]
					+SVerticalBox::Slot()
					.Padding(0.0f, 10.0f)
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						.FillWidth(1)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FileText", "File:"))
						]
						+SHorizontalBox::Slot()
						.Padding(10.0f, 0.0f)
						.FillWidth(7)
						[
							SNew(SEditableTextBox)
							.HintText(LOCTEXT("WorkspacePathHint", "/Relative/Path/To/ProjectFile.uproject"))
						]
						+SHorizontalBox::Slot()
						.FillWidth(2)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.Text(LOCTEXT("BrowseText", "Browse..."))
						]
					]
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(20.0f, 10.0f)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.Padding(10.0f, 0.0f)
				.FillWidth(6) // Todo: figure out how to right justify below buttons without using this invisible dummy button as a space filler
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Visibility(EVisibility::Hidden)
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.FillWidth(2)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("OkText", "Ok"))
					.OnClicked(this, &SWorkspaceWindow::OnOkClicked)
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.Padding(10.0f, 0.0f, 0.0f, 0.0f)
				.FillWidth(2)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("CancelText", "Cancel"))
					.OnClicked(this, &SWorkspaceWindow::OnCancelClicked)
				]
			]
		]
	];
}

FReply SWorkspaceWindow::OnOkClicked()
{
	bool bIsWorkspaceValid = Tab->OnWorkspaceChosen(WorkspacePathText);
	if (bIsWorkspaceValid)
	{
		FSlateApplication::Get().FindWidgetWindow(AsShared())->RequestDestroyWindow();
	}
	else
	{
		// Todo: factor out into an error window widget class (check if one already exists)
		TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("ErrorWindowTitle", "Error Opening Project"))
		.SizingRule(ESizingRule::Autosized)
		.MaxWidth(400.0f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(10.0f, 10.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("ErrorText", "Error opening .uproject file, try again")) // Todo: detect the actual reason for error and report it here
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("ErrorWindowOkayButtonText", "Ok"))
				.OnClicked_Lambda([&Window]() 
				{
					Window->RequestDestroyWindow();
					return FReply::Handled(); 
				})
			]
		];

		FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveModalWindow(), false); // Todo: Figure out parent window for modal window
		// Todo: loading screen widget for detect settings
	}

	return FReply::Handled();
}

FReply SWorkspaceWindow::OnCancelClicked()
{
	FSlateApplication::Get().FindWidgetWindow(AsShared())->RequestDestroyWindow();
	return FReply::Handled();
}

FReply SWorkspaceWindow::OnBrowseClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	TArray<FString> OutOpenFilenames;
	if (DesktopPlatform)
	{
		DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
			LOCTEXT("OpenDialogTitle", "Open Unreal Project").ToString(),
			TEXT(""), // Todo: maybe give a default path (such as previously selected path)
			TEXT(""),
			TEXT("Unreal Project Files (*.uproject)|*.uproject"),
			EFileDialogFlags::None,
			OutOpenFilenames
		);
	}

	if (!OutOpenFilenames.IsEmpty())
	{
		LocalFileText->SetText(FText::FromString(OutOpenFilenames[0]));
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
