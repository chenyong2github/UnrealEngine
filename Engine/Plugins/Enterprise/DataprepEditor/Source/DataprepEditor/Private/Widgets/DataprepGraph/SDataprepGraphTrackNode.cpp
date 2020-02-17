// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/DataprepGraph/SDataprepGraphTrackNode.h"

#include "DataprepAsset.h"
#include "DataprepEditorLogCategory.h"
#include "DataprepEditorStyle.h"
#include "DataprepGraph/DataprepGraph.h"
#include "DataprepGraph/DataprepGraphActionNode.h"
#include "SchemaActions/DataprepDragDropOp.h"

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

FMargin SDataprepGraphTrackNode::NodePadding( 15.f, -5.f, 5.f, 10.f );

class SDataprepGraphTrackWidget : public SHorizontalBox
{
public:
	SLATE_BEGIN_ARGS(SDataprepGraphTrackWidget) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, TSharedPtr<SDataprepGraphTrackNode> InTrackNode);

	void MoveDropSlotTo(int32 SlotIndex);

	void SetActionSlot(int32 SlotIndex, const TSharedRef<SWidget>& Widget);

	// SWidget Interface
	virtual FVector2D ComputeDesiredSize(float InLayoutScaleMultiplier) const override;
	// End of SWidget Interface

	const FVector2D& GetWorkingArea() const
	{
		return WorkingArea;
	}

	FVector2D GetWorkingArea()
	{
		return WorkingArea;
	}

	FVector2D GetTrackArea()
	{
		return FVector2D(WorkingArea.X, TrackDesiredHeight);
	}

	const FVector2D& GetCanvasArea() const
	{
		return CanvasArea;
	}

	FVector2D GetCanvasArea()
	{
		return CanvasArea;
	}

	FVector2D GetActioNodeAbscissaRange();

	int32 GetHoveredActionNode(float Left, float Width);

	float GetActionNodeAbscissa(int32 Index)
	{
		return ActionSlots.IsValidIndex(Index) ? ActionSlots[Index]->OffsetAttr.Get(FMargin()).Left : 0.f;
	}

	float ValidateNodeAbscissa(float InAbscissa);

	FVector2D GetNodeSize(const TSharedRef<SWidget>& Widget) const;

	void RefreshLayout(SGraphPanel* GraphPanel);

	void OnStartNodeDrag(int32 Index);

	float OnNodeDragged(float Delta);

	int32 OnEndNodeDrag();

	bool IsDraggedNodeOnEnds();

	/**
	*/
	void UpdateLayoutForCopy();

	/**
	*/
	void UpdateLayoutForMove();

	/** Toggles display of action nodes between copy vs move drop modes */
	float OnControlKeyDown(bool bKeyDown);

	void UpdateDragIndicator(FVector2D MousePosition);

	int32 GetDragIndicatorIndex();

	void ResetDragIndicator()
	{
		for(int32 Index = 0; Index < DropSlots.Num(); ++Index)
		{
			(*DropSlots[Index])[DropFiller.ToSharedRef()];
		}
	}

	void OnActionsOrderChanged(const TArray<TSharedPtr<SDataprepGraphActionNode>>& InActionNodes);

	TWeakPtr<SDataprepGraphTrackNode> TrackNodePtr;
	TSharedPtr<SConstraintCanvas> TrackCanvas;
	TArray<TSharedRef<SWidget>> ActionNodes;
	TArray<SConstraintCanvas::FSlot*> DropSlots;
	TArray<SConstraintCanvas::FSlot*> ActionSlots;
	SConstraintCanvas::FSlot* CanvasSlot;

	TSharedPtr<SWidget> DropIndicator;
	TSharedPtr<SColorBlock> DropFiller;
	mutable FVector2D WorkingArea;
	mutable FVector2D CanvasArea;
	int32 LastDropSlot;

	SConstraintCanvas::FSlot* DragSlot;
	TArray<int32> NewActionsOrder;
	int32 DragStartIndex;
	int32 DragIndex;
	FVector2D LastDragPosition;
	FVector2D AbscissaRange;
	FVector2D TrackOffset;
	bool bIsCopying;

	static float SlotSpacing;
	static float NodeDesiredWidth;
	static float TrackDesiredHeight;

	friend SDataprepGraphTrackNode;
};

