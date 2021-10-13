// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "Widgets/SCompoundWidget.h"
#include "Debugging/SKismetDebugTreeView.h"

class SSearchBox;

typedef TSharedPtr<struct FPinValueInspectorTreeViewNode> FPinValueInspectorTreeViewNodePtr;

/**
 * Inspects the referenced pin object's underlying property value and presents it within a tree view.
 * Compound properties (e.g. structs/containers) will be broken down into a hierarchy of child nodes.
 */
class KISMET_API SPinValueInspector : public SCompoundWidget
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

	/** Passes SearchText through to tree view */
	void OnSearchTextChanged(const FText& InSearchText);

	/** requests the constrained box be resized */
	void OnExpansionChanged(FDebugTreeItemPtr InItem, bool bItemIsExpanded);

	/** Adds the pin to the tree view */
	void PopulateTreeView();

private:
	/** Holds a weak reference to the target pin. */
	FEdGraphPinReference PinRef;

	/** The instance that's currently selected as the debugging target. */
	TWeakObjectPtr<UObject> TargetObject;

	/** Presents a hierarchical display of the inspected value along with any sub-values as children. */
	TSharedPtr<SKismetDebugTreeView> TreeViewWidget;

	/** The box that handles resizing of the Tree View */
	TSharedPtr<class SPinValueInspector_ConstrainedBox> ConstrainedBox;
};