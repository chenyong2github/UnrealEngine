// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVM/Views/STrackLane.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

#include "MVVM/Extensions/IGeometryExtension.h"
#include "MVVM/Extensions/IHoveredExtension.h"
#include "MVVM/Extensions/IPinnableExtension.h"
#include "MVVM/Extensions/IResizableExtension.h"
#include "MVVM/Extensions/ITrackAreaExtension.h"
#include "MVVM/SharedViewModelData.h"
#include "MVVM/ViewModels/EditorViewModel.h"
#include "MVVM/ViewModels/OutlinerViewModel.h"
#include "MVVM/ViewModels/TrackAreaViewModel.h"
#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/ViewModels/ViewModelIterators.h"
#include "MVVM/Views/SOutlinerView.h"

namespace UE
{
namespace Sequencer
{

STrackLane::FSlot::FSlot(TSharedPtr<ITrackLaneWidget> InInterface)
	: Interface(InInterface.ToSharedRef())
{
}

STrackLane::FSlot::~FSlot()
{
}

STrackLane::STrackLane()
	: Children(this)
{
}

STrackLane::~STrackLane()
{
}

void STrackLane::Construct(const FArguments& InArgs, TWeakPtr<FTrackAreaViewModel> InTrackAreaViewModel, TWeakViewModelPtr<IOutlinerExtension> InWeakOutlinerItem, const FTrackAreaParameters& InTrackParams, const TSharedRef<SOutlinerView>& InTreeView)
{
	bWidgetsDirty = true;

	WeakTrackAreaViewModel = InTrackAreaViewModel;
	WeakOutlinerItem = InWeakOutlinerItem;
	TreeView = InTreeView;

	TrackParams = InTrackParams;

	FViewModelPtr OutlinerItem = GetOutlinerItem();
	if (ensure(OutlinerItem))
	{
		TSharedPtr<FSharedViewModelData> SharedData = OutlinerItem->GetSharedData();
		SharedData->SubscribeToHierarchyChanged(OutlinerItem)
			.AddSP(this, &STrackLane::OnHierarchyUpdated);
	}

	RecreateWidgets();

	SetVisibility(EVisibility::SelfHitTestInvisible);
}

TViewModelPtr<IOutlinerExtension> STrackLane::GetOutlinerItem() const
{
	return WeakOutlinerItem.Pin();
}

bool STrackLane::IsPinned() const
{
	TSharedPtr<IPinnableExtension> Pinnable = WeakOutlinerItem.ImplicitPin();
	if (Pinnable)
	{
		return Pinnable->IsPinned();
	}
	return false;
}

void STrackLane::OnHierarchyUpdated()
{
	bWidgetsDirty = true;
	RecreateWidgets();
}

void STrackLane::RecreateWidgets()
{	
	if (!bWidgetsDirty)
	{
		return;
	}

	bWidgetsDirty = false;
	Children.Empty();

	TSharedPtr<ITrackAreaExtension> TrackAreaExtension = WeakOutlinerItem.ImplicitPin();
	TSharedPtr<FTrackAreaViewModel> TrackAreaViewModel = WeakTrackAreaViewModel.Pin();
	if (!TrackAreaViewModel || !TrackAreaExtension)
	{
		return;
	}

	TArray<TSharedPtr<ITrackLaneWidget>> ChildrenWidgets;

	// Construct views for this track lane

	FCreateTrackLaneViewParams CreateLaneParams(TrackAreaViewModel->GetEditor());
	for (TTypedIterator<ITrackLaneExtension, FViewModelVariantIterator> It(TrackAreaExtension->GetTrackAreaModelList()); It; ++It)
	{
		TSharedPtr<ITrackLaneWidget> NewView = It->CreateTrackLaneView(CreateLaneParams);

		if (NewView)
		{
			if (NewView->AcceptsChildren())
			{
				for (const TViewModelPtr<ITrackLaneExtension>& Child : It.GetCurrentItem()->GetDescendantsOfType<ITrackLaneExtension>())
				{
					TSharedPtr<ITrackLaneWidget> ChildView = Child->CreateTrackLaneView(CreateLaneParams);
					NewView->AddChildLane(ChildView);
				}
			}
			ChildrenWidgets.Add(NewView);
		}
	}

	// Add children to the widget

	for (const TSharedPtr<ITrackLaneWidget>& ChildWidget : ChildrenWidgets)
	{
		FSlot::FSlotArguments SlotArguments(MakeUnique<FSlot>(ChildWidget));
		SlotArguments.AttachWidget(ChildWidget->AsWidget());
		Children.AddSlot(MoveTemp(SlotArguments));
	}
}

void STrackLane::OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const
{
	for (int32 WidgetIndex = 0; WidgetIndex < Children.Num(); ++WidgetIndex)
	{
		const FSlot&        Slot = Children[WidgetIndex];
		TSharedRef<SWidget> Widget    = Slot.GetWidget();

		EVisibility WidgetVisibility = Widget->GetVisibility();
		TSharedPtr<FTrackAreaViewModel> TrackAreaViewModel = WeakTrackAreaViewModel.Pin();
		if (TrackAreaViewModel.IsValid() && ArrangedChildren.Accepts(WidgetVisibility))
		{
			FTimeToPixel TimeToPixel = TrackAreaViewModel->GetTimeToPixel(AllottedGeometry);
			FTrackLaneScreenAlignment ScreenAlignment = Slot.Interface->GetAlignment(TimeToPixel, AllottedGeometry);
			if (ScreenAlignment.IsVisible())
			{
				FArrangedWidget ArrangedWidget = ScreenAlignment.ArrangeWidget(Widget, AllottedGeometry);
				ArrangedChildren.AddWidget(WidgetVisibility, ArrangedWidget);
			}
		}
	}
}

int32 STrackLane::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = PaintLaneBackground(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle);

