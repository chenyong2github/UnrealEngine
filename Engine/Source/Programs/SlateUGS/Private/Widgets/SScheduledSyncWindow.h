// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"

class UGSTab;

class SScheduledSyncWindow : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SScheduledSyncWindow) {}
		SLATE_ARGUMENT(UGSTab*, Tab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnSaveClicked();
	FReply OnCancelClicked();

	UGSTab* Tab;
};
