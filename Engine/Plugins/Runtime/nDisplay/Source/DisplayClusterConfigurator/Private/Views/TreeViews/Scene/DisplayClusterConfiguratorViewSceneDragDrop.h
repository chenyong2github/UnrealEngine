// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GraphEditorDragDropAction.h"

class FDisplayClusterConfiguratorBlueprintEditor;
class FDisplayClusterConfiguratorTreeItemScene;
class IDisplayClusterConfiguratorViewTree;

class FDisplayClusterConfiguratorViewSceneDragDrop
	: public FGraphEditorDragDropAction
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDisplayClusterConfiguratorViewSceneDragDrop, FGraphEditorDragDropAction)

	TSharedPtr<FDisplayClusterConfiguratorTreeItemScene> GetTreeItemScene() const { return TreeItemScenePtr.Pin(); }

	/** Constructs a new drag/drop operation */
	static TSharedRef<FDisplayClusterConfiguratorViewSceneDragDrop> New(const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit, const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree, const TSharedRef<FDisplayClusterConfiguratorTreeItemScene>& InTreeItemScene);

	void SetCanDrop(bool InCanDrop);

	bool CanDrop() const { return bCanDrop; }

private:
	TWeakPtr<FDisplayClusterConfiguratorBlueprintEditor> ToolkitPtr;

	TWeakPtr<IDisplayClusterConfiguratorViewTree> ViewTreePtr;

	TWeakPtr<FDisplayClusterConfiguratorTreeItemScene> TreeItemScenePtr;

	bool bCanDrop;
};