	FArrangedChildren ArrangedChildren(EVisibility::Visible);
	ArrangeChildren(AllottedGeometry, ArrangedChildren);

	for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
	{
		FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];
		FSlateRect ChildClipRect = MyCullingRect.IntersectionWith( CurWidget.Geometry.GetLayoutBoundingRect() );
		LayerId = CurWidget.Widget->Paint( Args.WithNewParent(this), CurWidget.Geometry, ChildClipRect, OutDrawElements, LayerId, InWidgetStyle, ShouldBeEnabled( bParentEnabled ) );
	}

	return LayerId + 1;
}

int32 STrackLane::PaintLaneBackground(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	static const FName BorderName("Sequencer.AnimationOutliner.DefaultBorder");
	static const FName SelectionColorName("SelectionColor");

	TViewModelPtr<IOutlinerExtension> OutlinerItem = WeakOutlinerItem.Pin();
	if (!OutlinerItem || OutlinerItem->IsFilteredOut())
	{
		return LayerId;
	}

	TViewModelPtr<IHoveredExtension> Hoverable = OutlinerItem.ImplicitCast();

	float TotalNodeHeight = OutlinerItem->GetOutlinerSizing().GetTotalHeight();

	const EOutlinerSelectionState SelectionState = OutlinerItem->GetSelectionState();

	// draw selection border
	if (SelectionState == EOutlinerSelectionState::SelectedDirectly)
	{
		FLinearColor SelectionColor = FAppStyle::GetSlateColor(SelectionColorName).GetColor(InWidgetStyle);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(0, 0),
				FVector2D(AllottedGeometry.GetLocalSize().X, TotalNodeHeight)
			),
			FAppStyle::GetBrush(BorderName),
			ESlateDrawEffect::None,
			SelectionColor
		);
	}

	// draw hovered or node has keys or sections selected border
	else
	{
		FLinearColor HighlightColor;
		bool bDrawHighlight = false;
		if (SelectionState != EOutlinerSelectionState::None)
		{
			bDrawHighlight = true;
			HighlightColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.15f);
		}
		else if (Hoverable && Hoverable->IsHovered())
		{
			bDrawHighlight = true;
			HighlightColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.05f);
		}

		if (bDrawHighlight)
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(
					FVector2D(0, 0),
					FVector2D(AllottedGeometry.GetLocalSize().X, TotalNodeHeight)
				),
				FAppStyle::GetBrush(BorderName),
				ESlateDrawEffect::None,
				HighlightColor
			);
		}
	}

	return LayerId + 1;
}

void STrackLane::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bWidgetsDirty)
	{
		RecreateWidgets();
	}
	
	// Sort children so they can be drawn bottom to top
	Children.StableSort([](const FSlot& A, const FSlot& B)
		{
			return A.Interface->GetOverlapPriority() < B.Interface->GetOverlapPriority();
		});

	FVector2D ThisFrameDesiredSize = GetDesiredSize();

	if (LastDesiredSize.IsSet() && ThisFrameDesiredSize.Y != LastDesiredSize.GetValue().Y)
	{
		TSharedPtr<SOutlinerView> PinnedTree = TreeView.Pin();
		if (PinnedTree.IsValid())
		{
			PinnedTree->RequestTreeRefresh();
		}
	}

	LastDesiredSize = ThisFrameDesiredSize;

	for (int32 WidgetIndex = 0; WidgetIndex < Children.Num(); ++WidgetIndex)
	{
		Children[WidgetIndex].Interface->ReportParentGeometry(AllottedGeometry);
	}
}

FVector2D STrackLane::ComputeDesiredSize(float LayoutScale) const
{
	TSharedPtr<FOutlinerViewModel> Outliner = WeakTrackAreaViewModel.Pin()->GetEditor()->GetOutliner();

	TViewModelPtr<IOutlinerExtension>  OutlinerItem = WeakOutlinerItem.Pin();
	TViewModelPtr<ITrackAreaExtension> TrackArea    = OutlinerItem.ImplicitCast();

	float Height = 0.f;

	if (TrackArea && OutlinerItem)
	{
		FTrackAreaParameters Parameters = TrackArea->GetTrackAreaParameters();

		Height = OutlinerItem->GetOutlinerSizing().GetTotalHeight();

		// Include child heights if necessary
		if (Parameters.LaneType == ETrackAreaLaneType::Nested && OutlinerItem->IsExpanded())
		{
			for (auto It = OutlinerItem.AsModel()->GetDescendantsOfType<IOutlinerExtension>(); It; ++It)
			{
				if ((*It)->IsFilteredOut())
				{
					It.IgnoreCurrentChildren();
					continue;
				}

				Height += It->GetOutlinerSizing().GetTotalHeight();

				if (!(*It)->IsExpanded())
				{
					It.IgnoreCurrentChildren();
				}
			}
		}
	}

	return FVector2D(100.f, Height);
}

