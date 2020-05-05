// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SlotBase.h"
#include "Widgets/SWidget.h"
#include "Layout/Children.h"
#include "Widgets/SPanel.h"

class FArrangedChildren;

/**
 * With EOrientation::Orient_Horizontal
 * Arranges widgets left-to-right.
 * When the widgets exceed the PreferredSize
 * the SWrapBox will place widgets on the next line.
 *
 * Illustration:
 *                      +-----Preferred Size
 *                      |
 *       [-----------][-|-]
 *       [--][------[--]|
 *       [--------------|]
 *       [---]          |
 */

 /**
  * With EOrientation::Orient_Vertical
  * Arranges widgets top-to-bottom.
  * When the widgets exceed the PreferredSize
  * the SVerticalWrapBox will place widgets on the next line.
  *
  * Illustration:
  *
  *      [___]  [___]
  *      [-1-]  [-3-]
  *
  *		 [___]  [___]
  *      [-2-]  [-4-]
  *
  *      [___]
  *==============================>--------Preferred Size
  *		 [-3-]
  */

class SLATE_API SWrapBox : public SPanel
{
public:

	/** A slot that support alignment of content and padding */
	class FSlot : public TSlotBase<FSlot>, public TSupportsContentAlignmentMixin<FSlot>, public TSupportsContentPaddingMixin<FSlot>
	{
	public:
		FSlot()
			: TSlotBase<FSlot>()
			, TSupportsContentAlignmentMixin<FSlot>(HAlign_Fill, VAlign_Fill)
			, SlotFillLineWhenWidthLessThan()
			, SlotFillLineWhenSizeLessThan()
			, bSlotFillEmptySpace(false)
		{
		}

		UE_DEPRECATED(4.26, "Deprecated, please use FillLineWhenSizeLessThan() instead")
		FSlot& FillLineWhenWidthLessThan(TOptional<float> InFillLineWhenWidthLessThan)
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			SlotFillLineWhenWidthLessThan = InFillLineWhenWidthLessThan;
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			return *(static_cast<FSlot*>(this));
		}

		/** Dependently of the Orientation, if the total available horizontal or vertical space in the wrap panel drops below this threshold, this slot will attempt to fill an entire line. */
		FSlot& FillLineWhenSizeLessThan(TOptional<float> InFillLineWhenSizeLessThan)
		{
			SlotFillLineWhenSizeLessThan = InFillLineWhenSizeLessThan;
			return *(static_cast<FSlot*>(this));
		}

		/** Should this slot fill the remaining space on the line? */
		FSlot& FillEmptySpace(bool bInFillEmptySpace)
		{
			bSlotFillEmptySpace = bInFillEmptySpace;
			return *(static_cast<FSlot*>(this));
		}

		UE_DEPRECATED(4.26, "Deprecated, please use SlotFillLineWhenSizeLessThan instead")
		TOptional<float> SlotFillLineWhenWidthLessThan;

		TOptional<float> SlotFillLineWhenSizeLessThan;
		bool bSlotFillEmptySpace;
	};


	SLATE_BEGIN_ARGS(SWrapBox)
		: _PreferredWidth(100.f)
		, _PreferredSize(100.f)
		, _InnerSlotPadding(FVector2D::ZeroVector)
		, _UseAllottedWidth(false)
		, _UseAllottedSize(false)
		, _Orientation(EOrientation::Orient_Horizontal)
		{
			_Visibility = EVisibility::SelfHitTestInvisible;
		}

		/** The slot supported by this panel */
		SLATE_SUPPORTS_SLOT( FSlot )

		/** The preferred width, if not set will fill the space */
		SLATE_ATTRIBUTE( float, PreferredWidth )

		/** The preferred size, if not set will fill the space */
		SLATE_ATTRIBUTE( float, PreferredSize )

		/** The inner slot padding goes between slots sharing borders */
		SLATE_ARGUMENT( FVector2D, InnerSlotPadding )

		/** if true, the PreferredWidth will always match the room available to the SWrapBox  */
		SLATE_ARGUMENT( bool, UseAllottedWidth )

		/** if true, the PreferredSize will always match the room available to the SWrapBox  */
		SLATE_ARGUMENT( bool, UseAllottedSize )

		/** Determines if the wrap box needs to arrange the slots left-to-right or top-to-bottom.*/
		SLATE_ARGUMENT_DEFAULT(EOrientation, Orientation) { EOrientation::Orient_Horizontal };
	SLATE_END_ARGS()

	SWrapBox();

	static FSlot& Slot();

	FSlot& AddSlot();

	/** Removes a slot from this box panel which contains the specified SWidget
	 *
	 * @param SlotWidget The widget to match when searching through the slots
	 * @returns The index in the children array where the slot was removed and -1 if no slot was found matching the widget
	 */
	int32 RemoveSlot( const TSharedRef<SWidget>& SlotWidget );

	void Construct( const FArguments& InArgs );

	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;

	virtual void OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const override;

	void ClearChildren();

	virtual FVector2D ComputeDesiredSize(float) const override;

	virtual FChildren* GetChildren() override;

	/** See InnerSlotPadding Attribute */
	void SetInnerSlotPadding(FVector2D InInnerSlotPadding);

	/** Set the width at which the wrap panel should wrap its content. */
	UE_DEPRECATED(4.26, "Deprecated, please use SetWrapSize() instead")
	void SetWrapWidth(const TAttribute<float>& InWrapWidth);

	/** Set the size at which the wrap panel should wrap its content. */
	void SetWrapSize( const TAttribute<float>& InWrapSize );

	/** When true, use the WrapWidth property to determine where to wrap to the next line. */
	UE_DEPRECATED(4.26, "Deprecated, please use SetUseAllottedSize() instead")
	void SetUseAllottedWidth(bool bInUseAllottedWidth);

	/** When true, use the WrapSize property to determine where to wrap to the next line. */
	void SetUseAllottedSize(bool bInUseAllottedSize);

	/** Set the Orientation to determine if the wrap box needs to arrange the slots left-to-right or top-to-bottom */
	void SetOrientation(EOrientation InOrientation);

private:

	/** How wide this panel should appear to be. Any widgets past this line will be wrapped onto the next line. */
	UE_DEPRECATED(4.26, "Deprecated, please use PreferredSize instead")
	TAttribute<float> PreferredWidth;

	/** How wide or long, dependently of the orientation, this panel should appear to be. Any widgets past this line will be wrapped onto the next line. */
	TAttribute<float> PreferredSize;

	/** The slots that contain this panel's children. */
	TPanelChildren<FSlot> Slots;

	/** When two slots end up sharing a border, this will put that much padding between then, but otherwise wont. */
	FVector2D InnerSlotPadding;

	/** If true the box will have a preferred width equal to its alloted width  */
	UE_DEPRECATED(4.26, "Deprecated, please use bUseAllottedSize instead")
	bool bUseAllottedWidth;

	/** If true the box will have a preferred size equal to its alloted size  */
	bool bUseAllottedSize;

	/** Determines if the wrap box needs to arrange the slots left-to-right or top-to-bottom.*/
	EOrientation Orientation = EOrientation::Orient_Horizontal;

	class FChildArranger;
	friend class SWrapBox::FChildArranger;
};