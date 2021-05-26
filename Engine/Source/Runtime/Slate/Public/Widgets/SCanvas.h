// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Layout/Visibility.h"
#include "SlotBase.h"
#include "Widgets/SWidget.h"
#include "Layout/Children.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SPanel.h"

class FArrangedChildren;
class FPaintArgs;
class FSlateWindowElementList;

/**
 * Canvas is a layout widget that allows you to arbitrary position and size child widgets in a relative coordinate space
 */
class SLATE_API SCanvas
	: public SPanel
{
public:

	/**
	 * Canvas slots allow child widgets to be positioned and sized
	 *
	 * Horizontal Alignment 
	 *  Given a top aligned slot, where '+' represents the 
	 *  anchor point defined by PositionAttr.
	 *  
	 *   Left				Center				Right
	 *	+ _ _ _ _            _ _ + _ _          _ _ _ _ +
	 *	|		  |		   | 		   |	  |		    |
	 *	| _ _ _ _ |        | _ _ _ _ _ |	  | _ _ _ _ |
	 * 
	 *  Note: FILL is NOT supported.
	 *
	 * Vertical Alignment 
	 *   Given a left aligned slot, where '+' represents the 
	 *   anchor point defined by PositionAttr.
	 *  
	 *   Top				Center			  Bottom
	 *	+_ _ _ _ _          _ _ _ _ _		 _ _ _ _ _ 
	 *	|		  |		   | 		 |		|		  |
	 *	| _ _ _ _ |        +		 |		|		  |
	 *					   | _ _ _ _ |		+ _ _ _ _ |
	 * 
	 *  Note: FILL is NOT supported.
	 */
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	class SLATE_API FSlot : public TSlotBase<FSlot>, public TAlignmentWidgetSlotMixin<FSlot>
	{
	public:
		SLATE_SLOT_BEGIN_ARGS_OneMixin(FSlot, TSlotBase<FSlot>, TAlignmentWidgetSlotMixin<FSlot>)
			SLATE_ATTRIBUTE(FVector2D, Position)
			SLATE_ATTRIBUTE(FVector2D, Size)
		SLATE_SLOT_END_ARGS()

		void SetPosition( TAttribute<FVector2D> InPosition )
		{
			PositionAttr = MoveTemp(InPosition);
		}
		FVector2D GetPosition() const
		{
			return PositionAttr.Get();
		}

		void SetSize( TAttribute<FVector2D> InSize )
		{
			SizeAttr = MoveTemp(InSize);
		}
		FVector2D GetSize() const
		{
			return SizeAttr.Get();
		}

	public:
		/** Position */
		UE_DEPRECATED(5.0, "Direct access to PositionAttr is now deprecated. Use the getter or setter.")
		TAttribute<FVector2D> PositionAttr;

		/** Size */
		UE_DEPRECATED(5.0, "Direct access to SizeAttr is now deprecated. Use the getter or setter.")
		TAttribute<FVector2D> SizeAttr;

	public:
		/** Default values for a slot. */
		FSlot()
			: TSlotBase<FSlot>()
			, TAlignmentWidgetSlotMixin<FSlot>(HAlign_Left, VAlign_Top)
			, PositionAttr( FVector2D::ZeroVector )
			, SizeAttr( FVector2D( 1.0f, 1.0f ) )
		{ }

		void Construct(const FChildren& SlotOwner, FSlotArguments&& InArg);
	};
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	SLATE_BEGIN_ARGS( SCanvas )
		{
			_Visibility = EVisibility::SelfHitTestInvisible;
		}

		SLATE_SLOT_ARGUMENT( FSlot, Slots )

	SLATE_END_ARGS()

	SCanvas();

	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct( const FArguments& InArgs );

	static FSlot::FSlotArguments Slot();

	using FScopedWidgetSlotArguments = TPanelChildren<FSlot>::FScopedWidgetSlotArguments;
	/**
	 * Adds a content slot.
	 *
	 * @return The added slot.
	 */
	FScopedWidgetSlotArguments AddSlot();

	/**
	 * Removes a particular content slot.
	 *
	 * @param SlotWidget The widget in the slot to remove.
	 */
	int32 RemoveSlot( const TSharedRef<SWidget>& SlotWidget );

	/**
	 * Removes all slots from the panel.
	 */
	void ClearChildren();

public:

	// SWidget overrides

	virtual void OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const override;
	virtual int32 OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override;
	virtual FChildren* GetChildren() override;

protected:
	// Begin SWidget overrides.
	virtual FVector2D ComputeDesiredSize(float) const override;
	// End SWidget overrides.

protected:

	/** The canvas widget's children. */
	TPanelChildren< FSlot > Children;
};
