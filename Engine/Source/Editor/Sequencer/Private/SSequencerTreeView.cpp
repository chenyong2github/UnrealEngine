// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SSequencerTreeView.h"
#include "SSequencerTrackLane.h"
#include "EditorStyleSet.h"
#include "Algo/BinarySearch.h"
#include "Algo/Copy.h"
#include "SequencerDisplayNodeDragDropOp.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "SSequencerTreeView.h"

static FName TrackAreaName = "TrackArea";

SSequencerTreeViewRow::~SSequencerTreeViewRow()
{
	const TSharedPtr<SSequencerTreeView>& TreeView = StaticCastSharedPtr<SSequencerTreeView>(OwnerTablePtr.Pin());
	TSharedPtr<FSequencerDisplayNode> PinnedNode = Node.Pin();
	if (TreeView.IsValid() && PinnedNode.IsValid())
	{
		TreeView->OnChildRowRemoved(PinnedNode.ToSharedRef());
	}
}

/** Construct function for this widget */
void SSequencerTreeViewRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, const FDisplayNodeRef& InNode)
{
	Node = InNode;
	OnGenerateWidgetForColumn = InArgs._OnGenerateWidgetForColumn;
	bool bIsSelectable = InNode->IsSelectable();

	SMultiColumnTableRow::Construct(
		SMultiColumnTableRow::FArguments()
			.OnDragDetected(this, &SSequencerTreeViewRow::OnDragDetected)
			.OnCanAcceptDrop(this, &SSequencerTreeViewRow::OnCanAcceptDrop)
			.OnAcceptDrop(this, &SSequencerTreeViewRow::OnAcceptDrop)
			.ShowSelection(bIsSelectable)
			.Padding(this, &SSequencerTreeViewRow::GetRowPadding),
		OwnerTableView);
}

FMargin SSequencerTreeViewRow::GetRowPadding() const
{
	TSharedPtr<FSequencerDisplayNode> PinnedNode = Node.Pin();
	TSharedPtr<FSequencerDisplayNode> ParentNode = PinnedNode ? PinnedNode->GetParentOrRoot() : nullptr;

	if (ParentNode.IsValid() && ParentNode->GetType() == ESequencerNode::Root && ParentNode->GetChildNodes()[0] != PinnedNode)
	{
		return FMargin(0.f, 1.f, 0.f, 0.f);
	}
	return FMargin(0.f, 0.f, 0.f, 0.f);
}

TSharedRef<SWidget> SSequencerTreeViewRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	TSharedPtr<FSequencerDisplayNode> PinnedNode = Node.Pin();
	if (PinnedNode.IsValid())
	{
		return OnGenerateWidgetForColumn.Execute(PinnedNode.ToSharedRef(), ColumnId, SharedThis(this));
	}

	return SNullWidget::NullWidget;
}

FReply SSequencerTreeViewRow::OnDragDetected( const FGeometry& InGeometry, const FPointerEvent& InPointerEvent )
{
	TSharedPtr<FSequencerDisplayNode> DisplayNode = Node.Pin();
	if ( DisplayNode.IsValid() )
	{
		FSequencer& Sequencer = DisplayNode->GetParentTree().GetSequencer();
		if ( Sequencer.GetSelection().GetSelectedOutlinerNodes().Num() > 0 )
		{
			TArray<TSharedRef<FSequencerDisplayNode> > DraggableNodes;
			for ( const TSharedRef<FSequencerDisplayNode> SelectedNode : Sequencer.GetSelection().GetSelectedOutlinerNodes() )
			{
				if ( SelectedNode->CanDrag() )
				{
					DraggableNodes.Add(SelectedNode);
				}
			}

			// If there were no nodes selected, don't start a drag drop operation.
			if (DraggableNodes.Num() == 0)
			{
				return FReply::Unhandled();
			}

			FText DefaultText = FText::Format( NSLOCTEXT( "SequencerTreeViewRow", "DefaultDragDropFormat", "Move {0} item(s)" ), FText::AsNumber( DraggableNodes.Num() ) );
			TSharedRef<FSequencerDisplayNodeDragDropOp> DragDropOp = FSequencerDisplayNodeDragDropOp::New( DraggableNodes, DefaultText, nullptr );

			return FReply::Handled().BeginDragDrop( DragDropOp );
		}
	}
	return FReply::Unhandled();
}

TOptional<EItemDropZone> SSequencerTreeViewRow::OnCanAcceptDrop( const FDragDropEvent& DragDropEvent, EItemDropZone InItemDropZone, FDisplayNodeRef DisplayNode )
{
	TSharedPtr<FSequencerDisplayNodeDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSequencerDisplayNodeDragDropOp>();
	if ( DragDropOp.IsValid() )
	{
		DragDropOp->ResetToDefaultToolTip();
		TOptional<EItemDropZone> AllowedDropZone = DisplayNode->CanDrop( *DragDropOp, InItemDropZone );
		if ( AllowedDropZone.IsSet() == false )
		{
			DragDropOp->CurrentIconBrush = FEditorStyle::GetBrush( TEXT( "Graph.ConnectorFeedback.Error" ) );
		}
		return AllowedDropZone;
	}
	return TOptional<EItemDropZone>();
}

