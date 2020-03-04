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
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "DataprepGraphEditor"

FVector2D SDataprepGraphTrackNode::TrackAnchor( 15.f, 40.f );

class SDataprepEmptyActionNode : public SVerticalBox
{
public:
	SLATE_BEGIN_ARGS(SDataprepEmptyActionNode){}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs)
	{
		SVerticalBox::Construct(SVerticalBox::FArguments());

		AddSlot()
		.AutoHeight()
		.Padding(FDataprepEditorStyle::GetMargin( "DataprepActionStep.Padding" ))
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			.Padding(10.f)
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SNew(SBox)
				.MinDesiredWidth(250.f)
			]

			+ SOverlay::Slot()
			.Padding(0.f)
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SNew(SImage)
				.ColorAndOpacity(FDataprepEditorStyle::GetColor("DataprepAction.EmptyStep.Outer.Hovered"))
				.Image(FEditorStyle::GetBrush( "Graph.StateNode.Body" ))
			]

			+ SOverlay::Slot()
			.Padding(1.f)
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SNew(SImage)
				.ColorAndOpacity(FDataprepEditorStyle::GetColor("DataprepAction.EmptyStep.Background.Hovered"))
				.Image(FEditorStyle::GetBrush( "Graph.StateNode.Body" ))
			]

			+ SOverlay::Slot()
			.Padding(10.f)
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Fill)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DataprepEmptyActionLabel", "+ Add Action"))
					.TextStyle( &FDataprepEditorStyle::GetWidgetStyle<FTextBlockStyle>( "DataprepAction.TitleTextBlockStyle" ) )
					.ColorAndOpacity(FDataprepEditorStyle::GetColor("DataprepAction.EmptyStep.Text.Hovered"))
					.Justification(ETextJustify::Center)
				]
			]
		];
	}
};

class SDataprepGraphTrackWidget : public SConstraintCanvas
{
public:
	SLATE_BEGIN_ARGS(SDataprepGraphTrackWidget) {}
	SLATE_END_ARGS();


	void Construct(const FArguments& InArgs, TSharedPtr<SDataprepGraphTrackNode> InTrackNode);

	void SetActionSlot(int32 SlotIndex, const TSharedRef<SWidget>& Widget);

	const FVector2D& GetWorkingSize() const
	{
		return WorkingSize;
	}

	FVector2D GetWorkingSize()
	{
		return WorkingSize;
	}

	FVector2D GetTrackArea()
	{
		return FVector2D(WorkingSize.X, TrackDesiredHeight);
	}

	FVector2D GetCanvasArea() const
	{
		return FVector2D(CanvasOffset.Right, CanvasOffset.Bottom);
	}

	FVector2D GetLineSize() const
	{
		return LineSize;
	}

	FVector2D GetActioNodeAbscissaRange();

	int32 GetHoveredActionNode(float Left, float Width);

	float GetActionNodeAbscissa(int32 Index)
	{
		return ActionSlots.IsValidIndex(Index) ? ActionSlots[Index]->OffsetAttr.Get(FMargin()).Left : 0.f;
	}

	FVector2D GetNodeSize(const TSharedRef<SWidget>& Widget) const;

	void RefreshLayout();

	void OnStartNodeDrag(int32 Index);

	float OnNodeDragged(float Delta);

	int32 OnEndNodeDrag();

	bool IsDraggedNodeOnEnds();

	/**
	*/
	void UpdateLayoutOnDrag(int8 DragDirection = 0);

	/** Toggles display of action nodes between copy vs move drop modes */
	float OnControlKeyDown(bool bKeyDown);

	void UpdateDragIndicator(FVector2D MousePosition);

	int32 GetDragIndicatorIndex() { return DragIndicatorIndex; }

	void ResetDragIndicator()
	{
		DragIndicatorIndex = INDEX_NONE;

		FMargin SlotOffset = ActionSlots[SlotCount]->OffsetAttr.Get(FMargin());
		TrackSlots[SlotCount]->OffsetAttr.Set( FMargin(SlotOffset.Left + SDataprepGraphActionNode::DefaultWidth * 0.5f - 16.f, LineTopPadding + TrackSlotTopOffset, 32, 32));
		TrackSlots[SlotCount]->AttachWidget(SNullWidget::NullWidget);

		UpdateLine(SlotCount-1);

	}

	FVector2D UpdateActionSlot(FMargin& SlotOffset, int32 SlotIndex, const TSharedRef<SWidget>& ActionNode, bool bIsEmpty = false)
	{
		DropSlots[SlotIndex]->OffsetAttr.Set(SlotOffset);
		SlotOffset.Left += InterSlotSpacing;

		const float NodeTopPadding = LineTopPadding + NodeTopOffset;
		const float TrackSlotTopPadding = LineTopPadding + TrackSlotTopOffset;

		SConstraintCanvas::FSlot* ActionSlot = ActionSlots[SlotIndex];

		FVector2D Size = GetNodeSize(ActionNode);

		ActionSlot->OffsetAttr.Set(FMargin(SlotOffset.Left, NodeTopPadding, Size.X, Size.Y));
		ActionSlot->AttachWidget(bIsEmpty ? SNew(SColorBlock).Color(FLinearColor::Transparent).Size( Size ) : ActionNode);

		TrackSlots[SlotIndex]->OffsetAttr.Set( FMargin(SlotOffset.Left + Size.X * 0.5f - 16.f, TrackSlotTopPadding, 32, 32));
		TrackSlots[SlotIndex]->AttachWidget(SlotIndex == HoveredDragIndex ? TrackSlotDrop.ToSharedRef() : TrackSlotRegular.ToSharedRef());

		SlotOffset.Left += Size.X;

		return Size + FVector2D(0.f, NodeTopPadding);
	}

	void OnActionsOrderChanged(const TArray<TSharedPtr<SDataprepGraphActionNode>>& InActionNodes);

	void CreateHelperWidgets();

