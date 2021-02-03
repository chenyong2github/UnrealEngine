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

	//~ Begin SWidget interface
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	//~ End of SWidget interface

	//~ Begin SGraphNode interface
	virtual void UpdateGraphNode() override;
	virtual const FSlateBrush* GetShadowBrush(bool bSelected) const override;
	virtual bool CanBeSelected(const FVector2D& MousePositionInNode) const override;
	virtual bool ShouldAllowCulling() const override;
	virtual int32 GetSortDepth() const override;
	//~ End SGraphNode interface

	virtual UObject* GetEditingObject() const = 0;

	/**
	 * Apply new node size
	 *
	 * @param InLocalSize - Size in local space
	 * @param bFixedAspectRatio - Indicates the node should have a fixed aspect ratio
	 */
	virtual void SetNodeSize(const FVector2D InLocalSize, bool bFixedAspectRatio) {}

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


	/**
	 * @return true if node should be visible
	 */
	virtual bool IsNodeVisible() const;

	/**
	 * @return The depth index of the layer the node belongs to. 
	 */
	virtual int32 GetNodeLayerIndex() const { return 0; }

	/**
	 * @return The intended size of the node, taken from the backing EdGraphNode 
	 */
	virtual FVector2D GetSize() const;

	void ExecuteMouseButtonDown(const FPointerEvent& MouseEvent);

protected:
	EVisibility GetNodeVisibility() const;
	EVisibility GetSelectionVisibility() const;
	TOptional<EMouseCursor::Type> GetCursor() const;

	template<class TObjectType>
	TObjectType* GetGraphNodeChecked() const
	{
		TObjectType* CastedNode = Cast<TObjectType>(GraphNode);
		check(CastedNode);
		return CastedNode;
	}

protected:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	int32 ZIndex;
	bool bIsObjectFocused;
};
