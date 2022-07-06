// Copyright Epic Games, Inc. All Rights Reserved.

#include "UGSTab.h"

#define LOCTEXT_NAMESPACE "UGSTab"

UGSTab::UGSTab() : TabArgs(nullptr, FTabId()),
				   TabWidget(SNew(SDockTab)),
				   EmptyTabView(SNew(SEmptyTab).Tab(this)),
				   GameSyncTabView(SNew(SGameSyncTab))
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

bool UGSTab::OnWorkspaceChosen(const FString& Path)
{
	bool bIsDataValid = !Path.IsEmpty(); // Todo: Actually validate the data
	if (bIsDataValid)
	{
		TabWidget->SetContent(GameSyncTabView); // Todo: Set GameSyncTabView data
		return true;
	}
	
	return false;
}

#undef LOCTEXT_NAMESPACE
