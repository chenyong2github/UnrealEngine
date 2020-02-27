// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/DataprepGraph/SDataprepGraphActionStepNode.h"

// Dataprep includes
#include "DataprepActionAsset.h"
#include "DataprepCoreUtils.h"
#include "DataprepEditorLogCategory.h"
#include "DataprepEditorStyle.h"
#include "DataprepGraph/DataprepGraphActionNode.h"
#include "DataprepOperation.h"
#include "SchemaActions/DataprepDragDropOp.h"
#include "SchemaActions/DataprepSchemaAction.h"
#include "SelectionSystem/DataprepFilter.h"
#include "SelectionSystem/DataprepSelectionTransform.h"
#include "Widgets/DataprepGraph/SDataprepActionSteps.h"
#include "Widgets/DataprepGraph/SDataprepFilter.h"
#include "Widgets/DataprepGraph/SDataprepGraphActionNode.h"
#include "Widgets/DataprepGraph/SDataprepSelectionTransform.h"
#include "Widgets/DataprepGraph/SDataprepGraphTrackNode.h"
#include "Widgets/DataprepGraph/SDataprepOperation.h"

// Engine Includes
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "SGraphPanel.h"
#include "SLevelOfDetailBranchNode.h"
#include "SPinTypeSelector.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DataprepGraphEditor"

void SDataprepGraphActionStepNode::Construct(const FArguments& InArgs, UDataprepGraphActionStepNode* InActionStepNode, const TSharedPtr<SDataprepGraphActionNode>& InParent)
{
	StepIndex = InActionStepNode->GetStepIndex();

	ParentNodePtr = InParent;
	GraphNode = InActionStepNode;

	SetCursor(EMouseCursor::ResizeUpDown);
	UpdateGraphNode();
}

void SDataprepGraphActionStepNode::SetParentTrackNode(TSharedPtr<SDataprepGraphTrackNode> InParentTrackNode)
{
	ParentTrackNodePtr = InParentTrackNode;
}

TSharedPtr<SWidget> SDataprepGraphActionStepNode::GetStepTitleWidget() const
{
	if(SDataprepActionBlock* ActionStepBlock = ActionStepBlockPtr.Get())
	{
		return SNew(SBorder)
		.BorderImage( FEditorStyle::GetBrush( "Graph.StateNode.Body" ) )
		.Padding(0.0f)
		.BorderBackgroundColor( FDataprepEditorStyle::GetColor("DataprepActionStep.DragAndDrop") )
		[
			SNew(SBox)
			.Padding( FMargin( 2.0f ) )
			.Content()
			[
				ActionStepBlock->GetBlockTitleWidget()
			]
		];
	}

	return TSharedPtr<SWidget>();
}

void SDataprepGraphActionStepNode::UpdateGraphNode()
{
	// Reset SGraphNode members.
	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	TSharedRef<SWidget> ActionBlock = SNullWidget::NullWidget;

	if(UDataprepGraphActionStepNode* ActionStepNode = Cast<UDataprepGraphActionStepNode>(GraphNode))
	{
		TSharedRef< FDataprepSchemaActionContext > StepData = MakeShared< FDataprepSchemaActionContext >();
		StepData->DataprepActionPtr = ActionStepNode->GetDataprepActionAsset();
		StepData->DataprepActionStepPtr =  ActionStepNode->GetDataprepActionStep();
		StepData->StepIndex = ActionStepNode->GetStepIndex();

		if ( UDataprepActionStep* ActionStep = StepData->DataprepActionStepPtr.Get())
		{
			UDataprepParameterizableObject* StepObject = ActionStep->GetStepObject();
			UClass* StepType = FDataprepCoreUtils::GetTypeOfActionStep(StepObject);
			if (StepType == UDataprepOperation::StaticClass())
			{
				UDataprepOperation* Operation = static_cast<UDataprepOperation*>( StepObject );
				ActionStepBlockPtr = StaticCastSharedRef<SDataprepActionBlock>( SNew(SDataprepOperation, Operation, StepData) );
			}
			else if (StepType == UDataprepFilter::StaticClass())
			{
				UDataprepFilter* Filter = static_cast<UDataprepFilter*>( StepObject );
				ActionStepBlockPtr = StaticCastSharedRef<SDataprepActionBlock>( SNew(SDataprepFilter, *Filter, StepData) );
			}
			else if (StepType == UDataprepSelectionTransform::StaticClass())
			{
				UDataprepSelectionTransform* SelectionTransform = static_cast<UDataprepSelectionTransform*>( StepObject );
				ActionStepBlockPtr = StaticCastSharedRef<SDataprepActionBlock>( SNew(SDataprepSelectionTransform, SelectionTransform, StepData) );
			}

			if(ActionStepBlockPtr.IsValid())
			{
#ifndef NO_BLUEPRINT
				ActionStepBlockPtr->bIsSimplifiedGraph = true;
#endif
				ActionBlock = ActionStepBlockPtr->AsShared();
			}
		}
	}

	this->ContentScale.Bind( this, &SGraphNode::GetContentScale );
	this->GetOrAddSlot( ENodeZone::Center )
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.f, 0.f, 10.f, 0.f)
		[
			SNew(SBorder)
			.BorderBackgroundColor( this, &SDataprepGraphActionStepNode::GetDragAndDropColor )
			.BorderImage(FEditorStyle::GetBrush("Graph.StateNode.Body"))
			[
				SNew(SBox)
				.HeightOverride(2.0)
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage( FEditorStyle::GetBrush( "Graph.StateNode.Body" ) )
			.Padding(2.0f)
			.BorderBackgroundColor( this, &SDataprepGraphActionStepNode::GetBorderBackgroundColor )
			[
				SNew(SBox)
				.Padding( FMargin( 1.0f ) )
				.Content()
				[
					ActionBlock
				]
			]
		]
	];
}