float SDataprepGraphTrackWidget::SlotSpacing = 16.f;
float SDataprepGraphTrackWidget::NodeDesiredWidth = 300.f;
float SDataprepGraphTrackWidget::TrackDesiredHeight = 40.f;

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

	if(UDataprepAsset* DataprepAsset = DataprepAssetPtr.Get())
	{
		TSharedPtr<SGraphPanel> GraphPanelPtr = OwnerGraphPanelPtr.Pin();

		UEdGraph* EdGraph = GraphNode->GetGraph();

		TSharedPtr<SDataprepGraphTrackNode> ParentTrackNodePtr = SharedThis(this);

		const int32 ActionsCount = DataprepAsset->GetActionCount();
		ActionNodes.Empty(ActionsCount);
		ActionNodes.SetNum(ActionsCount);
		EdGraphActionNodes.Reset(ActionsCount);

		for(int32 Index = 0; Index < ActionsCount; ++Index)
		{
			if(UDataprepActionAsset* ActionAsset = DataprepAsset->GetAction(Index))
			{
				EdGraphActionNodes.Emplace(NewObject<UDataprepGraphActionNode>( EdGraph, UDataprepGraphActionNode::StaticClass(), NAME_None, RF_Transactional ));
				UDataprepGraphActionNode* NewActionNode = EdGraphActionNodes.Last().Get();

				NewActionNode->CreateNewGuid();
				NewActionNode->PostPlacedNewNode();

				NewActionNode->NodePosX = 0;
				NewActionNode->NodePosY = 0;

				NewActionNode->Initialize(ActionAsset, Index);

				// #ueent_wip: Widget is created twice :-(
				TSharedPtr< SDataprepGraphActionNode > ActionWidgetPtr = StaticCastSharedPtr<SDataprepGraphActionNode>(FNodeFactory::CreateNodeWidget(NewActionNode));
				if(SDataprepGraphActionNode* ActionWidget = ActionWidgetPtr.Get())
				{
					if(GraphPanelPtr.IsValid())
					{
						ActionWidget->SetOwner(GraphPanelPtr.ToSharedRef());
					}

					ActionWidget->UpdateGraphNode();
					ActionWidget->ComputeDesiredSize(1.0f);

					ActionWidget->SetParentTrackNode(ParentTrackNodePtr);

					ActionNodes[Index] = ActionWidgetPtr;
				}
			}
		}
	}

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
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew( TrackWidgetPtr, SDataprepGraphTrackWidget, SharedThis(this) )
					]
				]
			]
		]
	];

	GraphNode->NodePosX = NodePadding.Left;
	GraphNode->NodePosY = NodePadding.Top;
}

bool SDataprepGraphTrackNode::CustomPrepass(float LayoutScaleMultiplier)
{
	return false;
}

void SDataprepGraphTrackNode::OnActionsOrderChanged()
{
	if(!DataprepAssetPtr.IsValid())
	{
		return;
	}

	TMap<UDataprepActionAsset*, TSharedPtr< SDataprepGraphActionNode >> WidgetByAsset;

	for(TSharedPtr< SDataprepGraphActionNode >& ActionWidgetPtr : ActionNodes)
	{

		if( SDataprepGraphActionNode* ActionWidget = ActionWidgetPtr.Get() )
		{
			ensure(Cast<UDataprepGraphActionNode>(ActionWidgetPtr->GetNodeObj()) != nullptr);
			WidgetByAsset.Emplace(Cast<UDataprepGraphActionNode>(ActionWidgetPtr->GetNodeObj())->GetDataprepActionAsset(), ActionWidgetPtr);
		}
	}

	for(int32 Index = 0; Index < DataprepAssetPtr->GetActionCount(); ++Index)
	{
		if(UDataprepActionAsset* ActionAsset = DataprepAssetPtr->GetAction(Index))
		{
			TSharedPtr< SDataprepGraphActionNode >* ActionWidgetPtr = WidgetByAsset.Find(ActionAsset);
			if(ActionWidgetPtr != nullptr)
			{
				UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>((*ActionWidgetPtr)->GetNodeObj());
				ensure(ActionNode != nullptr);

				ActionNode->Initialize( ActionAsset, Index);
				(*ActionWidgetPtr)->UpdateExecutionOrder();
			}
		}
	}

	// Reorder array according to new execution order
	ActionNodes.Sort([](const TSharedPtr<SDataprepGraphActionNode> A, const TSharedPtr<SDataprepGraphActionNode> B) { return A->GetExecutionOrder() < B->GetExecutionOrder(); });

	// Rearrange actions nodes along track
	TrackWidgetPtr->OnActionsOrderChanged(ActionNodes);

	// Invalidate graph panel for a redraw of all widgets
	OwnerGraphPanelPtr.Pin()->Invalidate(EInvalidateWidgetReason::ChildOrder | EInvalidateWidgetReason::Layout);
}

bool SDataprepGraphTrackNode::RefreshLayout()
{
	if(OwnerGraphPanelPtr.IsValid() && OwnerGraphPanelPtr.Pin()->GetAllChildren()->Num() > 0)
	{
		ensure(TrackWidgetPtr.IsValid());

		const FVector2D TrackWidgetOrigin = TrackWidgetPtr->GetCachedGeometry().LocalToAbsolute(FVector2D::ZeroVector);
		const FVector2D TrackNodeOrigin = GetCachedGeometry().LocalToAbsolute(FVector2D::ZeroVector);

		// Weird: Ultimate offset between track widget and node is 5 but it may take more draw call to stabilize
		//		  Forcing 5.
		TrackWidgetPtr->TrackOffset = FVector2D(5.f)/*TrackWidgetOrigin - TrackNodeOrigin*/ + GetPosition();

		TrackWidgetPtr->RefreshLayout(OwnerGraphPanelPtr.Pin().Get());

		return true;
	}

	return false;
}

void SDataprepGraphTrackNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter)
{
	// Block track node to specific position
	SGraphNode::MoveTo( FVector2D(NodePadding.Left, 0.f), NodeFilter);
}

const FSlateBrush* SDataprepGraphTrackNode::GetShadowBrush(bool bSelected) const
{
	return  FEditorStyle::GetNoBrush();
}

