// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterConfiguratorTreeItemCluster.h"

class SButton;
class FDisplayClusterConfiguratorClusterNodeDragDropOp;

class FDisplayClusterConfiguratorTreeItemHost
	: public FDisplayClusterConfiguratorTreeItemCluster
{
public:
	NDISPLAY_TREE_ITEM_TYPE(FDisplayClusterConfiguratorTreeItemHost, FDisplayClusterConfiguratorTreeItemCluster)

	FDisplayClusterConfiguratorTreeItemHost(const FName& InName,
		const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
		const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit,
		UObject* InObjectToEdit);

	//~ Begin FDisplayClusterConfiguratorTreeItem Interface
	virtual void Initialize() override;
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName, TSharedPtr<ITableRow> TableRow, const TAttribute<FText>& FilterText, FIsSelected InIsSelected) override;
	virtual void DeleteItem() const override;

	virtual void HandleDragEnter(const FDragDropEvent& DragDropEvent) override;
	virtual void HandleDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<IDisplayClusterConfiguratorTreeItem> TargetItem) override;
	virtual FReply HandleAcceptDrop(const FDragDropEvent& DragDropEvent, TSharedPtr<IDisplayClusterConfiguratorTreeItem> TargetItem) override;

	virtual FName GetAttachName() const override;

	virtual bool CanDuplicateItem() const override { return false; }
	virtual bool CanHideItem() const override { return true; }
	virtual void SetItemHidden(bool bIsHidden);

protected:
	virtual void FillItemColumn(TSharedPtr<SHorizontalBox> Box, const TAttribute<FText>& FilterText, FIsSelected InIsSelected) override;
	virtual void OnDisplayNameCommitted(const FText& NewText, ETextCommit::Type CommitInfo) override;
	//~ End FDisplayClusterConfiguratorTreeItem Interface

private:
	FText GetHostAddress() const;
	FSlateColor GetHostColor() const;
	FReply OnHostClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	bool CanDropClusterNodes(TSharedPtr<FDisplayClusterConfiguratorClusterNodeDragDropOp> ClusterNodeDragDropOp, FText& OutErrorMessage) const;

	const FSlateBrush* GetVisibilityButtonBrush() const;
	FReply OnVisibilityButtonClicked();

	const FSlateBrush* GetEnabledButtonBrush() const;
	FReply OnEnabledButtonClicked();

	void OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent);

private:
	TSharedPtr<SButton> VisibilityButton;
	TSharedPtr<SButton> EnabledButton;
};