FReply SSequencerTreeViewRow::OnAcceptDrop( const FDragDropEvent& DragDropEvent, EItemDropZone InItemDropZone, FDisplayNodeRef DisplayNode )
{
	TSharedPtr<FSequencerDisplayNodeDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSequencerDisplayNodeDragDropOp>();
	if ( DragDropOp.IsValid())
	{
		DisplayNode->Drop( DragDropOp->GetDraggedNodes(), InItemDropZone );
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedPtr<FSequencerDisplayNode> SSequencerTreeViewRow::GetDisplayNode() const
{
	return Node.Pin();
}

void SSequencerTreeViewRow::AddTrackAreaReference(const TSharedPtr<SSequencerTrackLane>& Lane)
{
	TrackLaneReference = Lane;
}

void SSequencerTreeViewRow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	StaticCastSharedPtr<SSequencerTreeView>(OwnerTablePtr.Pin())->ReportChildRowGeometry(Node.Pin().ToSharedRef(), AllottedGeometry);
}

void SSequencerTreeView::Construct(const FArguments& InArgs, const TSharedRef<FSequencerNodeTree>& InNodeTree, const TSharedRef<SSequencerTrackArea>& InTrackArea)
{
	SequencerNodeTree = InNodeTree;
	TrackArea = InTrackArea;
	bUpdatingSequencerSelection = false;
	bUpdatingTreeSelection = false;
	bSequencerSelectionChangeBroadcastWasSupressed = false;
	bPhysicalNodesNeedUpdate = false;
	bRightMouseButtonDown = false;
	bShowPinnedNodes = false;

	// We 'leak' these delegates (they'll get cleaned up automatically when the invocation list changes)
	// It's not safe to attempt their removal in ~SSequencerTreeView because SequencerNodeTree->GetSequencer() may not be valid
	FSequencer& Sequencer = InNodeTree->GetSequencer();
	Sequencer.GetSelection().GetOnOutlinerNodeSelectionChanged().AddSP(this, &SSequencerTreeView::SynchronizeTreeSelectionWithSequencerSelection);

	HeaderRow = SNew(SHeaderRow).Visibility(EVisibility::Collapsed);
	OnGetContextMenuContent = InArgs._OnGetContextMenuContent;

	SetupColumns(InArgs);

	STreeView::Construct
	(
		STreeView::FArguments()
		.TreeItemsSource(&RootNodes)
		.SelectionMode(ESelectionMode::Multi)
		.OnGenerateRow(this, &SSequencerTreeView::OnGenerateRow)
		.OnGetChildren(this, &SSequencerTreeView::OnGetChildren)
		.HeaderRow(HeaderRow)
		.ExternalScrollbar(InArgs._ExternalScrollbar)
		.OnExpansionChanged(this, &SSequencerTreeView::OnExpansionChanged)
		.AllowOverscroll(EAllowOverscroll::No)
		.OnContextMenuOpening( this, &SSequencerTreeView::OnContextMenuOpening )
		.OnSetExpansionRecursive(this, &SSequencerTreeView::SetItemExpansionRecursive)
		.HighlightParentNodesForSelection(true)
	);
}

void SSequencerTreeView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bSequencerSelectionChangeBroadcastWasSupressed && !FSlateApplication::Get().AnyMenusVisible())
	{
		FSequencerSelection& SequencerSelection = SequencerNodeTree->GetSequencer().GetSelection();
		if (SequencerSelection.IsBroadcasting())
		{
			SequencerSelection.RequestOutlinerNodeSelectionChangedBroadcast();
			bSequencerSelectionChangeBroadcastWasSupressed = false;
		}
	}

	STreeView::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	
	// These are updated in both tick and paint since both calls can cause changes to the cached rows and the data needs
	// to be kept synchronized so that external measuring calls get correct and reliable results.
	if (bPhysicalNodesNeedUpdate)
	{
		PhysicalNodes.Reset();
		CachedRowGeometry.GenerateValueArray(PhysicalNodes);

		PhysicalNodes.Sort([](const FCachedGeometry& A, const FCachedGeometry& B) {
			return A.PhysicalTop < B.PhysicalTop;
		});
	}

	HighlightRegion = TOptional<FHighlightRegion>();

	if (SequencerNodeTree->GetHoveredNode().IsValid())
	{
		TSharedRef<FSequencerDisplayNode> OutermostParent = SequencerNodeTree->GetHoveredNode()->GetOutermostParent();

		TOptional<float> PhysicalTop = ComputeNodePosition(OutermostParent);

		if (PhysicalTop.IsSet())
		{
			// Compute total height of the highlight
			float TotalHeight = 0.f;
			OutermostParent->TraverseVisible_ParentFirst([&](FSequencerDisplayNode& InNode){
				TotalHeight += InNode.GetNodeHeight() + InNode.GetNodePadding().Combined();
				return true;
			});

			HighlightRegion = FHighlightRegion(PhysicalTop.GetValue(), PhysicalTop.GetValue() + TotalHeight);
		}
	}
}

