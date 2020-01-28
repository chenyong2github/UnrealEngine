// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/DataprepGraph/SDataprepGraphActionNode.h"

// Dataprep includes
#include "DataprepActionAsset.h"
#include "DataprepEditorLogCategory.h"
#include "DataprepEditorStyle.h"
#include "DataprepGraph/DataprepGraphActionNode.h"
#include "SchemaActions/DataprepDragDropOp.h"
#include "Widgets/DataprepGraph/SDataprepGraphActionStepNode.h"
#include "Widgets/DataprepGraph/SDataprepGraphTrackNode.h"

// Engine Includes
#include "SGraphPanel.h"
#include "NodeFactory.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "DataprepGraphEditor"

/**
 * The SDataprepEmptyActionStepNode is a helper class that handles drag and drop event at
 * the bottom of the SDataprepGraphActionNode widget
 */
class SDataprepEmptyActionStepNode : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDataprepEmptyActionStepNode) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, const TSharedPtr<SDataprepGraphActionNode>& InParent)
	{
		ParentPtr = InParent;

		const float InterStepSpacing = 5.f;

		bIsHovered = false;

		ChildSlot
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderBackgroundColor( this, &SDataprepEmptyActionStepNode::GetBorderBackgroundColor )
				.BorderImage(FEditorStyle::GetBrush("BTEditor.Graph.BTNode.Body"))
				[
					SNew(SBox)
					.HeightOverride(InterStepSpacing)
				]
			]
		];

		bShowInsertionSlot = false;
	}

	// SWidget Interface
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		TSharedPtr<FDataprepDragDropOp> DragActionStepNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
		if(DragActionStepNodeOp.IsValid() && ParentPtr.IsValid())
		{
			ParentTrackNodePtr.Pin()->OnDragLeave(DragDropEvent);

			DragActionStepNodeOp->SetHoveredNode(ParentPtr.Pin()->GetNodeObj());
			bShowInsertionSlot = true;
		}

		SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		TSharedPtr<FDataprepDragDropOp> DragActionStepNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
		if(DragActionStepNodeOp.IsValid() && ParentPtr.IsValid())
		{
			DragActionStepNodeOp->SetHoveredNode(ParentPtr.Pin()->GetNodeObj());
			bShowInsertionSlot = true;

			return FReply::Handled();
		}

		return SCompoundWidget::OnDragOver(MyGeometry, DragDropEvent);
	}

	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override
	{
		TSharedPtr<FDataprepDragDropOp> DragActionStepNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
		if(DragActionStepNodeOp.IsValid())
		{
			DragActionStepNodeOp->SetHoveredNode(nullptr);
		}

		bShowInsertionSlot = false;

		SCompoundWidget::OnDragLeave(DragDropEvent);
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		bShowInsertionSlot = false;
	
		// Process OnDrop if done by FDataprepDragDropOp
		TSharedPtr<FDataprepDragDropOp> DragActionStepNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
		if (DragActionStepNodeOp.IsValid() && ParentPtr.IsValid())
		{
			const FVector2D NodeAddPosition = ParentPtr.Pin()->NodeCoordToGraphCoord( MyGeometry.AbsoluteToLocal( DragDropEvent.GetScreenSpacePosition() ) );
			return DragActionStepNodeOp->DroppedOnNode(DragDropEvent.GetScreenSpacePosition(), NodeAddPosition);
		}

		return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
	}
	// End of SWidget Interface

	void SetParentTrackNode(TSharedPtr<SDataprepGraphTrackNode> InParentTrackNode)
	{
		ParentTrackNodePtr = InParentTrackNode;
	}

	void ShowInsertionSlot(bool bShow)
	{
		bShowInsertionSlot = bShow;
	}

private:
	FSlateColor GetBorderBackgroundColor() const
	{
		static FLinearColor Hovered = FDataprepEditorStyle::GetColor("DataprepActionStep.DragAndDrop");
		return bShowInsertionSlot ? Hovered : FLinearColor::Transparent;
	}

private:
	bool bShowInsertionSlot;
	TWeakPtr<SDataprepGraphActionNode> ParentPtr;
	TWeakPtr<SDataprepGraphTrackNode> ParentTrackNodePtr;
};

