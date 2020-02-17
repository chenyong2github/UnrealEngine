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
#include "GraphEditorSettings.h"
#include "NodeFactory.h"
#include "SGraphPanel.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "DataprepGraphEditor"

class SDataprepGraphActionProxyNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SDataprepGraphActionStepNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SDataprepGraphActionNode>& InParentNode)
	{
		ParentNodePtr = InParentNode;
		GraphNode = InParentNode->GetNodeObj();

		SetCursor(EMouseCursor::Default);
		UpdateGraphNode();
	}

	// SWidget interface
	// End of SWidget interface

	// SGraphNode interface
	virtual void UpdateGraphNode() override
	{
		this->ContentScale.Bind( this, &SGraphNode::GetContentScale );
		this->GetOrAddSlot( ENodeZone::Center )
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SColorBlock)
				.Color( FLinearColor::Transparent )
				.Size( TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP( this, &SDataprepGraphActionProxyNode::GetSize ) ) )
			]
		];
	}

	const FSlateBrush* GetShadowBrush(bool bSelected) const
	{
		return  FEditorStyle::GetNoBrush();
	}
	// End of SGraphNode interface

	FVector2D GetSize()
	{
		FVector2D Size(10.f);

		if(SDataprepGraphActionNode* ParentNode = ParentNodePtr.Pin().Get())
		{
			Size = ParentNode->GetCachedGeometry().GetLocalSize();

			if(Size == FVector2D::ZeroVector)
			{
				Size = ParentNode->GetDesiredSize();
				if(Size == FVector2D::ZeroVector)
				{
					Size.Set(10.f, 10.f);
				}
			}
		}

		return Size;
	}

	void SetPosition(const FVector2D& Position)
	{
		GraphNode->NodePosX = Position.X;
		GraphNode->NodePosY = Position.Y;
	}

private:
	/** Pointer to the SDataprepGraphTrackNode displayed in the graph editor  */
	TWeakPtr<SDataprepGraphActionNode> ParentNodePtr;
};

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
			.Padding(10.f, 0.f, 10.f, 0.f)
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
	}

	// SWidget Interface
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		TSharedPtr<FDataprepDragDropOp> DragActionStepNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
		if(DragActionStepNodeOp.IsValid() && ParentPtr.IsValid())
		{
			ParentTrackNodePtr.Pin()->OnDragLeave(DragDropEvent);

			DragActionStepNodeOp->SetHoveredNode(ParentPtr.Pin()->GetNodeObj());
			ParentPtr.Pin()->SetHoveredIndex( ParentPtr.Pin()->GetDataprepAction()->GetStepsCount() );
		}

		SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		TSharedPtr<FDataprepDragDropOp> DragActionStepNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
		if(DragActionStepNodeOp.IsValid() && ParentPtr.IsValid())
		{
			DragActionStepNodeOp->SetHoveredNode(ParentPtr.Pin()->GetNodeObj());
			ParentPtr.Pin()->SetHoveredIndex( ParentPtr.Pin()->GetDataprepAction()->GetStepsCount() );

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

		ParentPtr.Pin()->SetHoveredIndex( INDEX_NONE );

		SCompoundWidget::OnDragLeave(DragDropEvent);
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		// Reset dragged index as drag is completed
		ParentPtr.Pin()->SetDraggedIndex( INDEX_NONE );

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

private:
	FSlateColor GetBorderBackgroundColor() const
	{
		return ParentPtr.Pin()->GetInsertColor( ParentPtr.Pin()->GetDataprepAction()->GetStepsCount() );
	}

private:
	TWeakPtr<SDataprepGraphActionNode> ParentPtr;
	TWeakPtr<SDataprepGraphTrackNode> ParentTrackNodePtr;
};

void SDataprepGraphActionNode::Construct(const FArguments& InArgs, UDataprepGraphActionNode* InActionNode)
{
	DataprepActionPtr = InActionNode->GetDataprepActionAsset();
	check(DataprepActionPtr.IsValid());

	ExecutionOrder = InActionNode->GetExecutionOrder();
	DraggedIndex = INDEX_NONE;
	InsertIndex = INDEX_NONE;

	DataprepActionPtr->GetOnStepsOrderChanged().AddSP(this, &SDataprepGraphActionNode::OnStepsChanged);

	GraphNode = InActionNode;

	ProxyNodePtr = SNew(SDataprepGraphActionProxyNode, SharedThis(this));

	SetCursor(EMouseCursor::ResizeLeftRight);
	UpdateGraphNode();
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

void SDataprepGraphActionNode::UpdateProxyNode(const FVector2D& Position)
{
	ProxyNodePtr->SetPosition(Position);
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
		];
	}

	return 	SNew(STextBlock)
	.ColorAndOpacity( FSlateColor( FLinearColor::Red ) )
	.Text( FText::FromString( TEXT("This node doesn't have a dataprep action!") ) );
}

