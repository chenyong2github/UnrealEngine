// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"

class UNiagaraStackViewModel;
class UNiagaraStackEntry;
class SBox;

class SNiagaraStackEntryWidget : public SCompoundWidget
{
public:
	FSlateColor GetTextColorForSearch(FSlateColor DefaultColor) const;
	FReply ExpandEntry();
	
protected:
	bool IsCurrentSearchMatch() const;
	
protected:
	UNiagaraStackViewModel* StackViewModel;
	UNiagaraStackEntry* StackEntryItem;
};

class SNiagaraStackDisplayName : public SNiagaraStackEntryWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackDisplayName) { }
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackEntry& InStackEntry, UNiagaraStackViewModel& InStackViewModel, FName InTextStyleName);

	~SNiagaraStackDisplayName();

private:
	TSharedRef<SWidget> ConstructChildren();

	FText GetTopLevelDisplayName(TWeakPtr<UNiagaraStackViewModel::FTopLevelViewModel> TopLevelViewModelWeak) const;

	void StackViewModelStructureChanged();

	bool GetIsEnabled() const;

private:
	FName TextStyleName;

	TSharedPtr<SBox> Container;

	mutable FText TopLevelDisplayNameCache;
	mutable FText TopLevelDisplayNameFormattedCache;
	int32 TopLevelViewModelCountAtLastConstruction;
};
