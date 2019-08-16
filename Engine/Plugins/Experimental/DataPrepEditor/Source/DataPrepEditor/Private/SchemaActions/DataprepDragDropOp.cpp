// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SchemaActions/DataprepDragDropOp.h"

#include "BlueprintNodes/K2Node_DataprepAction.h"
#include "DataprepActionAsset.h"
#include "DataprepSchemaActionUtils.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EditorStyleSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Templates/UnrealTypeTraits.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DataprepDragAndDrop"

FDataprepDragDropOp::FDataprepDragDropOp()
	: FGraphEditorDragDropAction()
	, HoveredDataprepActionContext()
{
	bDropTargetValid = false;
	Construct();
}

TSharedRef<FDataprepDragDropOp> FDataprepDragDropOp::New(TSharedRef<FDataprepSchemaAction> InAction)
{
	TSharedRef< FDataprepDragDropOp > DragDrop = MakeShared< FDataprepDragDropOp >();
	DragDrop->DataprepGraphOperation.BindSP( InAction, &FDataprepSchemaAction::ExecuteAction );
	return DragDrop;
}

TSharedRef<FDataprepDragDropOp> FDataprepDragDropOp::New(FDataprepGraphOperation&& DataprepGraphOperation)
{
	TSharedRef< FDataprepDragDropOp > DragDrop = MakeShared< FDataprepDragDropOp >();
	DragDrop->DataprepGraphOperation = MoveTemp( DataprepGraphOperation );
	return DragDrop;
}

void FDataprepDragDropOp::SetHoveredDataprepActionContext(TOptional<FDataprepSchemaActionContext> Context)
{
	if ( HoveredDataprepActionContext != Context )
	{
		HoveredDataprepActionContext = Context;
		HoverTargetChanged();
	}
}

FReply FDataprepDragDropOp::DroppedOnDataprepActionContext(const FDataprepSchemaActionContext& Context)
{
	if ( DataprepPreDropConfirmation.IsBound() )
	{
		TFunction<void ()> OnConfirmation( [Operation = StaticCastSharedRef<FDataprepDragDropOp>(AsShared()), Context = FDataprepSchemaActionContext(Context)] ()
			{
				Operation->DoDropOnDataprepActionContext( Context );
			} );

		DataprepPreDropConfirmation.Execute( Context, OnConfirmation );
	}
	else
	{
		DoDropOnDataprepActionContext( Context );
	}

	return FReply::Handled();
}

void FDataprepDragDropOp::HoverTargetChanged()
{
	FText DrapDropText;
	if ( HoveredDataprepActionContext )
	{
		bDropTargetValid = true;
		DrapDropText = LOCTEXT("TargetIsDataprepActionContext", "Add a Step to Dataprep Action");
	}
	else if ( UEdGraph* EdGraph = GetHoveredGraph() )
	{
		if ( const UEdGraphSchema_K2* GraphSchema_k2 = Cast<UEdGraphSchema_K2>( EdGraph->GetSchema() ) )
		{
			bDropTargetValid = true;
			DrapDropText = LOCTEXT("TargetIsBlueprintGraph", "Add a Dataprep Action");
		}
		else
		{
			bDropTargetValid = false;
			DrapDropText = LOCTEXT("TargetGraphIsInvalid", "Can only be drop on a blueprint graph");
		}
	}
	else
	{
		bDropTargetValid = false;
		DrapDropText = FText::FromString( TEXT("Can't drop here") );
	}

	const FSlateBrush* Symbol = bDropTargetValid ? FEditorStyle::GetBrush( TEXT("Graph.ConnectorFeedback.OK") ) :  FEditorStyle::GetBrush( TEXT("Graph.ConnectorFeedback.Error") );
	SetSimpleFeedbackMessage( Symbol, FLinearColor::White, DrapDropText );
}

FReply FDataprepDragDropOp::DroppedOnPanel(const TSharedRef<SWidget>& Panel, FVector2D ScreenPosition, FVector2D GraphPosition, UEdGraph& Graph)
{
	if ( bDropTargetValid )
	{
		if ( DataprepPreDropConfirmation.IsBound() )
		{
			TFunction<void ()> OnConfirmation ( [Operation = StaticCastSharedRef<FDataprepDragDropOp>( AsShared() ), Panel, ScreenPosition, GraphPosition, GraphPtr = TWeakObjectPtr<UEdGraph>(&Graph)] ()
				{
					UEdGraph* Graph = GraphPtr.Get();
					if ( Graph )
					{
						Operation->DoDropOnPanel(Panel, ScreenPosition, GraphPosition, *Graph);
					}
				} );

			DataprepPreDropConfirmation.Execute( FDataprepSchemaActionContext(), OnConfirmation);
		
			return FReply::Handled();
		}
		else
		{
			DoDropOnPanel( Panel, ScreenPosition, GraphPosition, Graph );
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void FDataprepDragDropOp::SetPreDropConfirmation(FDataprepPreDropConfirmation && Confirmation)
{
	DataprepPreDropConfirmation = MoveTemp( Confirmation );
}

bool FDataprepDragDropOp::DoDropOnDataprepActionContext(const FDataprepSchemaActionContext& Context)
{
	if ( DataprepGraphOperation.IsBound() )
	{
		FScopedTransaction Transaction( LOCTEXT("AddStep", "Add a Step to a Dataprep Action") );
		bool bDidModification = DataprepGraphOperation.Execute( Context );
		if ( !bDidModification )
		{
			Transaction.Cancel();
		}
		return bDidModification;
	}
	return false;
}

void FDataprepDragDropOp::DoDropOnPanel(const TSharedRef<SWidget>& Panel, FVector2D ScreenPosition, FVector2D GraphPosition, UEdGraph& Graph)
{
	if (UEdGraph* EdGraph = GetHoveredGraph())
	{
		FScopedTransaction Transaction( LOCTEXT("AddNode", "Add Dataprep Action Node") );
		UK2Node_DataprepAction* DataprepActionNode = DataprepSchemaActionUtils::SpawnEdGraphNode< UK2Node_DataprepAction >( Graph, GraphPosition );
		check( DataprepActionNode );
		DataprepActionNode->CreateDataprepActionAsset();
		DataprepActionNode->AutowireNewNode( GetHoveredPin() );

		FDataprepSchemaActionContext Context;
		Context.DataprepActionPtr = DataprepActionNode->GetDataprepAction();
		if ( !DoDropOnDataprepActionContext( Context ) )
		{
			Transaction.Cancel();
		}

		if ( UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraphChecked( EdGraph ) )
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified( Blueprint );
		}
	}
}
#undef LOCTEXT_NAMESPACE