	void GetDraggedNodePositions(FVector2D& Left, FVector2D& Right, const FGeometry& TargetGeometry);

	void UpdateLine(int32 LastIndex);

	TWeakPtr<SDataprepGraphTrackNode> TrackNodePtr;
	TSharedPtr<SConstraintCanvas> TrackCanvas;
	TArray<TSharedRef<SWidget>> ActionNodes;
	TArray<SConstraintCanvas::FSlot*> DropSlots;
	TArray<SConstraintCanvas::FSlot*> ActionSlots;
	TArray<SConstraintCanvas::FSlot*> TrackSlots;
	SConstraintCanvas::FSlot* CanvasSlot;
	SConstraintCanvas::FSlot* LineSlot;

	FVector2D WorkingSize;
	FMargin CanvasOffset;
	FVector2D LineSize;
	int32 LastDropSlot;

	TSharedPtr<SWidget> DropIndicator;
	TSharedPtr<SColorBlock> DropFiller;
	TSharedPtr<SWidget> TrackSlotRegular;
	TSharedPtr<SWidget> TrackSlotDrop;
	TSharedPtr<SWidget> TrackSlotSelected;
	TSharedPtr<SDataprepEmptyActionNode> DummyAction;

	/** Number of visible slots. THis number can only change when dragging actions */
	int32 SlotCount;

	SConstraintCanvas::FSlot* DragSlot;
	int32 DragStartIndex;
	int32 DragIndex;
	int32 HoveredDragIndex;
	int32 DragIndicatorIndex;
	FVector2D LastDragPosition;
	FVector2D AbscissaRange;
	FVector2D TrackOffset;
	bool bIsCopying;

	static float TrackDesiredHeight;
	static float InterSlotSpacing;
	static float LineTopPadding;
	static float NodeTopOffset;
	static float TrackSlotTopOffset;

	friend SDataprepGraphTrackNode;
};

float SDataprepGraphTrackWidget::InterSlotSpacing = 16.f;
float SDataprepGraphTrackWidget::TrackDesiredHeight = 40.f;
float SDataprepGraphTrackWidget::LineTopPadding = 10.f;
float SDataprepGraphTrackWidget::NodeTopOffset = 25.f;
float SDataprepGraphTrackWidget::TrackSlotTopOffset = -15.f;

void SDataprepGraphTrackNode::Construct(const FArguments& InArgs, UDataprepGraphRecipeNode* InNode)
{
	NodeFactory = InArgs._NodeFactory;

	bNodeDragging = false;
	SetCursor(EMouseCursor::Default);
	GraphNode = InNode;
	check(GraphNode);

	UDataprepGraph* DataprepGraph = Cast<UDataprepGraph>(GraphNode->GetGraph());
	check(DataprepGraph);

	DataprepAssetPtr = DataprepGraph->GetDataprepAsset();
	check(DataprepAssetPtr.IsValid());

	SNodePanel::SNode::FNodeSet NodeFilter;
	SGraphNode::MoveTo( TrackAnchor, NodeFilter);

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
				TSharedPtr<SGraphNode> ActionGraphNode;
				if ( FGraphNodeFactory* GraphNodeFactor = NodeFactory.Get() )
				{
					ActionGraphNode = GraphNodeFactor->CreateNodeWidget( NewActionNode );
				}
				else
				{
					ActionGraphNode = FNodeFactory::CreateNodeWidget( NewActionNode );
				}

				TSharedPtr< SDataprepGraphActionNode > ActionWidgetPtr = StaticCastSharedPtr<SDataprepGraphActionNode>( ActionGraphNode );
				if(SDataprepGraphActionNode* ActionWidget = ActionWidgetPtr.Get())
				{
					if(GraphPanelPtr.IsValid())
					{
						ActionWidget->SetOwner(GraphPanelPtr.ToSharedRef());
					}

					ActionWidget->UpdateGraphNode();

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

		TrackWidgetPtr->RefreshLayout();

		return true;
	}

	return false;
}

void SDataprepGraphTrackNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter)
{
	// Block track node to specific position
	SGraphNode::MoveTo( TrackAnchor, NodeFilter);
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

	TSharedPtr<FDataprepDragDropOp> DragActionNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if(DragActionNodeOp.IsValid())
	{
		int32 InsertIndex = TrackWidgetPtr->GetDragIndicatorIndex();
		TrackWidgetPtr->ResetDragIndicator();

		DragActionNodeOp->DoDropOnTrack(DataprepAssetPtr.Get(), InsertIndex);

		return FReply::Unhandled();
	}

	return SGraphNode::OnDrop(MyGeometry, DragDropEvent);
}

FReply SDataprepGraphTrackNode::OnDragOver(const FGeometry & MyGeometry, const FDragDropEvent & DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragActionNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if(DragActionNodeOp.IsValid())
	{
		DragActionNodeOp->SetHoveredNode(GraphNode);
		TrackWidgetPtr->UpdateDragIndicator(DragDropEvent.GetScreenSpacePosition());
	}

	return SGraphNode::OnDragOver(MyGeometry, DragDropEvent);
}