FReply SDataprepGraphTrackNode::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SetCursor(EMouseCursor::Default);

	TSharedPtr<FDragDropActionNode> DragOperation = DragDropEvent.GetOperationAs<FDragDropActionNode>();
	if (DragOperation.IsValid())
	{
		return FReply::Handled().EndDragDrop();
	}

	TSharedPtr<FDataprepDragDropOp> DragActionStepNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if(DragActionStepNodeOp.IsValid())
	{
		int32 InsertIndex = TrackWidgetPtr->GetDragIndicatorIndex();
		TrackWidgetPtr->ResetDragIndicator();

		DragActionStepNodeOp->DoDropOnTrack(DataprepAssetPtr.Get(), InsertIndex);

		return FReply::Unhandled();
	}

	return SGraphNode::OnDrop(MyGeometry, DragDropEvent);
}

FReply SDataprepGraphTrackNode::OnDragOver(const FGeometry & MyGeometry, const FDragDropEvent & DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragActionStepNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if(DragActionStepNodeOp.IsValid())
	{
		DragActionStepNodeOp->SetHoveredNode(GraphNode);
		TrackWidgetPtr->UpdateDragIndicator(DragDropEvent.GetScreenSpacePosition());
	}

	return SGraphNode::OnDragOver(MyGeometry, DragDropEvent);
}

void SDataprepGraphTrackNode::OnDragLeave(const FDragDropEvent & DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragActionStepNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if(DragActionStepNodeOp.IsValid())
	{
		TrackWidgetPtr->ResetDragIndicator();
	}

	SGraphNode::OnDragLeave(DragDropEvent);
}

void SDataprepGraphTrackNode::SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel)
{
	ensure(!OwnerGraphPanelPtr.IsValid());

	SGraphNode::SetOwner(OwnerPanel);

	for(TSharedPtr<SDataprepGraphActionNode>& ActionNodePtr : ActionNodes)
	{
		if (ActionNodePtr.IsValid())
		{
			ActionNodePtr->SetOwner(OwnerPanel);
		}
	}
}

FVector2D SDataprepGraphTrackNode::Update(const FVector2D& LocalSize, float ZoomAmount)
{
	if(SDataprepGraphTrackWidget* TrackWidget = TrackWidgetPtr.Get())
	{
		RefreshLayout();

		return TrackWidget->GetWorkingArea();
	}

	return FVector2D(SDataprepGraphTrackWidget::NodeDesiredWidth);
}

void SDataprepGraphTrackNode::OnStartNodeDrag(const TSharedRef<SDataprepGraphActionNode>& ActionNode)
{
	bNodeDragging = true;
	bSkipNextDragUpdate = false;
 
	TrackWidgetPtr->OnStartNodeDrag(ActionNode->GetExecutionOrder());

	LastDragScreenSpacePosition = FSlateApplication::Get().GetCursorPos();
	const FGeometry& DesktopGeometry = GetOwnerPanel()->GetPersistentState().DesktopGeometry;
	FVector2D DragLocalPosition = DesktopGeometry.AbsoluteToLocal( LastDragScreenSpacePosition );

	SGraphPanel* GraphPanel = OwnerGraphPanelPtr.Pin().Get();
	ensure(GraphPanel);

	GraphPanel->Invalidate(EInvalidateWidgetReason::Layout);
}

int32 SDataprepGraphTrackNode::OnEndNodeDrag()
{
	bNodeDragging = false;
	bSkipNextDragUpdate = false;

	SGraphPanel* GraphPanel = OwnerGraphPanelPtr.Pin().Get();
	ensure(GraphPanel);

	GraphPanel->Invalidate(EInvalidateWidgetReason::Layout);

	return 	TrackWidgetPtr->OnEndNodeDrag();
}

