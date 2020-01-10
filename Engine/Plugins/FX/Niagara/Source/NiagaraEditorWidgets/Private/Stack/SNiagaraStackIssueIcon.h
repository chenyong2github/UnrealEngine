// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/SlateDelegates.h"

class UNiagaraStackViewModel;
class UNiagaraStackEntry;

class SNiagaraStackIssueIcon : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackIssueIcon) {}
		SLATE_EVENT(FOnClicked, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UNiagaraStackViewModel* InStackViewModel, UNiagaraStackEntry* InStackEntry);

	~SNiagaraStackIssueIcon();
	
private:
	bool GetIconIsEnabled() const;

	const FSlateBrush* GetIconBrush() const;

	FText GetIconToolTip() const;

	void UpdateFromEntry();

private:
	const FSlateBrush* IconBrush;

	mutable TOptional<FText> IconToolTipCache;

	UNiagaraStackViewModel* StackViewModel;
	TWeakObjectPtr<UNiagaraStackEntry> StackEntry;
};