int32 SSequencerTreeView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = STreeView::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// These are updated in both tick and paint since both calls can cause changes to the cached rows and the data needs
	// to be kept synchronized so that external measuring calls get correct and reliable results.
	if (bPhysicalNodesNeedUpdate)
	{
		PhysicalNodes.Reset();
		CachedRowGeometry.GenerateValueArray(PhysicalNodes);

		PhysicalNodes.Sort([](const FCachedGeometry& A, const FCachedGeometry& B) {
			return A.PhysicalTop < B.PhysicalTop;
		});
	}

	if (HighlightRegion.IsSet())
	{
		// Black tint for highlighted regions
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId+1,
			AllottedGeometry.ToPaintGeometry(FVector2D(2.f, HighlightRegion->Top - 4.f), FVector2D(AllottedGeometry.Size.X - 4.f, 4.f)),
			FEditorStyle::GetBrush("Sequencer.TrackHoverHighlight_Top"),
			ESlateDrawEffect::None,
			FLinearColor::Black
		);
		
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId+1,
			AllottedGeometry.ToPaintGeometry(FVector2D(2.f, HighlightRegion->Bottom), FVector2D(AllottedGeometry.Size.X - 4.f, 4.f)),
			FEditorStyle::GetBrush("Sequencer.TrackHoverHighlight_Bottom"),
			ESlateDrawEffect::None,
			FLinearColor::Black
		);
	}

	return LayerId + 1;
}

TOptional<SSequencerTreeView::FCachedGeometry> SSequencerTreeView::GetPhysicalGeometryForNode(const FDisplayNodeRef& InNode) const
{
	if (const FCachedGeometry* FoundGeometry = CachedRowGeometry.Find(InNode))
	{
		return *FoundGeometry;
	}

	return TOptional<FCachedGeometry>();
}

TOptional<float> SSequencerTreeView::ComputeNodePosition(const FDisplayNodeRef& InNode) const
{
	// Positioning strategy:
	// Attempt to root out any visible node in the specified node's sub-hierarchy, and compute the node's offset from that
	float NegativeOffset = 0.f;
	TOptional<float> Top;
	
	// Iterate parent first until we find a tree view row we can use for the offset height
	auto Iter = [this, &NegativeOffset, &Top](FSequencerDisplayNode& InDisplayNode)
	{
		TOptional<FCachedGeometry> ChildRowGeometry = this->GetPhysicalGeometryForNode(InDisplayNode.AsShared());
		if (ChildRowGeometry.IsSet())
		{
			Top = ChildRowGeometry->PhysicalTop;
			// Stop iterating
			return false;
		}

		NegativeOffset -= InDisplayNode.GetNodeHeight() + InDisplayNode.GetNodePadding().Combined();
		return true;
	};

	InNode->TraverseVisible_ParentFirst(Iter);

	if (Top.IsSet())
	{
		return NegativeOffset + Top.GetValue();
	}

	return Top;
}

void SSequencerTreeView::ReportChildRowGeometry(const FDisplayNodeRef& InNode, const FGeometry& InGeometry)
{
	float ChildOffset = TransformPoint(
		Concatenate(
			InGeometry.GetAccumulatedLayoutTransform(),
			GetCachedGeometry().GetAccumulatedLayoutTransform().Inverse()
		),
		FVector2D(0,0)
	).Y;

	if (InNode->IsPinned() != bShowPinnedNodes)
	{
		CachedRowGeometry.Remove(InNode);
	}
	else
	{
		CachedRowGeometry.Add(InNode, FCachedGeometry(InNode, ChildOffset, InGeometry.Size.Y));
	}

	bPhysicalNodesNeedUpdate = true;

	for (TSharedPtr<SSequencerTreeView> SlaveTreeView : SlaveTreeViews)
	{
		SlaveTreeView->ReportChildRowGeometry(InNode, InGeometry);
	}
}

void SSequencerTreeView::OnChildRowRemoved(const FDisplayNodeRef& InNode)
{
	CachedRowGeometry.Remove(InNode);
	bPhysicalNodesNeedUpdate = true;
}

TSharedPtr<FSequencerDisplayNode> SSequencerTreeView::HitTestNode(float InPhysical) const
{
	// Find the first node with a top after the specified value - the hit node must be the one preceeding this
	const int32 FoundIndex = Algo::UpperBoundBy(PhysicalNodes, InPhysical, &FCachedGeometry::PhysicalTop) - 1;
	if (FoundIndex >= 0)
	{
		return PhysicalNodes[FoundIndex].Node;
	}
	return nullptr;
}

