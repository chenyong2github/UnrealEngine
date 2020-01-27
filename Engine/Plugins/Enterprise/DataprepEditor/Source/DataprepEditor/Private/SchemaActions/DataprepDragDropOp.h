// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SchemaActions/DataprepSchemaAction.h"

#include "CoreMinimal.h"
#include "Editor/GraphEditor/Private/DragNode.h"
#include "GraphEditorDragDropAction.h"
#include "Misc/Optional.h"

struct FDataprepSchemaActionContext;
class SDataprepGraphActionStepNode;
class SGraphPanel;
class UDataprepActionAsset;
class UDataprepGraphActionStepNode;

// Return true if there was a modification that require a transaction
DECLARE_DELEGATE_RetVal_OneParam( bool, FDataprepGraphOperation, const FDataprepSchemaActionContext& /** Context */ )

DECLARE_DELEGATE_TwoParams( FDataprepPreDropConfirmation, const FDataprepSchemaActionContext& /** Context */, TFunction<void ()> /** CallBack To confirm drag and drop */)

/**
 * The Dataprep drag and drop is a specialized  drag and drop that can interact with the dataprep action nodes.
 * When dropped on a dataprep action node it will do a callback on the Dataprep Graph Operation.
 * If dropped on a compatible graph, the dataprep drag and drop operation will create a new dataprep action node and execute the callback that new node
 */
class FDataprepDragDropOp : public FGraphEditorDragDropAction
{
public:
	FDataprepDragDropOp();

	DRAG_DROP_OPERATOR_TYPE(FDataprepDragDropOp, FGraphEditorDragDropAction)

	static TSharedRef<FDataprepDragDropOp> New(TSharedRef<FDataprepSchemaAction> InAction);
	static TSharedRef<FDataprepDragDropOp> New(FDataprepGraphOperation&& GraphOperation);
	static TSharedRef<FDataprepDragDropOp> New(const TSharedRef<SGraphPanel>& InGraphPanel, const TSharedRef<SDataprepGraphActionStepNode>& InDraggedNode);
	static TSharedRef<FDataprepDragDropOp> New(UDataprepActionStep* InActionStep);

	void SetHoveredDataprepActionContext(TOptional<FDataprepSchemaActionContext> Context);

	virtual FReply DroppedOnDataprepActionContext(const FDataprepSchemaActionContext& Context);

	// FGraphEditorDragDropAction Interface
	virtual void HoverTargetChanged() override;
	virtual FReply DroppedOnNode(FVector2D ScreenPosition, FVector2D GraphPosition) override;
	virtual FReply DroppedOnPanel(const TSharedRef<SWidget>& Panel, FVector2D ScreenPosition, FVector2D GraphPosition, UEdGraph& Graph) override;
	virtual void OnDragged (const class FDragDropEvent& DragDropEvent ) override;
	virtual EVisibility GetIconVisible() const;
	virtual EVisibility GetErrorIconVisible() const;
	// End of FGraphEditorDragDropAction Interface

	// Allow to add an extra step to the drag and drop before doing the dropping
	void SetPreDropConfirmation(FDataprepPreDropConfirmation && Confirmation);

	// Returns action step node targeted for the drop
	UDataprepGraphActionStepNode* GetDropTargetNode() const;

	void SetGraphPanel(const TSharedPtr<SGraphPanel>& InGraphPanel)
	{
		GraphPanelPtr = InGraphPanel;
	}

	bool IsValidDrop() { return bDropTargetValid; }

protected:
	typedef FGraphEditorDragDropAction Super;

	bool DoDropOnDataprepActionContext(const FDataprepSchemaActionContext& Context);
	void DoDropOnPanel(const TSharedRef<SWidget>& Panel, FVector2D ScreenPosition, FVector2D GraphPosition, UEdGraph& Graph);

	/** Executes drop on existing action step */
	FReply DoDropOnActionStep(FVector2D ScreenPosition, FVector2D GraphPosition);

	/** Executes drop on existing action step */
	FReply DoDropOnActionAsset(FVector2D ScreenPosition, FVector2D GraphPosition);

	virtual void HoverTargetChangedWithNodes();

	TOptional<FDataprepSchemaActionContext> HoveredDataprepActionContext;

	FDataprepPreDropConfirmation DataprepPreDropConfirmation;
	FDataprepGraphOperation DataprepGraphOperation;

private:
	FText GetMessageText();
	const FSlateBrush* GetIcon() const;

private:
	typedef TTuple<TWeakObjectPtr<UDataprepActionAsset>,int32,TWeakObjectPtr<UDataprepActionStep>> FDraggedStepEntry;

	/** Graph panel associated with the Dataprep graph editor */
	TSharedPtr<SGraphPanel> GraphPanelPtr;

	/** Array of action steps being dragged */
	TArray<TSharedRef<SDataprepGraphActionStepNode>> DraggedNodeWidgets;

	/** Array of action steps being dragged */
	TArray<FDraggedStepEntry> DraggedSteps;

	/** Offset information for the decorator widget */
	FVector2D	DecoratorAdjust;

	/** Cache last displayed text message */
	FText LastMessageText;
};
