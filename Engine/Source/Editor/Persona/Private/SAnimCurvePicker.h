// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"

class IEditableSkeleton;

DECLARE_DELEGATE_OneParam(FOnCurveNamePicked, const FName& /*PickedName*/)

class SAnimCurvePicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAnimCurvePicker) {}

	SLATE_EVENT(FOnCurveNamePicked, OnCurveNamePicked)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<IEditableSkeleton>& InEditableSkeleton);

private:
	/** Refresh the list of available curves */
	void RefreshListItems();

	/** Filter available curves */
	void FilterAvailableCurves();

	/** UI handlers */
	void HandleSelectionChanged(TSharedPtr<FString> InItem, ESelectInfo::Type InSelectionType);
	TSharedRef<ITableRow> HandleGenerateRow(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& InOwnerTable);
	void HandleFilterTextChanged(const FText& InFilterText);

private:
	/** Delegate fired when a curve name is picked */
	FOnCurveNamePicked OnCurveNamePicked;

	/** The editable skeleton we use to grab curves from */
	TWeakPtr<IEditableSkeleton> EditableSkeleton;

	/** The names of the curves we are displaying */
	TArray<TSharedPtr<FString>> CurveNames;

	/** All the unique curve names we can find */
	TSet<FString> UniqueCurveNames;

	/** The string we use to filter curve names */
	FString FilterText;

	/** The list view used to display names */
	TSharedPtr<SListView<TSharedPtr<FString>>> NameListView;
};