// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Layout/Geometry.h"
#include "Input/CursorReply.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SlotBase.h"
#include "Layout/Children.h"
#include "Widgets/SPanel.h"
#include "MVVM/Views/SequencerInputHandlerStack.h"
#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/ViewModelPtr.h"

class FArrangedChildren;
class FPaintArgs;
class FSlateWindowElementList;
class ISequencerEditTool;

namespace UE
{
namespace Sequencer
{

class SOutlinerView;
class IOutlinerExtension;
class FTrackAreaViewModel;
class FViewModel;
class STrackLane;

/**
 * Structure representing a slot in the track area.
 */
class FTrackAreaSlot
	: public TSlotBase<FTrackAreaSlot>, public TAlignmentWidgetSlotMixin<FTrackAreaSlot>
{
public:

	/** Construction from a track lane */
	FTrackAreaSlot(const TSharedPtr<STrackLane>& InSlotContent);

	/** The track lane that we represent. */
	TWeakPtr<STrackLane> TrackLane;
};


/**
 * The area where tracks( rows of sections ) are displayed.
 */
class SEQUENCERCORE_API STrackAreaView
	: public SPanel
{
public:

	SLATE_BEGIN_ARGS( STrackAreaView )
	{
		_Clipping = EWidgetClipping::ClipToBounds;
	}
	SLATE_END_ARGS()

	STrackAreaView();
	~STrackAreaView();

	/** Construct this widget */
	void Construct(const FArguments& InArgs, TWeakPtr<FTrackAreaViewModel> InWeakViewModel);

	TSharedPtr<FTrackAreaViewModel> GetViewModel() const;
	
	void SetVirtualPosition(float InVirtualTop);

public:

	/** Empty the track area */
	void Empty();

	/** Add a new track slot to this area for the given node. The slot will be automatically cleaned up when all external references to the supplied slot are removed. */
	void AddTrackSlot(const TViewModelPtr<IOutlinerExtension>& InDataModel, const TSharedPtr<STrackLane>& InSlot);

	/** Attempt to find an existing slot relating to the given node */
	TSharedPtr<STrackLane> FindTrackSlot(const TViewModelPtr<IOutlinerExtension>& InDataModel);

	/** Assign a tree view to this track area. */
	void SetOutliner(const TSharedPtr<SOutlinerView>& InOutliner);
	TWeakPtr<SOutlinerView> GetOutliner() const { return WeakOutliner.Pin(); }

	/** Set whether this TrackArea should show only pinned nodes or only non-pinned nodes  */
	void SetShowPinned(bool bShowPinned) { bShowPinnedNodes = bShowPinned; }
	bool ShowPinned() const { return bShowPinnedNodes; }

	/** Set whether this TrackArea is pinned to another TrackArea and should skip updating external controls */
	void SetIsPinned(bool bInIsPinned) { bIsPinned = bInIsPinned; }
	bool IsPinned() const { return bIsPinned; }

	static FLinearColor BlendDefaultTrackColor(FLinearColor InColor);

public:

	/*~ SWidget interface */
	FReply OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	FReply OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	FReply OnMouseMove( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	FReply OnMouseWheel( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override;
	void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	FCursorReply OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const override;
	void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
	void OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const override;
	FVector2D ComputeDesiredSize(float) const override;
	FChildren* GetChildren() override;
	void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

protected:

	virtual void OnResized(const FVector2D& OldSize, const FVector2D& NewSize){}

	/** Update any hover state required for the track area */
	virtual void UpdateHoverStates( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent );

protected:


private:

	/** The track area's children. */
	TPanelChildren<FTrackAreaSlot> Children;

protected:

	/** Input handler stack responsible for routing input to the different handlers */
	FInputHandlerStack InputStack;

	/** A map of child slot content that exist in our view. */
	TMap<TWeakViewModelPtr<IOutlinerExtension>, TWeakPtr<STrackLane>> TrackSlots;

	/** Weak pointer to the track area view model. */
	TWeakPtr<FTrackAreaViewModel> WeakViewModel;

	/** Weak pointer to the outliner view (used for scrolling interactions). */
	TWeakPtr<SOutlinerView> WeakOutliner;

	/** Keep a hold of the size of the area so we can maintain zoom levels. */
	TOptional<FVector2D> SizeLastFrame;

	/** Weak pointer to the dropped node */
	TWeakViewModelPtr<IOutlinerExtension> WeakDroppedItem;

	float VirtualTop;

	/** Whether the dropped node is allowed to be dropped onto */
	bool bAllowDrop;

	/** The frame range of the section about to be dropped */
	TOptional<TRange<FFrameNumber>> DropFrameRange;

	/** Whether this TrackArea is for pinned nodes or non-pinned nodes */
	bool bShowPinnedNodes;

	/** Whether this TrackArea is pinned to another TrackArea and should skip updating external controls */
	bool bIsPinned;
};

} // namespace Sequencer
} // namespace UE