void SDataprepGraphTrackNode::OnDragLeave(const FDragDropEvent & DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragActionNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if(DragActionNodeOp.IsValid())
	{
		TrackWidgetPtr->ResetDragIndicator();
		TrackWidgetPtr->RefreshLayout();
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

FSlateRect SDataprepGraphTrackNode::Update()
{
	FSlateRect WorkingArea;

	if(SDataprepGraphTrackWidget* TrackWidget = TrackWidgetPtr.Get())
	{
		RefreshLayout();

		SGraphPanel& GraphPanel = *OwnerGraphPanelPtr.Pin();

		const FVector2D& WorkingSize = TrackWidget->WorkingSize;

		// Determine canvas offset attribute in track widget's coordinates
		const FVector2D PanelSize = GraphPanel.GetTickSpaceGeometry().GetLocalSize() / GraphPanel.GetZoomAmount();

		const FVector2D TrackPosition = GetPosition().GetAbs();
		const FVector2D TargetSize = FVector2D(FMath::Max(WorkingSize.X, PanelSize.X), FMath::Max(WorkingSize.Y, PanelSize.Y)) + TrackPosition + 20.f;

		FMargin& CanvasOffset = TrackWidgetPtr->CanvasOffset;

		CanvasOffset.Left = -TrackPosition.X - 10.f;
		CanvasOffset.Top = -TrackPosition.Y - 10.f;

		if(TargetSize.X > CanvasOffset.Right)
		{
			CanvasOffset.Right = TargetSize.X;
		}

		if(TargetSize.Y > CanvasOffset.Bottom)
		{
			CanvasOffset.Bottom = TargetSize.Y;
		}

		TrackWidgetPtr->CanvasSlot->OffsetAttr.Set(CanvasOffset);

		WorkingArea = FSlateRect(FVector2D::ZeroVector, WorkingSize + TrackPosition);
	}

	return WorkingArea;
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
	const FVector2D& PanelSize = DesktopGeometry.GetLocalSize();
	const float ZoomAmount = GraphPanel->GetZoomAmount();
	const FVector2D& TrackSize = TrackWidgetPtr->GetWorkingSize() * ZoomAmount;

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
		else if(TrackSize.X > PanelSize.X)
		{
			FVector2D LeftPosition;
			FVector2D RightPosition;
			TrackWidgetPtr->GetDraggedNodePositions(LeftPosition, RightPosition, DesktopGeometry);

			if(RightPosition.X > PanelSize.X)
			{
				FVector2D ViewOffset = GraphPanel->GetViewOffset();
				ViewOffset.X += RightPosition.X - PanelSize.X + 10.f;
				GraphPanel->RestoreViewSettings(ViewOffset, ZoomAmount);
			}
			else if(LeftPosition.X < 0.f)
			{
				FVector2D ViewOffset = GraphPanel->GetViewOffset();
				ViewOffset.X += LeftPosition.X - 10.f;
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

void SDataprepGraphTrackNode::RequestViewportPan(const FVector2D& InScreenSpacePosition)
{
	// Note: Code copied from SNodePanel::RequestDeferredPan and its subsequent calls. Calling SNodePanel::RequestDeferredPan is not an option
	//		 since it changes the view offset during the call to OnPaint which bypasses the adjustments done by the SDataprepGraphEditor in
	//		 SDataprepGraphEditor::Tick 
	if(SGraphPanel* GraphPanel = OwnerGraphPanelPtr.Pin().Get())
	{
		// How quickly to ramp up the pan speed as the user moves the mouse further past the edge of the graph panel.
		constexpr float EdgePanSpeedCoefficient = 2.f;
		constexpr float EdgePanSpeedPower = 0.6f;
		// Never pan faster than this - probably not really required since we raise to a power of 0.6
		constexpr float MaxPanSpeed = 200.0f;
		// Start panning before we reach the edge of the graph panel.
		constexpr float EdgePanForgivenessZone = 30.0f;

		//const FVector2D PanAmount = ComputeEdgePanAmount( GraphPanel->GetTickSpaceGeometry(), InScreenSpacePosition ) / GraphPanel->GetZoomAmount();
		const FGeometry& PanelGeometry = GraphPanel->GetTickSpaceGeometry();
		const FVector2D PanelPosition = PanelGeometry.AbsoluteToLocal( InScreenSpacePosition );
		const FVector2D PanelSize = PanelGeometry.GetLocalSize();
		const float ZoomAmount = GraphPanel->GetZoomAmount();

		FVector2D PanAmount(FVector2D::ZeroVector);
		if ( PanelPosition.X <= EdgePanForgivenessZone )
		{
			PanAmount.X = FMath::Max( -MaxPanSpeed, EdgePanSpeedCoefficient * -FMath::Pow(EdgePanForgivenessZone - PanelPosition.X, EdgePanSpeedPower) );
		}
		else if( PanelPosition.X >= PanelSize.X - EdgePanForgivenessZone )
		{
			PanAmount.X = FMath::Min( MaxPanSpeed, EdgePanSpeedCoefficient * FMath::Pow(PanelPosition.X - PanelSize.X + EdgePanForgivenessZone, EdgePanSpeedPower) );
		}

		if ( PanelPosition.Y <= EdgePanForgivenessZone )
		{
			PanAmount.Y = FMath::Max( -MaxPanSpeed, EdgePanSpeedCoefficient * -FMath::Pow(EdgePanForgivenessZone - PanelPosition.Y, EdgePanSpeedPower) );
		}
		else if( PanelPosition.Y >= PanelSize.Y - EdgePanForgivenessZone )
		{
			PanAmount.Y = FMath::Min( MaxPanSpeed, EdgePanSpeedCoefficient * FMath::Pow(PanelPosition.Y - PanelSize.Y + EdgePanForgivenessZone, EdgePanSpeedPower) );
		}

		if(PanAmount != FVector2D::ZeroVector)
		{
			GraphPanel->RestoreViewSettings(GraphPanel->GetViewOffset() + PanAmount / ZoomAmount, ZoomAmount);
		}
	}
}

void SDataprepGraphTrackNode::RequestRename(const UEdGraphNode* Node)
{
	if(SGraphPanel* GraphPanel = OwnerGraphPanelPtr.Pin().Get())
	{
		if(const UDataprepGraphActionNode* ActionEdNode = Cast<UDataprepGraphActionNode>(Node))
		{
			int32 ActionIndex = ActionEdNode->GetExecutionOrder();
			if(ActionNodes.IsValidIndex(ActionIndex))
			{
				TSharedPtr<SDataprepGraphActionNode>& ActionNode = ActionNodes[ActionIndex];

				if(ActionNode.IsValid() && !GraphPanel->HasMouseCapture())
				{
					FSlateRect TitleRect = ActionNode->GetTitleRect();
					const FVector2D TopLeft = FVector2D( TitleRect.Left, TitleRect.Top );
					const FVector2D BottomRight = FVector2D( TitleRect.Right, TitleRect.Bottom );

					bool bTitleVisible = GraphPanel->IsRectVisible( TopLeft, BottomRight );
					if( !bTitleVisible )
					{
						bTitleVisible = GraphPanel->JumpToRect( TopLeft, BottomRight );
					}

					if( bTitleVisible )
					{
						ActionNode->RequestRename();
						//GraphPanel->JumpToNode(Node, false, true);
						ActionNode->ApplyRename();
					}
				}
			}
		}
	}
}

void SDataprepGraphTrackWidget::Construct(const FArguments& InArgs, TSharedPtr<SDataprepGraphTrackNode> InTrackNode)
{
	TrackNodePtr = InTrackNode;
	ensure(TrackNodePtr.IsValid());

	SConstraintCanvas::Construct( SConstraintCanvas::FArguments());

	CreateHelperWidgets();

	const FLinearColor TrackColor = FDataprepEditorStyle::GetColor( "DataprepAction.BackgroundColor" );

	CanvasSlot = &AddSlot()
	.Anchors( FAnchors( 0.f, 0.f, 0.f, 0.f ) )
	.Offset( FMargin(-25.f, -25.f, 0.f, 0.f) )
	.AutoSize(false)
	.Alignment(FVector2D::ZeroVector)
	.ZOrder(0)
	[
		SNew(SColorBlock)
		.Color( FDataprepEditorStyle::GetColor("Dataprep.Background.Black") )
		.Size( TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP( this, &SDataprepGraphTrackWidget::GetCanvasArea ) ) )
	];

	LineSlot = &AddSlot()
	.Anchors( FAnchors( 0.f, 0.f, 0.f, 0.f ) )
	.Offset( FMargin(InterSlotSpacing +  3.f, LineTopPadding - 2.f, 0.f, 0.f) )
	.AutoSize(true)
	.Alignment(FVector2D::ZeroVector)
	.ZOrder(0)
	[
		SNew(SColorBlock)
		.Color( TrackColor )
		.Size( TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP( this, &SDataprepGraphTrackWidget::GetLineSize ) ) )
	];

	AddSlot()
	.Anchors( FAnchors( 0.f, 0.f, 1.f, 0.f ) )
	.Offset( FMargin(0.f, 10.f, 0.f, 0.f) )
	.AutoSize(true)
	.Alignment(FVector2D::ZeroVector)
	.ZOrder(1)
	[
		SNew(SColorBlock)
		.Color(FLinearColor::Transparent)
		.Size( TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP( this, &SDataprepGraphTrackWidget::GetTrackArea ) ) )
	];

	LastDropSlot = INDEX_NONE;

	TSharedPtr<SColorBlock> NullNode = SNew(SColorBlock)
	.Color( FLinearColor::Transparent )
	.Size( FVector2D(SDataprepGraphActionNode::DefaultWidth, TrackDesiredHeight) );

	const FVector2D DefaultNodeSize(SDataprepGraphActionNode::DefaultWidth, SDataprepGraphActionNode::DefaultHeight);

	CanvasOffset = FMargin(FVector4(FVector2D::ZeroVector, DefaultNodeSize));

	const float NodeTopPadding = LineTopPadding + NodeTopOffset;
	const float TrackSlotTopPadding = LineTopPadding + TrackSlotTopOffset;

	UDataprepAsset* DataprepAsset = InTrackNode->GetDataprepAsset();
	SlotCount = DataprepAsset ? DataprepAsset->GetActionCount() : 0;

	DropSlots.Reserve(SlotCount + 2);
	ActionSlots.Reserve(SlotCount + 1);
	TrackSlots.Reserve(SlotCount + 2);
	ActionNodes.Reserve(SlotCount);

	float LeftOffset = 0.f;
	if(DataprepAsset)
	{
		TArray<TSharedPtr<SDataprepGraphActionNode>>& InActionNodes = InTrackNode->ActionNodes;

		for(int32 Index = 0; Index < SlotCount; ++Index)
		{
			// Add drop slot ahead of action node
			{
				SConstraintCanvas::FSlot& Slot = AddSlot()
				.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
				.Offset( FMargin(LeftOffset, NodeTopPadding + 5.f, InterSlotSpacing, TrackDesiredHeight ) )
				.Alignment(FVector2D::ZeroVector)
				.ZOrder(2)
				[
					DropFiller.ToSharedRef()
				];
				DropSlots.Add( &Slot );
			}
			LeftOffset += InterSlotSpacing;

			const FVector2D Size = DefaultNodeSize;

			// Add slot to host of action node
			{
				TSharedRef<SWidget>& ActionNode = ActionNodes.Add_GetRef(InActionNodes[Index].IsValid() ? InActionNodes[Index]->AsShared() : NullNode->AsShared());

				SConstraintCanvas::FSlot& Slot = AddSlot()
				.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
				.Offset( FMargin(LeftOffset, NodeTopPadding, Size.X, Size.Y ) )
				.Alignment(FVector2D::ZeroVector)
				.AutoSize(true)
				.ZOrder(2)
				[
					ActionNode
				];
				ActionSlots.Add( &Slot );
			}

			// Add track slot which is at the middle of the corresponding action
			{
				SConstraintCanvas::FSlot& Slot = AddSlot()
				.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
				.Offset( FMargin(LeftOffset + SDataprepGraphActionNode::DefaultWidth * 0.5f + 16.f, TrackSlotTopPadding, 32, 32 ) )
				.Alignment(FVector2D::ZeroVector)
				.AutoSize(true)
				.ZOrder(2)
				[
					TrackSlotRegular.ToSharedRef()
				];
				TrackSlots.Add( &Slot );
			}
			LeftOffset += Size.X;

			CanvasOffset.Bottom = CanvasOffset.Bottom < Size.Y ? Size.Y : CanvasOffset.Bottom;
		}
	}
	else
	{
		SlotCount = 0;
	}

	// Add drop slot at end of track
	{
		SConstraintCanvas::FSlot& Slot = AddSlot()
		.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
		.Offset( FMargin(LeftOffset, NodeTopPadding + 5.f, InterSlotSpacing, TrackDesiredHeight ) )
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(2)
		[
			SNullWidget::NullWidget
		];
		DropSlots.Add( &Slot );
		LeftOffset += InterSlotSpacing;
	}

	// Add dummy action slot at end of track
	{
		SConstraintCanvas::FSlot& Slot = AddSlot()
		.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
		.Offset( FMargin(LeftOffset, TrackDesiredHeight, SDataprepGraphActionNode::DefaultWidth, SDataprepGraphActionNode::DefaultWidth ) )
		.Alignment(FVector2D::ZeroVector)
		.AutoSize(true)
		.ZOrder(2)
		[
			SNullWidget::NullWidget
		];
		ActionSlots.Add( &Slot );
	}

	// Add track slot which is at the middle of the corresponding action
	{
		SConstraintCanvas::FSlot& Slot = AddSlot()
		.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
		.Offset( FMargin(LeftOffset + SDataprepGraphActionNode::DefaultWidth * 0.5f + 16.f, TrackSlotTopPadding, 32, 32 ) )
		.Alignment(FVector2D::ZeroVector)
		.AutoSize(true)
		.ZOrder(2)
		[
			SNullWidget::NullWidget
		];
		TrackSlots.Add( &Slot );
	}
	LeftOffset += SDataprepGraphActionNode::DefaultWidth;

	// Add drop slot at end of track
	{
		SConstraintCanvas::FSlot& Slot = AddSlot()
		.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
		.Offset( FMargin(LeftOffset, NodeTopPadding + 5.f, InterSlotSpacing, TrackDesiredHeight ) )
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(2)
		[
			SNullWidget::NullWidget
		];
		DropSlots.Add( &Slot );
		LeftOffset += InterSlotSpacing;
	}

	// Add slot to host dragged node???
	DragSlot = &AddSlot()
	.Anchors( FAnchors( 0., 0.f, 0.f, 0.f ) )
	.Offset( FMargin(LeftOffset, 0.f, SDataprepGraphActionNode::DefaultWidth, SDataprepGraphActionNode::DefaultWidth ) )
	.Alignment(FVector2D::ZeroVector)
	.AutoSize(true)
	.ZOrder(3)
	[
		SNullWidget::NullWidget
	];

	WorkingSize = FVector2D::ZeroVector;
	CanvasOffset.Right = LeftOffset;

	HoveredDragIndex = INDEX_NONE;
	DragStartIndex = INDEX_NONE;
	DragIndex = INDEX_NONE;
	DragIndicatorIndex = INDEX_NONE;
}

void SDataprepGraphTrackWidget::CreateHelperWidgets()
{
	const FLinearColor TrackColor = FDataprepEditorStyle::GetColor( "DataprepAction.BackgroundColor" );
	const FLinearColor DragAndDropColor = FDataprepEditorStyle::GetColor( "DataprepAction.DragAndDrop" );

	DropIndicator = SNew(SBorder)
		.BorderBackgroundColor( DragAndDropColor )
		.BorderImage(FEditorStyle::GetBrush("Graph.StateNode.Body"))
		.HAlign(EHorizontalAlignment::HAlign_Center)
		[
			SNew(SBox)
			.HeightOverride(TrackDesiredHeight - 2.f)
			.WidthOverride(InterSlotSpacing - 2.f)
		];

	DropFiller = SNew(SColorBlock)
		.Color(FLinearColor::Transparent)
		.Size( FVector2D(InterSlotSpacing, TrackDesiredHeight) );

	TrackSlotRegular = SNew(SOverlay)
		+SOverlay::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		.Padding(0.f)
		[
			SNew(SBox)
			.WidthOverride(32.f)
			.HeightOverride(32.f)
		]
		+SOverlay::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		.Padding(0.f)
		[
			SNew(SImage)
			.ColorAndOpacity(TrackColor)
			.Image(FDataprepEditorStyle::GetBrush( "DataprepEditor.TrackNode.Slot" ))
		];

	TrackSlotDrop = SNew(SOverlay)
		+SOverlay::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		.Padding(0.f)
		[
			SNew(SBox)
			.WidthOverride(32.f)
			.HeightOverride(32.f)
		]
		+SOverlay::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		.Padding(0.f)
		[
			SNew(SImage)
			.ColorAndOpacity(DragAndDropColor)
			.Image(FDataprepEditorStyle::GetBrush( "DataprepEditor.TrackNode.Slot" ))
		];

	DummyAction = SNew(SDataprepEmptyActionNode);
}

void SDataprepGraphTrackWidget::GetDraggedNodePositions(FVector2D& Left, FVector2D& Right, const FGeometry& TargetGeometry)
{
	const FMargin DragSlotOffset = DragSlot->OffsetAttr.Get(FMargin());
	const FGeometry& Geometry = GetCachedGeometry();

	Left = TargetGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D(DragSlotOffset.Left, 0.f)));
	Right = TargetGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D(DragSlotOffset.Left + DragSlotOffset.Right, 0.f)));
}

