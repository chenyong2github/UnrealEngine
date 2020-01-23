// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/DataprepGraph/SDataprepGraphTrackNode.h"

#include "DataprepAsset.h"
#include "DataprepEditorLogCategory.h"
#include "DataprepEditorStyle.h"
#include "DataprepGraph/DataprepGraph.h"
#include "DataprepGraph/DataprepGraphActionNode.h"

#include "Layout/Children.h"
#include "Widgets/DataprepGraph/SDataprepGraphActionNode.h"
#include "Widgets/DataprepGraph/SDataprepGraphEditor.h"
#include "Widgets/DataprepWidgets.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "SGraphPanel.h"
#include "NodeFactory.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "DataprepGraphEditor"

float SDataprepGraphTrackNode::NodeDesiredWidth = 300.f;
float SDataprepGraphTrackNode::NodeDesiredSpacing = 16.f;
float SDataprepGraphTrackNode::TrackDesiredHeight = 40.f;
FMargin SDataprepGraphTrackNode::NodePadding( 15.f, -5.f, 5.f, 10.f );

class SDataprepGraphTrackWidget : public SHorizontalBox
{
public:
	SLATE_BEGIN_ARGS(SDataprepGraphTrackWidget) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, TSharedPtr<SDataprepGraphTrackNode> InTrackNode)
	{
		TrackNode = InTrackNode;

		SHorizontalBox::FSlot* FirstSlot = nullptr;
		SHorizontalBox::FSlot* LastSlot = nullptr;

		SHorizontalBox::Construct( SHorizontalBox::FArguments()
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SColorBlock)
				.Color( FDataprepEditorStyle::GetColor( "Graph.TrackEnds.BackgroundColor" ) )
				.Size( TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP( InTrackNode.Get(), &SDataprepGraphTrackNode::GetLeftBlockSize ) ) )
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew( InnerCanvas, SConstraintCanvas )

				// The outline. This is done by a background image
				+ SConstraintCanvas::Slot()
				.Anchors( FAnchors( 0.f, 0.f, 1.f, 1.f ) )
				.Offset( FMargin() )
				[
					SNew(SColorBlock)
					.Color( FDataprepEditorStyle::GetColor( "Graph.TrackInner.BackgroundColor" ) )
					.Size( TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP( InTrackNode.Get(), &SDataprepGraphTrackNode::GetInnerBlockSize ) ) )
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SColorBlock)
				.Color( FDataprepEditorStyle::GetColor( "Graph.TrackEnds.BackgroundColor" ) )
				.Size( TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP( InTrackNode.Get(), &SDataprepGraphTrackNode::GetRightBlockSize ) ) )
			]
		);
	}

	// SWidget Interface
	virtual FVector2D ComputeDesiredSize(float) const override
	{
		if(SDataprepGraphTrackNode* TrackNodeRaw = TrackNode.Pin().Get())
		{
			const float TrackNodeWidth = TrackNodeRaw->GetLeftBlockSize().X + TrackNodeRaw->GetInnerBlockSize().X + TrackNodeRaw->GetRightBlockSize().X;
			return FVector2D( TrackNodeWidth, SDataprepGraphTrackNode::TrackDesiredHeight );
		}
		
		return FVector2D::ZeroVector;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		FArrangedChildren ArrangedChildren( EVisibility::Visible );
		{
			ArrangeChildren( AllottedGeometry, ArrangedChildren );
		}

		int32 MaxLayerId = LayerId;
		for( int32 ChildIndex = 0, NodeIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex )
		{
			FArrangedWidget& CurWidget = ArrangedChildren[ ChildIndex ];

			const int32 CurWidgetsMaxLayerId = CurWidget.Widget->Paint(Args.WithNewParent(this), CurWidget.Geometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, ShouldBeEnabled(bParentEnabled));
			MaxLayerId = FMath::Max(MaxLayerId, CurWidgetsMaxLayerId);
		}

		return MaxLayerId;
	}
	// End of SWidget Interface

	TWeakPtr<SDataprepGraphTrackNode> TrackNode;
	TSharedPtr<SConstraintCanvas> InnerCanvas;

	friend SDataprepGraphTrackNode;
};