FSlateColor SDataprepGraphActionStepNode::GetDragAndDropColor() const
{
	return ParentNodePtr.Pin()->GetInsertColor(StepIndex);
}

FSlateColor SDataprepGraphActionStepNode::GetBorderBackgroundColor() const
{
	static const FLinearColor Selected = FDataprepEditorStyle::GetColor( "DataprepActionStep.DragAndDrop" );
	static const FLinearColor BackgroundColor = FDataprepEditorStyle::GetColor("DataprepActionStep.BackgroundColor");

	const bool bIsSelected = GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode);

	return bIsSelected ? Selected : BackgroundColor;
}

FReply SDataprepGraphActionStepNode::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	GetOwnerPanel()->SelectionManager.ClickedOnNode(GraphNode, MouseEvent);

	if ( MouseEvent.GetEffectingButton() ==  EKeys::LeftMouseButton )
	{
		return FReply::Handled().DetectDrag( AsShared(), EKeys::LeftMouseButton );
	}

	return SGraphNode::OnMouseButtonDown( MyGeometry, MouseEvent);
}

FReply SDataprepGraphActionStepNode::OnMouseMove(const FGeometry& SenderGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Unhandled();
}

FReply SDataprepGraphActionStepNode::OnDragDetected(const FGeometry & MyGeometry, const FPointerEvent & MouseEvent)
{
	if (UDataprepGraphActionStepNode* ActionStepNode = Cast<UDataprepGraphActionStepNode>(GraphNode))
	{
		if (UDataprepActionStep* ActionStep = ActionStepNode->GetDataprepActionStep())
		{
			ParentNodePtr.Pin()->SetDraggedIndex(StepIndex);
			return FReply::Handled().BeginDragDrop(FDataprepDragDropOp::New( GetOwnerPanel().ToSharedRef(), SharedThis(this)));
		}
	}

	return FReply::Unhandled();
}

void SDataprepGraphActionStepNode::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Is someone dragging a node?
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (DragNodeOp.IsValid())
	{
		ParentTrackNodePtr.Pin()->OnDragLeave(DragDropEvent);

		// Inform the Drag and Drop operation that we are hovering over this node.
		DragNodeOp->SetHoveredNode(GraphNode);
		ParentNodePtr.Pin()->SetHoveredIndex(StepIndex);

		return;
	}

	SGraphNode::OnDragEnter(MyGeometry, DragDropEvent);
}

FReply SDataprepGraphActionStepNode::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Is someone dragging a node?
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (DragNodeOp.IsValid())
	{
		// Inform the Drag and Drop operation that we are hovering over this node.
		DragNodeOp->SetHoveredNode(GraphNode);
		ParentNodePtr.Pin()->SetHoveredIndex(StepIndex);

		return FReply::Handled();
	}

	return SGraphNode::OnDragOver(MyGeometry, DragDropEvent);
}

void SDataprepGraphActionStepNode::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (DragNodeOp.IsValid())
	{
		// Inform the Drag and Drop operation that we are not this widget anymore
		DragNodeOp->SetHoveredNode(nullptr);
		ParentNodePtr.Pin()->SetHoveredIndex(INDEX_NONE);

		return;
	}

	SGraphNode::OnDragLeave(DragDropEvent);
}

FReply SDataprepGraphActionStepNode::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	ParentNodePtr.Pin()->SetDraggedIndex(INDEX_NONE);

	// Process OnDrop if done by FDataprepDragDropOp
	TSharedPtr<FDataprepDragDropOp> DragActionStepNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (DragActionStepNodeOp.IsValid() && !GetOwnerPanel()->SelectionManager.IsNodeSelected(GraphNode))
	{
		const FVector2D NodeAddPosition = NodeCoordToGraphCoord( MyGeometry.AbsoluteToLocal( DragDropEvent.GetScreenSpacePosition() ) );
		return DragActionStepNodeOp->DroppedOnNode(DragDropEvent.GetScreenSpacePosition(), NodeAddPosition);
	}

	return SGraphNode::OnDrop(MyGeometry, DragDropEvent);
}

#undef LOCTEXT_NAMESPACE