void SDataprepGraphActionNode::Construct(const FArguments& InArgs, UDataprepGraphActionNode* InActionNode)
{
	DataprepActionPtr = InActionNode->GetDataprepActionAsset();
	check(DataprepActionPtr.IsValid());

	ExecutionOrder = InActionNode->GetExecutionOrder();

	DataprepActionPtr->GetOnStepsOrderChanged().AddSP(this, &SDataprepGraphActionNode::OnStepsChanged);

	GraphNode = InActionNode;

	SetCursor(EMouseCursor::ResizeLeftRight);
	UpdateGraphNode();
}

void SDataprepGraphActionNode::MoveTo(const FVector2D& DesiredPosition, FNodeSet& NodeFilter)
{
	FVector2D NewPosition = ParentTrackNodePtr.IsValid() ? ParentTrackNodePtr.Pin()->ComputeActionNodePosition( DesiredPosition ) : DesiredPosition;

	SGraphNode::MoveTo(NewPosition, NodeFilter);
}

void SDataprepGraphActionNode::SetParentTrackNode(TSharedPtr<SDataprepGraphTrackNode> InParentTrackNode)
{
	ParentTrackNodePtr = InParentTrackNode;

	// Update parent track on step widgets
	for(TSharedPtr<SDataprepGraphActionStepNode>& ActionStepGraphNode : ActionStepGraphNodes)
	{
		ActionStepGraphNode->SetParentTrackNode(InParentTrackNode);
	}

	// Update parent on empty bottom widget
	FChildren* StepListChildren = ActionStepListWidgetPtr->GetChildren();
	TSharedRef<SDataprepEmptyActionStepNode> EmptyWidgetPtr = StaticCastSharedRef<SDataprepEmptyActionStepNode>(StepListChildren->GetChildAt(StepListChildren->Num() - 1));
	EmptyWidgetPtr->SetParentTrackNode(InParentTrackNode);
}

void SDataprepGraphActionNode::UpdateExecutionOrder()
{
	ensure(Cast<UDataprepGraphActionNode>(GraphNode));
	ExecutionOrder = Cast<UDataprepGraphActionNode>(GraphNode)->GetExecutionOrder();
}

TSharedRef<SWidget> SDataprepGraphActionNode::CreateNodeContentArea()
{
	if(DataprepActionPtr.IsValid())
	{
		PopulateActionStepListWidget();

		return SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			ActionStepListWidgetPtr.ToSharedRef()
		]

		//+SVerticalBox::Slot()
		//.AutoHeight()
		//[
		//	SNew(SHorizontalBox)
		//	+SHorizontalBox::Slot()
		//	.AutoWidth()
		//	[
		//		SNew(SColorBlock)
		//		.Color(FLinearColor::Transparent)
		//		.Size( FVector2D(200.f, 10.f) )
		//	]
		//]
		;
	}

	return 	SNew(STextBlock)
	.ColorAndOpacity( FSlateColor( FLinearColor::Red ) )
	.Text( FText::FromString( TEXT("This node doesn't have a dataprep action!") ) );
}

FReply SDataprepGraphActionNode::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	GetOwnerPanel()->SelectionManager.ClickedOnNode(GraphNode, MouseEvent);
	BorderBackgroundColor.Set(FDataprepEditorStyle::GetColor("DataprepActionStep.DragAndDrop"));

	if( MouseEvent.GetEffectingButton() ==  EKeys::LeftMouseButton )
	{
		return FReply::Handled().DetectDrag( AsShared(), EKeys::LeftMouseButton );
	}

	return FReply::Unhandled();
}

FCursorReply SDataprepGraphActionNode::OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const
{
	TOptional<EMouseCursor::Type> TheCursor = Cursor.Get();
	return ( TheCursor.IsSet() )
		? FCursorReply::Cursor( TheCursor.GetValue() )
		: FCursorReply::Unhandled();
}

void SDataprepGraphActionNode::SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel)
{
	if(!OwnerGraphPanelPtr.IsValid())
	{
		SGraphNode::SetOwner(OwnerPanel);

		for(TSharedPtr<SDataprepGraphActionStepNode>& ActionStepGraphNode : ActionStepGraphNodes)
		{
			if (ActionStepGraphNode.IsValid())
			{
				ActionStepGraphNode->SetOwner(OwnerPanel);
				OwnerPanel->AttachGraphEvents(ActionStepGraphNode);
			}
		}
	}
	else
	{
		ensure(OwnerPanel == OwnerGraphPanelPtr);
	}
}