void SDataprepGraphTrackWidget::SetActionSlot(int32 SlotIndex, const TSharedRef<SWidget>& Widget)
{
	ensure(ActionSlots.IsValidIndex(SlotIndex));
	ActionSlots[SlotIndex]->AttachWidget(Widget);

	Invalidate(EInvalidateWidgetReason::Layout);
}

FVector2D SDataprepGraphTrackWidget::GetActioNodeAbscissaRange()
{
	FVector2D Range(InterSlotSpacing, InterSlotSpacing + SDataprepGraphActionNode::DefaultWidth + InterSlotSpacing);

	if(SDataprepGraphTrackNode* TrackNode = TrackNodePtr.Pin().Get())
	{
		Range.Y = ActionSlots[SlotCount-1]->OffsetAttr.Get(FMargin()).Left;
	}

	return Range;
}

int32 SDataprepGraphTrackWidget::GetHoveredActionNode(float LeftCorner, float Width)
{
	const float CenterAbscissa = LeftCorner + Width * 0.5f;

	// Compute origin abscissa of last action node on track
	for(int32 Index = 0; Index < SlotCount; ++Index)
	{
		const FMargin SlotOffset = DropSlots[Index]->OffsetAttr.Get(FMargin());
		const float RelativeAbscissa = CenterAbscissa - (SlotOffset.Left + SlotOffset.Right);
		const float ActionSlotWidth = ActionSlots[Index]->OffsetAttr.Get(FMargin()).Right;
		if(RelativeAbscissa > -SlotOffset.Right && RelativeAbscissa <= ActionSlotWidth)
		{
			return Index;
		}
	}

	return SlotCount - 1;
}

