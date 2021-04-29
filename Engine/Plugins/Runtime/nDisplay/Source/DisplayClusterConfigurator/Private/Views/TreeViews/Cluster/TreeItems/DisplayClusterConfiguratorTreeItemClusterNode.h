// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterConfiguratorTreeItemCluster.h"

class SButton;
class UDisplayClusterConfigurationHostDisplayData;
class FDisplayClusterConfiguratorViewportDragDropOp;

class FDisplayClusterConfiguratorTreeItemClusterNode
	: public FDisplayClusterConfiguratorTreeItemCluster
{
public:
	NDISPLAY_TREE_ITEM_TYPE(FDisplayClusterConfiguratorTreeItemClusterNode, FDisplayClusterConfiguratorTreeItemCluster)

	FDisplayClusterConfiguratorTreeItemClusterNode(const FName& InName,
		const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
		const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit,
		UObject* InObjectToEdit,
		UDisplayClusterConfigurationHostDisplayData* InHostObject);

	UDisplayClusterConfigurationHostDisplayData* GetHostDisplayData();
	void SelectHostDisplayData();

	void SetVisible(bool bIsVisible);
	void SetEnabled(bool bIsEnabled);

	//~ Begin FDisplayClusterConfiguratorTreeItem Interface
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
	virtual void FillItemColumn(TSharedPtr<SHorizontalBox> Box, const TAttribute<FText>& FilterText, FIsSelected InIsSelected) override;
	virtual void OnDisplayNameCommitted(const FText& NewText, ETextCommit::Type CommitInfo) override;
	//~ End FDisplayClusterConfiguratorTreeItem Interface

private:
	EVisibility GetMasterLabelVisibility() const;
	FSlateColor GetHostColor() const;
	FReply OnHostClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	bool CanDropViewports(TSharedPtr<FDisplayClusterConfiguratorViewportDragDropOp> ViewportDragDropOp, FText& OutErrorMessage) const;

	const FSlateBrush* GetVisibilityButtonBrush() const;
	FReply OnVisibilityButtonClicked();

	const FSlateBrush* GetEnabledButtonBrush() const;
	FReply OnEnabledButtonClicked();

private:
	TWeakObjectPtr<UDisplayClusterConfigurationHostDisplayData> HostObject;
	TSharedPtr<SButton> VisibilityButton;
	TSharedPtr<SButton> EnabledButton;
};