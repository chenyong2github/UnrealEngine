// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "DataTableEditorUtils.h"

class FDataTableEditor;
class SInlineEditableTextBlock;
/**
 * A widget to represent a row in a Data Table Editor widget. This widget allows us to do things like right-click
 * and take actions on a particular row of a Data Table.
 */
class SDataTableListViewRow : public SMultiColumnTableRow<FDataTableEditorRowListViewDataPtr>
{
public:

	SLATE_BEGIN_ARGS(SDataTableListViewRow) {}
	/** The owning object. This allows us access to the actual data table being edited as well as some other API functions. */
	SLATE_ARGUMENT(TSharedPtr<FDataTableEditor>, DataTableEditor)
		/** The row we're working with to allow us to get naming information. */
		SLATE_ARGUMENT(FDataTableEditorRowListViewDataPtr, RowDataPtr)
		SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView);

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	void OnRowRenamed(const FText& Text, ETextCommit::Type CommitType);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

	FText GetCurrentNameAsText() const;
	FName GetCurrentName() const;

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent);

private:

	void OnSearchForReferences();
	TSharedRef<SWidget> MakeCellWidget(const int32 InRowIndex, const FName& InColumnId);

	TSharedPtr<SInlineEditableTextBlock> InlineEditableText;

	TSharedPtr<FName> CurrentName;

	FDataTableEditorRowListViewDataPtr RowDataPtr;
	TWeakPtr<FDataTableEditor> DataTableEditor;
};