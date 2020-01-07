// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/STreeView.h"
#include "CurveEditorTreeTraits.h"
#include "CurveEditorTypes.h"

class FCurveEditor;
class ITableRow;
class SHeaderRow;
class STableViewBase;

class CURVEEDITOR_API SCurveEditorTree : public STreeView<FCurveEditorTreeItemID>
{
public:

	SLATE_BEGIN_ARGS(SCurveEditorTree){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FCurveEditor> InCurveEditor);

private:

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:

	TSharedRef<ITableRow> GenerateRow(FCurveEditorTreeItemID ItemID, const TSharedRef<STableViewBase>& OwnerTable);

	void GetTreeItemChildren(FCurveEditorTreeItemID Parent, TArray<FCurveEditorTreeItemID>& OutChildren);

	void OnTreeSelectionChanged(FCurveEditorTreeItemID, ESelectInfo::Type);

	void SetItemExpansionRecursive(FCurveEditorTreeItemID Model, bool bInExpansionState);

	void RefreshTree();

private:

	bool bFilterWasActive;

	TArray<FCurveEditorTreeItemID> RootItems;

	/** Set of item IDs that were expanded before a filter was applied */
	TSet<FCurveEditorTreeItemID> PreFilterExpandedItems;

	TSharedPtr<FCurveEditor> CurveEditor;

	TSharedPtr<SHeaderRow> HeaderRow;
};