// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UNiagaraStackItemGroup;
class SComboButton;

class SNiagaraStackItemGroupAddButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackItemGroupAddButton)
		:_Width(TextIconSize * 2)
		{}
		SLATE_ARGUMENT(float, Width)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackItemGroup& InStackItemGroup);

private:
	TSharedRef<SWidget> GetAddMenu();

	FReply AddDirectlyButtonClicked();

private:
	TWeakObjectPtr<UNiagaraStackItemGroup> StackItemGroupWeak;
	TSharedPtr<SComboButton> AddActionButton;
	static const float TextIconSize;
};