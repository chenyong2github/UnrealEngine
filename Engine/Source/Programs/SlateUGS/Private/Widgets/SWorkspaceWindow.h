// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"

class UGSTab;

class SWorkspaceWindow final : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SWorkspaceWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UGSTab* InTab);

private:
	FReply OnOkClicked();
	FReply OnCancelClicked();

	FReply OnBrowseClicked();
	FString PreviousProjectPath;

	FReply OnNewClicked();

	bool bIsLocalFileSelected = true;
	TSharedPtr<SEditableTextBox> LocalFileText = nullptr;
	FString WorkspacePathText;

	UGSTab* Tab = nullptr;
};
