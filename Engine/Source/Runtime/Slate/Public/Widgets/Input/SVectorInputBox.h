// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/CoreStyle.h"
#include "Framework/SlateDelegates.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Widgets/Input/NumericTypeInterface.h"

class FArrangedChildren;
class SHorizontalBox;

/**
 * Vector Slate control
 */
class SLATE_API SVectorInputBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SVectorInputBox )
		: _Font( FAppStyle::Get().GetFontStyle("NormalFont") )
		, _AllowSpin( false )
		, _SpinDelta( 1 )
		, _bColorAxisLabels( false )
		{}
		
		/** X Component of the vector */
		SLATE_ATTRIBUTE( TOptional<float>, X )

		/** Y Component of the vector */
		SLATE_ATTRIBUTE( TOptional<float>, Y )

		/** Z Component of the vector */
		SLATE_ATTRIBUTE( TOptional<float>, Z )

		/** Font to use for the text in this box */
		SLATE_ATTRIBUTE( FSlateFontInfo, Font )

		/** Whether or not values can be spun or if they should be typed in */
		SLATE_ARGUMENT( bool, AllowSpin )

		/** The delta amount to apply, per pixel, when the spinner is dragged. */
		SLATE_ATTRIBUTE( float, SpinDelta )

		/** Should the axis labels be colored */
		SLATE_ARGUMENT( bool, bColorAxisLabels )		

		/** Allow responsive layout to crush the label and margins when there is not a lot of room */
		UE_DEPRECATED(5.0, "AllowResponsiveLayout unused as it is no longer necessary.")
		FArguments& AllowResponsiveLayout(bool bAllow)
		{
			return Me();
		}

		/** Called when the x value of the vector is changed */
		SLATE_EVENT( FOnFloatValueChanged, OnXChanged )

		/** Called when the y value of the vector is changed */
		SLATE_EVENT( FOnFloatValueChanged, OnYChanged )

		/** Called when the z value of the vector is changed */
		SLATE_EVENT( FOnFloatValueChanged, OnZChanged )

		/** Called when the x value of the vector is committed */
		SLATE_EVENT( FOnFloatValueCommitted, OnXCommitted )

		/** Called when the y value of the vector is committed */
		SLATE_EVENT( FOnFloatValueCommitted, OnYCommitted )

		/** Called when the z value of the vector is committed */
		SLATE_EVENT( FOnFloatValueCommitted, OnZCommitted )

		/** Menu extender delegate for the X value */
		SLATE_EVENT( FMenuExtensionDelegate, ContextMenuExtenderX )

		/** Menu extender delegate for the Y value */
		SLATE_EVENT( FMenuExtensionDelegate, ContextMenuExtenderY )

		/** Menu extender delegate for the Z value */
		SLATE_EVENT( FMenuExtensionDelegate, ContextMenuExtenderZ )

		/** Called right before the slider begins to move for any of the vector components */
		SLATE_EVENT( FSimpleDelegate, OnBeginSliderMovement )
		
		/** Called right after the slider handle is released by the user for any of the vector components */
		SLATE_EVENT( FOnFloatValueChanged, OnEndSliderMovement )

		/** Provide custom type functionality for the vector */
		SLATE_ARGUMENT( TSharedPtr< INumericTypeInterface<float> >, TypeInterface )

	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct( const FArguments& InArgs );
private:


	/**
	 * Construct widgets for the X Value
	 */
	void ConstructX( const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox );

	/**
	 * Construct widgets for the Y Value
	 */
	void ConstructY( const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox );

	/**
	 * Construct widgets for the Z Value
	 */
	void ConstructZ( const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox );
};