void SDataprepGraphTrackWidget::OnActionsOrderChanged(const TArray<TSharedPtr<SDataprepGraphActionNode>>& InActionNodes)
{
	ensure(InActionNodes.Num() == ActionNodes.Num());

	for(int32 Index = 0; Index < ActionNodes.Num(); ++Index)
	{
		ActionNodes[Index] = InActionNodes[Index]->AsShared();
	}

	RefreshLayout();
}

void SDataprepGraphTrackWidget::RefreshLayout()
{
	const float NodeTopPadding = LineTopPadding + NodeTopOffset;

	LineSize = FVector2D::ZeroVector;
	WorkingSize = FVector2D(SDataprepGraphActionNode::DefaultWidth, SDataprepGraphActionNode::DefaultHeight);
	FMargin DropSlotOffset = DropSlots[0]->OffsetAttr.Get(FMargin());

	float MaxWidth = SDataprepGraphActionNode::DefaultWidth;

	for(int32 Index = 0; Index < SlotCount; ++Index)
	{
		// Update action's proxy node registered to graph panel
		StaticCastSharedRef<SDataprepGraphActionNode>(ActionNodes[Index])->UpdateProxyNode(FVector2D(DropSlotOffset.Left + InterSlotSpacing, NodeTopPadding) + TrackOffset);

		const FVector2D Size = UpdateActionSlot(DropSlotOffset, Index, ActionNodes[Index]);

		if(WorkingSize.Y < Size.Y)
		{
			WorkingSize.Y = Size.Y;
		}

		if(MaxWidth < Size.X)
		{
			MaxWidth = Size.X;
		}
	}

	DropSlots[SlotCount]->OffsetAttr.Set(DropSlotOffset);
	DropSlots[SlotCount]->AttachWidget(SNullWidget::NullWidget);
	DropSlotOffset.Left += InterSlotSpacing;

	ActionSlots[SlotCount]->OffsetAttr.Set( FMargin(DropSlotOffset.Left, NodeTopPadding, 0., 0.));
	ActionSlots[SlotCount]->AttachWidget(SNullWidget::NullWidget);

	TrackSlots[SlotCount]->OffsetAttr.Set( FMargin(DropSlotOffset.Left + SDataprepGraphActionNode::DefaultWidth * 0.5f - 16.f, LineTopPadding + TrackSlotTopOffset, 32, 32));
	TrackSlots[SlotCount]->AttachWidget(SNullWidget::NullWidget);

	WorkingSize.X = DropSlotOffset.Left + SDataprepGraphActionNode::DefaultWidth + InterSlotSpacing;
	WorkingSize.Y += NodeTopPadding;

	UpdateLine(SlotCount-1);

	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::ChildOrder);
}

