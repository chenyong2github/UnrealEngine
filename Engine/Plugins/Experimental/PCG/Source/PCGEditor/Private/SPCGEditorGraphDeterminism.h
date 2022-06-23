// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tests/Determinism/PCGDeterminismTestsCommon.h"
#include "Widgets/Views/SListView.h"

class FPCGEditor;

typedef TSharedPtr<PCGDeterminismTests::FPCGDeterminismResult> FPCGDeterminismResultPtr;

class SPCGEditorGraphDeterminismRow : public SMultiColumnTableRow<FPCGDeterminismResultPtr>
{
	SLATE_BEGIN_ARGS(SPCGEditorGraphDeterminismRow) {}
	SLATE_END_ARGS()

	/** Construct a row of the ListView */
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const FPCGDeterminismResultPtr& Item);

	/** Generates a column, given the column's ID */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnId) override;

protected:
	FPCGDeterminismResultPtr CurrentItem;
};

class SPCGEditorGraphDeterminismListView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGEditorGraphDeterminismListView) { }
	SLATE_END_ARGS()

	/** Construct the ListView */
	void Construct(const FArguments& InArgs, TWeakPtr<FPCGEditor> InPCGEditor);
	/** Add an item to the ListView */
	void AddItem(const FPCGDeterminismResultPtr Item);
	/** Clear all items from the ListView */
	void Clear();
	/** Validates if the ListView has been constructed */
	bool IsContructed() const;

private:
	TSharedRef<ITableRow> OnGenerateRow(const FPCGDeterminismResultPtr Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Weak pointer to the PCGEditor */
	TWeakPtr<FPCGEditor> PCGEditorPtr;

	TSharedPtr<SListView<FPCGDeterminismResultPtr>> ListView;
	TArray<FPCGDeterminismResultPtr> ListViewItems;

	bool bIsConstructed = false;
};