// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SNiagaraStackEntryWidget.h"
#include "Styling/SlateTypes.h"
#include "Layout/Visibility.h"

class UNiagaraStackItem;
class UNiagaraStackViewModel; 

class SNiagaraStackItem: public SNiagaraStackEntryWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackItem) { }
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackItem& InItem, UNiagaraStackViewModel* InStackViewModel);
	
private:
	EVisibility GetResetToBaseButtonVisibility() const;

	FText GetResetToBaseButtonToolTipText() const;

	FReply ResetToBaseButtonClicked();

	EVisibility GetDeleteButtonVisibility() const;

	FText GetDeleteButtonToolTipText() const;

	bool GetDeleteButtonEnabled() const;

	FReply DeleteClicked();

	void OnCheckStateChanged(ECheckBoxState InCheckState);

	ECheckBoxState CheckEnabledStatus() const;

private:
	UNiagaraStackItem* Item;
};