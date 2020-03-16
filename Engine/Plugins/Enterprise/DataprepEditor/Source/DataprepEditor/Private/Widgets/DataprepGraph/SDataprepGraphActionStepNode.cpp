// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/DataprepGraph/SDataprepGraphActionStepNode.h"

// Dataprep includes
#include "DataprepActionAsset.h"
#include "DataprepCoreUtils.h"
#include "DataprepEditor.h"
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
#include "Widgets/DataprepGraph/SDataprepGraphTrackNode.h"
#include "Widgets/DataprepGraph/SDataprepOperation.h"
#include "Widgets/DataprepGraph/SDataprepSelectionTransform.h"

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
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "DataprepGraphEditor"

void SDataprepGraphActionStepNode::Construct(const FArguments& InArgs, UDataprepGraphActionStepNode* InActionStepNode, const TSharedPtr<SDataprepGraphActionNode>& InParent)
{
	StepIndex = InActionStepNode->GetStepIndex();

	ParentNodePtr = InParent;
	GraphNode = InActionStepNode;
	DataprepEditor = InArgs._DataprepEditor;

	SetCursor(EMouseCursor::CardinalCross);
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
		TAttribute<FSlateColor> BlockColorAndOpacity = TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SDataprepGraphActionStepNode::GetBlockOverlayColor));

		return SNew(SOverlay)
			+ SOverlay::Slot()
			.Padding(FDataprepEditorStyle::GetMargin( "DataprepActionStep.DnD.Outter.Padding" ))
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SDataprepGraphActionNode::CreateBackground(BlockColorAndOpacity)
			]

			+ SOverlay::Slot()
			.Padding(FDataprepEditorStyle::GetMargin( "DataprepActionStep.DnD.Inner.Padding" ))
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SDataprepGraphActionNode::CreateBackground(FDataprepEditorStyle::GetColor( "DataprepActionStep.BackgroundColor" ))
			]

			+ SOverlay::Slot()
			.Padding(FDataprepEditorStyle::GetMargin( "DataprepActionStep.DnD.Inner.Padding" ))
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(5.f, 10.f)
				.VAlign(VAlign_Center)
				[
					ActionStepBlock->GetTitleWidget()
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

	TSharedRef<SWidget> ActionBlockPtr = SNullWidget::NullWidget;
	if(UDataprepGraphActionStepNode* ActionStepNode = Cast<UDataprepGraphActionStepNode>(GraphNode))
	{
		TSharedRef< FDataprepSchemaActionContext > StepData = MakeShared< FDataprepSchemaActionContext >();
		StepData->DataprepActionPtr = ActionStepNode->GetDataprepActionAsset();
		StepData->DataprepActionStepPtr =  ActionStepNode->GetDataprepActionStep();
		StepData->StepIndex = ActionStepNode->GetStepIndex();

		if ( UDataprepActionStep* ActionStep = StepData->DataprepActionStepPtr.Get())
		{
			UDataprepParameterizableObject* StepObject = ActionStep->GetStepObject();

			bool bIsPreviewed = false;
			if (TSharedPtr<FDataprepEditor> DataprepEditorPtr = DataprepEditor.Pin())
			{
				bIsPreviewed = DataprepEditorPtr->IsPreviewingStep( StepObject );
			}

			UClass* StepType = FDataprepCoreUtils::GetTypeOfActionStep( StepObject );
			if (StepType == UDataprepOperation::StaticClass())
			{
				UDataprepOperation* Operation = static_cast<UDataprepOperation*>( StepObject );
				ActionStepBlockPtr = StaticCastSharedRef<SDataprepActionBlock>( SNew(SDataprepOperation, Operation, StepData) );
			}
			else if (StepType == UDataprepFilter::StaticClass())
			{
				UDataprepFilter* Filter = static_cast<UDataprepFilter*>( StepObject );

				ActionStepBlockPtr = StaticCastSharedRef<SDataprepActionBlock>( 
					SNew( SDataprepFilter, *Filter, StepData )
						.IsPreviewed( bIsPreviewed )
					);
			}
			else if (StepType == UDataprepSelectionTransform::StaticClass())
			{
				UDataprepSelectionTransform* SelectionTransform = static_cast<UDataprepSelectionTransform*>( StepObject );
				ActionStepBlockPtr = StaticCastSharedRef<SDataprepActionBlock>( SNew(SDataprepSelectionTransform, SelectionTransform, StepData) );
			}

			if(ActionStepBlockPtr.IsValid())
			{
				ActionBlockPtr = ActionStepBlockPtr->AsShared();
			}
		}
	}

	TAttribute<FMargin> OverlayPadding = TAttribute<FMargin>::Create(TAttribute<FMargin>::FGetter::CreateSP(this, &SDataprepGraphActionStepNode::GetBlockPadding));
	TAttribute<FSlateColor> BlockColorAndOpacity = TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SDataprepGraphActionStepNode::GetBlockOverlayColor));

	this->ContentScale.Bind( this, &SGraphNode::GetContentScale );
	this->GetOrAddSlot( ENodeZone::Center )
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.f, 0.f)
		[
			SNew( SSeparator )
			.SeparatorImage(FEditorStyle::GetBrush( "ThinLine.Horizontal" ))
			.Thickness(2.f)
			.Orientation(EOrientation::Orient_Horizontal)
			.ColorAndOpacity(this, &SDataprepGraphActionStepNode::GetDragAndDropColor)
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			.Padding(OverlayPadding)
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SDataprepGraphActionNode::CreateBackground(BlockColorAndOpacity)
			]

			+ SOverlay::Slot()
			.Padding(FDataprepEditorStyle::GetMargin( "DataprepActionStep.Padding" ))
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SDataprepGraphActionNode::CreateBackground(FDataprepEditorStyle::GetColor( "DataprepActionStep.BackgroundColor" ))
			]

			+ SOverlay::Slot()
			.Padding(FDataprepEditorStyle::GetMargin( "DataprepActionStep.Padding" ))
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				ActionBlockPtr
			]
		]
	];
}