void SDataprepGraphTrackNode::OnNodeDragged( TSharedPtr<SDataprepGraphActionNode>& ActionNodePtr, const FVector2D& DragScreenSpacePosition, const FVector2D& ScreenSpaceDelta)
{
	ensure(bNodeDragging);
	ensure(ActionNodePtr.IsValid());

	// This update is most likely due to a call to FSlateApplication::SetCursorPos. Skip it
	if(bSkipNextDragUpdate || ScreenSpaceDelta.X == 0.f)
	{
		bSkipNextDragUpdate = false;
		return;
	}

	TSharedPtr<SGraphPanel> GraphPanel = GetOwnerPanel();
	const FGeometry& DesktopGeometry = GraphPanel->GetCachedGeometry();
	const FVector2D& Size = DesktopGeometry.GetLocalSize();
	const float ZoomAmount = GraphPanel->GetZoomAmount();

	float AbscissaDelta = TrackWidgetPtr->OnNodeDragged(ScreenSpaceDelta.X / ZoomAmount);
		
	FVector2D NewScreenSpaceDelta( AbscissaDelta * ZoomAmount, ScreenSpaceDelta.Y );

	// Request the active panel to scroll if required
	{
		// Keep the mouse at the same position if dragged node has reached when of the ends of the track
		if(AbscissaDelta == 0.f)
		{
			// Do nothing, the mouse's cursor and the dragged node should not move
		}
		// Panel is narrower than track, update panel if dragged node has entered non visible part of track
		else if((TrackWidgetPtr->GetWorkingArea().X * ZoomAmount) > Size.X)
		{
			const FGeometry& NodeGeometry = ActionNodePtr->GetCachedGeometry();
			FVector2D NodeAbsolutePosition = NodeGeometry.LocalToAbsolute(FVector2D(NodeGeometry.GetLocalSize().X, 0.f));
			FVector2D NodeRelativePosition = DesktopGeometry.AbsoluteToLocal(NodeAbsolutePosition);

			if(NodeRelativePosition.X > Size.X)
			{
				FVector2D ViewOffset = GraphPanel->GetViewOffset();
				ViewOffset.X += NodeRelativePosition.X - DesktopGeometry.GetLocalSize().X;
				GraphPanel->RestoreViewSettings(ViewOffset, ZoomAmount);
			}
			else
			{
				// Make sure cursor stays at a constant height
				LastDragScreenSpacePosition.X = DragScreenSpacePosition.X;
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
}

void SDataprepGraphTrackNode::OnControlKeyChanged(bool bControlKeyDown)
{
	if(bNodeDragging)
	{
		if(bControlKeyDown)
		{
			TrackWidgetPtr->OnControlKeyDown(true);
		}
		else
		{
			float MouseOffset = TrackWidgetPtr->OnControlKeyDown(false);

			if(MouseOffset != 0.f)
			{
				// Update mouse position
				LastDragScreenSpacePosition.X -= MouseOffset;

				bSkipNextDragUpdate = true;
				FSlateApplication::Get().SetCursorPos( LastDragScreenSpacePosition );
			}
		}
	}
}

void SDataprepGraphTrackWidget::Construct(const FArguments& InArgs, TSharedPtr<SDataprepGraphTrackNode> InTrackNode)
{
	TrackNodePtr = InTrackNode;
	ensure(TrackNodePtr.IsValid());

	SHorizontalBox::Construct( SHorizontalBox::FArguments()
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SAssignNew( TrackCanvas, SConstraintCanvas )
		]
	);

	CanvasSlot = &TrackCanvas->AddSlot()
	.Anchors( FAnchors( 0.f, 0.f, 0.f, 0.f ) )
	.Offset( FMargin(-20.f, -50.f, 0.f, 0.f) )
	.AutoSize(true)
	.Alignment(FVector2D::ZeroVector)
	.ZOrder(0)
	[
		SNew(SColorBlock)
		.Color( FLinearColor::Transparent )
		.Size( TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP( this, &SDataprepGraphTrackWidget::GetCanvasArea ) ) )
	];

	TrackCanvas->AddSlot()
	.Anchors( FAnchors( 0.f, 0.f, 1.f, 0.f ) )
	.Offset( FMargin(0.f, 10.f, 0.f, 0.f) )
	.AutoSize(true)
	.Alignment(FVector2D::ZeroVector)
	.ZOrder(1)
	[
		SNew(SColorBlock)
		.Color(FDataprepEditorStyle::GetColor( "Graph.TrackInner.BackgroundColor" ))
		.Size( TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP( this, &SDataprepGraphTrackWidget::GetTrackArea ) ) )
	];

	LastDropSlot = INDEX_NONE;

	DropIndicator = SNew(SBorder)
	.BorderBackgroundColor( FDataprepEditorStyle::GetColor( "DataprepActionStep.DragAndDrop" ) )
	.BorderImage(FEditorStyle::GetBrush("Graph.StateNode.Body"))
	.HAlign(EHorizontalAlignment::HAlign_Center)
	[
		SNew(SBox)
		.HeightOverride(TrackDesiredHeight - 2.f)
		.WidthOverride(SlotSpacing - 2.f)
	];

	DropFiller = SNew(SColorBlock)
	.Color(FLinearColor::Transparent)
	.Size( FVector2D(SlotSpacing, TrackDesiredHeight) );

	TSharedPtr<SColorBlock> NullNode = SNew(SColorBlock)
	.Color( FLinearColor::Transparent )
	.Size( FVector2D(NodeDesiredWidth, TrackDesiredHeight) );

	CanvasArea = FVector2D(NodeDesiredWidth, NodeDesiredWidth);

	float LeftOffset = 0.f;
	if(UDataprepAsset* DataprepAsset = InTrackNode->GetDataprepAsset())
	{
		int32 ActionCount = DataprepAsset->GetActionCount();

		TArray<TSharedPtr<SDataprepGraphActionNode>>& InActionNodes = InTrackNode->ActionNodes;

		DropSlots.Reserve(ActionCount + 1);
		ActionSlots.Reserve(ActionCount);
		ActionNodes.Reserve(ActionCount);

		for(int32 Index = 0; Index < ActionCount; ++Index)
		{
			// Add drop slot ahead of action node
			{
				SConstraintCanvas::FSlot& Slot = TrackCanvas->AddSlot()
				.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
				.Offset( FMargin(LeftOffset, 10.f, SlotSpacing, TrackDesiredHeight ) )
				.Alignment(FVector2D::ZeroVector)
				.ZOrder(2)
				[
					DropFiller.ToSharedRef()
				];
				DropSlots.Add( &Slot );
			}
			LeftOffset += SlotSpacing;

			// Add slot to host of action node
			{
				ActionNodes.Add(InActionNodes[Index].IsValid() ? InActionNodes[Index]->AsShared() : NullNode->AsShared());

				SConstraintCanvas::FSlot& Slot = TrackCanvas->AddSlot()
				.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
				.Offset( FMargin(LeftOffset, 0.f, NodeDesiredWidth, NodeDesiredWidth ) )
				.Alignment(FVector2D::ZeroVector)
				.AutoSize(true)
				.ZOrder(2)
				[
					ActionNodes[Index]
				];
				ActionSlots.Add( &Slot );
			}
			LeftOffset += NodeDesiredWidth;
		}
	}

	CanvasArea.X += LeftOffset + TrackDesiredHeight;

	// Add drop slot at end of track
	{
		SConstraintCanvas::FSlot& Slot = TrackCanvas->AddSlot()
		.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
		.Offset( FMargin(LeftOffset, 5.f, SlotSpacing, TrackDesiredHeight ) )
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(2)
		[
			DropFiller.ToSharedRef()
		];
		DropSlots.Add( &Slot );
		LeftOffset += SlotSpacing;
	}

	// Add action slot at end of track
	{
		SConstraintCanvas::FSlot& Slot = TrackCanvas->AddSlot()
		.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
		.Offset( FMargin(LeftOffset, 0.f, NodeDesiredWidth, NodeDesiredWidth ) )
		.Alignment(FVector2D::ZeroVector)
		.AutoSize(true)
		.ZOrder(2)
		[
			SNullWidget::NullWidget
		];
		ActionSlots.Add( &Slot );
	}

	// Add slot to host dragged node
	DragSlot = &TrackCanvas->AddSlot()
	.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
	.Offset( FMargin(LeftOffset, 0.f, NodeDesiredWidth, NodeDesiredWidth ) )
	.Alignment(FVector2D::ZeroVector)
	.AutoSize(true)
	.ZOrder(3)
	[
		SNullWidget::NullWidget
	];

	DragStartIndex = INDEX_NONE;
	DragIndex = INDEX_NONE;
}

