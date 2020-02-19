// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "Framework/SlateDelegates.h"

class UNiagaraStackViewModel;
class UNiagaraStackEntry;
class SBox;
class SInlineEditableTextBlock;

class SNiagaraStackEntryWidget : public SCompoundWidget
{
public:
	FReply ExpandEntry();
	
	FSlateColor GetTextColorForSearch(FSlateColor DefaultColor) const;

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
		SLATE_EVENT(FOnTextCommitted, OnRenameCommitted);
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TypeNameStyle)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackEntry& InStackEntry, UNiagaraStackViewModel& InStackViewModel, FName InTextStyleName);

	~SNiagaraStackDisplayName();

	void StartRename();

private:
	void OnEndRename();

	TSharedRef<SWidget> ConstructChildren();

	FText GetTopLevelDisplayName(TWeakPtr<UNiagaraStackViewModel::FTopLevelViewModel> TopLevelViewModelWeak) const;

	void StackViewModelStructureChanged();

	FText GetOriginalName() const;

	EVisibility ShouldShowOriginalName() const;

private:
	FName TextStyleName;
	const FTextBlockStyle* TypeNameStyle;

	TSharedPtr<SBox> Container;

	TSharedPtr<SInlineEditableTextBlock> EditableTextBlock;
	FOnTextCommitted OnRenameCommitted;

	mutable FText TopLevelDisplayNameCache;
	mutable FText TopLevelDisplayNameFormattedCache;
	int32 TopLevelViewModelCountAtLastConstruction;
};