float SSequencerTreeView::PhysicalToVirtual(float InPhysical) const
{
	// Find the first node with a top after the specified value - the hit node must be the one preceeding this
	const int32 FoundIndex = Algo::UpperBoundBy(PhysicalNodes, InPhysical, &FCachedGeometry::PhysicalTop) - 1;
	if (FoundIndex >= 0)
	{
		const FCachedGeometry& Found = PhysicalNodes[FoundIndex];
		const float FractionalHeight = (InPhysical - Found.PhysicalTop) / Found.PhysicalHeight;
		return Found.Node->GetVirtualTop() + (Found.Node->GetVirtualBottom() - Found.Node->GetVirtualTop()) * FractionalHeight;
	}

	if (PhysicalNodes.Num())
	{
		const FCachedGeometry& First = PhysicalNodes[0];
		if (InPhysical < First.PhysicalTop)
		{
			return First.Node->GetVirtualTop() + (InPhysical - First.PhysicalTop);
		}
		else
		{
			const FCachedGeometry& Last = PhysicalNodes.Last();
			return Last.Node->GetVirtualTop() + (InPhysical - Last.PhysicalTop);
		}
	}

	return InPhysical;
}

float SSequencerTreeView::VirtualToPhysical(float InVirtual) const
{
	auto GetVirtualTop = [](const FCachedGeometry& In)
	{
		return In.Node->GetVirtualTop();
	};
	// Find the first node with a top after the specified value - the hit node must be the one preceeding this
	const int32 FoundIndex = Algo::UpperBoundBy(PhysicalNodes, InVirtual, GetVirtualTop) - 1;
	if (FoundIndex >= 0)
	{
		const FCachedGeometry& Found = PhysicalNodes[FoundIndex];

		const float FractionalHeight = (InVirtual - Found.Node->GetVirtualTop()) / (Found.Node->GetVirtualBottom() - Found.Node->GetVirtualTop());
		return Found.PhysicalTop + Found.PhysicalHeight * FractionalHeight;
	}

	if (PhysicalNodes.Num())
	{
		const FCachedGeometry& Last = PhysicalNodes.Last();
		return Last.PhysicalTop + (InVirtual - Last.Node->GetVirtualTop());
	}

	return InVirtual;
}

void SSequencerTreeView::SetupColumns(const FArguments& InArgs)
{
	FSequencer& Sequencer = SequencerNodeTree->GetSequencer();

	// Define a column for the Outliner
	auto GenerateOutliner = [](const FDisplayNodeRef& InNode, const TSharedRef<SSequencerTreeViewRow>& InRow)
	{
		return InNode->GenerateContainerWidgetForOutliner(InRow);
	};

	Columns.Add("Outliner", FSequencerTreeViewColumn(GenerateOutliner, 1.f));

	// Now populate the header row with the columns
	for (TTuple<FName, FSequencerTreeViewColumn>& Pair : Columns)
	{
		if (Pair.Key != TrackAreaName)
		{
			HeaderRow->AddColumn(
				SHeaderRow::Column(Pair.Key)
				.FillWidth(Pair.Value.Width)
			);
		}
	}
}

void SSequencerTreeView::UpdateTrackArea()
{
	FSequencer& Sequencer = SequencerNodeTree->GetSequencer();

	// Add or remove the column
	if (const FSequencerTreeViewColumn* Column = Columns.Find(TrackAreaName))
	{
		HeaderRow->AddColumn(
			SHeaderRow::Column(TrackAreaName)
			.FillWidth(Column->Width)
		);
	}
}

void SSequencerTreeView::AddSlaveTreeView(TSharedPtr<SSequencerTreeView> SlaveTreeView)
{
	SlaveTreeViews.Add(SlaveTreeView);
	SlaveTreeView->SetMasterTreeView(SharedThis(this));
}

void SSequencerTreeView::OnRightMouseButtonDown(const FPointerEvent& MouseEvent)
{
	STreeView::OnRightMouseButtonDown(MouseEvent);
	bRightMouseButtonDown = true;
}

void SSequencerTreeView::OnRightMouseButtonUp(const FPointerEvent& MouseEvent)
{
	STreeView::OnRightMouseButtonUp(MouseEvent);
	bRightMouseButtonDown = false;
}