void SDataprepGraphTrackNode::Construct(const FArguments& InArgs, UDataprepGraphRecipeNode* InNode)
{
	bNodeDragging = false;
	SetCursor(EMouseCursor::Default);
	GraphNode = InNode;
	check(GraphNode);

	UDataprepGraph* DataprepGraph = Cast<UDataprepGraph>(GraphNode->GetGraph());
	check(DataprepGraph);

	DataprepAssetPtr = DataprepGraph->GetDataprepAsset();
	check(DataprepAssetPtr.IsValid());

	SNodePanel::SNode::FNodeSet NodeFilter;
	SGraphNode::MoveTo( FVector2D::ZeroVector, NodeFilter);

	InNode->SetWidget(SharedThis(this));

	UpdateGraphNode();
}

void SDataprepGraphTrackNode::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	TSharedPtr<SGraphPanel> GraphPanelPtr = OwnerGraphPanelPtr.Pin();

	ContentScale.Bind( this, &SGraphNode::GetContentScale );

	GetOrAddSlot( ENodeZone::Center )
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	[
		SNew(SBorder)
		.BorderImage( FEditorStyle::GetBrush( "NoBorder" ) )
		.Padding(0.f)
		.BorderBackgroundColor( FLinearColor( 0.3f, 0.3f, 0.3f, 1.0f ) )
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5.f)
			[
				SNew(SBox)
				//.MinDesiredHeight(NodeHeight)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(LeftNodeBox, SVerticalBox)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew( TrackWidgetPtr, SDataprepGraphTrackWidget, SharedThis(this) )
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(RightNodeBox, SVerticalBox)
					]
				]
			]
		]
	];

	if(UDataprepAsset* DataprepAsset = DataprepAssetPtr.Get())
	{
		UEdGraph* EdGraph = GraphNode->GetGraph();

		TSharedPtr<SDataprepGraphTrackNode> ParentTrackNodePtr = SharedThis(this);

		const int32 ActionsCount = DataprepAsset->GetActionCount();
		ActionNodes.Empty(ActionsCount);
		ActionNodes.SetNum(ActionsCount);

		for(int32 Index = 0; Index < DataprepAsset->GetActionCount(); ++Index)
		{
			if(UDataprepActionAsset* ActionAsset = DataprepAsset->GetAction(Index))
			{
				UDataprepGraphActionNode* NewActionNode = NewObject<UDataprepGraphActionNode>( EdGraph, UDataprepGraphActionNode::StaticClass(), NAME_None, RF_Transactional );

				NewActionNode->CreateNewGuid();
				NewActionNode->PostPlacedNewNode();

				NewActionNode->NodePosX = 0;
				NewActionNode->NodePosY = 0;

				NewActionNode->Initialize(ActionAsset, Index);
				
				TSharedPtr< SDataprepGraphActionNode > ActionWidgetPtr = StaticCastSharedPtr<SDataprepGraphActionNode>(FNodeFactory::CreateNodeWidget(NewActionNode));
				if(SDataprepGraphActionNode* ActionWidget = ActionWidgetPtr.Get())
				{
					if(GraphPanelPtr.IsValid())
					{
						GraphPanelPtr->AddGraphNode(ActionWidgetPtr.ToSharedRef());
					}

					ActionWidget->UpdateGraphNode();
					ActionWidget->ComputeDesiredSize(1.0f);

					ActionWidget->SetParentTrackNode(ParentTrackNodePtr);

					ActionNodes[Index] = ActionWidgetPtr;
				}
			}
		}

		NodeAbscissaMin = FMath::FloorToInt(LeftBlockSize.X + NodePadding.Left + InterNodeSpacing * 0.5f);
		NodeAbscissaMax = NodeAbscissaMin + FMath::FloorToInt((float)(ActionNodes.Num()-1)*(NodeDesiredWidth + InterNodeSpacing));

		// Position action nodes
		ReArrangeActionNodes();
	}
}