void SDataprepGraphTrackWidget::MoveDropSlotTo(int32 SlotIndex)
{
	ensure(DropSlots.IsValidIndex(SlotIndex));
	if(LastDropSlot >= 0)
	{
		(*DropSlots[LastDropSlot])[SNullWidget::NullWidget->AsShared()];
	}

	LastDropSlot = SlotIndex;
	if(LastDropSlot >= 0)
	{
		(*DropSlots[LastDropSlot])[DropIndicator.ToSharedRef()];
	}

	Invalidate(EInvalidateWidgetReason::Layout);
}

void SDataprepGraphTrackWidget::SetActionSlot(int32 SlotIndex, const TSharedRef<SWidget>& Widget)
{
	ensure(ActionSlots.IsValidIndex(SlotIndex));
	(*ActionSlots[SlotIndex])[Widget];

	Invalidate(EInvalidateWidgetReason::Layout);
}

FVector2D SDataprepGraphTrackWidget::ComputeDesiredSize(float InLayoutScaleMultiplier) const
{
	//TrackCanvas->SlatePrepass(InLayoutScaleMultiplier);
	return SHorizontalBox::ComputeDesiredSize(InLayoutScaleMultiplier);
}

FVector2D SDataprepGraphTrackWidget::GetActioNodeAbscissaRange()
{
	FVector2D Range(SlotSpacing);

	if(SDataprepGraphTrackNode* TrackNode = TrackNodePtr.Pin().Get())
	{
		for(int32 Index = 0; Index < ActionNodes.Num() - 1; ++Index)
		{
			Range.Y += GetNodeSize(ActionNodes[Index]).X + SlotSpacing;
		}
	}

	return Range;
}

int32 SDataprepGraphTrackWidget::GetHoveredActionNode(float LeftCorner, float Width)
{
	const float CenterAbscissa = LeftCorner + Width * 0.5f;

	// Compute origin abscissa of last action node on track
	for(int32 Index = 0; Index < ActionNodes.Num(); ++Index)
	{
		const FMargin SlotOffset = ActionSlots[Index]->OffsetAttr.Get(FMargin());
		const float RelativeAbscissa = CenterAbscissa - SlotOffset.Left;
		if(RelativeAbscissa > -SlotSpacing && RelativeAbscissa <= SlotOffset.Right)
		{
			return Index;
		}
	}

	return bIsCopying ? ActionNodes.Num() : ActionNodes.Num() - 1;
}

float SDataprepGraphTrackWidget::ValidateNodeAbscissa(float InAbscissa)
{
	if(ActionSlots.Num() > 0)
	{
		AbscissaRange.X = ActionSlots[0]->OffsetAttr.Get(FVector2D::ZeroVector).Left;
		AbscissaRange.Y = ActionSlots[ActionSlots.Num() - 1]->OffsetAttr.Get(FVector2D::ZeroVector).Left;

		return InAbscissa < AbscissaRange.X ? AbscissaRange.X : (InAbscissa > AbscissaRange.Y ? AbscissaRange.Y : InAbscissa);
	}

	return InAbscissa;
}

void SDataprepGraphTrackWidget::OnActionsOrderChanged(const TArray<TSharedPtr<SDataprepGraphActionNode>>& InActionNodes)
{
	ensure(InActionNodes.Num() == ActionNodes.Num());

	for(int32 Index = 0; Index < ActionNodes.Num(); ++Index)
	{
		ActionNodes[Index] = InActionNodes[Index]->AsShared();
	}

	RefreshLayout(nullptr);
}

