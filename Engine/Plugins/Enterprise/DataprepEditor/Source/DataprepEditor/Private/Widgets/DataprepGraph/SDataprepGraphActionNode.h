// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SDataprepGraphTrackNode;
class SDataprepGraphActionProxyNode;
class SDataprepGraphActionStepNode;
class SVerticalBox;
class UDataprepActionAsset;
class UDataprepGraphActionNode;
class UDataprepGraphActionStepNode;

/**
 * The SDataprepGraphActionNode class is the SGraphNode associated
 * to an UDataprepGraphActionNode to display the action's steps in a SDataprepGraphEditor.
 */
class SDataprepGraphActionNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SDataprepGraphActionNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UDataprepGraphActionNode* InActionNode);

	// SWidget interface
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	int32 OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override;
	// End of SWidget interface

	// SGraphNode interface
	virtual TSharedRef<SWidget> CreateNodeContentArea() override;
	virtual FReply OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override;
	virtual const FSlateBrush* GetShadowBrush(bool bSelected) const override;
	// End of SGraphNode interface

	void SetParentTrackNode(TSharedPtr<SDataprepGraphTrackNode> InParentTrackNode);

	int32 GetExecutionOrder() const { return ExecutionOrder; }
	void UpdateExecutionOrder();

	const UDataprepActionAsset* GetDataprepAction() const { return DataprepActionPtr.Get(); }

	/** Update the proxy node with relative position in track node */
	void UpdateProxyNode(const FVector2D& Position);

	/** Callback used by insert nodes to determine their background color */
	FSlateColor GetInsertColor(int32 Index);

	/** Set index of step node being dragged */
	void SetDraggedIndex(int32 Index);

	/** Set index of step node being hovered */
	void SetHoveredIndex(int32 Index);

private:
	/** Callback to handle changes on array of steps in action */
	void OnStepsChanged();

	/** Reconstructs the list of widgets associated with the action's steps */
	void PopulateActionStepListWidget();

private:
	/** Weak pointer to the associated action asset */
	TWeakObjectPtr<class UDataprepActionAsset> DataprepActionPtr;

	/** Order in which the associated action will be executed by the Dataprep asset */
	int32 ExecutionOrder;

	/** Pointer to the SDataprepGraphTrackNode displayed in the graph editor  */
	TWeakPtr<SDataprepGraphTrackNode> ParentTrackNodePtr;

	/** Pointer to widget containing all the SDataprepGraphActionStepNode representing the associated action's steps */
	TSharedPtr<SVerticalBox> ActionStepListWidgetPtr;

	/** Array of pointers to the SDataprepGraphActionStepNode representing the associated action's steps */
	TArray<TSharedPtr<SDataprepGraphActionStepNode>> ActionStepGraphNodes;

	/** Pointer to the proxy SGraphNode inserted in the graph panel */
	TSharedPtr<SDataprepGraphActionProxyNode> ProxyNodePtr;

	/** Index of step node being dragged */
	int32 DraggedIndex;

	/** Index of insert widget to be highlighted */
	int32 InsertIndex;

	/** Array of strong pointers to the UEdGraphNodes created for the action's steps */
	TArray<TStrongObjectPtr<UDataprepGraphActionStepNode>> EdGraphStepNodes;
};
