// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"

class UGSTab;

class SSyncFilterWindow : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SSyncFilterWindow) {}
		SLATE_ARGUMENT(UGSTab*, Tab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnShowCombinedFilterClicked();
	FReply OnCustomViewSyntaxClicked();
	FReply OnOkClicked();
	FReply OnCancelClicked();

	UGSTab* Tab;
};
