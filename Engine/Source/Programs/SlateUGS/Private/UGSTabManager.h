// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UGSTab.h"

class UGSTabManager
{
public:
	UGSTabManager() = default;
	void ConstructTabs();
	TSharedRef<SDockTab> SpawnTab(int Index, const FSpawnTabArgs& Args);
	bool CanSpawnTab() const;
	int GetMaxTabs() const;
private:
	static constexpr int MaxTabs = 1;
	TStaticArray<UGSTab, MaxTabs> Tabs;
};