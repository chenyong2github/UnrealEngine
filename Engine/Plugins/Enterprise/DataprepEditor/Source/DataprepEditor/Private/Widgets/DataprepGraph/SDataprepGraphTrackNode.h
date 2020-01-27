// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataprepGraph/DataprepGraph.h"

#include "DataprepAsset.h"

#include "Editor/GraphEditor/Private/DragNode.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "SGraphNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SWidget.h"

class SDataprepGraphActionNode;
class SDataprepGraphEditor;
class SDataprepGraphTrackNode;
class SDataprepGraphTrackWidget;
class UDataprepAsset;
class UDataprepGraphActionK2Node;
class UDataprepGraphActionNode;
class UEdGraph;

/**
 * The SDataprepGraphTrackNode class is a specialization of SGraphNode
 * to handle the actions of a Dataprep asset
 */
class SDataprepGraphTrackNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SDataprepGraphTrackNode){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UDataprepGraphRecipeNode* InNode);

	// SNodePanel::SNode interface
	virtual bool CanBeSelected(const FVector2D& /*MousePositionInNode*/) const override
	{
		return false;
	}
	virtual void SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel) override;
	virtual void MoveTo( const FVector2D& NewPosition, FNodeSet& NodeFilter ) override;
	// End of SNodePanel::SNode interface

	float GetInterNodeSpacing() const { return InterNodeSpacing; }

	/** Recompute the boundaries of the graph based on the new size and the new zoom factor */
	FVector2D Update(const FVector2D& LocalSize, float ZoomAmount);

	/**
	 * Computes a new position for an action node based on the dimension of the graph
	 * Mainly keeps the node at the right height
	 */
	FVector2D ComputeActionNodePosition(const FVector2D& InPosition);

	/** Initiates the horizontal drag of an action node */
	void OnStartNodeDrag(const TSharedRef<SDataprepGraphActionNode>& ActionNode);

	/** Reorganizes the array of action nodes based after a drag is completed */
	void OnNodeDropped(bool bDropWasHandled);

	/** Updates the position of other action nodes based on the position of the incoming node */
	void OnNodeDragged( TSharedPtr<SDataprepGraphActionNode>& ActionNodePtr, const FVector2D& DragScreenSpacePosition, const FVector2D& ScreenSpaceDelta);

	/** Recomputes the position of each action node */
	void ReArrangeActionNodes();

	/** Miscellaneous values used in the display */
	// #ueent_wip: Will be moved to the Dataprep editor's style
	static float NodeDesiredWidth;
	static float NodeDesiredSpacing;
	static FMargin NodePadding;
	static float TrackDesiredHeight;

	// SGraphNode interface
	virtual void UpdateGraphNode() override;
	// End of SGraphNode interface

private:
	/** Updates the graph editor's canvas after a drag  */
	void UpdatePanelOnDrag(const FVector2D& DragScreenSpacePosition, const FVector2D& ScreenSpaceDelta);

	FVector2D GetInnerBlockSize() const
	{
		return InnerBlockSize;
	}

	FVector2D GetLeftBlockSize() const
	{
		return LeftBlockSize;
	}

	FVector2D GetRightBlockSize() const
	{
		return RightBlockSize;
	}

private:
	/** Pointer to the widget displaying the track */
	TSharedPtr<SDataprepGraphTrackWidget> TrackWidgetPtr;

	/** Array of action node's widgets */
	mutable TArray<TSharedPtr<SDataprepGraphActionNode>> ActionNodes;

	/** Weak pointer to the Dataprep asset holding the displayed actions */
	TWeakObjectPtr<UDataprepAsset> DataprepAssetPtr;

	/** Size of the section of the track encompassing the width of the action node's widgets */
	FVector2D InnerBlockSize;

	/** Size of the section of the track for right padding */
	FVector2D RightBlockSize;

	/** Size of the section of the track for left padding */
	FVector2D LeftBlockSize;

	/** Spacing between action nodes. The value varies according to the zoom factor */
	float InterNodeSpacing;

	/** Minimum of position on the X axis */
	float NodeAbscissaMin;

	/** Maximum of position on the X axis */
	float NodeAbscissaMax;

	/** Indicates a drag is happening */
	bool bNodeDragging;

	/**
	 * Indicates to skip the next mouse position update as it has been triggered
	 * by a call to FSlateApplication::SetCursorPos
	 */
	bool bSkipNextDragUpdate;

	/** Cached of the last position of the cursor as the drag is happening */
	FVector2D LastDragScreenSpacePosition;

	/** Cached ordinate of the cursor when the drag started */
	float DragOrdinate;

	/** Execution order of the dragged node when the drag started */
	int32 OriginalOrder;

	/** Execution order of the dragged node as the drag is happening */
	int32 CurrentOrder;

	/** Array tracking the new execution order of actions while a drag is happening */
	TArray<int32> NewActionsOrder;

	friend SDataprepGraphTrackWidget;
};

class FDragGraphActionNode : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDragGraphActionNode, FDragDropOperation)

	static TSharedRef<FDragGraphActionNode> New(const TSharedRef<SDataprepGraphTrackNode>& InTrackNodePtr, const TSharedRef<SDataprepGraphActionNode>& InDraggedNode);
	static TSharedRef<FDragGraphActionNode> New(const TSharedRef<SDataprepGraphTrackNode>& InTrackNodePtr, const TArray< TSharedRef<SDataprepGraphActionNode> >& InDraggedNodes);

	// FDragDropOperation interface
	virtual void OnDrop( bool bDropWasHandled, const FPointerEvent& MouseEvent );
	virtual void OnDragged( const class FDragDropEvent& DragDropEvent );
	// End of FDragDropOperation interface

protected:
	TSharedPtr<SDataprepGraphTrackNode> TrackNodePtr;
	TSharedPtr<SDataprepGraphActionNode> ActionNodePtr;
};