FReply SSequencerTreeView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const TArray<FDisplayNodeRef>& ItemsSourceRef = (*this->ItemsSource);

	// Don't respond to key-presses containing "Alt" as a modifier
	if (ItemsSourceRef.Num() > 0 && !InKeyEvent.IsAltDown())
	{
		bool bWasHandled = false;
		NullableItemType ItemNavigatedTo(nullptr);

		// Check for selection manipulation keys (Up, Down, Home, End, PageUp, PageDown)
		if (InKeyEvent.GetKey() == EKeys::Up)
		{
			int32 SelectionIndex = 0;
			if (TListTypeTraits<FDisplayNodeRef>::IsPtrValid(SelectorItem))
			{
				SelectionIndex = ItemsSourceRef.Find(TListTypeTraits<FDisplayNodeRef>::NullableItemTypeConvertToItemType(SelectorItem));
			}

			--SelectionIndex;

			for (; SelectionIndex >=0; --SelectionIndex)
			{
				if (ItemsSourceRef[SelectionIndex]->IsSelectable())
				{
					ItemNavigatedTo = ItemsSourceRef[SelectionIndex];
					break;
				}
			}
			bWasHandled = true;
		}
		else if (InKeyEvent.GetKey() == EKeys::Down)
		{
			int32 SelectionIndex = 0;
			if (TListTypeTraits<FDisplayNodeRef>::IsPtrValid(SelectorItem))
			{
				SelectionIndex = ItemsSourceRef.Find(TListTypeTraits<FDisplayNodeRef>::NullableItemTypeConvertToItemType(SelectorItem));
			}

			++SelectionIndex;

			for (; SelectionIndex < ItemsSourceRef.Num(); ++SelectionIndex)
			{
				if (ItemsSourceRef[SelectionIndex]->IsSelectable())
				{
					ItemNavigatedTo = ItemsSourceRef[SelectionIndex];
					break;
				}
			}
			bWasHandled = true;
		}
		else if (InKeyEvent.GetKey() == EKeys::Home)
		{
			// Select the first item
			for (int32 SelectionIndex = 0; SelectionIndex < ItemsSourceRef.Num(); ++SelectionIndex)
			{
				if (ItemsSourceRef[SelectionIndex]->IsSelectable())
				{
					ItemNavigatedTo = ItemsSourceRef[SelectionIndex];
					break;
				}
			}
			bWasHandled = true;
		}
		else if (InKeyEvent.GetKey() == EKeys::End)
		{
			// Select the last item
			for (int32 SelectionIndex = ItemsSourceRef.Num() -1; SelectionIndex >=0 ; --SelectionIndex)
			{
				if (ItemsSourceRef[SelectionIndex]->IsSelectable())
				{
					ItemNavigatedTo = ItemsSourceRef[SelectionIndex];
					break;
				}
			}
			bWasHandled = true;
		}
		else if (InKeyEvent.GetKey() == EKeys::PageUp)
		{
			int32 SelectionIndex = 0;
			if (TListTypeTraits<FDisplayNodeRef>::IsPtrValid(SelectorItem))
			{
				SelectionIndex = ItemsSourceRef.Find(TListTypeTraits<FDisplayNodeRef>::NullableItemTypeConvertToItemType(SelectorItem));
			}

			int32 NumItemsInAPage = GetNumLiveWidgets();
			int32 Remainder = NumItemsInAPage % GetNumItemsPerLine();
			NumItemsInAPage -= Remainder;

			if (SelectionIndex >= NumItemsInAPage)
			{
				// Select an item on the previous page
				SelectionIndex = SelectionIndex - NumItemsInAPage;

				// Scan up for the first selectable node
				for (; SelectionIndex >= 0; --SelectionIndex)
				{
					if (ItemsSourceRef[SelectionIndex]->IsSelectable())
					{
						ItemNavigatedTo = ItemsSourceRef[SelectionIndex];
						break;
					}
				}
			}

			// If we had less than a page to jump, or we haven't found a selectable node yet,
			// scan back toward our current node until we find one.
			if (!ItemNavigatedTo)
			{
				SelectionIndex = 0;
				for (; SelectionIndex < ItemsSourceRef.Num(); ++SelectionIndex)
				{
					if (ItemsSourceRef[SelectionIndex]->IsSelectable())
					{
						ItemNavigatedTo = ItemsSourceRef[SelectionIndex];
						break;
					}
				}
			}

			bWasHandled = true;
		}
		else if (InKeyEvent.GetKey() == EKeys::PageDown)
		{
			int32 SelectionIndex = 0;
			if (TListTypeTraits<FDisplayNodeRef>::IsPtrValid(SelectorItem))
			{
				SelectionIndex = ItemsSourceRef.Find(TListTypeTraits<FDisplayNodeRef>::NullableItemTypeConvertToItemType(SelectorItem));
			}

			int32 NumItemsInAPage = GetNumLiveWidgets();
			int32 Remainder = NumItemsInAPage % GetNumItemsPerLine();
			NumItemsInAPage -= Remainder;


			if (SelectionIndex < ItemsSourceRef.Num() - NumItemsInAPage)
			{
				// Select an item on the next page
				SelectionIndex = SelectionIndex + NumItemsInAPage;

				for (; SelectionIndex < ItemsSourceRef.Num(); ++SelectionIndex)
				{
					if (ItemsSourceRef[SelectionIndex]->IsSelectable())
					{
						ItemNavigatedTo = ItemsSourceRef[SelectionIndex];
						break;
					}
				}
			}

			// If we had less than a page to jump, or we haven't found a selectable node yet,
			// scan back toward our current node until we find one.
			if (!ItemNavigatedTo)
			{
				SelectionIndex = ItemsSourceRef.Num() - 1;
				for (; SelectionIndex >= 0; --SelectionIndex)
				{
					if (ItemsSourceRef[SelectionIndex]->IsSelectable())
					{
						ItemNavigatedTo = ItemsSourceRef[SelectionIndex];
						break;
					}
				}
			}
			bWasHandled = true;
		}

		if (TListTypeTraits<FDisplayNodeRef>::IsPtrValid(ItemNavigatedTo))
		{
			FDisplayNodeRef ItemToSelect(TListTypeTraits<FDisplayNodeRef>::NullableItemTypeConvertToItemType(ItemNavigatedTo));
			NavigationSelect(ItemToSelect, InKeyEvent);
		}

		if (bWasHandled)
		{
			return FReply::Handled();
		}
	}

	return STreeView<FDisplayNodeRef>::OnKeyDown(MyGeometry, InKeyEvent);
}
	

