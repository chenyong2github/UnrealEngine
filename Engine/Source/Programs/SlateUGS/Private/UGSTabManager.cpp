// Copyright Epic Games, Inc. All Rights Reserved.

#include "UGSTabManager.h"
#include "Containers/Array.h"
#include "UGSLog.h"

void UGSTabManager::ConstructTabs()
{
	TSharedRef<FTabManager::FStack> TabStack = FTabManager::NewStack();
	TSharedPtr<FGlobalTabmanager> TabManager = FGlobalTabmanager::Get();

	for (int TabIndex = 0; TabIndex < MaxTabs; TabIndex++)
	{
		const FName TabId = GetTabId(TabIndex);
		TabManager->RegisterTabSpawner(TabId, FOnSpawnTab::CreateLambda([this, TabIndex] (const FSpawnTabArgs& Args) { return SpawnTab(TabIndex, Args); }));

		// Leave the first tab opened, close the rest
		if (TabIndex == 0)
		{
			TabStack->AddTab(TabId, ETabState::OpenedTab);
		}
		else
		{
			TabStack->AddTab(TabId, ETabState::ClosedTab);
		}
	}

	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("UGS_Layout")
	->AddArea
	(
		FTabManager::NewArea(1230, 900)
		->Split(TabStack)
	);

	TabManager->RestoreFrom(Layout, nullptr);
}

void UGSTabManager::Tick()
{
	for (UGSTab& Tab : Tabs)
	{
		Tab.Tick();
	}
}

TSharedRef<SDockTab> UGSTabManager::SpawnTab(int Index, const FSpawnTabArgs& Args)
{
	Tabs[Index].Initialize();
	Tabs[Index].SetTabArgs(Args);
	Tabs[Index].SetTabManager(this);
	return Tabs[Index].GetTabWidget();
}

// Todo: replace this super hacky way of fetching the first available closed tab
void UGSTabManager::ActivateTab()
{
	TSharedPtr<FGlobalTabmanager> TabManager = FGlobalTabmanager::Get();
	for (int TabIndex = 0; TabIndex < MaxTabs; TabIndex++)
	{
		FName TabId = GetTabId(TabIndex);
		if (TabManager->FindExistingLiveTab(TabId).IsValid())
		{
			continue;
		}
		if (TabManager->TryInvokeTab(TabId, false).IsValid())
		{
			Tabs[TabIndex].Initialize();
			return;
		}
	}

	UE_LOG(LogSlateUGS, Warning, TEXT("Cannot activate any more tabs"));
}

FName UGSTabManager::GetTabId(int TabIndex) const
{
	return FName(FString(TEXT("UGS Tab: ")) + FString::FromInt(TabIndex));
}