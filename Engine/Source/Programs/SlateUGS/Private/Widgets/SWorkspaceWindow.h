// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"

class UGSTab;

class SWorkspaceWindow : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SWorkspaceWindow) {}
		SLATE_ARGUMENT(UGSTab*, Tab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnOkClicked();
	FReply OnCancelClicked();

	FReply OnBrowseClicked();
	FReply OnNewClicked();

	bool bIsLocalFileSelected = true;
	TSharedPtr<SEditableTextBox> LocalFileText = nullptr;
	FString WorkspacePathText;

	UGSTab* Tab;
};
