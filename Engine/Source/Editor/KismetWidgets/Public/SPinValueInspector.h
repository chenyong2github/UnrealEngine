// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class SSearchBox;

typedef TSharedPtr<struct FPinValueInspectorTreeViewNode> FPinValueInspectorTreeViewNodePtr;

/**
 * Inspects the referenced pin object's underlying property value and presents it within a tree view.
 * Compound properties (e.g. structs/containers) will be broken down into a hierarchy of child nodes.
 */
class KISMETWIDGETS_API SPinValueInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPinValueInspector)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FEdGraphPinReference& InPinRef);

	/** Whether the search filter UI should be visible. */
	bool ShouldShowSearchFilter() const;

protected:
	/** @return Visibility of the search box filter widget. */
	EVisibility GetSearchFilterVisibility() const;

	/** Obtains the set of children for the given value item node. */
	void OnGetTreeViewNodeChildren(FPinValueInspectorTreeViewNodePtr InNode, TArray<FPinValueInspectorTreeViewNodePtr>& OutChildren);

	/** Generates a row widget that presents the given value item node. */
	TSharedRef<ITableRow> OnGenerateRowForTreeViewNode(FPinValueInspectorTreeViewNodePtr InNode, const TSharedRef<STableViewBase>& OwnerTable);

private:
	/** Holds a weak reference to the target pin. */
	FEdGraphPinReference PinRef;

	/** The instance that's currently selected as the debugging target. */
	TWeakObjectPtr<UObject> TargetObject;

	/** Root node(s) presented through the tree view widget. */
	TArray<FPinValueInspectorTreeViewNodePtr> RootNodes;

	/** Presents a hierarchical display of the inspected value along with any sub-values as children. */
	TSharedPtr<STreeView<FPinValueInspectorTreeViewNodePtr>> TreeViewWidget;

	/** Holds a reference to the search box widget, used to filter the tree view display. */
	TSharedPtr<SSearchBox> SearchBoxWidget;
};