FReply SDataprepGraphActionNode::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>(GraphNode))
	{
		if (UDataprepActionAsset* ActionAsset = ActionNode->GetDataprepActionAsset())
		{
			return FReply::Handled().BeginDragDrop(FDragDropActionNode::New(SharedThis(ParentTrackNodePtr.Pin().Get()), SharedThis(this)));
		}
	}

	return FReply::Unhandled();
}

FReply SDataprepGraphActionNode::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SetCursor(EMouseCursor::Default);

	TSharedPtr<FDragDropActionNode> DragOperation = DragDropEvent.GetOperationAs<FDragDropActionNode>();
	if (DragOperation.IsValid())
	{
		return FReply::Handled().EndDragDrop();
	}

	return SGraphNode::OnDrop(MyGeometry, DragDropEvent);
}

void SDataprepGraphActionNode::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Track node is not notified of drag left, do it
	TSharedPtr<FDragDropActionNode> DragOperation = DragDropEvent.GetOperationAs<FDragDropActionNode>();
	if (ParentTrackNodePtr.IsValid() && DragOperation.IsValid())
	{
		ParentTrackNodePtr.Pin()->OnDragLeave(DragDropEvent);
	}

	SGraphNode::OnDragEnter(MyGeometry, DragDropEvent);
}

void SDataprepGraphActionNode::PopulateActionStepListWidget()
{
	if(!ActionStepListWidgetPtr.IsValid())
	{
		ActionStepListWidgetPtr = SNew(SVerticalBox);
	}
	else
	{
		ActionStepListWidgetPtr->ClearChildren();
	}

	const float InterStepSpacing = 2.f;
	UDataprepActionAsset* DataprepAction = DataprepActionPtr.Get();

	UEdGraph* EdGraph = GraphNode->GetGraph();
	const int32 StepsCount = DataprepAction->GetStepsCount();
	const UClass* GraphActionStepNodeClass = UDataprepGraphActionStepNode::StaticClass();

	TSharedPtr<SDataprepGraphTrackNode> TrackNodePtr = ParentTrackNodePtr.Pin();

	ActionStepGraphNodes.SetNum(StepsCount);

	TSharedPtr<SGraphPanel> GraphPanelPtr = GetOwnerPanel();

	for ( int32 Index = 0; Index < StepsCount; ++Index )
	{
		UDataprepGraphActionStepNode* ActionStepNode = NewObject<UDataprepGraphActionStepNode>( EdGraph, GraphActionStepNodeClass, NAME_None, RF_Transactional );

		ActionStepNode->CreateNewGuid();
		ActionStepNode->PostPlacedNewNode();

		ActionStepNode->NodePosX = GraphNode->NodePosX;
		ActionStepNode->NodePosY = GraphNode->NodePosY;

		ActionStepNode->Initialize(DataprepAction, Index);

		TSharedPtr<SDataprepGraphActionStepNode> ActionStepGraphNode = StaticCastSharedPtr<SDataprepGraphActionStepNode>(FNodeFactory::CreateNodeWidget(ActionStepNode));

		ActionStepGraphNode->SetParentNode(SharedThis(this));

		if(TrackNodePtr.IsValid())
		{
			ActionStepGraphNode->SetParentTrackNode(TrackNodePtr);
		}

		ActionStepListWidgetPtr->AddSlot()
		.AutoHeight()
		[
			ActionStepGraphNode.ToSharedRef()
		];

		ActionStepGraphNodes[Index] = ActionStepGraphNode;
	}

	if(GraphPanelPtr.IsValid())
	{
		for ( TSharedPtr<SDataprepGraphActionStepNode>& ActionStepGraphNode : ActionStepGraphNodes )
		{
			ActionStepGraphNode->SetOwner(GraphPanelPtr.ToSharedRef());
		}
	}

	TSharedPtr<SDataprepEmptyActionStepNode> BottomSlot;
	ActionStepListWidgetPtr->AddSlot()
	.AutoHeight()
	[
		SAssignNew(BottomSlot, SDataprepEmptyActionStepNode, SharedThis(this))
	];

	if(TrackNodePtr.IsValid())
	{
		BottomSlot->SetParentTrackNode(TrackNodePtr);
	}
}

void SDataprepGraphActionNode::OnStepsChanged()
{
	if(DataprepActionPtr.IsValid())
	{
		PopulateActionStepListWidget();
	}
}

#undef LOCTEXT_NAMESPACE