void SDataprepGraphTrackWidget::RefreshLayout(SGraphPanel* GraphPanel)
{
	WorkingArea = FVector2D(0.f, TrackDesiredHeight);
	FMargin DropSlotOffset = DropSlots[0]->OffsetAttr.Get(FMargin());

	float MaxWidth = 0.f;

	for(int32 Index = 0; Index < ActionNodes.Num(); ++Index)
	{
		DropSlots[Index]->OffsetAttr.Set(DropSlotOffset);
		DropSlotOffset.Left += SlotSpacing;

		(*DropSlots[Index])[DropFiller.ToSharedRef()];

		TSharedRef<SWidget>& ActionNode = ActionNodes[Index];

		(*ActionSlots[Index])
		[
			ActionNode
		];

		// Update action's proxy node registered to graph panel
		StaticCastSharedRef<SDataprepGraphActionNode>(ActionNode)->UpdateProxyNode(FVector2D(DropSlotOffset.Left, 0.f) + TrackOffset);

		const FVector2D Size = GetNodeSize(ActionNode);

		ActionSlots[Index]->OffsetAttr.Set( FMargin(DropSlotOffset.Left, 0.f, Size.X, Size.Y));
		DropSlotOffset.Left += Size.X;

		if(WorkingArea.Y < Size.Y)
		{
			WorkingArea.Y = Size.Y;
		}

		if(MaxWidth < Size.X)
		{
			MaxWidth = Size.X;
		}
	}

	DropSlots[ActionNodes.Num()]->OffsetAttr.Set(DropSlotOffset);
	(*DropSlots[ActionNodes.Num()])[DropFiller.ToSharedRef()];

	WorkingArea.X = DropSlotOffset.Left + SlotSpacing + MaxWidth/* * 0.5f + 1.f*/;
	WorkingArea.Y += 30.f;

	if(GraphPanel)
	{
		float ZoomAmount = GraphPanel->GetZoomAmount();
		FVector2D PanelSize = GraphPanel->GetCachedGeometry().GetLocalSize() / ZoomAmount;

		CanvasArea.X = PanelSize.X > WorkingArea.X ? PanelSize.X : WorkingArea.X;
		CanvasArea.Y = PanelSize.Y > WorkingArea.Y ? PanelSize.Y : WorkingArea.Y;
		CanvasArea = CanvasArea + FVector2D(40.f, 40.f / ZoomAmount);

		CanvasSlot->OffsetAttr.Set(FMargin(-40.f, -40.f / ZoomAmount, 0.f, 0.f));
	}

	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::ChildOrder);
}

void SDataprepGraphTrackWidget::UpdateLayoutForMove()
{
	// Clear the leftest slot
	ActionSlots[ActionNodes.Num()]->OffsetAttr.Set(FVector2D::ZeroVector);
	(*ActionSlots[ActionNodes.Num()])[SNullWidget::NullWidget];

	// Bring the dragged slot to the last fixed slot if applicable
	FMargin DragSlotOffset = DragSlot->OffsetAttr.Get(FVector2D::ZeroVector);
	if(DragSlotOffset.Left > AbscissaRange.Y)
	{
		DragSlotOffset.Left = AbscissaRange.Y;
		DragSlot->OffsetAttr.Set(DragSlotOffset);
	}

	// Reconstruct action order if dragged node at left end
	if(DragIndex == ActionNodes.Num())
	{
		for(int32 Index = 0; Index < DragStartIndex; ++Index)
		{
			NewActionsOrder[Index] = Index;
		}

		for(int32 Index = DragStartIndex; Index < DragIndex; ++Index)
		{
			NewActionsOrder[Index] = Index + 1;
		}

		--DragIndex;
		NewActionsOrder[DragIndex] = DragStartIndex;
	}

	FMargin DropSlotOffset = DropSlots[0]->OffsetAttr.Get(FMargin());

	for(int32 Index = 0; Index < ActionNodes.Num(); ++Index)
	{
		DropSlots[Index]->OffsetAttr.Set(DropSlotOffset);
		DropSlotOffset.Left += SlotSpacing;

		if(SConstraintCanvas::FSlot* ActionSlot = ActionSlots[Index])
		{
			FVector2D Size;

			if(Index == DragIndex)
			{
				Size = GetNodeSize(DragSlot->GetWidget());

				(*ActionSlot)
					[
						SNew(SColorBlock)
						.Color( FLinearColor::Transparent )
					.Size( Size )
					];
			}
			else
			{
				TSharedRef<SWidget>& ActionNode = ActionNodes[NewActionsOrder[Index]];

				Size = GetNodeSize(ActionNode);

				(*ActionSlot)
					[
						ActionNode
					];
			}

			ActionSlot->OffsetAttr.Set( FMargin(DropSlotOffset.Left, 0.f, Size.X, Size.Y));
			DropSlotOffset.Left += Size.X;
		}
		else
		{
			ensure(false);
		}
	}

	DropSlots[ActionNodes.Num()]->OffsetAttr.Set(DropSlotOffset);
	DropSlotOffset.Left += SlotSpacing;

	ActionSlots[ActionNodes.Num()]->OffsetAttr.Set(DropSlotOffset);
	(*ActionSlots[ActionNodes.Num()])[SNullWidget::NullWidget];
}

