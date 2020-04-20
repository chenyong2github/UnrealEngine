// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ChildActorComponent.h"

class UToolMenu;
class SSCSEditor;
class FSCSEditorTreeNode;

class UNREALED_API FChildActorComponentEditorUtils
{
public:
	/** Returns true if the given SCS editor tree node is a child actor node */
	static bool IsChildActorNode(TSharedPtr<const FSCSEditorTreeNode> InNodePtr);

	/** Returns true if the given SCS editor tree node belongs to a child actor template */
	static bool IsChildActorSubtreeNode(TSharedPtr<const FSCSEditorTreeNode> InNodePtr);

	/** Returns true if any array element belongs to a child actor template */
	static bool ContainsChildActorSubtreeNode(const TArray<TSharedPtr<FSCSEditorTreeNode>>& InNodePtrs);

	/** Returns the child actor component root node of a child actor subtree */
	static TSharedPtr<FSCSEditorTreeNode> GetOuterChildActorComponentNode(TSharedPtr<const FSCSEditorTreeNode> InNodePtr);

	/** Returns whether child actor tree view expansion is enabled in project settings */
	static bool IsChildActorTreeViewExpansionEnabled();

	/** Returns the default visualization mode for child actors in a component tree view */
	static EChildActorComponentTreeViewVisualizationMode GetProjectDefaultTreeViewVisualizationMode();

	/** Modify editor state to toggle visibility of the given component in a tree view */
	static void ToggleComponentNodeVisibility(UChildActorComponent* ChildActorComponent, TWeakPtr<SSCSEditor> WeakEditorPtr);

	/** Modify editor state to toggle Child Actor node visibility for the given component */
	static void ToggleChildActorNodeVisibility(UChildActorComponent* ChildActorComponent, TWeakPtr<SSCSEditor> WeakEditorPtr);

	/** Whether the given component should be visible as a unique node in a component tree view */
	static bool ShouldShowComponentNodeInTreeView(UChildActorComponent* ChildActorComponent);

	/** Whether the Child Actor should be shown in a component tree view for the given component */
	static bool ShouldShowChildActorNodeInTreeView(UChildActorComponent* ChildActorComponent);

	/** Populates the given menu with options for the given Child Actor component */
	static void FillComponentContextMenuOptions(UToolMenu* Menu, UChildActorComponent* ChildActorComponent);

	/** Populates the given menu with additional options if the given SCS editor tree node represents a Child Actor node */
	static void FillChildActorContextMenuOptions(UToolMenu* Menu, TSharedPtr<const FSCSEditorTreeNode> InNodePtr);
};