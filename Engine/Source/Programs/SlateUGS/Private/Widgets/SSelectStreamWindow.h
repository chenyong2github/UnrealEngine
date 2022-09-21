// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"

class UGSTab;
struct FStreamNode;

class SSelectStreamWindow final : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SSelectStreamWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UGSTab* InTab);

private:
	FReply OnOkClicked();
	FReply OnCancelClicked();

	TSharedPtr<SEditableTextBox> FilterText;
	TArray<TSharedPtr<FStreamNode>> StreamsTree;

	UGSTab* Tab = nullptr;
};
