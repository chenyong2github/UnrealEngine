// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"

class UGSTab;

class SNewWorkspaceWindow final : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SNewWorkspaceWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UGSTab* InTab);

private:
	FReply OnBrowseStreamClicked();
	FReply OnBrowseRootDirectoryClicked();

	FReply OnCreateClicked();
	FReply OnCancelClicked();

	TSharedPtr<SEditableTextBox> StreamTextBox = nullptr;
	TSharedPtr<SEditableTextBox> RootDirTextBox = nullptr;
	TSharedPtr<SEditableTextBox> FileNameTextBox = nullptr;
	FString WorkspacePathText;

	UGSTab* Tab = nullptr;
};
