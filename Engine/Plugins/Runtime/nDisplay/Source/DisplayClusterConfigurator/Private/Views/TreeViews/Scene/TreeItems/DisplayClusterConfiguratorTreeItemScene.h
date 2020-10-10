// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/TreeViews/DisplayClusterConfiguratorTreeItem.h"

#include "Input/DragAndDrop.h"
#include "Input/Reply.h"

class IDisplayClusterConfiguratorViewTree;
enum class EItemDropZone;

class FDisplayClusterConfiguratorTreeItemScene
	: public FDisplayClusterConfiguratorTreeItem
{
public:
	NDISPLAY_TREE_ITEM_TYPE(FDisplayClusterConfiguratorTreeItemScene, FDisplayClusterConfiguratorTreeItem)

	FDisplayClusterConfiguratorTreeItemScene(const FName& InName,
		const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
		const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
		UObject* InObjectToEdit,
		FString InIconStyle,
		bool InbRoot = false);

	//~ Begin IDisplayClusterConfiguratorTreeItem Interface
	virtual FName GetRowItemName() const override { return Name; }
	virtual FString GetIconStyle() const override { return IconStyle; }
	virtual FReply HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void HandleDragEnter(const FDragDropEvent& DragDropEvent) override;
	virtual FReply HandleAcceptDrop(const FDragDropEvent& DragDropEvent, TSharedPtr<IDisplayClusterConfiguratorTreeItem> TargetItem) override;
	virtual void OnMouseEnter() override;
	virtual void OnMouseLeave() override;
	virtual bool IsHovered() const override;
	//~ End IDisplayClusterConfiguratorTreeItem Interface

private:
	FName Name;
	FString IconStyle;
};