void SDataprepGraphTrackNode::ReArrangeActionNodes()
{
	SNodePanel::SNode::FNodeSet NodeFilter;
	const float InitialOffset = LeftBlockSize.X + NodePadding.Left + InterNodeSpacing * 0.5f;
	const float Increment = NodeDesiredWidth + InterNodeSpacing;
	const float PositionY = NodePadding.Top;

	if(bNodeDragging)
	{
		FString SequenceText;
		for(int32 Index = 0 ; Index < NewActionsOrder.Num(); ++Index)
		{
			const int32 OldExecutionOrder = NewActionsOrder[Index];
			SequenceText += FString::Printf(TEXT("%d "), ActionNodes[OldExecutionOrder]->GetExecutionOrder());

			if(Index == CurrentOrder)
			{
				continue;
			}

			if( SDataprepGraphActionNode* ActionWidget = ActionNodes[OldExecutionOrder].Get())
			{
				ActionWidget->SGraphNode::MoveTo( FVector2D( NodeAbscissaMin + (float)Index * Increment, PositionY), NodeFilter);
			}
		}
	}
	else
	{
		for(TSharedPtr< SDataprepGraphActionNode >& ActionWidgetPtr : ActionNodes)
		{
			if( SDataprepGraphActionNode* ActionWidget = ActionWidgetPtr.Get() )
			{
				ActionWidget->SGraphNode::MoveTo( FVector2D( NodeAbscissaMin + (float)ActionWidget->GetExecutionOrder() * Increment, PositionY), NodeFilter);
				ActionWidget->Invalidate(EInvalidateWidgetReason::RenderTransform);
			}
		}
	}
}

void SDataprepGraphTrackNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter)
{
	SGraphNode::MoveTo( FVector2D::ZeroVector, NodeFilter);

	NodeAbscissaMin = FMath::FloorToInt(LeftBlockSize.X + NodePadding.Left + InterNodeSpacing * 0.5f);
	NodeAbscissaMax = NodeAbscissaMin + FMath::FloorToInt((float)(ActionNodes.Num()-1)*(NodeDesiredWidth + InterNodeSpacing));
}

void SDataprepGraphTrackNode::SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel)
{
	ensure(!OwnerGraphPanelPtr.IsValid());

	SGraphNode::SetOwner(OwnerPanel);

	for(TSharedPtr<SDataprepGraphActionNode>& ActionNodePtr : ActionNodes)
	{
		if (ActionNodePtr.IsValid())
		{
			OwnerPanel->AddGraphNode( ActionNodePtr.ToSharedRef() );
		}
	}
}

FVector2D SDataprepGraphTrackNode::Update(const FVector2D& LocalSize, float ZoomAmount)
{
	// #ueent_wip: Find a way to avoid recompute of everything on each call
	const float InvZoomAmount = 1.f / ZoomAmount;

	if(NodeDesiredSpacing * ZoomAmount < 6.f)
	{
		InterNodeSpacing = FMath::CeilToFloat( NodeDesiredSpacing * InvZoomAmount );
	}
	else
	{
		InterNodeSpacing = NodeDesiredSpacing;
	}

	NodeAbscissaMin = FMath::FloorToInt(LeftBlockSize.X + NodePadding.Left + InterNodeSpacing * 0.5f);
	NodeAbscissaMax = NodeAbscissaMin + FMath::FloorToInt((float)(ActionNodes.Num()-1)*(NodeDesiredWidth + InterNodeSpacing));

	InnerBlockSize.Set( (NodePadding.Left + NodePadding.Right + NodeDesiredWidth + InterNodeSpacing) * float(ActionNodes.Num() > 0 ? ActionNodes.Num() : 1), TrackDesiredHeight );

	RightBlockSize.Set( InnerBlockSize.X, InnerBlockSize.Y);

	const float ZoomedSizeInX = LocalSize.X * InvZoomAmount;
	if(InnerBlockSize.X < ZoomedSizeInX)
	{
		RightBlockSize.X = ZoomedSizeInX - InnerBlockSize.X;
	}

	LeftBlockSize.Set( FMath::CeilToFloat( 10.f * InvZoomAmount ), InnerBlockSize.Y);

	float NodeMaxHeight = NodeDesiredWidth;

	for(TSharedPtr<SDataprepGraphActionNode>& ActionNodePtr : ActionNodes)
	{
		if (SDataprepGraphActionNode* ActionNode = ActionNodePtr.Get())
		{
			const float ActionNodeHeight = ActionNode->GetDesiredSize().Y;
			if(NodeMaxHeight < ActionNodeHeight)
			{
				NodeMaxHeight = ActionNodeHeight;
			}
		}
	}

	ReArrangeActionNodes();

	return FVector2D( InnerBlockSize.X, NodeMaxHeight);
}