void STrackLane::SetVerticalPosition(float InPosition)
{
	Position = InPosition;
}

float STrackLane::GetVerticalPosition() const
{
	return Position;
}

void STrackLane::PositionParentTrackLanes(TViewModelPtr<IOutlinerExtension> InItem, float InItemTop)
{
	// If the given item is our own item, our position is the same as its position. Simply set it,
	// and start positioning our parent.
	TViewModelPtr<IOutlinerExtension> OutlinerItem = WeakOutlinerItem.Pin();
	if (InItem == OutlinerItem)
	{
		SetVerticalPosition(InItemTop);

		if (ParentLane)
		{
			ParentLane->PositionParentTrackLanes(OutlinerItem, InItemTop);
		}

		return;
	}

	// The given item is somewhere inside the descendants of our owning outliner item. Walk up its hierarchy
	// and accumulate the height of all siblings that come before (above) it. We might have to go up by
	// multiple levels until we hit our owning outliner item. Also, to be safe, we need to support having
	// non-outliner items mixed in with the rest.
	float AccumulatedItemTop = InItemTop;
	TSharedPtr<FViewModel> StopAtChild = InItem.AsModel();
	TSharedPtr<FViewModel> ParentItem = StopAtChild->GetParent();
	while (ParentItem)
	{
		constexpr bool bIncludeThis = true;
		for (FParentFirstChildIterator It(ParentItem, bIncludeThis); It; ++It)
		{
			if (*It == StopAtChild)
			{
				break;
			}

			if (IOutlinerExtension* OutlinerExtension = It->CastThis<IOutlinerExtension>())
			{
				AccumulatedItemTop -= OutlinerExtension->GetOutlinerSizing().GetTotalHeight();

				if (!OutlinerExtension->IsExpanded())
				{
					It.IgnoreCurrentChildren();
				}
			}
		}

		if (OutlinerItem == ParentItem)
		{
			break;
		}

		StopAtChild = ParentItem;
		ParentItem = ParentItem->GetParent();
		ensureMsgf(ParentItem, TEXT("We reached the root item without finding the one that owns this track lane!"));
	}

	SetVerticalPosition(AccumulatedItemTop);

	if (ParentLane)
	{
		ParentLane->PositionParentTrackLanes(GetOutlinerItem(), AccumulatedItemTop);
	}
}

void STrackLane::SetParent(TSharedPtr<STrackLane> InParentLane)
{
	ParentLane = InParentLane;
}

FReply STrackLane::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	static float HitThreshold = 5.f;

	TViewModelPtr<IResizableExtension> ResizableExtension = WeakOutlinerItem.ImplicitPin();

	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && ResizableExtension)
	{
		FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		if (LocalPos.Y >= MyGeometry.GetLocalSize().Y - HitThreshold)
		{
			if (ResizableExtension->IsResizable())
			{
				TViewModelPtr<IOutlinerExtension> OutlinerItem = WeakOutlinerItem.Pin();
				const float OriginalHeight = OutlinerItem ? OutlinerItem->GetOutlinerSizing().GetTotalHeight() : 10.f;

				DragParameters = FDragParameters(OriginalHeight, MouseEvent.GetScreenSpacePosition().Y);
				return FReply::Handled().CaptureMouse(AsShared());
			}
		}
	}

	return FReply::Unhandled();
}

FReply STrackLane::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (DragParameters.IsSet() && HasMouseCapture())
	{
		DragParameters.Reset();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply STrackLane::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TViewModelPtr<IResizableExtension> ResizableExtension = WeakOutlinerItem.ImplicitPin();

	if (DragParameters.IsSet() && HasMouseCapture() && ResizableExtension)
	{
		float NewHeight = DragParameters->OriginalHeight + (MouseEvent.GetScreenSpacePosition().Y - DragParameters->DragStartY);

		if (FMath::RoundToInt(NewHeight) != FMath::RoundToInt(DragParameters->OriginalHeight))
		{
			ResizableExtension->Resize(NewHeight);
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FCursorReply STrackLane::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	static float HitThreshold = 5.f;

	TViewModelPtr<IResizableExtension> ResizableExtension = WeakOutlinerItem.ImplicitPin();

	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
	if ((LocalPos.Y >= MyGeometry.GetLocalSize().Y - HitThreshold) && ResizableExtension)
	{
		if (ResizableExtension->IsResizable())
		{
			return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
		}
	}

	return FCursorReply::Unhandled();
}

} // namespace Sequencer
} // namespace UE

