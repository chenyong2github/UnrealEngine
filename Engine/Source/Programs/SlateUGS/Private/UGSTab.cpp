// Copyright Epic Games, Inc. All Rights Reserved.

#include "UGSTab.h"

#define LOCTEXT_NAMESPACE "UGSTab"

UGSTab::UGSTab() : TabArgs(nullptr, FTabId()),
				   TabWidget(SNew(SDockTab)),
				   EmptyTabView(SNew(SEmptyTab).Tab(this)),
				   GameSyncTabView(SNew(SGameSyncTab)),
				   WorkspaceWindowView(SNew(SWorkspaceWindow).Tab(this))
{
	TabWidget->SetContent(EmptyTabView);
}

const TSharedRef<SDockTab> UGSTab::GetTabWidget()
{
	return TabWidget;
}

void UGSTab::SetTabArgs(FSpawnTabArgs InTabArgs) 
{ 
	TabArgs = InTabArgs; 
}

FSpawnTabArgs UGSTab::GetTabArgs() const 
{ 
	return TabArgs; 
}

FReply UGSTab::OnWorkspaceChosen(const FString& Path)
{
	fprintf(stderr, "Workspace path text received by OnWorkspaceChosen is: %s\n", TCHAR_TO_ANSI(*Path)); // Todo: Actually get the path variable bound
	bool bIsDataValid = !Path.IsEmpty(); // Todo: Actually validate the data
	if (bIsDataValid)
	{
		TabWidget->SetContent(GameSyncTabView); // Todo: Set GameSyncTabView data
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

#undef LOCTEXT_NAMESPACE
