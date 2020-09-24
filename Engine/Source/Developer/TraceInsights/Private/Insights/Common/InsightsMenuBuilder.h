// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FWorkspaceItem;
class FMenuBuilder;

class FInsightsMenuBuilder
{
public:
	FInsightsMenuBuilder();

	TSharedRef<FWorkspaceItem> GetInsightsToolsGroup();
	TSharedRef<FWorkspaceItem> GetWindowsGroup();

	void PopulateMenu(FMenuBuilder& MenuBuilder);

private:
#if !WITH_EDITOR
	TSharedRef<FWorkspaceItem> InsightsToolsGroup;
	TSharedRef<FWorkspaceItem> WindowsGroup;
#endif
};
