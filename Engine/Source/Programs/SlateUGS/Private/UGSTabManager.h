// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UGSTab.h"

class UGSTabManager
{
public:
	UGSTabManager() = default;
	void ConstructTabs();

	void Tick();

	TSharedRef<SDockTab> SpawnTab(int Index, const FSpawnTabArgs& Args);
	void ActivateTab();
	FName GetTabId(int TabIndex) const;

private:
	static constexpr int MaxTabs = 10;
	TStaticArray<UGSTab, MaxTabs> Tabs;
};
