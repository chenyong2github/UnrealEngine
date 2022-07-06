// Copyright Epic Games, Inc. All Rights Reserved.

#include "UGSTabManager.h"
#include "Containers/Array.h"

void UGSTabManager::ConstructTabs()
{
	TSharedRef<FTabManager::FStack> TabStack = FTabManager::NewStack();

	for (int TabIndex = 0; TabIndex < MaxTabs; TabIndex++)
	{
		const FName TabId = FName(FString(TEXT("UGS Tab: ")) + FString::FromInt(TabIndex));
		FGlobalTabmanager::Get()->RegisterTabSpawner(TabId, 
			FOnSpawnTab::CreateLambda([this, TabIndex] (const FSpawnTabArgs& Args) { return SpawnTab(TabIndex, Args); }));//,)
		// 	FCanSpawnTab::CreateLambda([this] (const FSpawnTabArgs& Args) { }) // Todo: going to need to limit this to max tabs later
		// );

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

	FGlobalTabmanager::Get()->RestoreFrom(Layout, nullptr);
}

TSharedRef<SDockTab> UGSTabManager::SpawnTab(int Index, const FSpawnTabArgs& Args)
{
	Tabs[Index].SetTabArgs(Args);
	return Tabs[Index].GetTabWidget();
}
