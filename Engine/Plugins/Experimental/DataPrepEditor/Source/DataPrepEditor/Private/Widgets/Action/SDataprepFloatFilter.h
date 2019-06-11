// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "UObject/GCObject.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SComboButton;
class UDataprepFloatFilter;

template<class t>
class SComboBox;

class SDataprepFloatFilter : public SCompoundWidget, public FGCObject
{
	SLATE_BEGIN_ARGS(SDataprepFloatFilter) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UDataprepFloatFilter& InFilter);

private:
	// The string matching option for the combo box (Displayed text, Tooltip, mapping for the UEnum)
	using FListEntry = TTuple<FText, FText, int32>;

	// Those function are for the float matching criteria display
	TSharedRef<SWidget> OnGenerateWidgetForMatchingCriteria(TSharedPtr<FListEntry> ListEntry) const;
	FText GetSelectedCriteriaText() const;
	FText GetSelectedCriteriaTooltipText() const;
	void OnSelectedCriteriaChanged(TSharedPtr<FListEntry> ListEntry, ESelectInfo::Type SelectionType);
	void OnCriteriaComboBoxOpenning();

	// Those functions are for the float that will be compare against the fetched float
	float GetEqualValue() const;
	void OnEqualValueChanged(float NewEqualValue);
	void OnEqualValueComitted(float NewEqualValue, ETextCommit::Type CommitType);

	// Those function are for the tolerance display
	EVisibility GetToleranceRowVisibility() const;
	float GetTolerance() const;
	void OnToleranceChanged(float NewTolerance);
	void OnToleranceComitted(float NewTolerance, ETextCommit::Type CommitType);

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ End FGCObject interface

	float OldEqualValue;
	float OldTolerance;

	UDataprepFloatFilter* Filter;

	TArray<TSharedPtr<FListEntry>> FloatMatchingOptions;

	TSharedPtr<SComboBox<TSharedPtr<FListEntry>>> FloatMatchingCriteriaWidget;
};