void SDataprepGraphTrackWidget::UpdateLayoutOnDrag(int8 DragDirection)
{
	FMargin SlotOffset = DropSlots[0]->OffsetAttr.Get(FMargin());

	if(bIsCopying)
	{
		for(int32 Index = 0; Index < HoveredDragIndex; ++Index)
		{
			UpdateActionSlot(SlotOffset, Index, ActionNodes[Index]);
		}

		UpdateActionSlot(SlotOffset, HoveredDragIndex, DragSlot->GetWidget(), true);

		for(int32 Index = HoveredDragIndex + 1; Index < SlotCount; ++Index)
		{
			UpdateActionSlot(SlotOffset, Index, ActionNodes[Index-1]);
		}
	}
	else if(HoveredDragIndex < DragStartIndex)
	{
		for(int32 Index = 0; Index < HoveredDragIndex; ++Index)
		{
			UpdateActionSlot(SlotOffset, Index, ActionNodes[Index]);
		}

		UpdateActionSlot(SlotOffset, HoveredDragIndex, DragSlot->GetWidget(), true);

		for(int32 Index = HoveredDragIndex + 1; Index <= DragStartIndex; ++Index)
		{
			UpdateActionSlot(SlotOffset, Index, ActionNodes[Index-1]);
		}

		for(int32 Index = DragStartIndex+1; Index < SlotCount; ++Index)
		{
			UpdateActionSlot(SlotOffset, Index, ActionNodes[Index]);
		}
	}
	else
	{
		for(int32 Index = 0; Index < DragStartIndex; ++Index)
		{
			UpdateActionSlot(SlotOffset, Index, ActionNodes[Index]);
		}

		for(int32 Index = DragStartIndex; Index < HoveredDragIndex; ++Index)
		{
			UpdateActionSlot(SlotOffset, Index, ActionNodes[Index+1]);
		}

		UpdateActionSlot(SlotOffset, HoveredDragIndex, DragSlot->GetWidget(), true);

		for(int32 Index = HoveredDragIndex + 1; Index < SlotCount; ++Index)
		{
			UpdateActionSlot(SlotOffset, Index, ActionNodes[Index]);
		}
	}

	const FMargin DragSlotOffset = DragSlot->OffsetAttr.Get(FMargin());
	const FMargin HoveredDropSlotOffset = DropSlots[HoveredDragIndex]->OffsetAttr.Get(FMargin());

	if(HoveredDragIndex > 0 && DragSlotOffset.Left < HoveredDropSlotOffset.Left)
	{
		SConstraintCanvas::FSlot* OverlappedSlot = ActionSlots[HoveredDragIndex-1];
		FMargin OverlappedSlotOffset = OverlappedSlot->OffsetAttr.Get(FMargin());

		const float OverlappedActionSlotLeft = HoveredDropSlotOffset.Left - OverlappedSlotOffset.Right;
		const float Delta =  HoveredDropSlotOffset.Left - DragSlotOffset.Left;

		const float MaximalLeftOffset = HoveredDropSlotOffset.Left + HoveredDropSlotOffset.Right;
		const float NewLeftOffset = OverlappedActionSlotLeft + Delta;

		if(NewLeftOffset < MaximalLeftOffset)
		{
			OverlappedSlotOffset.Left = NewLeftOffset;
			OverlappedSlot->OffsetAttr.Set(OverlappedSlotOffset);
		}
	}
	else if(HoveredDragIndex < (SlotCount-1) && DragSlotOffset.Left > (HoveredDropSlotOffset.Left + HoveredDropSlotOffset.Right) )
	{
		SConstraintCanvas::FSlot* OverlappedSlot = ActionSlots[HoveredDragIndex+1];
		FMargin OverlappedSlotOffset = OverlappedSlot->OffsetAttr.Get(FMargin());
		FMargin OverlappedDropSlotOffset = DropSlots[HoveredDragIndex+1]->OffsetAttr.Get(FMargin());

		const float OverlappedActionSlotLeft = OverlappedDropSlotOffset.Left + OverlappedDropSlotOffset.Right;
		const float MinimalLeftOffset = HoveredDropSlotOffset.Left + HoveredDropSlotOffset.Right;
		const float Delta = DragSlotOffset.Left - MinimalLeftOffset;

		float NewLeftOffset = OverlappedActionSlotLeft - Delta;
		if(NewLeftOffset > MinimalLeftOffset)
		{
			OverlappedSlotOffset.Left = NewLeftOffset;
			OverlappedSlot->OffsetAttr.Set(OverlappedSlotOffset);
		}
	}

	UpdateLine(SlotCount-1);

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

	DragStartIndex = IndexDragged;
	DragIndex = IndexDragged;
	HoveredDragIndex = IndexDragged;
	DragIndicatorIndex = INDEX_NONE;

	FMargin Offset = ActionSlots[IndexDragged]->OffsetAttr.Get(FMargin());
	DragSlot->OffsetAttr.Set(Offset);
	DragSlot->AttachWidget(ActionNodes[IndexDragged]);

	AbscissaRange.X = ActionSlots[0]->OffsetAttr.Get(FMargin()).Left;

	const FModifierKeysState ModifierKeyState = FSlateApplication::Get().GetModifierKeys();
	bIsCopying = !(ModifierKeyState.IsControlDown() || ModifierKeyState.IsCommandDown());

	OnControlKeyDown(!bIsCopying);
}