FVector2D SDataprepGraphTrackNode::ComputeActionNodePosition(const FVector2D& InPosition)
{
	const float NewAbscissa = InPosition.X < NodeAbscissaMin ? NodeAbscissaMin : (InPosition.X > NodeAbscissaMax ? NodeAbscissaMax : InPosition.X);

	return FVector2D( NewAbscissa, NodePadding.Top);
}

void SDataprepGraphTrackNode::OnStartNodeDrag(const TSharedRef<SDataprepGraphActionNode>& ActionNode)
{
	bNodeDragging = true;
	bSkipNextDragUpdate = false;
 
	const FGeometry& DesktopGeometry = GetOwnerPanel()->GetPersistentState().DesktopGeometry;
	LastDragScreenSpacePosition = FSlateApplication::Get().GetCursorPos();
	FVector2D DragLocalPosition = DesktopGeometry.AbsoluteToLocal( LastDragScreenSpacePosition );
	DragOrdinate = DragLocalPosition.Y;

	OriginalOrder = ActionNode->GetExecutionOrder();
	CurrentOrder = OriginalOrder;

	NewActionsOrder.SetNum(ActionNodes.Num());
	for(int32 Index = 0; Index < NewActionsOrder.Num(); ++Index)
	{
		NewActionsOrder[Index] = Index;
	}
}

void SDataprepGraphTrackNode::OnNodeDropped(bool bDropWasHandled)
{
	FString SequenceText;
	for(int32 Index = 0 ; Index < NewActionsOrder.Num(); ++Index)
	{
		SequenceText += FString::Printf(TEXT("%d "), ActionNodes[NewActionsOrder[Index]]->GetExecutionOrder());
	}

	if(bDropWasHandled && CurrentOrder != OriginalOrder)
	{
		// #ueent_wip: Apply change from OriginalOrder to CurrentOrder onto Dataprep asset's array of actions
		// And react to notification of change
		for(int32 Index = 0; Index < NewActionsOrder.Num(); ++Index)
		{
			if( SDataprepGraphActionNode* ActionWidget = ActionNodes[NewActionsOrder[Index]].Get())
			{
				ActionWidget->SetExecutionOrder(Index);
			}
		}

		ActionNodes.Sort([](const TSharedPtr<SDataprepGraphActionNode> A, const TSharedPtr<SDataprepGraphActionNode> B) { return A->GetExecutionOrder() < B->GetExecutionOrder(); });
	}

	bNodeDragging = false;
	NewActionsOrder.Empty(ActionNodes.Num());

	ReArrangeActionNodes();
}

