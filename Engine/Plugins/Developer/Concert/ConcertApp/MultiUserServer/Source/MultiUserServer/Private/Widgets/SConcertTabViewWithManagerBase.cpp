// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertTabViewWithManagerBase.h"

#include "Framework/Docking/TabManager.h"

void SConcertTabViewWithManagerBase::Construct(const FArguments& InArgs, FName InStatusBarId)
{
	check(InArgs._ConstructUnderWindow && InArgs._ConstructUnderMajorTab);
	SConcertTabViewBase::Construct(
		SConcertTabViewBase::FArguments()
		.Content()
		[
			CreateTabs(InArgs)
		],
		InStatusBarId
		);
}

TSharedRef<SWidget> SConcertTabViewWithManagerBase::CreateTabs(const FArguments& InArgs)
{
	TabManager = FGlobalTabmanager::Get()->NewTabManager(InArgs._ConstructUnderMajorTab.ToSharedRef());
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(InArgs._LayoutName);
	InArgs._CreateTabs.ExecuteIfBound(TabManager.ToSharedRef(), Layout);
	return TabManager->RestoreFrom(Layout, InArgs._ConstructUnderWindow).ToSharedRef();
}