void SSequencerTreeView::SynchronizeTreeSelectionWithSequencerSelection()
{
	if ( bUpdatingSequencerSelection == false )
	{
		bUpdatingTreeSelection = true;
		{
			Private_ClearSelection();

			FSequencer& Sequencer = SequencerNodeTree->GetSequencer();
			for ( const TSharedRef<FSequencerDisplayNode>& Node : Sequencer.GetSelection().GetSelectedOutlinerNodes() )
			{
				if (Node->IsSelectable() && Node->GetOutermostParent()->IsPinned() == bShowPinnedNodes)
				{
					Private_SetItemSelection( Node, true, false );
				}
			}

			Private_SignalSelectionChanged( ESelectInfo::Direct );
		}
		bUpdatingTreeSelection = false;
	}

	for (TSharedPtr<SSequencerTreeView> SlaveTreeView : SlaveTreeViews)
	{
		SlaveTreeView->SynchronizeTreeSelectionWithSequencerSelection();
	}
}

void SSequencerTreeView::Private_SetItemSelection( FDisplayNodeRef TheItem, bool bShouldBeSelected, bool bWasUserDirected )
{
	STreeView::Private_SetItemSelection( TheItem, bShouldBeSelected, bWasUserDirected );
	if ( bUpdatingTreeSelection == false )
	{
		// Don't broadcast the sequencer selection change on individual tree changes.  Wait for signal selection changed.
		FSequencerSelection& SequencerSelection = SequencerNodeTree->GetSequencer().GetSelection();
		SequencerSelection.SuspendBroadcast();
		bSequencerSelectionChangeBroadcastWasSupressed = true;
		if ( bShouldBeSelected )
		{
			SequencerSelection.AddToSelection( TheItem );
		}
		else
		{
			SequencerSelection.RemoveFromSelection( TheItem );
		}
		SequencerSelection.ResumeBroadcast();
	}
}


void SSequencerTreeView::Private_ClearSelection()
{
	STreeView::Private_ClearSelection();
	if ( bUpdatingTreeSelection == false )
	{
		// Don't broadcast the sequencer selection change on individual tree changes.  Wait for signal selection changed.
		FSequencerSelection& SequencerSelection = SequencerNodeTree->GetSequencer().GetSelection();
		SequencerSelection.SuspendBroadcast();
		bSequencerSelectionChangeBroadcastWasSupressed = true;
		SequencerSelection.EmptySelectedOutlinerNodes();
		SequencerSelection.ResumeBroadcast();
	}
}

void SSequencerTreeView::Private_SelectRangeFromCurrentTo( FDisplayNodeRef InRangeSelectionEnd )
{
	STreeView::Private_SelectRangeFromCurrentTo( InRangeSelectionEnd );
	if ( bUpdatingTreeSelection == false )
	{
		// Don't broadcast the sequencer selection change on individual tree changes.  Wait for signal selection changed.
		FSequencerSelection& SequencerSelection = SequencerNodeTree->GetSequencer().GetSelection();
		SequencerSelection.SuspendBroadcast();
		bSequencerSelectionChangeBroadcastWasSupressed = true;
		SynchronizeSequencerSelectionWithTreeSelection();
		SequencerSelection.ResumeBroadcast();
	}
}

void SSequencerTreeView::Private_SignalSelectionChanged(ESelectInfo::Type SelectInfo)
{
	if ( bUpdatingTreeSelection == false && !bRightMouseButtonDown)
	{
		bUpdatingSequencerSelection = true;
		{
			FSequencerSelection& SequencerSelection = SequencerNodeTree->GetSequencer().GetSelection();
			SequencerSelection.SuspendBroadcast();
			bool bSelectionChanged = SynchronizeSequencerSelectionWithTreeSelection();
			SequencerSelection.ResumeBroadcast();
			if (bSequencerSelectionChangeBroadcastWasSupressed || bSelectionChanged)
			{
				if (SequencerSelection.IsBroadcasting())
				{
					SequencerSelection.RequestOutlinerNodeSelectionChangedBroadcast();
					bSequencerSelectionChangeBroadcastWasSupressed = false;
				}
			}
		}
		bUpdatingSequencerSelection = false;
	}

	STreeView::Private_SignalSelectionChanged(SelectInfo);
}