void SDataprepGraphTrackNode::UpdatePanelOnDrag(const FVector2D& DragScreenSpacePosition, const FVector2D& ScreenSpaceDelta)
{
	const float MaxAbscissa = LeftBlockSize.X + InnerBlockSize.X;

	SDataprepGraphActionNode* ActionWidget = ActionNodes[NewActionsOrder[CurrentOrder]].Get();
	FVector2D NodePosition = ActionWidget->GetPosition();

	TSharedPtr<SGraphPanel> GraphPanel = GetOwnerPanel();

	float ZoomAmount = GraphPanel->GetZoomAmount();

	const FGeometry& DesktopGeometry = GraphPanel->GetPersistentState().DesktopGeometry;
	const FVector2D& Size = DesktopGeometry.GetLocalSize() / ZoomAmount;

	// Keep the mouse at the same position if dragged node has reached when of the ends of the track
	if(NodePosition.X == NodeAbscissaMin || NodePosition.X == NodeAbscissaMax)
	{
		// Do nothing, the mouse's cursor and the dragged node should not move
	}
	// Panel is narrower than track, update panel if dragged node has entered non visible part of track
	else if(MaxAbscissa > Size.X)
	{
		const float DragIncrementStep = NodeDesiredWidth / 6.f;
		FVector2D ViewOffset = GraphPanel->GetViewOffset();
		FVector2D DragPosition = DesktopGeometry.AbsoluteToLocal(DragScreenSpacePosition);
		float AbscissaPanOffset = 0.f;

		const float LeftCornerAbscissa = NodePosition.X - InterNodeSpacing * 0.5f - ViewOffset.X;
		const float RightCornerAbscissa = NodePosition.X + NodeDesiredWidth + InterNodeSpacing * 0.5f - ViewOffset.X;

		// Dragged node's right corner is disappearing on the right, bring it back
		if(  ScreenSpaceDelta.X > 0.f && RightCornerAbscissa > Size.X )
		{
			// Compute offset to display right corner and bring right neighbor too if applicable
			AbscissaPanOffset = CurrentOrder < (NewActionsOrder.Num() - 1) ? RightCornerAbscissa - Size.X + DragIncrementStep : RightCornerAbscissa - Size.X;
		}
		// Dragged node's left corner is disappearing on the left, bring it back
		else if( ScreenSpaceDelta.X < 0.f && LeftCornerAbscissa < 0.f )
		{
			// Compute offset to display left corner and bring left neighbor's back if applicable
			AbscissaPanOffset = CurrentOrder > 0 ? LeftCornerAbscissa - DragIncrementStep : LeftCornerAbscissa;
		}
		else
		{
			// Make sure cursor stays at a constant height
			LastDragScreenSpacePosition = DesktopGeometry.LocalToAbsolute( FVector2D( DragPosition.X, DragOrdinate ) );
		}

		// Apply offset to panel's canvas and move mouse to stay on top of dragged node if required
		if(AbscissaPanOffset != 0.f)
		{
			// Compute new cursor's new screen space position
			LastDragScreenSpacePosition = DesktopGeometry.LocalToAbsolute( FVector2D( DragPosition.X - AbscissaPanOffset, DragOrdinate ) );

			// Pan panel accordingly
			AbscissaPanOffset *= ZoomAmount;
			FVector2D NewViewOffset(ViewOffset.X + AbscissaPanOffset, ViewOffset.Y);
			GraphPanel->RestoreViewSettings(NewViewOffset, ZoomAmount);
		}
	}
	else
	{
		// Make sure cursor stays at a constant height
		LastDragScreenSpacePosition.X = DragScreenSpacePosition.X;
	}

	// Update cursor's position
	bSkipNextDragUpdate = LastDragScreenSpacePosition.X != DragScreenSpacePosition.X;
	FSlateApplication::Get().SetCursorPos( LastDragScreenSpacePosition );
}

