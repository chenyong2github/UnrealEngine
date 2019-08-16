// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SchemaActions/DataprepSchemaAction.h"

#include "CoreMinimal.h"
#include "GraphEditorDragDropAction.h"
#include "Misc/Optional.h"

struct FDataprepSchemaActionContext;

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

	void SetHoveredDataprepActionContext(TOptional<FDataprepSchemaActionContext> Context);

	virtual FReply DroppedOnDataprepActionContext(const FDataprepSchemaActionContext& Context);

	// FGraphEditorDragDropAction Interface
	virtual void HoverTargetChanged() override;
	virtual FReply DroppedOnPanel(const TSharedRef<SWidget>& Panel, FVector2D ScreenPosition, FVector2D GraphPosition, UEdGraph& Graph) override;
	// End of FGraphEditorDragDropAction Interface

	// Allow to add an extra step to the drag and drop before doing the dropping
	void SetPreDropConfirmation(FDataprepPreDropConfirmation && Confirmation);

protected:
	bool DoDropOnDataprepActionContext(const FDataprepSchemaActionContext& Context);
	void DoDropOnPanel(const TSharedRef<SWidget>& Panel, FVector2D ScreenPosition, FVector2D GraphPosition, UEdGraph& Graph);

	TOptional<FDataprepSchemaActionContext> HoveredDataprepActionContext;

	FDataprepPreDropConfirmation DataprepPreDropConfirmation;
	FDataprepGraphOperation DataprepGraphOperation;
};
