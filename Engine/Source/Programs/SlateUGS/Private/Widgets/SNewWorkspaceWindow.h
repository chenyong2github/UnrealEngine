// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"

class UGSTab;

class SNewWorkspaceWindow final : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SNewWorkspaceWindow) {}
	SLATE_ARGUMENT(UGSTab*, Tab)
SLATE_END_ARGS()

void Construct(const FArguments& InArgs);

private:
	FReply OnBrowseStreamClicked();
	FReply OnBrowseRootDirectoryClicked();

	FReply OnCreateClicked();
	FReply OnCancelClicked();

	TSharedPtr<SEditableTextBox> LocalFileText = nullptr;
	FString WorkspacePathText;

	UGSTab* Tab = nullptr;
};