FSlateColor SDataprepGraphActionStepNode::GetBlockOverlayColor() const
{
	static const FSlateColor BlockColor = FDataprepEditorStyle::GetColor( "DataprepActionStep.Filter.OutlineColor" );

	return ActionStepBlockPtr.IsValid() ? ActionStepBlockPtr->GetOutlineColor() : BlockColor;
}

FMargin SDataprepGraphActionStepNode::GetBlockPadding()
{
	static const FMargin Selected = FDataprepEditorStyle::GetMargin( "DataprepActionStep.Outter.Selected.Padding" );
	static const FMargin Regular = FDataprepEditorStyle::GetMargin( "DataprepActionStep.Outter.Regular.Padding" );

	const bool bIsSelected = GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode);

	return bIsSelected ? Selected : Regular;
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
	if ( MouseEvent.GetEffectingButton() ==  EKeys::LeftMouseButton )
	{
		GetOwnerPanel()->SelectionManager.ClickedOnNode( GraphNode, MouseEvent );
		return FReply::Handled().DetectDrag( AsShared(), EKeys::LeftMouseButton );
	}

	// Take ownership of the mouse if right mouse button clicked to display contextual menu
	if ( MouseEvent.GetEffectingButton() == EKeys::RightMouseButton )
	{
		if ( !GetOwnerPanel()->SelectionManager.SelectedNodes.Contains( GraphNode ) )
		{
			GetOwnerPanel()->SelectionManager.ClickedOnNode( GraphNode, MouseEvent );
		}
		return FReply::Handled();
	}

	return SGraphNode::OnMouseButtonDown( MyGeometry, MouseEvent);
}

FReply SDataprepGraphActionStepNode::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( MouseEvent.GetEffectingButton() == EKeys::RightMouseButton )
	{
		ensure(OwnerGraphPanelPtr.IsValid());

		const FVector2D Position = MouseEvent.GetScreenSpacePosition();
		OwnerGraphPanelPtr.Pin()->SummonContextMenu( Position, Position, GraphNode, nullptr, TArray<UEdGraphPin*>() );

		return FReply::Handled();
	}

	return FReply::Unhandled();
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
			if(SDataprepGraphActionNode* ActionNode = ParentNodePtr.Pin().Get())
			{
				ActionNode->SetDraggedIndex(StepIndex);
				return FReply::Handled().BeginDragDrop(FDataprepDragDropOp::New( SharedThis(ActionNode->GetParentTrackNode().Get()), SharedThis(this)));
			}
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