void SDataprepGraphTrackWidget::UpdateLayoutForCopy()
{
	const FMargin DragSlotOffset = DragSlot->OffsetAttr.Get(FMargin());
	const int32 HoveredIndex = GetHoveredActionNode( DragSlotOffset.Left, DragSlotOffset.Right);

	FMargin SlotOffset = DropSlots[0]->OffsetAttr.Get(FMargin());

	for(int32 Index = 0; Index < HoveredIndex; ++Index)
	{
		DropSlots[Index]->OffsetAttr.Set(SlotOffset);
		SlotOffset.Left += SlotSpacing;

		SConstraintCanvas::FSlot* ActionSlot = ActionSlots[Index];

		const FVector2D Size = GetNodeSize(ActionNodes[Index]);

		ActionSlot->OffsetAttr.Set(FMargin(SlotOffset.Left, 0.f, Size.X, Size.Y));
		(*ActionSlot)[ActionNodes[Index]];

		SlotOffset.Left += Size.X;
	}

	if(HoveredIndex == ActionNodes.Num())
	{
		DropSlots[ActionNodes.Num()]->OffsetAttr.Set(SlotOffset);
		SlotOffset.Left += SlotSpacing;

		const FVector2D Size = GetNodeSize(DragSlot->GetWidget());

		SConstraintCanvas::FSlot* ActionSlot = ActionSlots[ActionNodes.Num()];
		ActionSlot->OffsetAttr.Set(FMargin(SlotOffset.Left, 0.f, Size.X, Size.Y));
		(*ActionSlot)
		[
			SNew(SColorBlock)
			.Color( FLinearColor::Transparent )
			.Size( Size )
		];
	}
	else
	{
		DropSlots[HoveredIndex]->OffsetAttr.Set(SlotOffset);
		SlotOffset.Left += SlotSpacing;

		FVector2D Size = GetNodeSize(DragSlot->GetWidget());

		(*ActionSlots[HoveredIndex])
		[
			SNew(SColorBlock)
			.Color( FLinearColor::Transparent )
			.Size( Size )
		];

		ActionSlots[HoveredIndex]->OffsetAttr.Set(FMargin(SlotOffset.Left, 0.f, Size.X, Size.Y));
		SlotOffset.Left += Size.X;

		for(int32 Index = HoveredIndex + 1; Index <= ActionNodes.Num(); ++Index)
		{
			DropSlots[Index]->OffsetAttr.Set(SlotOffset);
			SlotOffset.Left += SlotSpacing;

			SConstraintCanvas::FSlot* ActionSlot = ActionSlots[Index];

			Size = GetNodeSize(ActionNodes[Index - 1]);

			ActionSlot->OffsetAttr.Set(FMargin(SlotOffset.Left, 0.f, Size.X, Size.Y));
			(*ActionSlot)[ActionNodes[Index - 1]];

			SlotOffset.Left += Size.X;
		}
	}

	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
}

void SDataprepGraphTrackWidget::OnStartNodeDrag(int32 IndexDragged)
{
	if(!DragSlot)
	{
		ensure(false);
		return;
	}

	ensure(TrackNodePtr.IsValid());
	ensure(ActionSlots.IsValidIndex(IndexDragged));

	NewActionsOrder.SetNum(ActionNodes.Num() + 1);
	for(int32 Index = 0; Index < NewActionsOrder.Num(); ++Index)
	{
		NewActionsOrder[Index] = Index;
	}

	DragStartIndex = IndexDragged;
	DragIndex = IndexDragged;

	FMargin Offset = ActionSlots[IndexDragged]->OffsetAttr.Get(FVector2D::ZeroVector);
	DragSlot->OffsetAttr.Set(Offset);
	(*DragSlot)[ActionNodes[IndexDragged]];

	AbscissaRange.X = ActionSlots[0]->OffsetAttr.Get(FVector2D::ZeroVector).Left;

	const FModifierKeysState ModifierKeyState = FSlateApplication::Get().GetModifierKeys();
	bIsCopying = !(ModifierKeyState.IsControlDown() || ModifierKeyState.IsCommandDown());

	OnControlKeyDown(!bIsCopying);
}

float SDataprepGraphTrackWidget::OnNodeDragged(float DragDelta)
{
	if(!DragSlot)
	{
		ensure(false);
		return 0.f;
	}

	FMargin DragSlotOffset = DragSlot->OffsetAttr.Get(FVector2D::ZeroVector);

	float NewAbscissa = DragSlotOffset.Left + DragDelta;
	 
	bool bValidDrag = (NewAbscissa >= AbscissaRange.X) && (DragSlotOffset.Left < AbscissaRange.Y || NewAbscissa < AbscissaRange.Y);

	if(bValidDrag)
	{
		TSharedPtr<SDataprepGraphActionNode> DraggedActionNode = StaticCastSharedRef<SDataprepGraphActionNode>(DragSlot->GetWidget());
		ensure(DraggedActionNode.IsValid());

		const float CachedNodeAbscissa = DragSlotOffset.Left;

		DragSlotOffset.Left = NewAbscissa < AbscissaRange.X ? AbscissaRange.X : (NewAbscissa > AbscissaRange.Y ? AbscissaRange.Y : NewAbscissa);

		DragSlot->OffsetAttr.Set(DragSlotOffset);
		DraggedActionNode->GetNodeObj()->NodePosX = DragSlotOffset.Left;

		// Check if center of dragged widget is over a neighboring widget by at least half its size
		const int32 NewDragIndex = GetHoveredActionNode(DragSlotOffset.Left, DragSlotOffset.Right);

		if(NewDragIndex != DragIndex)
		{
			ensure( NewActionsOrder.IsValidIndex(NewDragIndex) );

			// Record the swap
			int32 TempOrder = NewActionsOrder[DragIndex];
			NewActionsOrder[DragIndex] = NewActionsOrder[NewDragIndex];
			NewActionsOrder[NewDragIndex] = TempOrder;

			const int32 CachedIndex = DragIndex;
			DragIndex = NewDragIndex;

			if(bIsCopying)
			{
				UpdateLayoutForCopy();
			}
			else
			{
				// Cache action node in new index
				TSharedRef<SWidget> CachedActionNode = ActionSlots[DragIndex]->GetWidget();

				const FVector2D Size = GetNodeSize(DragSlot->GetWidget());

				(*ActionSlots[DragIndex])
				[
					SNew(SColorBlock)
					.Color( FLinearColor::Transparent )
					.Size( Size )
				];

				(*ActionSlots[CachedIndex])
				[
					CachedActionNode
				];
			}
		}

		TrackCanvas->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::ChildOrder);

		return DragSlotOffset.Left - CachedNodeAbscissa;
	}

	return 0.f;
}

