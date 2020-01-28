// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SDataprepGraphTrackNode;
class SDataprepGraphActionStepNode;
class SVerticalBox;
class UDataprepActionAsset;
class UDataprepGraphActionNode;

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
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// End of SWidget interface

	// SGraphNode interface
	virtual void MoveTo( const FVector2D& NewPosition, FNodeSet& NodeFilter ) override;
	virtual TSharedRef<SWidget> CreateNodeContentArea() override;
	virtual FReply OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override;
	// End of SGraphNode interface

	void SetParentTrackNode(TSharedPtr<SDataprepGraphTrackNode> InParentTrackNode);

	int32 GetExecutionOrder() const { return ExecutionOrder; }
	void UpdateExecutionOrder();

	const UDataprepActionAsset* GetDataprepAction() const { return DataprepActionPtr.Get(); }

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
};