float SDataprepGraphTrackWidget::OnNodeDragged(float DragDelta)
{
	float ActualDelta = 0.f;

	if(!DragSlot)
	{
		ensure(false);
		return ActualDelta;
	}

	FMargin DragSlotOffset = DragSlot->OffsetAttr.Get(FMargin());

	float NewAbscissa = DragSlotOffset.Left + DragDelta;
	 
	bool bValidDrag = (NewAbscissa >= AbscissaRange.X) && (DragSlotOffset.Left < AbscissaRange.Y || NewAbscissa < AbscissaRange.Y);

	if(bValidDrag)
	{
		TSharedPtr<SDataprepGraphActionNode> DraggedActionNode = StaticCastSharedRef<SDataprepGraphActionNode>(DragSlot->GetWidget());
		ensure(DraggedActionNode.IsValid());

		const float CachedNodeAbscissa = DragSlotOffset.Left;

		DragSlotOffset.Left = NewAbscissa < AbscissaRange.X ? AbscissaRange.X : (NewAbscissa > AbscissaRange.Y ? AbscissaRange.Y : NewAbscissa);
		ActualDelta = DragSlotOffset.Left - CachedNodeAbscissa;

		DragSlot->OffsetAttr.Set(DragSlotOffset);
		DraggedActionNode->GetNodeObj()->NodePosX = DragSlotOffset.Left;

		// Check if center of dragged widget is over a neighboring widget by at least half its size
		HoveredDragIndex = GetHoveredActionNode(DragSlotOffset.Left, DragSlotOffset.Right);
		UpdateLayoutOnDrag(ActualDelta < 0.f ? -1 : (ActualDelta > 0.f ? 1 : 0));

		Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::ChildOrder);
	}

	return ActualDelta;
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

	SlotCount = ActionNodes.Num();

	DragSlot->OffsetAttr.Set(FVector2D(-100., -10.f));
	DragSlot->AttachWidget(SNullWidget::NullWidget);

	DropSlots[SlotCount]->AttachWidget(SNullWidget::NullWidget);
	ActionSlots[SlotCount]->AttachWidget(SNullWidget::NullWidget);

	RefreshLayout();

	int32 CachedHoveredDragIndex = HoveredDragIndex;
	DragStartIndex = INDEX_NONE;
	DragIndex = INDEX_NONE;
	HoveredDragIndex = INDEX_NONE;

	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::ChildOrder);

	return CachedHoveredDragIndex;
}

