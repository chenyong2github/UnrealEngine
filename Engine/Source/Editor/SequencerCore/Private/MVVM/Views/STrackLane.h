// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SPanel.h"
#include "Layout/Children.h"

#include "MVVM/Extensions/ITrackAreaExtension.h"
#include "MVVM/Extensions/ITrackLaneExtension.h"
#include "MVVM/Extensions/IOutlinerExtension.h"

class FViewModel;
class FPaintArgs;
class FViewModel;
class FSlateWindowElementList;
class FTrackAreaViewModel;

namespace UE
{
namespace Sequencer
{

class SOutlinerView;

/**
 * A wrapper widget responsible for positioning a track lane within the section area
 */
class STrackLane
	: public SPanel
{
public:
	SLATE_BEGIN_ARGS(STrackLane){}
	SLATE_END_ARGS()

	STrackLane();
	~STrackLane();

	/** Construct this widget */
	void Construct(const FArguments& InArgs, TWeakPtr<FTrackAreaViewModel> InTrackAreaViewModel, TWeakViewModelPtr<IOutlinerExtension> InWeakOutlinerItem, const FTrackAreaParameters& InTrackParams, const TSharedRef<SOutlinerView>& InTreeView);

	/*~ SWidget Interface */
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	/*~ SPanel Interface */
	void OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const override;
	FVector2D ComputeDesiredSize(float) const override;
	FChildren* GetChildren() override { return &Children; }

	/** Gets the outliner view-model that created this track lane */
	TViewModelPtr<IOutlinerExtension> GetOutlinerItem() const;

	/** Sets the parent track lane of this track lane */
	void SetParent(TSharedPtr<STrackLane> InParentLane);

	/** Gets the vertical offset of this track lane */
	float GetVerticalPosition() const;
	/** Sets the vertical offset of this track lane */
	void SetVerticalPosition(float InPosition);

	/**
	 * Sets the vertical offset of this track lane and all parent track lanes given the vertical offset of
	 * a particular outliner item inside the current track lane (i.e. an outliner item that's a child of the 
	 * item that created this track lane)
	 * Note that the given item *could* actually be the one that created this track lane.
	 */
	void PositionParentTrackLanes(TViewModelPtr<IOutlinerExtension> InItem, float InItemTop);

	/** Returns whether the outliner item that created this track lane is pinned or not */
	bool IsPinned() const;

protected:

	int32 PaintLaneBackground(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;

	void OnHierarchyUpdated();
	void RecreateWidgets();

private:
	TWeakPtr<FTrackAreaViewModel> WeakTrackAreaViewModel;

	/** The outliner item that created this lane */
	TWeakViewModelPtr<IOutlinerExtension> WeakOutlinerItem;

	/** Pointer back to the tree view for virtual <-> physical space conversions. Important: weak ptr to avoid circular references */
	TWeakPtr<SOutlinerView> TreeView;

	/** Parent track lane - intentionally a _strong_ ptr to the parent in order to keep it alive even if it is scrolled out of view */
	TSharedPtr<STrackLane> ParentLane;

	struct FSlot : TSlotBase<FSlot>
	{
		FSlot(TSharedPtr<ITrackLaneWidget> InInterface);
		~FSlot();

		TSharedRef<ITrackLaneWidget> Interface;
	};

	/** All the widgets in the panel */
	TPanelChildren<FSlot> Children;

	/** Our desired size last frame */
	TOptional<FVector2D> LastDesiredSize;

	struct FDragParameters
	{
		FDragParameters(float InOriginalHeight, float InDragStartY) : OriginalHeight(InOriginalHeight), DragStartY(InDragStartY) {}

		float OriginalHeight;
		float DragStartY;
	};
	TOptional<FDragParameters> DragParameters;

	FTrackAreaParameters TrackParams;

	float Position;

	bool bWidgetsDirty;
};

} // namespace Sequencer
} // namespace UE