const FSlateBrush* SDataprepGraphActionNode::GetShadowBrush(bool bSelected) const
{
	return  FEditorStyle::GetNoBrush();
}

int32 SDataprepGraphActionNode::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Since only a proxy is in the graph panel, draw selection outline if applicable 
	if(SGraphPanel* GraphPanel = GetOwnerPanel().Get())
	{
		if (GraphPanel->SelectionManager.SelectedNodes.Contains(GraphNode))
		{
			const FSlateBrush* ShadowBrush = FEditorStyle::GetBrush(TEXT("Graph.Node.ShadowSelected"));
			const FVector2D NodeShadowSize = GetDefault<UGraphEditorSettings>()->GetShadowDeltaSize();

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				GetPaintSpaceGeometry().ToInflatedPaintGeometry(NodeShadowSize),
				ShadowBrush,
				ESlateDrawEffect::None,
				FLinearColor(1.0f, 1.0f, 1.0f, 1.f)
			);
		}
	}

	return SGraphNode::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

FReply SDataprepGraphActionNode::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	GetOwnerPanel()->SelectionManager.ClickedOnNode(GraphNode, MouseEvent);
	BorderBackgroundColor.Set(FDataprepEditorStyle::GetColor("DataprepActionStep.DragAndDrop"));

	if( MouseEvent.GetEffectingButton() ==  EKeys::LeftMouseButton )
	{
		return FReply::Handled().DetectDrag( AsShared(), EKeys::LeftMouseButton );
	}

	// Take ownership of the mouse if right mouse button clicked to display contextual menu
	if ( MouseEvent.GetEffectingButton() == EKeys::RightMouseButton )
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SDataprepGraphActionNode::OnMouseButtonUp(const FGeometry & MyGeometry, const FPointerEvent & MouseEvent)
{
	if ( MouseEvent.GetEffectingButton() == EKeys::RightMouseButton )
	{
		ensure(OwnerGraphPanelPtr.IsValid());

		const FVector2D Position = MouseEvent.GetScreenSpacePosition();
		OwnerGraphPanelPtr.Pin()->SummonContextMenu(Position, Position, GraphNode, nullptr, TArray<UEdGraphPin*>());

		// Release mouse capture
		return FReply::Handled().ReleaseMouseCapture();
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
		OwnerPanel->AttachGraphEvents(SharedThis(this));

		OwnerPanel->AddGraphNode(SharedThis(ProxyNodePtr.Get()));

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

FSlateColor SDataprepGraphActionNode::GetInsertColor(int32 Index)
{
	static const FSlateColor BackgroundColor = FDataprepEditorStyle::GetColor("DataprepActionStep.BackgroundColor");
	static const FSlateColor DragAndDrop = FDataprepEditorStyle::GetColor("DataprepActionStep.DragAndDrop");

	return Index == InsertIndex ? DragAndDrop : BackgroundColor;
}

void SDataprepGraphActionNode::SetDraggedIndex(int32 Index)
{
	DraggedIndex = Index;
	InsertIndex = INDEX_NONE;
}

void SDataprepGraphActionNode::SetHoveredIndex(int32 Index)
{
	if(DraggedIndex == INDEX_NONE || Index == DataprepActionPtr->GetStepsCount())
	{
		InsertIndex = Index;
	}
	else
	{
		InsertIndex = Index > DraggedIndex ? Index + 1 : (Index < DraggedIndex ? Index : INDEX_NONE);
	}
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
	
	EdGraphStepNodes.Reset(StepsCount);

	TSharedPtr<SDataprepGraphTrackNode> TrackNodePtr = ParentTrackNodePtr.Pin();

	ActionStepGraphNodes.SetNum(StepsCount);

	TSharedPtr<SGraphPanel> GraphPanelPtr = GetOwnerPanel();

	for ( int32 Index = 0; Index < StepsCount; ++Index )
	{
		EdGraphStepNodes.Emplace(NewObject<UDataprepGraphActionStepNode>( EdGraph, GraphActionStepNodeClass, NAME_None, RF_Transactional ));
		UDataprepGraphActionStepNode* ActionStepNode = EdGraphStepNodes.Last().Get();

		ActionStepNode->CreateNewGuid();
		ActionStepNode->PostPlacedNewNode();

		ActionStepNode->NodePosX = GraphNode->NodePosX;
		ActionStepNode->NodePosY = GraphNode->NodePosY;

		ActionStepNode->Initialize(DataprepAction, Index);

		TSharedPtr<SDataprepGraphActionStepNode> ActionStepGraphNode = SNew(SDataprepGraphActionStepNode, ActionStepNode, SharedThis(this));

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
		ParentTrackNodePtr.Pin()->RefreshLayout();
	}
}

#undef LOCTEXT_NAMESPACE