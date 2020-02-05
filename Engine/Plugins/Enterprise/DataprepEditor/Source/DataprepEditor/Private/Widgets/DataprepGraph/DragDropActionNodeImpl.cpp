// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/DataprepGraph/SDataprepGraphTrackNode.h"

#include "DataprepAsset.h"
#include "DataprepEditorLogCategory.h"
#include "DataprepEditorStyle.h"
#include "DataprepGraph/DataprepGraph.h"
#include "DataprepGraph/DataprepGraphActionNode.h"

#include "ScopedTransaction.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Layout/Children.h"
#include "NodeFactory.h"
#include "SGraphPanel.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/DataprepGraph/SDataprepGraphActionNode.h"
#include "Widgets/DataprepGraph/SDataprepGraphEditor.h"
#include "Widgets/DataprepWidgets.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "DataprepGraphEditor"

class FDragDropActionNodeImpl : public FDragDropActionNode
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDragDropActionNodeImpl, FDragDropActionNode)

	// FDragDropOperation interface
	virtual void OnDrop( bool bDropWasHandled, const FPointerEvent& MouseEvent ) override;
	virtual void OnDragged( const class FDragDropEvent& DragDropEvent ) override;
	virtual FCursorReply OnCursorQuery() override { return FDragDropOperation::OnCursorQuery(); }
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return FDragDropOperation::GetDefaultDecorator(); }
	virtual FVector2D GetDecoratorPosition() const override { return FDragDropOperation::GetDecoratorPosition(); }
	virtual void SetDecoratorVisibility(bool bVisible) override { FDragDropOperation::SetDecoratorVisibility(bVisible); }
	virtual bool IsExternalOperation() const override { return FDragDropOperation::IsExternalOperation(); }
	virtual bool IsWindowlessOperation() const override { return FDragDropOperation::IsWindowlessOperation(); }
	// End of FDragDropOperation interface

	TSharedPtr<SDataprepGraphTrackNode> TrackNodePtr;
	TSharedPtr<SDataprepGraphActionNode> ActionNodePtr;
};

TSharedRef<FDragDropActionNode> FDragDropActionNode::New(const TSharedRef<SDataprepGraphTrackNode>& InTrackNodePtr, const TSharedRef<SDataprepGraphActionNode>& InDraggedNode)
{
	FDragDropActionNodeImpl* OperationImpl = new FDragDropActionNodeImpl;

	OperationImpl->TrackNodePtr = InTrackNodePtr;
	OperationImpl->ActionNodePtr = InDraggedNode;

	OperationImpl->bCreateNewWindow = false;
	OperationImpl->Construct();

	InTrackNodePtr->OnStartNodeDrag(InDraggedNode);

	TSharedRef<FDragDropActionNode> Operation = MakeShareable(new FDragDropActionNode);
	Operation->Impl = TSharedPtr<FDragDropActionNode>(OperationImpl);

	return Operation;
}

void FDragDropActionNodeImpl::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	int32 NewExecutionOrder = TrackNodePtr->OnEndNodeDrag();

	if(bDropWasHandled)
	{
		if(UDataprepAsset* DataprepAsset = TrackNodePtr->GetDataprepAsset())
		{
			FModifierKeysState ModifierKeyState = FSlateApplication::Get().GetModifierKeys();
			const bool bCopyRequested = ModifierKeyState.IsControlDown() || ModifierKeyState.IsCommandDown();

			FScopedTransaction Transaction( bCopyRequested ? LOCTEXT("OnDropCopy", "Add/Insert action") : LOCTEXT("OnDropMove", "Move action") );
			bool bTransactionSuccessful = false;

			if(bCopyRequested)
			{
				if(NewExecutionOrder >= DataprepAsset->GetActionCount())
				{
					bTransactionSuccessful = DataprepAsset->AddAction(ActionNodePtr->GetDataprepAction()) != INDEX_NONE;
				}
				else
				{
					bTransactionSuccessful = DataprepAsset->InsertAction(ActionNodePtr->GetDataprepAction(), NewExecutionOrder);
				}
			}
			else if( NewExecutionOrder != ActionNodePtr->GetExecutionOrder())
			{
				bTransactionSuccessful = DataprepAsset->MoveAction(ActionNodePtr->GetExecutionOrder(), NewExecutionOrder);
			}
			else
			{
				TrackNodePtr->RefreshLayout();
			}

			if(!bTransactionSuccessful)
			{
				Transaction.Cancel();
			}
		}
	}
	else
	{
		TrackNodePtr->RefreshLayout();
	}

	FDragDropOperation::OnDrop(bDropWasHandled, MouseEvent);
}

void FDragDropActionNodeImpl::OnDragged(const FDragDropEvent& DragDropEvent)
{
	TrackNodePtr->OnNodeDragged( ActionNodePtr, DragDropEvent.GetScreenSpacePosition(), DragDropEvent.GetCursorDelta() );

	FDragDropOperation::OnDragged(DragDropEvent);
}

#undef LOCTEXT_NAMESPACE