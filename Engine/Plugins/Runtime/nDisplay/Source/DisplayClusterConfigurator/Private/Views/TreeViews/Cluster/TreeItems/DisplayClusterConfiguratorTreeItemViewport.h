// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterConfiguratorTreeItemCluster.h"

class SButton;

class FDisplayClusterConfiguratorTreeItemViewport
	: public FDisplayClusterConfiguratorTreeItemCluster
{
public:
	NDISPLAY_TREE_ITEM_TYPE(FDisplayClusterConfiguratorTreeItemViewport, FDisplayClusterConfiguratorTreeItemCluster)

		FDisplayClusterConfiguratorTreeItemViewport(const FName& InName,
			const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
			const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit,
			UObject* InObjectToEdit);

	void SetVisible(bool bIsVisible);
	void SetEnabled(bool bIsEnabled);

	//~ Begin FDisplayClusterConfiguratorTreeItem Interface
	virtual void OnSelection() override;

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName, TSharedPtr<ITableRow> TableRow, const TAttribute<FText>& FilterText, FIsSelected InIsSelected) override;
	virtual void DeleteItem() const override;
	virtual bool CanHideItem() const override { return true; }
	virtual void SetItemHidden(bool bIsHidden) { SetVisible(!bIsHidden); }

	virtual FReply HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void HandleDragEnter(const FDragDropEvent& DragDropEvent) override;
	virtual void HandleDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<IDisplayClusterConfiguratorTreeItem> TargetItem) override;
	virtual FReply HandleAcceptDrop(const FDragDropEvent& DragDropEvent, TSharedPtr<IDisplayClusterConfiguratorTreeItem> TargetItem) override;

protected:
	virtual void OnDisplayNameCommitted(const FText& NewText, ETextCommit::Type CommitInfo) override;
	//~ End FDisplayClusterConfiguratorTreeItem Interface

private:
	FSlateColor GetHostColor() const;
	FReply OnHostClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	const FSlateBrush* GetVisibilityButtonBrush() const;
	FReply OnVisibilityButtonClicked();

	const FSlateBrush* GetEnabledButtonBrush() const;
	FReply OnEnabledButtonClicked();

private:
	TSharedPtr<SButton> VisibilityButton;
	TSharedPtr<SButton> EnabledButton;
};