bool SSequencerTreeView::SynchronizeSequencerSelectionWithTreeSelection()
{
	// If this is a slave SSequencerTreeView it only has a partial view of what is selected. The master should handle syncing the entire selection instead.
	if (MasterTreeView.IsValid())
	{
		return MasterTreeView->SynchronizeSequencerSelectionWithTreeSelection();
	}

	bool bSelectionChanged = false;
	const TSet<TSharedRef<FSequencerDisplayNode>>& SequencerSelection = SequencerNodeTree->GetSequencer().GetSelection().GetSelectedOutlinerNodes();
	TSet<TSharedRef<FSequencerDisplayNode> > AllSelectedItems(SelectedItems);

	// If we have slave SSequencerTreeViews, combine their selected items as well
	for (TSharedPtr<SSequencerTreeView> SlaveTreeView : SlaveTreeViews)
	{
		AllSelectedItems.Append(SlaveTreeView->GetSelectedItems());
	}

	if (AllSelectedItems.Num() != SequencerSelection.Num() || AllSelectedItems.Difference(SequencerSelection).Num() != 0 )
	{
		FSequencer& Sequencer = SequencerNodeTree->GetSequencer();
		FSequencerSelection& Selection = Sequencer.GetSelection();
		Selection.EmptySelectedOutlinerNodes();
		for ( const TSharedRef<FSequencerDisplayNode>& Item : AllSelectedItems)
		{
			Selection.AddToSelection( Item );
		}
		bSelectionChanged = true;
	}
	return bSelectionChanged;
}

TSharedPtr<SWidget> SSequencerTreeView::OnContextMenuOpening()
{
	// Open a context menu for the first selected item if it is selectable
	for (TSharedRef<FSequencerDisplayNode> SelectedNode : SequencerNodeTree->GetSequencer().GetSelection().GetSelectedOutlinerNodes())
	{
		if (SelectedNode->IsSelectable())
		{
			return SelectedNode->OnSummonContextMenu();
		}
		break;
	}

	// Otherwise, add a general menu for options
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, SequencerNodeTree->GetSequencer().GetCommandBindings());

	OnGetContextMenuContent.ExecuteIfBound(MenuBuilder);
	
	MenuBuilder.BeginSection("Edit");
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SSequencerTreeView::Refresh()
{
	RootNodes.Reset();

	for (auto& Item : SequencerNodeTree->GetRootNodes())
	{
		if (Item->IsVisible() && Item->IsPinned() == bShowPinnedNodes)
		{
			RootNodes.Add(Item);
		}
	}
	
	// Reset item expansion since we don't know if any expansion states may have changed in-between refreshes
	{
		STreeView::OnExpansionChanged.Unbind();

		ClearExpandedItems();
		auto Traverse_SetExpansionStates = [this](FSequencerDisplayNode& InNode)
		{
			this->SetItemExpansion(InNode.AsShared(), InNode.IsExpanded());
			return true;
		};
		const bool bIncludeRootNode = false;
		SequencerNodeTree->GetRootNode()->Traverse_ParentFirst(Traverse_SetExpansionStates, bIncludeRootNode);

		STreeView::OnExpansionChanged.BindSP(this, &SSequencerTreeView::OnExpansionChanged);
	}

	// Force synchronization of selected tree view items here since the tree nodes may have been rebuilt
	// and the treeview's selection will now be invalid.
	bUpdatingTreeSelection = true;
	SynchronizeTreeSelectionWithSequencerSelection();
	bUpdatingTreeSelection = false;

	RebuildList();

	for (TSharedPtr<SSequencerTreeView> SlaveTreeView : SlaveTreeViews)
	{
		SlaveTreeView->Refresh();
	}
}

void SSequencerTreeView::ScrollByDelta(float DeltaInSlateUnits)
{
	ScrollBy( GetCachedGeometry(), DeltaInSlateUnits, EAllowOverscroll::No );
}

template<typename T>
bool ShouldExpand(const T& InContainer, ETreeRecursion Recursion)
{
	bool bAllExpanded = true;
	for (auto& Item : InContainer)
	{
		bAllExpanded &= Item->IsExpanded();
		if (Recursion == ETreeRecursion::Recursive)
		{
			Item->TraverseVisible_ParentFirst([&](FSequencerDisplayNode& InNode){
				bAllExpanded &= InNode.IsExpanded();
				return true;
			});
		}
	}
	return !bAllExpanded;
}

void SSequencerTreeView::ToggleExpandCollapseNodes(ETreeRecursion Recursion, bool bExpandAll)
{
	bool bExpand = false;
	if (bExpandAll)
	{
		bExpand = ShouldExpand(SequencerNodeTree->GetRootNodes(), Recursion);
	}
	else
	{
		FSequencer& Sequencer = SequencerNodeTree->GetSequencer();

		const TSet< FDisplayNodeRef >& SelectedNodes = Sequencer.GetSelection().GetSelectedOutlinerNodes();

		bExpand = ShouldExpand(SelectedNodes, Recursion);
	}

	ExpandOrCollapseNodes(Recursion, bExpandAll, bExpand);
}