void SDataprepGraphTrackNode::OnNodeDragged( TSharedPtr<SDataprepGraphActionNode>& ActionNodePtr, const FVector2D& DragScreenSpacePosition, const FVector2D& ScreenSpaceDelta)
{
	ensure(bNodeDragging);

	// This update is most likely due to a call to FSlateApplication::SetCursorPos. Skip it
	if(bSkipNextDragUpdate)
	{
		bSkipNextDragUpdate = false;
		return;
	}

	if(SDataprepGraphActionNode* ActionNode = ActionNodePtr.Get())
	{
		const FVector2D NodePosition = ActionNode->GetPosition();

		bool bValidMove = 
			// Dragged node on left end but mouse moving to the right
			(NodePosition.X == NodeAbscissaMin && ScreenSpaceDelta.X > 0) ||
			// Dragged node on right end but mouse moving to the left
			(NodePosition.X == NodeAbscissaMax && ScreenSpaceDelta.X < 0) ||
			// Dragged node within track
			(NodePosition.X != NodeAbscissaMin && NodePosition.X != NodeAbscissaMax);

		if(bValidMove)
		{
			const FVector2D NodeNewPosition = ComputeActionNodePosition( NodePosition + (ScreenSpaceDelta / GetOwnerPanel()->GetZoomAmount()));

			SNodePanel::SNode::FNodeSet NodeFilter;
			ActionNode->SGraphNode::MoveTo( NodeNewPosition, NodeFilter);

			// Check if center of dragged widget is over a neighboring widget by at least half its size
			const float NodeRelativeCenterAbscissa = NodeNewPosition.X + (NodeDesiredWidth * 0.5f) + (InterNodeSpacing * 0.5f) - (LeftBlockSize.X + NodePadding.Left);
			const int32 NewOrder = FMath::FloorToInt( NodeRelativeCenterAbscissa / (NodeDesiredWidth + InterNodeSpacing));

			if(NewOrder != CurrentOrder)
			{
				ensure( NewActionsOrder.IsValidIndex(NewOrder) );

				// Make the swap
				int32 TempOrder = NewActionsOrder[CurrentOrder];
				NewActionsOrder[CurrentOrder] = NewActionsOrder[NewOrder];
				NewActionsOrder[NewOrder] = TempOrder;

				CurrentOrder = NewOrder;

				// Reflect swap in graph editor
				ReArrangeActionNodes();
			}
		}

		// Request the active panel to scroll if required
		UpdatePanelOnDrag( DragScreenSpacePosition, ScreenSpaceDelta );
	}
}

TSharedRef<FDragGraphActionNode> FDragGraphActionNode::New(const TSharedRef<SDataprepGraphTrackNode>& InTrackNodePtr, const TSharedRef<SDataprepGraphActionNode>& InDraggedNode)
{
	TSharedRef<FDragGraphActionNode> Operation = MakeShareable(new FDragGraphActionNode);

	Operation->TrackNodePtr = InTrackNodePtr;
	Operation->ActionNodePtr = InDraggedNode;

	Operation->bCreateNewWindow = false;
	Operation->Construct();

	InTrackNodePtr->OnStartNodeDrag(InDraggedNode);

	return Operation;
}

TSharedRef<FDragGraphActionNode> FDragGraphActionNode::New(const TSharedRef<SDataprepGraphTrackNode>& InTrackNodePtr, const TArray< TSharedRef<SDataprepGraphActionNode> >& InDraggedNodes)
{
	TSharedRef<FDragGraphActionNode> Operation = MakeShareable(new FDragGraphActionNode);

	Operation->TrackNodePtr = InTrackNodePtr;
	Operation->ActionNodePtr = InDraggedNodes[0];

	Operation->bCreateNewWindow = false;
	Operation->Construct();

	InTrackNodePtr->OnStartNodeDrag(InDraggedNodes[0]);

	return Operation;
}

void FDragGraphActionNode::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	TrackNodePtr->OnNodeDropped(bDropWasHandled);

	FDragDropOperation::OnDrop(bDropWasHandled, MouseEvent);
}

void FDragGraphActionNode::OnDragged(const FDragDropEvent& DragDropEvent)
{
	TrackNodePtr->OnNodeDragged( ActionNodePtr, DragDropEvent.GetScreenSpacePosition(), DragDropEvent.GetCursorDelta() );

	FDragDropOperation::OnDragged(DragDropEvent);
}

#undef LOCTEXT_NAMESPACE