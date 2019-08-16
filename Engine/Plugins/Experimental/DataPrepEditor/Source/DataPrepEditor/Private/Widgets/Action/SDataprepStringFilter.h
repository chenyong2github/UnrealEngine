// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "UObject/GCObject.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SComboButton;
class UDataprepStringFilter;

template<class t>
class SComboBox;

class SDataprepStringFilter : public SCompoundWidget,  public FGCObject
{
	SLATE_BEGIN_ARGS(SDataprepStringFilter) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UDataprepStringFilter& InFilter);

private:
	// The string matching option for the combo box ( Displayed text, Tooltip, mapping for the UEnum)
	using FListEntry = TTuple<FText, FText, int32>;

	// Those function are for the string matching criteria display
	TSharedRef<SWidget> OnGenerateWidgetForMatchingCriteria(TSharedPtr<FListEntry> ListEntry) const;
	FText GetSelectedCriteriaText() const;
	FText GetSelectedCriteriaTooltipText() const;
	void OnSelectedCriteriaChanged(TSharedPtr<FListEntry> ListEntry, ESelectInfo::Type SelectionType);
	void OnCriteriaComboBoxOpenning();

	// This function is for the string that will be compare against the fetched string
	FText GetUserString() const;
	void OnUserStringChanged(const FText& NewText);
	void OnUserStringComitted(const FText& NewText, ETextCommit::Type CommitType);

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ End FGCObject interface

	FString OldUserString;

	UDataprepStringFilter* Filter;

	TArray<TSharedPtr<FListEntry>> StringMatchingOptions;

	TSharedPtr<SComboBox<TSharedPtr<FListEntry>>> StringMatchingCriteriaWidget;
};