int32 SDataprepGraphTrackWidget::OnEndNodeDrag()
{
	if(!DragSlot)
	{
		ensure(false);
		return INDEX_NONE;
	}

	TSharedPtr<SDataprepGraphActionNode> ActionNode = StaticCastSharedRef<SDataprepGraphActionNode>(DragSlot->GetWidget());
	ensure(ActionNode.IsValid());

	bIsCopying = false;
	int32 CachedLastOrder = DragIndex;

	const FVector2D Size = GetNodeSize(ActionNode->AsShared());

	DragSlot->OffsetAttr.Set(FVector2D::ZeroVector);
	(*DragSlot)[SNullWidget::NullWidget];

	DropSlots[ActionNodes.Num()]->OffsetAttr.Set(FVector2D::ZeroVector);
	(*DropSlots[ActionNodes.Num()])[SNullWidget::NullWidget];

	RefreshLayout(nullptr);

	DragStartIndex = INDEX_NONE;
	DragIndex = INDEX_NONE;

	NewActionsOrder.Reset();

	TrackCanvas->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::ChildOrder);

	return CachedLastOrder;
}

void SDataprepGraphTrackWidget::UpdateDragIndicator(FVector2D MousePosition)
{
	ResetDragIndicator();

	float LocalAbscissa = GetCachedGeometry().AbsoluteToLocal(MousePosition).X - 1.f;

	if(LocalAbscissa < DropSlots[0]->OffsetAttr.Get(FMargin()).Left)
	{
		(*DropSlots[0])[DropIndicator.ToSharedRef()];
		return;
	}

	for(int32 Index = 0; Index < ActionNodes.Num(); ++Index)
	{
		FMargin SlotOffset = ActionSlots[Index]->OffsetAttr.Get(FMargin());
		if(LocalAbscissa <= (SlotOffset.Left + SlotOffset.Right * 0.5f))
		{
			(*DropSlots[Index])[DropIndicator.ToSharedRef()];
			return;
		}
	}

	(*DropSlots[ActionNodes.Num()])[DropIndicator.ToSharedRef()];
}

int32 SDataprepGraphTrackWidget::GetDragIndicatorIndex()
{
	for(int32 Index = 0; Index < DropSlots.Num(); ++Index)
	{
		if(DropSlots[Index]->GetWidget() == DropIndicator)
		{
			return Index;
		}
	}

	return -1;
}

float SDataprepGraphTrackWidget::OnControlKeyDown(bool bCtrlDown)
{
	if(bIsCopying == bCtrlDown)
	{
		return 0.f;
	}

	bIsCopying = bCtrlDown;

	float MouseOffset = 0.f;

	if(bIsCopying)
	{
		UpdateLayoutForCopy();

		// Set the maximum move on the right
		const FVector2D DragSize = GetNodeSize(DragSlot->GetWidget());
		AbscissaRange.Y = ActionSlots[ActionNodes.Num()]->OffsetAttr.Get(FVector2D::ZeroVector).Left + 1.f - (DragSize.X * 0.5f);
	}
	else
	{
		AbscissaRange.Y = ActionSlots[ActionNodes.Num() - 1]->OffsetAttr.Get(FVector2D::ZeroVector).Left;

		if(DragSlot->OffsetAttr.Get(FVector2D::ZeroVector).Left > AbscissaRange.Y)
		{
			// Update offset of DragSlot
			FMargin DragSlotOffset = DragSlot->OffsetAttr.Get(FVector2D::ZeroVector);

			MouseOffset = DragSlotOffset.Left - AbscissaRange.Y;

			DragSlotOffset.Left = AbscissaRange.Y;
			DragSlot->OffsetAttr.Set(DragSlotOffset);
		}

		UpdateLayoutForMove();
	}

	TrackCanvas->Invalidate(EInvalidateWidgetReason::ChildOrder | EInvalidateWidgetReason::Layout);

	return MouseOffset;
}

bool SDataprepGraphTrackWidget::IsDraggedNodeOnEnds()
{
	if(DragSlot)
	{
		const FMargin DragSlotOffset = DragSlot->OffsetAttr.Get(FVector2D::ZeroVector);
		return DragSlotOffset.Left == AbscissaRange.X || DragSlotOffset.Left == AbscissaRange.Y;
	}

	ensure(false);
	return false;
}

FVector2D SDataprepGraphTrackWidget::GetNodeSize(const TSharedRef<SWidget>& Widget) const
{
	FVector2D Size = Widget->GetCachedGeometry().GetLocalSize();

	if(Size == FVector2D::ZeroVector)
	{
		Size = Widget->GetDesiredSize();
		if(Size == FVector2D::ZeroVector)
		{
			Size.Set(NodeDesiredWidth, NodeDesiredWidth);
		}
	}

	return Size;
}

#undef LOCTEXT_NAMESPACE