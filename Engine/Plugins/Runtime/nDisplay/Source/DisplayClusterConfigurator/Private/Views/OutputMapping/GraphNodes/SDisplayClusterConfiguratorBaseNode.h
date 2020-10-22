// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FDisplayClusterConfiguratorToolkit;
class IDisplayClusterConfiguratorTreeItem;
class SBox;
class UDisplayClusterConfiguratorBaseNode;
class UTexture;
struct FSlateColor;

class SDisplayClusterConfiguratorBaseNode
	: public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorBaseNode)
		{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UDisplayClusterConfiguratorBaseNode* InBaseNode, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	virtual UObject* GetEditingObject() const = 0;

	/**
	 * Apply new node postion offset
	 *
	 * @param InLocalOffset				Offset in local space
	 *
	 */
	virtual void SetNodePositionOffset(const FVector2D InLocalOffset) {}

	/**
	 * Apply new node size
	 *
	 * @param InLocalSize				Size in local space
	 *
	 */
	virtual void SetNodeSize(const FVector2D InLocalSize) {}

	/**
	 * Selected Item handler function. Fires when the item has been selected in the tree view
	 *
	 * @param InTreeItem				Selected Tree Item
	 *
	 */
	virtual void OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) {}

	/**
	 * Selected Item Cleared handler function. Fires when the item has been deselected in the tree view
	 */
	virtual void OnSelectedItemCleared();

	/** Sets the default background brush for node */
	virtual void SetBackgroundDefaultBrush() {};

	/**
	 * Sets the background brush from texture
	 *
	 * @param InTexture					Texture input
	 *
	 */
	virtual void SetBackgroundBrushFromTexture(UTexture* InTexture) {};

	//~ Begin SWidget interface
	virtual bool SupportsKeyboardFocus() const override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	//~ End of SWidget interface

	//~ Begin SGraphNode interface
	virtual void UpdateGraphNode() override;
	virtual const FSlateBrush* GetShadowBrush(bool bSelected) const override;
	virtual bool ShouldAllowCulling() const override;
	//~ End SGraphNode interface

	/**
	 * On Dragged node handler
	 *
	 * @param DragScreenSpacePosition			Drag mouse screen position
	 * @param ScreenSpaceDelta					Drag mouse delta
	 *
	 * @return true if drag event inside the panel
	 */
	bool OnNodeDragged(const FVector2D& DragScreenSpacePosition, const FVector2D& ScreenSpaceDelta);

	void ExecuteMouseButtonDown(const FPointerEvent& MouseEvent);

	TSharedRef<SWidget> CreateBackground(const TAttribute<FSlateColor>& ColorAndOpacity);

	EVisibility GetSelectionVisibility() const;
	
	/**
	 * @return true if node should be visible
	 */
	bool IsNodeVisible() const;

	EVisibility GetNodeVisibility() const;

protected:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	SNodePanel::SNode::FNodeSlot* NodeSlot;

	TSharedPtr<SBox> NodeSlotBox;

	bool InNodeVisibile;
};