void SSequencerTreeView::ExpandNodes(ETreeRecursion Recursion, bool bExpandAll)
{
	const bool bExpand = true;
	ExpandOrCollapseNodes(Recursion, bExpandAll, bExpand);
}

void SSequencerTreeView::CollapseNodes(ETreeRecursion Recursion, bool bExpandAll)
{
	const bool bExpand = false;
	ExpandOrCollapseNodes(Recursion, bExpandAll, bExpand);
}

void SSequencerTreeView::ExpandOrCollapseNodes(ETreeRecursion Recursion, bool bExpandAll, bool bExpand)
{
	FSequencer& Sequencer = SequencerNodeTree->GetSequencer();

	if (bExpandAll)
	{
		for (auto& Item : SequencerNodeTree->GetRootNodes())
		{
			ExpandCollapseNode(Item, bExpand, Recursion);
		}	
	}
	else
	{
		const TSet< FDisplayNodeRef >& SelectedNodes = Sequencer.GetSelection().GetSelectedOutlinerNodes();

		for (auto& Item : SelectedNodes)
		{
			ExpandCollapseNode(Item, bExpand, Recursion);
		}
	}
}

void SSequencerTreeView::ExpandCollapseNode(const FDisplayNodeRef& InNode, bool bExpansionState, ETreeRecursion Recursion)
{
	SetItemExpansion(InNode, bExpansionState);

	if (Recursion == ETreeRecursion::Recursive)
	{
		for (auto& Node : InNode->GetChildNodes())
		{
			ExpandCollapseNode(Node, bExpansionState, ETreeRecursion::Recursive);
		}
	}
}

void SSequencerTreeView::OnExpansionChanged(FDisplayNodeRef InItem, bool bIsExpanded)
{
	InItem->SetExpansionState(bIsExpanded);

	// Expand any children that are also expanded
	for (const FDisplayNodeRef& Child : InItem->GetChildNodes())
	{
		if (Child->IsExpanded())
		{
			SetItemExpansion(Child, true);
		}
	}
}

void SSequencerTreeView::SetItemExpansionRecursive(FDisplayNodeRef InItem, bool bIsExpanded)
{
	ExpandCollapseNode(InItem, bIsExpanded, ETreeRecursion::Recursive);
}

void SSequencerTreeView::OnGetChildren(FDisplayNodeRef InParent, TArray<FDisplayNodeRef>& OutChildren) const
{
	for (const auto& Node : InParent->GetChildNodes())
	{
		if (!Node->IsHidden())
		{
			OutChildren.Add(Node);
		}
	}
}

TSharedRef<ITableRow> SSequencerTreeView::OnGenerateRow(FDisplayNodeRef InDisplayNode, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SSequencerTreeViewRow> Row =
		SNew(SSequencerTreeViewRow, OwnerTable, InDisplayNode)
		.OnGenerateWidgetForColumn(this, &SSequencerTreeView::GenerateWidgetForColumn);

	// Ensure the track area is kept up to date with the virtualized scroll of the tree view
	TSharedPtr<FSequencerDisplayNode> SectionAuthority = InDisplayNode->GetSectionAreaAuthority();
	if (SectionAuthority.IsValid())
	{
		TSharedPtr<SSequencerTrackLane> TrackLane = TrackArea->FindTrackSlot(SectionAuthority.ToSharedRef());

		if (!TrackLane.IsValid())
		{
			// Add a track slot for the row
			TAttribute<TRange<double>> ViewRange = FAnimatedRange::WrapAttribute( TAttribute<FAnimatedRange>::Create(TAttribute<FAnimatedRange>::FGetter::CreateSP(&SequencerNodeTree->GetSequencer(), &FSequencer::GetViewRange)) );

			TrackLane = SNew(SSequencerTrackLane, SectionAuthority.ToSharedRef(), SharedThis(this))
			//.IsEnabled(!InDisplayNode->GetSequencer().IsReadOnly())
			[
				SectionAuthority->GenerateWidgetForSectionArea(ViewRange)
			];

			TrackArea->AddTrackSlot(SectionAuthority.ToSharedRef(), TrackLane);
		}

		if (ensure(TrackLane.IsValid()))
		{
			Row->AddTrackAreaReference(TrackLane);
		}
	}

	return Row;
}

TSharedRef<SWidget> SSequencerTreeView::GenerateWidgetForColumn(const FDisplayNodeRef& InNode, const FName& ColumnId, const TSharedRef<SSequencerTreeViewRow>& Row) const
{
	const auto* Definition = Columns.Find(ColumnId);

	if (ensureMsgf(Definition, TEXT("Invalid column name specified")))
	{
		return Definition->Generator(InNode, Row);
	}

	return SNullWidget::NullWidget;
}