void SDataprepGraphTrackWidget::UpdateDragIndicator(FVector2D MousePosition)
{
	int32 NewDragIndicatorIndex = INDEX_NONE;

	const float LocalAbscissa = GetTickSpaceGeometry().AbsoluteToLocal(MousePosition).X - 1.f;
	FMargin SlotOffset = ActionSlots[0]->OffsetAttr.Get(FMargin());
	float MaxAbscissa = SlotOffset.Left + (SlotOffset.Right * 0.25f);

	if(LocalAbscissa < MaxAbscissa)
	{
		NewDragIndicatorIndex = 0;
	}
	else
	{
		for(int32 Index = 0; Index <= SlotCount; ++Index)
		{
			SlotOffset = ActionSlots[Index]->OffsetAttr.Get(FMargin());
			if(LocalAbscissa < (SlotOffset.Left + SlotOffset.Right + 1.f))
			{
				NewDragIndicatorIndex = Index;
				break;
			}
		}

		if(NewDragIndicatorIndex == INDEX_NONE)
		{
			NewDragIndicatorIndex = SlotCount;
		}
	}

	if(NewDragIndicatorIndex == DragIndicatorIndex)
	{
		return;
	}

	ResetDragIndicator();

	DragIndicatorIndex = NewDragIndicatorIndex;

	SConstraintCanvas::FSlot* TrackSlot = TrackSlots[SlotCount];

	if(NewDragIndicatorIndex == SlotCount)
	{
		float TrackLeftOffset = ActionSlots[SlotCount]->OffsetAttr.Get(FMargin()).Left + (SDataprepGraphActionNode::DefaultWidth * 0.5f) - 16.f;

		SlotOffset = TrackSlots[0]->OffsetAttr.Get(FMargin());

		TrackSlot->OffsetAttr.Set(FMargin(TrackLeftOffset, SlotOffset.Top, SlotOffset.Right, SlotOffset.Bottom));
		TrackSlot->AttachWidget(TrackSlotDrop.ToSharedRef());

		UpdateLine(SlotCount);
	}
	else
	{
		TrackSlot->OffsetAttr.Set(TrackSlots[DragIndicatorIndex]->OffsetAttr.Get(FMargin()));
		TrackSlot->AttachWidget(TrackSlotDrop.ToSharedRef());
	}
}

float SDataprepGraphTrackWidget::OnControlKeyDown(bool bCtrlDown)
{
	if(bIsCopying == bCtrlDown)
	{
		return 0.f;
	}

	if(DragStartIndex == INDEX_NONE)
	{
		bIsCopying = false;
		return 0.f;
	}

	bIsCopying = bCtrlDown;

	float MouseOffset = 0.f;
	SlotCount = ActionNodes.Num();

	FMargin DragSlotOffset = DragSlot->OffsetAttr.Get(FMargin());
	HoveredDragIndex = GetHoveredActionNode(DragSlotOffset.Left, DragSlotOffset.Right);

	for(int32 Index = 0; Index < SlotCount; ++Index)
	{
		ActionSlots[SlotCount]->AttachWidget(ActionNodes[Index]);
	}

	if(bIsCopying)
	{
		// Set the maximum move on the right
		const FVector2D DragSize = GetNodeSize(DragSlot->GetWidget());
		FMargin LastDropSlotOffset = DropSlots[SlotCount]->OffsetAttr.Get(FMargin());
		AbscissaRange.Y = LastDropSlotOffset.Left + LastDropSlotOffset.Right + 1.f - (DragSize.X * 0.5f);

		DropSlots[SlotCount]->AttachWidget(DropFiller.ToSharedRef());
		ActionSlots[SlotCount]->AttachWidget(DummyAction.ToSharedRef());
		TrackSlots[SlotCount]->AttachWidget(TrackSlotRegular.ToSharedRef());

		++SlotCount;
		UpdateLayoutOnDrag();
	}
	else
	{
		FMargin LastDropSlotOffset = DropSlots[SlotCount - 1]->OffsetAttr.Get(FMargin());
		AbscissaRange.Y = LastDropSlotOffset.Left + LastDropSlotOffset.Right;

		if(DragSlotOffset.Left > AbscissaRange.Y)
		{
			MouseOffset = DragSlotOffset.Left - AbscissaRange.Y;

			DragSlotOffset.Left = AbscissaRange.Y;
			DragSlot->OffsetAttr.Set(DragSlotOffset);

			HoveredDragIndex = GetHoveredActionNode(DragSlotOffset.Left, DragSlotOffset.Right);
		}

		DropSlots[SlotCount]->AttachWidget(SNullWidget::NullWidget);
		ActionSlots[SlotCount]->AttachWidget(SNullWidget::NullWidget);
		TrackSlots[SlotCount]->AttachWidget(SNullWidget::NullWidget);

		UpdateLayoutOnDrag();
	}

	Invalidate(EInvalidateWidgetReason::ChildOrder | EInvalidateWidgetReason::Layout);

	return MouseOffset;
}

bool SDataprepGraphTrackWidget::IsDraggedNodeOnEnds()
{
	if(DragSlot)
	{
		const FMargin DragSlotOffset = DragSlot->OffsetAttr.Get(FMargin());
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
			Size.Set(SDataprepGraphActionNode::DefaultWidth, SDataprepGraphActionNode::DefaultWidth);
		}
	}

	return Size;
}

void SDataprepGraphTrackWidget::UpdateLine(int32 LastIndex)
{
	LineSize = FVector2D(0.f, 4.f);

	if(SlotCount > 1)
	{
		FMargin OffsetDiff = TrackSlots[LastIndex]->OffsetAttr.Get(FMargin()) - TrackSlots[0]->OffsetAttr.Get(FMargin());
		FMargin LineOffset = LineSlot->OffsetAttr.Get(FMargin());

		LineOffset.Left = TrackSlots[0]->OffsetAttr.Get(FMargin()).Left + 16.f;
		LineOffset.Right = OffsetDiff.Left;
		LineSize.X = OffsetDiff.Left;

		LineSlot->OffsetAttr.Set(LineOffset);
	}
}

#undef LOCTEXT_NAMESPACE